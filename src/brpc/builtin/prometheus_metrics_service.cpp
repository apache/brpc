// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.


#include <vector>
#include <iomanip>
#include <map>
#include <unordered_set>
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
DECLARE_int32(bvar_max_dump_multi_dimension_metric_number);
}

namespace brpc {

// Defined in server.cpp
extern const char* const g_server_info_prefix;

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
        , _server_prefix(server_prefix)
        , _current_family_refused(false) {
    }

    DISALLOW_COPY_AND_ASSIGN(PrometheusMetricsDumper);

    bool dump(const std::string& name, const butil::StringPiece& desc) override;
    bool dump_mvar(const std::string& name, const butil::StringPiece& desc) override;
    bool dump_comment(const std::string& name, const std::string& type) override;

private:

    // Return true iff name ends with suffix output by LatencyRecorder.
    bool DumpLatencyRecorderSuffix(const butil::StringPiece& name,
                                   const butil::StringPiece& desc);

    // Claim `names` for this scrape, all of them or none. Returns false when
    // one of them is already taken, in which case a line is logged and the
    // caller must write nothing: two metrics under one name make prometheus
    // reject the whole scrape, not just the offending line.
    bool ClaimMetricNames(const std::vector<std::string>& names);

    // The names a family called `name` occupies when it is dumped as `type`:
    // its own, plus the samples the prometheus text format fixes for that type.
    // Those suffixes belong here rather than to the variable, the format is
    // what decides them.
    static std::vector<std::string> FamilyNames(const std::string& name,
                                                const std::string& type);

    // 6 is the number of bvars in LatencyRecorder that indicating percentiles
    static const int NPERCENTILES = 6;

    struct SummaryItems {
        std::string latency_percentiles[NPERCENTILES];
        int64_t latency_avg;
        int64_t count;
        std::string metric_name;

        bool IsComplete() const {
            if (metric_name.empty()) {
                return false;
            }
            // `metric_name` alone is not enough: a LatencyRecorder whose expose()
            // stopped halfway leaves some of the percentiles behind, and writing
            // them out as empty values would make prometheus reject the scrape.
            for (int i = 0; i < NPERCENTILES; ++i) {
                if (latency_percentiles[i].empty()) {
                    return false;
                }
            }
            return true;
        }
    };
    const SummaryItems* ProcessLatencyRecorderSuffix(const butil::StringPiece& name,
                                                     const butil::StringPiece& desc);

private:
    butil::IOBufBuilder* _os;
    const std::string _server_prefix;
    std::map<std::string, SummaryItems> _m;
    // Every metric name written out so far, including the ones this dumper
    // makes up and which no variable is exposed under. Shared by the bvar and
    // the mbvar pass so that a name taken by one is not taken again by the
    // other.
    std::unordered_set<std::string> _dumped_names;
    // The family the last dump_comment() opened and whether it was refused.
    // A family is written across several calls, one comment then its samples,
    // so a refusal has to outlive the call that made it.
    std::string _current_family;
    bool _current_family_refused;
};

butil::StringPiece GetMetricsName(const std::string& name) {
    auto pos = name.find_first_of('{');
    int size = pos == std::string::npos ? name.size() : pos;
    return butil::StringPiece(name.data(), size);
}

// Case-insensitive match of [p, end) against the NUL-terminated, lower-case
// ASCII `word`. The whole rest of the input must be consumed, so "inf" matches
// but "infx" does not.
static bool MatchWordIgnoreCase(const char* p, const char* end, const char* word) {
    for (; p != end && *word != '\0'; ++p, ++word) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c != *word) {
            return false;
        }
    }
    return p == end && *word == '\0';
}

// Whether `s` is a number prometheus would accept as a sample value: a float64
// in the decimal, or one of the specials it spells as "+Inf"/"-Inf"/"NaN"
// (case-insensitive). The spelled-out "Infinity" is not part of the grammar,
// so it is rejected even though Go's strconv.ParseFloat would accept it. Nor
// is a signed NaN: prometheus consumes the sign and then only looks for Inf
// after it, so "-nan", which is what glibc's printf("%g") makes of a negative
// NaN, would fail the whole scrape.
//
// Everything else is rejected: a quoted string, the json of a Window<Histogram>
// or a compound PassiveStatus, and the bare `true`/'false` of a bool gflag,
// which sniffing only the first char let through. Skipping such a variable
// is not cosmetic: one malformed line makes prometheus reject the whole scrape,
// not just that one metric.
//
// Scans the StringPiece in place, no copy and no allocation. Hexadecimal
// floats are deliberately not accepted.
bool IsDumpableToPrometheus(butil::StringPiece s) {
    if (s.empty()) {
        return false;
    }
    const char* p = s.data();
    const char* const end = p + s.size();
    bool has_sign = false;
    if (*p == '+' || *p == '-') {
        has_sign = true;
        ++p;
        if (p == end) {
            return false;   // a lone sign
        }
    }
    const char c = *p;
    if (c >= '0' && c <= '9') {
        // "123", "123." or "123.45".
        while (p != end && *p >= '0' && *p <= '9') {
            ++p;
        }
        if (p != end && *p == '.') {
            ++p;
            while (p != end && *p >= '0' && *p <= '9') {
                ++p;
            }
        }
    } else if (c == '.') {
        // ".5": the digits before the point may be omitted, those after may not.
        ++p;
        if (p == end || *p < '0' || *p > '9') {
            return false;
        }
        while (p != end && *p >= '0' && *p <= '9') {
            ++p;
        }
    } else {
        // The specials of the prometheus text format only: "Infinity" is not
        // one of them, and passing it through could invalidate the whole scrape.
        // Only Inf may carry a sign, NaN may not.
        return MatchWordIgnoreCase(p, end, "inf") ||
               (!has_sign && MatchWordIgnoreCase(p, end, "nan"));
    }
    // Optional exponent, which must carry at least one digit.
    if (p != end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p != end && (*p == '+' || *p == '-')) {
            ++p;
        }
        if (p == end || *p < '0' || *p > '9') {
            return false;
        }
        while (p != end && *p >= '0' && *p <= '9') {
            ++p;
        }
    }
    // Anything left over ("12abc", "1.5.6", "1,2") is not a single number.
    return p == end;
}

