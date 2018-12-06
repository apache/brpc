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
    explicit PrometheusMetricsDumper(butil::IOBufBuilder* os,
                                     const std::string& server_prefix)
        : _os(os)
        , _server_prefix(server_prefix) {
    }

    bool dump(const std::string& name, const butil::StringPiece& desc) override;

private:
    DISALLOW_COPY_AND_ASSIGN(PrometheusMetricsDumper);

    // Return true iff name ends with suffix output by LatencyRecorder.
    bool DumpLatencyRecorderSuffix(const butil::StringPiece& name,
                                   const butil::StringPiece& desc);

    // 6 is the number of bvars in LatencyRecorder that indicating percentiles
    static const int NPERCENTILES = 6;

    struct SummaryItems {
        std::string latency_percentiles[NPERCENTILES];
        std::string latency_avg;
        std::string count;
        std::string metric_name;

        bool IsComplete() const { return !metric_name.empty(); }
    };
    const SummaryItems* ProcessLatencyRecorderSuffix(const butil::StringPiece& name,
                                                     const butil::StringPiece& desc);

private:
    butil::IOBufBuilder* _os;
    const std::string _server_prefix;
    std::map<std::string, SummaryItems> _m;
};

bool PrometheusMetricsDumper::dump(const std::string& name,
                                   const butil::StringPiece& desc) {
    if (!desc.empty() && desc[0] == '"') {
        // there is no necessary to monitor string in prometheus
        return true;
    }
    if (DumpLatencyRecorderSuffix(name, desc)) {
        // Has encountered name with suffix exposed by LatencyRecorder,
        // Leave it to DumpLatencyRecorderSuffix to output Summary.
        return true;
    }
    *_os << "# HELP " << name << '\n'
         << "# TYPE " << name << " gauge" << '\n'
         << name << " " << desc << '\n';
    return true;
}

const PrometheusMetricsDumper::SummaryItems*
PrometheusMetricsDumper::ProcessLatencyRecorderSuffix(const butil::StringPiece& name,
                                                      const butil::StringPiece& desc) {
    static std::string latency_names[] = {
        butil::string_printf("_latency_%d", (int)bvar::FLAGS_bvar_latency_p1),
        butil::string_printf("_latency_%d", (int)bvar::FLAGS_bvar_latency_p2),
        butil::string_printf("_latency_%d", (int)bvar::FLAGS_bvar_latency_p3),
        "_latency_999", "_latency_9999", "_max_latency"
    };
    CHECK(NPERCENTILES == arraysize(latency_names));
    butil::StringPiece metric_name(name);
    for (int i = 0; i < NPERCENTILES; ++i) {
        if (!metric_name.ends_with(latency_names[i])) {
            continue;
        }
        metric_name.remove_suffix(latency_names[i].size());
        SummaryItems* si = &_m[metric_name.as_string()];
        si->latency_percentiles[i] = desc.as_string();
        if (i == NPERCENTILES - 1) {
            // '_max_latency' is the last suffix name that appear in the sorted bvar
            // list, which means all related percentiles have been gathered and we are
            // ready to output a Summary.
            si->metric_name = metric_name.as_string();
        }
        return si;
    }
    // Get the average of latency in recent window size
    if (metric_name.ends_with("_latency")) {
        metric_name.remove_suffix(8);
        SummaryItems* si = &_m[metric_name.as_string()];
        si->latency_avg = desc.as_string();
        return si;
    }
    if (metric_name.ends_with("_count")) {
        metric_name.remove_suffix(6);
        SummaryItems* si = &_m[metric_name.as_string()];
        si->count = desc.as_string();
        return si;
    }
    return NULL;
}

bool PrometheusMetricsDumper::DumpLatencyRecorderSuffix(
    const butil::StringPiece& name,
    const butil::StringPiece& desc) {
    if (!name.starts_with(_server_prefix)) {
        return false;
    }
    const SummaryItems* si = ProcessLatencyRecorderSuffix(name, desc);
    if (!si) {
        return false;
    }
    if (!si->IsComplete()) {
        return true;
    }
    *_os << "# HELP " << si->metric_name << '\n'
         << "# TYPE " << si->metric_name << " summary\n"
         << si->metric_name << "{quantile=\""
         << (double)(bvar::FLAGS_bvar_latency_p1) / 100 << "\"} "
         << si->latency_percentiles[0] << '\n'
         << si->metric_name << "{quantile=\""
         << (double)(bvar::FLAGS_bvar_latency_p2) / 100 << "\"} "
         << si->latency_percentiles[1] << '\n'
         << si->metric_name << "{quantile=\""
         << (double)(bvar::FLAGS_bvar_latency_p3) / 100 << "\"} "
         << si->latency_percentiles[2] << '\n'
         << si->metric_name << "{quantile=\"0.999\"} "
         << si->latency_percentiles[3] << '\n'
         << si->metric_name << "{quantile=\"0.9999\"} "
         << si->latency_percentiles[4] << '\n'
         << si->metric_name << "{quantile=\"1\"} "
         << si->latency_percentiles[5] << '\n'
         << si->metric_name << "_sum "
         // There is no sum of latency in bvar output, just use
         // average * count as approximation
         << strtoll(si->latency_avg.data(), NULL, 10) *
                strtoll(si->count.data(), NULL, 10) << '\n'
         << si->metric_name << "_count " << si->count << '\n';
    return true;
}

void PrometheusMetricsService::default_method(::google::protobuf::RpcController* cntl_base,
                                              const ::brpc::MetricsRequest*,
                                              ::brpc::MetricsResponse*,
                                              ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    cntl->http_response().set_content_type("text/plain");
    butil::IOBufBuilder os;
    PrometheusMetricsDumper dumper(&os, _server->ServerPrefix());
    const int ndump = bvar::Variable::dump_exposed(&dumper, NULL);
    if (ndump < 0) {
        cntl->SetFailed("Fail to dump metrics");
        return;
    }
    os.move_to(cntl->response_attachment());
}

} // namespace brpc
