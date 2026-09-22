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

// brpc - A framework to host and access services throughout Baidu.

#include <gtest/gtest.h>
#include <unordered_set>
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/builtin/prometheus_metrics_service.h"
#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"
#include "butil/string_printf.h"
#include "echo.pb.h"
#include "bvar/bvar.h"
#include "bvar/histogram.h"
#include "bvar/multi_dimension.h"

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

class DummyEchoServiceImpl : public test::EchoService {
public:
    virtual ~DummyEchoServiceImpl() {}
    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const test::EchoRequest* request,
                      test::EchoResponse* response,
                      google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        return;
    }
};

enum STATE {
    HELP = 0,
    TYPE,
    GAUGE,
    SUMMARY,
    COUNTER,
    HISTOGRAM,
    // When meets a line with a gauge/counter with labels, we have no
    // idea the next line is a new HELP or the same gauge/counter just
    // with different labels
    HELP_OR_GAUGE,
    HELP_OR_COUNTER,
    // Same for a histogram: the samples of the next label set follow the
    // _count closing the previous one, unless the family is over
    HELP_OR_HISTOGRAM,
};

TEST(PrometheusMetrics, sanity) {
    brpc::Server server;
    DummyEchoServiceImpl echo_svc;
    ASSERT_EQ(0, server.AddService(&echo_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
    // An ephemeral port rather than a hardcoded one, which another test in the
    // same run may already be listening on.
    ASSERT_EQ(0, server.Start("127.0.0.1:0", nullptr));
    // Start() only launches the bthread exposing the per method bvars, it does
    // not wait for it. Scraping right away can catch that loop halfway and see
    // some methods but not others, which _method_map iterates in hash order.
    // Wait for the one this test reads back.
    std::string echo_count_name = butil::string_printf(
        "rpc_server_%d_test_echo_service_echo_count",
        server.listen_address().port);
    for (int i = 0; i < 500 &&
             bvar::Variable::describe_exposed(echo_count_name).empty(); ++i) {
        bthread_usleep(10000);
    }
    ASSERT_FALSE(bvar::Variable::describe_exposed(echo_count_name).empty());

    const std::list<std::string> labels = {"label1", "label2"};
    bvar::MultiDimension<bvar::Adder<uint32_t> > my_madder("madder", labels);
    bvar::Adder<uint32_t>* my_adder1 = my_madder.get_stats({"val1", "val2"});
    ASSERT_TRUE(my_adder1);
    *my_adder1 << 1 << 2;
    bvar::Adder<uint32_t>* my_adder2 = my_madder.get_stats({"val2", "val3"});
    ASSERT_TRUE(my_adder1);
    *my_adder2 << 3 << 4;

    bvar::MultiDimension<bvar::LatencyRecorder > my_mlat("mlat", labels);
    bvar::LatencyRecorder* my_lat1 = my_mlat.get_stats({"val1", "val2"});
    ASSERT_TRUE(my_lat1);
    *my_lat1 << 1 << 2;
    bvar::LatencyRecorder* my_lat2 = my_mlat.get_stats({"val2", "val3"});
    ASSERT_TRUE(my_lat2);
    *my_lat2 << 3 << 4;

    // Only a bvar prefixed with the server prefix is folded into a summary.
    bvar::LatencyRecorder my_lat3("rpc_server_lat_test");
    my_lat3 << 5 << 6;

    bvar::MultiDimension<bvar::Histogram> my_mhist("mhist", labels,
                                                   bvar::Histogram::BucketSchema({10, 20}));
    bvar::Histogram* my_hist1 = my_mhist.get_stats({"val1", "val2"});
    ASSERT_TRUE(my_hist1);
    // Spread over the buckets so that the cumulative counts differ from
    // each other and the +Inf one is not the only non-empty bucket.
    *my_hist1 << 1 << 15 << 100;
    bvar::Histogram* my_hist2 = my_mhist.get_stats({"val2", "val3"});
    ASSERT_TRUE(my_hist2);
    *my_hist2 << 3 << 40;

    // The single dimension counterpart of the multi dimension histogram above,
    // dumped as a family of its own with no labels.
    bvar::Histogram my_hist3("hist3", bvar::Histogram::BucketSchema({10, 20}));
    my_hist3 << 1 << 15 << 100;

    brpc::Channel channel;
    brpc::ChannelOptions channel_opts;
    channel_opts.protocol = "http";
    ASSERT_EQ(0, channel.Init(server.listen_address(), &channel_opts));
    brpc::Controller cntl;
    cntl.http_request().uri() = "/brpc_metrics";
    channel.CallMethod(nullptr, &cntl, nullptr, nullptr, nullptr);
    ASSERT_FALSE(cntl.Failed());
    std::string res = cntl.response_attachment().to_string();
    LOG(INFO) << "output:\n" << res;

    // The average latency is a separate metric rather than a quantile series,
    // because the quantile label must be parsable as a float.
    ASSERT_EQ(std::string::npos, res.find("quantile=\"avg\""));
    ASSERT_NE(std::string::npos, res.find("# TYPE mlat_avg_latency gauge\n"));
    ASSERT_NE(std::string::npos, res.find("mlat_avg_latency{label1=\"val1\","
                                          "label2=\"val2\"}"));
    // The single dimension LatencyRecorder uses the same suffix.
    ASSERT_NE(std::string::npos, res.find("_service_echo_avg_latency "));
    // Quantile is a fraction rather than an integer.
    ASSERT_NE(std::string::npos, res.find("quantile=\"0.99\""));
    ASSERT_NE(std::string::npos, res.find("quantile=\"0.999\""));
    ASSERT_NE(std::string::npos, res.find("quantile=\"0.9999\""));
    ASSERT_EQ(std::string::npos, res.find("quantile=\"99\""));
    ASSERT_EQ(std::string::npos, res.find("quantile=\"999\""));
    ASSERT_EQ(std::string::npos, res.find("quantile=\"9999\""));
    ASSERT_NE(std::string::npos, res.find("mlat_latency{label1=\"val1\",label2=\"val2\","
                                          "quantile=\"0.99\"}"));
    // The average must not be dumped as a series of `_latency` as well, otherwise
    // an aggregation over `_latency` would still pick it up.
    ASSERT_EQ(std::string::npos, res.find("mlat_latency{label1=\"val1\","
                                          "label2=\"val2\"} "));
    ASSERT_NE(std::string::npos, res.find("rpc_server_lat_test_count 2\n"));
    // `_avg_latency` is dumped before the summary it belongs to.
    size_t average_pos = res.find("# TYPE rpc_server_lat_test_avg_latency gauge\n");
    size_t summary_pos = res.find("# TYPE rpc_server_lat_test summary\n");
    ASSERT_NE(std::string::npos, average_pos);
    ASSERT_NE(std::string::npos, summary_pos);
    ASSERT_LT(average_pos, summary_pos);

    size_t start_pos = 0;
    size_t end_pos = 0;
    size_t label_start = 0;
    STATE state = HELP;
    char name_help[128];
    char name_type[128];
    char type[16];
    int matched = 0;
    int num = 0;
    bool summary_sum_gathered = false;
    bool summary_count_gathered = false;
    bool histogram_inf_gathered = false;
    bool histogram_sum_gathered = false;
    bool has_ever_summary = false;
    bool has_ever_gauge = false;
    bool has_ever_counter = false; // brought in by mvar latency recorder
    bool has_ever_histogram = false; // brought in by mvar histogram
    std::unordered_set<std::string> metric_name_set;

    while ((end_pos = res.find('\n', start_pos)) != butil::StringPiece::npos) {
        res[end_pos] = '\0';       // safe;
        switch (state) {
            case HELP:
                matched = sscanf(res.data() + start_pos, "# HELP %s", name_help);
                ASSERT_EQ(1, matched);
                state = TYPE;
                break;
            case TYPE:
                matched = sscanf(res.data() + start_pos, "# TYPE %s %s", name_type, type);
                ASSERT_EQ(2, matched);
                ASSERT_STREQ(name_type, name_help);
                if (strcmp(type, "gauge") == 0) {
                    state = GAUGE;
                } else if (strcmp(type, "summary") == 0) {
                    state = SUMMARY;
                } else if (strcmp(type, "counter") == 0) {
                    state = COUNTER;
                } else if (strcmp(type, "histogram") == 0) {
                    state = HISTOGRAM;
                } else {
                    ASSERT_TRUE(false) << "invalid type: " << type;
                }
                ASSERT_EQ(0, metric_name_set.count(name_type)) << "second TYPE line for metric name "
                    << name_type;
                metric_name_set.insert(name_help);
                break;
            case HELP_OR_GAUGE:
            case HELP_OR_COUNTER:
            case HELP_OR_HISTOGRAM:
                matched = sscanf(res.data() + start_pos, "# HELP %s", name_help);
                // Try to figure out current line is a new COMMENT or not
                if (matched == 1) {
                    state = HELP;
                } else if (state == HELP_OR_GAUGE) {
                    state = GAUGE;
                } else if (state == HELP_OR_COUNTER) {
                    state = COUNTER;
                } else {
                    state = HISTOGRAM;
                }
                res[end_pos] = '\n'; // revert to original
                continue; // do not jump to next line
            case GAUGE:
            case COUNTER:
                matched = sscanf(res.data() + start_pos, "%s %d", name_type, &num);
                ASSERT_EQ(2, matched);
                if (state == GAUGE) {
                    has_ever_gauge = true;
                }
                if (state == COUNTER) {
                    has_ever_counter = true;
                }
                label_start = butil::StringPiece(name_type).find("{");
                if (label_start == strlen(name_help)) { // mvar
                    ASSERT_EQ(name_type[strlen(name_type) - 1], '}');
                    ASSERT_TRUE(strncmp(name_type, name_help, strlen(name_help)) == 0);
                    state = state == GAUGE ? HELP_OR_GAUGE : HELP_OR_COUNTER;
                } else if (label_start == butil::StringPiece::npos) { // var
                    ASSERT_STREQ(name_type, name_help);
                    state = HELP;
                } else { // invalid
                    ASSERT_TRUE(false);
                }
                break;
            case SUMMARY:
                if (butil::StringPiece(res.data() + start_pos, end_pos - start_pos).find("quantile=")
                        == butil::StringPiece::npos) {
                    matched = sscanf(res.data() + start_pos, "%s %d", name_type, &num);
                    ASSERT_EQ(2, matched);
                    ASSERT_TRUE(strncmp(name_type, name_help, strlen(name_help)) == 0);
                    if (butil::StringPiece(name_type).ends_with("_sum")) {
                        ASSERT_FALSE(summary_sum_gathered);
                        summary_sum_gathered = true;
                    } else if (butil::StringPiece(name_type).ends_with("_count")) {
                        ASSERT_FALSE(summary_count_gathered);
                        summary_count_gathered = true;
                    } else {
                        ASSERT_TRUE(false);
                    }
                    if (summary_sum_gathered && summary_count_gathered) {
                        state = HELP;
                        summary_sum_gathered = false;
                        summary_count_gathered = false;
                        has_ever_summary = true;
                    }
                } // else find "quantile=", just break to next line
                break;
            case HISTOGRAM: {
                matched = sscanf(res.data() + start_pos, "%s %d", name_type, &num);
                ASSERT_EQ(2, matched);
                // Every sample of the family is the name on the TYPE line plus
                // one of the _bucket/_sum/_count suffixes, then the labels.
                ASSERT_TRUE(strncmp(name_type, name_help, strlen(name_help)) == 0);
                butil::StringPiece suffix(name_type + strlen(name_help));
                label_start = suffix.find("{");
                if (label_start != butil::StringPiece::npos) {
                    ASSERT_EQ(name_type[strlen(name_type) - 1], '}');
                    suffix = suffix.substr(0, label_start);
                }
                if (suffix == "_bucket") {
                    // The buckets of one label set come in ascending order and
                    // the unbounded one closes them.
                    ASSERT_FALSE(histogram_inf_gathered);
                    ASSERT_NE(butil::StringPiece::npos,
                              butil::StringPiece(name_type).find("le=\""));
                    if (butil::StringPiece(name_type).find("le=\"+Inf\"") !=
                        butil::StringPiece::npos) {
                        histogram_inf_gathered = true;
                    }
                } else if (suffix == "_sum") {
                    ASSERT_TRUE(histogram_inf_gathered);
                    ASSERT_FALSE(histogram_sum_gathered);
                    histogram_sum_gathered = true;
                } else if (suffix == "_count") {
                    ASSERT_TRUE(histogram_sum_gathered);
                    histogram_inf_gathered = false;
                    histogram_sum_gathered = false;
                    has_ever_histogram = true;
                    // Samples of another label set may follow.
                    state = HELP_OR_HISTOGRAM;
                } else {
                    ASSERT_TRUE(false) << "invalid histogram sample: " << name_type;
                }
                break;
            }
            default:
                ASSERT_TRUE(false);
                break;
        }
        start_pos = end_pos + 1;
    }
    ASSERT_TRUE(has_ever_gauge && has_ever_summary && has_ever_counter
                && has_ever_histogram);
    ASSERT_EQ(0, server.Stop(0));
    ASSERT_EQ(0, server.Join());
}

TEST(PrometheusMetrics, GetMetricsName) {
    EXPECT_EQ("", brpc::GetMetricsName(""));

    EXPECT_EQ("commit_count", brpc::GetMetricsName("commit_count"));

    EXPECT_EQ("commit_count", brpc::GetMetricsName("commit_count{region=\"1000\"}"));
}

// Values bvar produces: integers, doubles (DoubleToString may omit the
// integer part, as in ".5") and exponent form.
TEST(PrometheusMetrics, IsDumpableToPrometheus_numbers) {
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("0"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("42"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("-5"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("+7"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("3.14"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("-0.5"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus(".5"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("-.5"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("123."));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("1e10"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("1E10"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("1e+10"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("1e-10"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("1.5e-3"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus(".5e2"));
}

// Prometheus accepts these specials, case-insensitively; unlike
// butil::StringToDouble, whose behavior on them is undefined.
TEST(PrometheusMetrics, IsDumpableToPrometheus_specials) {
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("+Inf"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("-Inf"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("inf"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("INF"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("NaN"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("nan"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("nAn"));
}

// A NaN carries no sign in that grammar: prometheus consumes the sign and then
// looks for Inf only, so the "-nan" that glibc's printf("%g") makes of a
// negative NaN would fail the whole scrape.
TEST(PrometheusMetrics, IsDumpableToPrometheus_signed_specials) {
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("-nan"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("+nan"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("-NaN"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("+NAN"));
    // Inf on the other hand takes either sign.
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("-INF"));
    EXPECT_TRUE(brpc::IsDumpableToPrometheus("+inf"));
}

// A quoted string, a json object/array (Window<Histogram>, compound
// PassiveStatus) and the bare `true`/`false` of a bool gflag, which sniffing
// only the first char let through.
TEST(PrometheusMetrics, IsDumpableToPrometheus_non_numbers) {
    EXPECT_FALSE(brpc::IsDumpableToPrometheus(""));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus(butil::StringPiece()));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("true"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("false"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("True"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("\"running\""));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("{\"count\":4}"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("[1,2,3]"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("null"));
    // The spelled-out "Infinity" is not prometheus sample-value grammar either,
    // no matter what Go's strconv.ParseFloat thinks of it.
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("Infinity"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("-infinity"));
}

// Malformed numbers that a prefix parser such as strtod would accept.
TEST(PrometheusMetrics, IsDumpableToPrometheus_malformed) {
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("+"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("-"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("."));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("e5"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("1e"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("1e+"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("1.5.6"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("12abc"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("1,2"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus(" 1"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("1 "));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("0x10"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("infx"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("in"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("na"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("--1"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("1..2"));
    EXPECT_FALSE(brpc::IsDumpableToPrometheus("1e2e3"));
}

// Number of sample lines for `name`, namely the lines starting with it followed
// by a space or by the opening brace of its labels. Comment lines are left out,
// what has to be unique in a scrape is the sample name.
static int CountSampleLines(const std::string& text, const std::string& name) {
    int n = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        size_t len = (eol == std::string::npos ? text.size() : eol) - pos;
        butil::StringPiece line(text.data() + pos, len);
        if (line.starts_with(name) && line.size() > name.size() &&
            (line[name.size()] == ' ' || line[name.size()] == '{')) {
            ++n;
        }
        if (eol == std::string::npos) {
            break;
        }
        pos = eol + 1;
    }
    return n;
}

// `X_sum` is made up by the exporter out of a LatencyRecorder, no variable is
// exposed under it, so the name registry cannot see the clash and the dumper is
// the only place that can.
TEST(PrometheusMetrics, DumperSkipsBvarTakingASynthesizedName) {
    bvar::LatencyRecorder lat("rpc_server_0_sumdedup");
    lat << 1 << 2;
    bvar::Adder<int> sum;
    ASSERT_EQ(0, sum.expose("rpc_server_0_sumdedup_sum"));
    sum << 7;

    butil::IOBuf buf;
    ASSERT_EQ(0, brpc::DumpPrometheusMetricsToIOBuf(&buf));
    std::string res = buf.to_string();
    // `_max_latency` sorts before `_sum`, so the summary writes first and the
    // plain bvar is the one dropped.
    EXPECT_EQ(1, CountSampleLines(res, "rpc_server_0_sumdedup_sum")) << res;
    EXPECT_EQ(std::string::npos, res.find("rpc_server_0_sumdedup_sum 7\n")) << res;
    EXPECT_EQ(std::string::npos, res.find("# TYPE rpc_server_0_sumdedup_sum ")) << res;
}

// The reverse order, and a whole family at once: the summary occupies three
// names and writes none of them when one is taken. Half a summary is as bad for
// the scrape as a duplicate.
TEST(PrometheusMetrics, DumperDropsWholeSummaryWhenItsNameIsTaken) {
    bvar::Adder<int> base;
    ASSERT_EQ(0, base.expose("rpc_server_0_basededup"));
    base << 9;
    bvar::LatencyRecorder lat("rpc_server_0_basededup");
    lat << 1 << 2;

    butil::IOBuf buf;
    ASSERT_EQ(0, brpc::DumpPrometheusMetricsToIOBuf(&buf));
    std::string res = buf.to_string();
    EXPECT_EQ(1, CountSampleLines(res, "rpc_server_0_basededup")) << res;
    EXPECT_NE(std::string::npos, res.find("rpc_server_0_basededup 9\n")) << res;
    EXPECT_EQ(std::string::npos,
              res.find("# TYPE rpc_server_0_basededup summary")) << res;
    EXPECT_EQ(0, CountSampleLines(res, "rpc_server_0_basededup_sum")) << res;
    EXPECT_EQ(0, CountSampleLines(res, "rpc_server_0_basededup_count")) << res;
    // `_avg_latency` is a family of its own, a clash on the summary leaves it
    // alone.
    EXPECT_EQ(1, CountSampleLines(res, "rpc_server_0_basededup_avg_latency")) << res;
}

// The bvar pass and the mbvar pass write into one scrape, so they have to share
// one view of which names are taken. The registry cannot catch this pair either,
// the name the mbvar runs into belongs to no variable.
TEST(PrometheusMetrics, DumperSkipsMbvarTakingASynthesizedName) {
    bvar::LatencyRecorder lat("rpc_server_0_mdedup");
    lat << 1 << 2;
    const std::list<std::string> labels = {"label1"};
    bvar::MultiDimension<bvar::Adder<int> > madder("rpc_server_0_mdedup_sum", labels);
    bvar::Adder<int>* sub = madder.get_stats({"val1"});
    ASSERT_TRUE(sub);
    *sub << 6;

    butil::IOBuf buf;
    ASSERT_EQ(0, brpc::DumpPrometheusMetricsToIOBuf(&buf));
    std::string res = buf.to_string();
    EXPECT_EQ(1, CountSampleLines(res, "rpc_server_0_mdedup_sum")) << res;
    EXPECT_EQ(std::string::npos,
              res.find("rpc_server_0_mdedup_sum{label1=\"val1\"}")) << res;
}
