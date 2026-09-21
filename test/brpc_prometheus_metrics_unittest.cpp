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
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "butil/strings/string_piece.h"
#include "echo.pb.h"
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
