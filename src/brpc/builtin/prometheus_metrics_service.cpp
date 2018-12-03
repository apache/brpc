// Copyright (c) 2018 Bilibili, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Jiashun Zhu(zhujiashun@bilibili.com)

#include <vector>
#include <iomanip>
#include <map>
#include "brpc/controller.h"                // Controller
#include "brpc/server.h"                    // Server
#include "brpc/closure_guard.h"             // ClosureGuard
#include "brpc/builtin/prometheus_metrics_service.h"
#include "brpc/builtin/common.h"
#include "bvar/bvar.h"

namespace bvar {
DECLARE_int32(bvar_latency_p1);
DECLARE_int32(bvar_latency_p2);
DECLARE_int32(bvar_latency_p3);
}

namespace brpc {

// This is a class that convert bvar result to prometheus output.
// Currently the output only includes gauge and summary for two
// reasons:
// 1) We cannot tell gauge and counter just from name and what's
// more counter is just another gauge.
// 2) Histogram and summary is equivalent except that histogram
// calculates quantiles in the server side.
class PrometheusMetricsDumper : public bvar::Dumper {
public:
    explicit PrometheusMetricsDumper(butil::IOBufBuilder& os,
                           const std::string& server_prefix)
        : _os(os)
        , _server_prefix(server_prefix) {
    }

    bool dump(const std::string& name, const butil::StringPiece& desc);

private:
    DISALLOW_COPY_AND_ASSIGN(PrometheusMetricsDumper);

    bool ProcessLatencyRecorderSuffix(const butil::StringPiece& name,
                                      const butil::StringPiece& desc);

    // 6 is the number of bvar in LatencyRecorder that indicating
    // percentiles
    static const int NPERCENTILES = 6;

private:
    struct SummaryItems {
        SummaryItems() {
            latency_percentiles.resize(NPERCENTILES);
        }

        std::vector<std::string> latency_percentiles;
        std::string latency_avg;
        std::string count;
    };
    butil::IOBufBuilder& _os;
    std::string _server_prefix;
    std::map<std::string, SummaryItems> _m;
};

bool PrometheusMetricsDumper::dump(const std::string& name, const butil::StringPiece& desc) {
    if (!desc.empty() && desc[0] == '"') {
        // there is no necessary to monitor string in prometheus
        return true;
    }
    if (ProcessLatencyRecorderSuffix(name, desc)) {
        // Has encountered name with suffix exposed by LatencyRecorder,
        // Leave it to ProcessLatencyRecorderSuffix to output Summary.
        return true;
    }
    _os << "# HELP " << name << '\n'
        << "# TYPE " << name << " gauge" << '\n'
        << name << " " << desc << '\n';
    return true;
}

bool PrometheusMetricsDumper::ProcessLatencyRecorderSuffix(const butil::StringPiece& name,
                                                           const butil::StringPiece& desc) {
    static std::string latency_names[] = {
        butil::string_printf("latency_%d", (int)bvar::FLAGS_bvar_latency_p1),
        butil::string_printf("latency_%d", (int)bvar::FLAGS_bvar_latency_p2),
        butil::string_printf("latency_%d", (int)bvar::FLAGS_bvar_latency_p3),
        "latency_999", "latency_9999", "max_latency"
    };
    CHECK(NPERCENTILES == sizeof(latency_names) / sizeof(std::string));

    if (name.starts_with(_server_prefix)) {
        int i = 0;
        butil::StringPiece key(name);
        for (; i < NPERCENTILES; ++i) {
            if (key.ends_with(latency_names[i])) {
                key.remove_suffix(latency_names[i].size() + 1); /* 1 is sizeof('_') */
                _m[key.as_string()].latency_percentiles[i] = desc.as_string();
                break;
            }
        }
        if (i < NPERCENTILES) {
            // 'max_latency' is the last suffix name that appear in the sorted bvar
            // list, which means all related percentiles have been gathered and we are
            // ready to output a Summary.
            if (latency_names[i] == "max_latency") {
                const SummaryItems& items = _m[key.as_string()];
                _os << "# HELP " << key << '\n'
                    << "# TYPE " << key << " summary\n"
                    << key << "{quantile=\"" << std::setprecision(2)
                    << (double)(bvar::FLAGS_bvar_latency_p1) / 100 << "\"} "
                    << items.latency_percentiles[0] << '\n'
                    << key << "{quantile=\"" << std::setprecision(2)
                    << (double)(bvar::FLAGS_bvar_latency_p2) / 100 << "\"} "
                    << items.latency_percentiles[1] << '\n'
                    << key << "{quantile=\"" << std::setprecision(2)
                    << (double)(bvar::FLAGS_bvar_latency_p3) / 100 << "\"} "
                    << items.latency_percentiles[2] << '\n'
                    << key << "{quantile=\"0.999\"} " << items.latency_percentiles[3] << '\n'
                    << key << "{quantile=\"0.9999\"} " << items.latency_percentiles[4] << '\n'
                    << key << "{quantile=\"1\"} " << items.latency_percentiles[5] << '\n'
                    << key << "_sum "
                    // There is no sum of latency in bvar output, just use average * count as approximation
                    << strtoll(items.latency_avg.data(), NULL, 10) * strtoll(items.count.data(), NULL, 10) << '\n'
                    << key << "_count " << items.count << '\n';
            }
            return true;
        }
        // Get the average of latency in recent window size
        if (key.ends_with("latency")) {
            key.remove_suffix(8 /* sizeof("latency") + sizeof('_') */);
            _m[key.as_string()].latency_avg = desc.as_string();
            return true;
        }
        if (key.ends_with("count") &&
                !key.ends_with("builtin_service_count") &&
                // 'builtin_service_count' and 'connection_count' is not
                // exposed by bvar in LatencyRecorder
                !key.ends_with("connection_count")) {
            key.remove_suffix(6 /* sizeof("count") + sizeof('_') */);
            _m[key.as_string()].count = desc.as_string();
            return true;
        }
    }
    return false;
}

void PrometheusMetricsService::default_method(::google::protobuf::RpcController* cntl_base,
                                              const ::brpc::MetricsRequest*,
                                              ::brpc::MetricsResponse*,
                                              ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    cntl->http_response().set_content_type("text/plain");
    butil::IOBufBuilder os;
    PrometheusMetricsDumper dumper(os, _server->ServerPrefix());
    const int ndump = bvar::Variable::dump_exposed(&dumper, NULL);
    if (ndump < 0) {
        cntl->SetFailed("Fail to dump metrics");
        return;
    }
    os.move_to(cntl->response_attachment());
}

} // namespace brpc