std::vector<std::string> PrometheusMetricsDumper::FamilyNames(const std::string& name,
                                                              const std::string& type) {
    if (type == "summary") {
        return {name, name + "_sum", name + "_count"};
    }
    if (type == "histogram") {
        return {name, name + "_bucket", name + "_sum", name + "_count"};
    }
    return {name};
}

bool PrometheusMetricsDumper::ClaimMetricNames(const std::vector<std::string>& names) {
    for (const std::string& name : names) {
        if (name.empty() || _dumped_names.count(name) == 0) {
            continue;
        }
        LOG_EVERY_SECOND(ERROR)
            << "Skip metric=" << names[0] << " of /brpc_metrics because name="
            << name << " is already taken in this scrape, rename one of them";
        return false;
    }
    _dumped_names.insert(names.begin(), names.end());
    return true;
}

bool PrometheusMetricsDumper::dump(const std::string& name,
                                   const butil::StringPiece& desc) {
    if (!IsDumpableToPrometheus(desc)) {
        return true;
    }
    if (DumpLatencyRecorderSuffix(name, desc)) {
        // Has encountered name with suffix exposed by LatencyRecorder,
        // Leave it to DumpLatencyRecorderSuffix to output Summary.
        return true;
    }

    std::string metrics_name = GetMetricsName(name).as_string();
    if (!ClaimMetricNames({metrics_name})) {
        return true;
    }

    *_os << "# HELP " << metrics_name << '\n'
         << "# TYPE " << metrics_name << " gauge" << '\n'
         << name << " " << desc << '\n';
    return true;
}

bool PrometheusMetricsDumper::dump_mvar(const std::string& name, const butil::StringPiece& desc) {
    if (!IsDumpableToPrometheus(desc)) {
        return true;
    }
    if (_current_family_refused && GetMetricsName(name).starts_with(_current_family)) {
        return true;
    }
    *_os << name << " " << desc << "\n";
    return true;
}

bool PrometheusMetricsDumper::dump_comment(const std::string& name, const std::string& type) {
    _current_family = name;
    _current_family_refused = !ClaimMetricNames(FamilyNames(name, type));
    if (_current_family_refused) {
        return true;
    }
    *_os << "# HELP " << name << '\n'
         << "# TYPE " << name << " " << type << '\n';
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
    const std::string desc_str = desc.as_string();
    butil::StringPiece metric_name(name);
    for (int i = 0; i < NPERCENTILES; ++i) {
        if (!metric_name.ends_with(latency_names[i])) {
            continue;
        }
        metric_name.remove_suffix(latency_names[i].size());
        SummaryItems* si = &_m[metric_name.as_string()];
        si->latency_percentiles[i] = desc_str;
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
        si->latency_avg = strtoll(desc_str.data(), nullptr, 10);
        return si;
    }
    if (metric_name.ends_with("_count")) {
        metric_name.remove_suffix(6);
        SummaryItems* si = &_m[metric_name.as_string()];
        si->count = strtoll(desc_str.data(), nullptr, 10);
        return si;
    }
    return nullptr;
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
    // The average latency can not be a quantile series of the summary below,
    // because the quantile label must be parsable as a float. Dump it as a
    // separate gauge, which is the same as the multi dimension one does.
    // No bvar is exposed under this name, it is made up right here, so this is
    // also the only place that can tell whether it is still free.
    std::string avg_name = si->metric_name + "_avg_latency";
    if (ClaimMetricNames({avg_name})) {
        *_os << "# HELP " << avg_name << '\n'
             << "# TYPE " << avg_name << " gauge\n"
             << avg_name << ' ' << si->latency_avg << '\n';
    }
    // Same for the summary, whose `_sum` is made up as well. Its `_count` does
    // come from a bvar, but one this function swallowed without printing, so
    // claiming it here does not collide with itself.
    if (!ClaimMetricNames(FamilyNames(si->metric_name, "summary"))) {
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
         << si->latency_avg * si->count << '\n'
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
    if (DumpPrometheusMetricsToIOBuf(&cntl->response_attachment()) != 0) {
        cntl->SetFailed("Fail to dump metrics");
        return;
    }
}

int DumpPrometheusMetricsToIOBuf(butil::IOBuf* output) {
    butil::IOBufBuilder os;
    PrometheusMetricsDumper dumper(&os, g_server_info_prefix);
    int ndump = bvar::Variable::dump_exposed(&dumper, nullptr);
    if (ndump < 0) {
        return -1;
    }
    os.move_to(*output);

    if (bvar::FLAGS_bvar_max_dump_multi_dimension_metric_number > 0) {
        int ndump_md = bvar::MVariableBase::dump_exposed(&dumper, nullptr);
        if (ndump_md < 0) {
            return -1;
        }
        output->append(butil::IOBuf::Movable(os.buf()));
    }
    return 0;
}

} // namespace brpc
