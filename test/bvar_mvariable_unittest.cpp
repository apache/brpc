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

// Date 2021/11/17 14:57:49

// #if __cplusplus >= 201703L
#include <string_view>
// #endif // __cplusplus >= 201703L
#include <pthread.h>                                // pthread_*
#include <cstddef>
#include <memory>
#include <iostream>
#include <set>
#include <string>
#include <array>
#include <vector>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/strings/string_number_conversions.h"
#include "bvar/bvar.h"
#include "bvar/multi_dimension.h"

static const std::list<std::string> labels = {"idc", "method", "status"};

class MVariableTest : public testing::Test {
protected:
    void SetUp() {}
    void TearDown() {
    }
};

namespace foo {
namespace bar {
class Apple {};
class BaNaNa {};
class Car_Rot {};
class RPCTest {};
class HELLO {};
}
}

TEST_F(MVariableTest, expose) {
    std::vector<std::string> list_exposed_vars;
    std::list<std::string> labels_value1 {"bj", "get", "200"};
    bvar::MultiDimension<bvar::Adder<int> > my_madder1(labels);
    ASSERT_EQ(0UL, bvar::MVariableBase::count_exposed());
    my_madder1.expose("request_count_madder");
    ASSERT_EQ(1UL, bvar::MVariableBase::count_exposed());
    bvar::Adder<int>* my_adder1 = my_madder1.get_stats(labels_value1);
    ASSERT_TRUE(my_adder1);
    ASSERT_STREQ("request_count_madder", my_madder1.name().c_str());

    ASSERT_EQ(0, my_madder1.expose("request_count_madder_another"));
    ASSERT_STREQ("request_count_madder_another", my_madder1.name().c_str());

    ASSERT_EQ(0, my_madder1.expose("request-count::madder"));
    ASSERT_STREQ("request_count_madder", my_madder1.name().c_str());

    ASSERT_EQ(0, my_madder1.expose("request.count-madder::BaNaNa"));
    ASSERT_STREQ("request_count_madder_ba_na_na", my_madder1.name().c_str());

    ASSERT_EQ(0, my_madder1.expose_as("foo::bar::Apple", "request"));
    ASSERT_STREQ("foo_bar_apple_request", my_madder1.name().c_str());

    ASSERT_EQ(0, my_madder1.expose_as("foo.bar::BaNaNa", "request"));
    ASSERT_STREQ("foo_bar_ba_na_na_request", my_madder1.name().c_str());

    ASSERT_EQ(0, my_madder1.expose_as("foo::bar.Car_Rot", "request"));
    ASSERT_STREQ("foo_bar_car_rot_request", my_madder1.name().c_str());

    ASSERT_EQ(0, my_madder1.expose_as("foo-bar-RPCTest", "request"));
    ASSERT_STREQ("foo_bar_rpctest_request", my_madder1.name().c_str());

    ASSERT_EQ(0, my_madder1.expose_as("foo-bar-HELLO", "request"));
    ASSERT_STREQ("foo_bar_hello_request", my_madder1.name().c_str());

    my_madder1.expose("request_count_madder");
    ASSERT_STREQ("request_count_madder", my_madder1.name().c_str());
    list_exposed_vars.push_back("request_count_madder");

    ASSERT_EQ(1UL, my_madder1.count_stats());
    ASSERT_EQ(1UL, bvar::MVariableBase::count_exposed());

    std::list<std::string> labels2 {"user", "url", "cost"};
    bvar::MultiDimension<bvar::Adder<int> > my_madder2("client_url", labels2);
    ASSERT_EQ(2UL, bvar::MVariableBase::count_exposed());
    list_exposed_vars.push_back("client_url");

    std::list<std::string> labels3 {"product", "system", "module"};
    bvar::MultiDimension<bvar::Adder<int> > my_madder3("request_from", labels3);
    list_exposed_vars.push_back("request_from");
    ASSERT_EQ(3UL, bvar::MVariableBase::count_exposed());

    std::vector<std::string> exposed_vars;
    bvar::MVariableBase::list_exposed(&exposed_vars);
    ASSERT_EQ(3, exposed_vars.size());

    my_madder3.hide();
    ASSERT_EQ(2UL, bvar::MVariableBase::count_exposed());
    list_exposed_vars.pop_back();
    exposed_vars.clear();
    bvar::MVariableBase::list_exposed(&exposed_vars);
    ASSERT_EQ(2, exposed_vars.size());
}

TEST_F(MVariableTest, prometheus_name_conflicts_with_bvar) {
    std::list<std::string> one_label = {"method"};
    bvar::Status<int> scalar;
    bvar::MultiDimension<bvar::Adder<int> > multi(one_label);

    ASSERT_EQ(0, scalar.expose("shared-prometheus-name"));
    ASSERT_EQ(-1, multi.expose("shared.prometheus.name"));
    ASSERT_TRUE(multi.name().empty());

    ASSERT_TRUE(scalar.hide());
    ASSERT_EQ(0, multi.expose("shared::prometheus::name"));
    ASSERT_EQ(-1, scalar.expose("shared prometheus name"));
    ASSERT_TRUE(scalar.is_hidden());

    ASSERT_TRUE(multi.hide());
    ASSERT_EQ(0, scalar.expose("shared_prometheus_name"));
}

TEST_F(MVariableTest, hide_all_releases_prometheus_names) {
    std::list<std::string> one_label = {"method"};
    bvar::MultiDimension<bvar::Histogram> multi(
        one_label, bvar::Histogram::BucketSchema({1}));
    ASSERT_EQ(0, multi.expose("hide_all_prometheus_name"));

    bvar::MVariableBase::hide_all();
    ASSERT_EQ(0UL, bvar::MVariableBase::count_exposed());
    ASSERT_TRUE(multi.name().empty());

    bvar::Status<int> scalar;
    ASSERT_EQ(0, scalar.expose("hide_all_prometheus_name_bucket"));
    ASSERT_TRUE(scalar.hide());
    ASSERT_EQ(0, multi.expose("hide_all_prometheus_name"));
}

TEST_F(MVariableTest, dump) {
    std::string old_bvar_dump_interval;
    std::string old_mbvar_dump;
    std::string old_mbvar_dump_prefix;
    std::string old_mbvar_dump_format;

    GFLAGS_NAMESPACE::GetCommandLineOption("bvar_dump_interval", &old_bvar_dump_interval);
    GFLAGS_NAMESPACE::GetCommandLineOption("mbvar_dump", &old_mbvar_dump);
    GFLAGS_NAMESPACE::GetCommandLineOption("mbvar_dump_prefix", &old_mbvar_dump_prefix);
    GFLAGS_NAMESPACE::GetCommandLineOption("mbvar_dump_format", &old_mbvar_dump_format);

    GFLAGS_NAMESPACE::SetCommandLineOption("bvar_dump_interval", "1");
    GFLAGS_NAMESPACE::SetCommandLineOption("mbvar_dump", "true");
    GFLAGS_NAMESPACE::SetCommandLineOption("mbvar_dump_prefix", "my_mdump_prefix");
    GFLAGS_NAMESPACE::SetCommandLineOption("mbvar_dump_format", "common");

    bvar::MultiDimension<bvar::Adder<int> > my_madder("dump_adder", labels);
    std::list<std::string> labels_value1 {"gz", "post", "200"};
    bvar::Adder<int>* adder1 = my_madder.get_stats(labels_value1);
    ASSERT_TRUE(adder1);
    *adder1 << 1 << 3 << 5;

    std::list<std::string> labels_value2 {"tc", "get", "200"};
    bvar::Adder<int>* adder2 = my_madder.get_stats(labels_value2);
    ASSERT_TRUE(adder2);
    *adder2 << 2 << 4 << 6;

    std::list<std::string> labels_value3 {"jx", "post", "500"};
    bvar::Adder<int>* adder3 = my_madder.get_stats(labels_value3);
    ASSERT_TRUE(adder3);
    *adder3 << 3 << 6 << 9;

    bvar::MultiDimension<bvar::Maxer<int> > my_mmaxer("dump_maxer", labels);
    bvar::Maxer<int>* maxer1 = my_mmaxer.get_stats(labels_value1);
    ASSERT_TRUE(maxer1);
    *maxer1 << 3 << 1 << 5;

    bvar::Maxer<int>* maxer2 = my_mmaxer.get_stats(labels_value2);
    ASSERT_TRUE(maxer2);
    *maxer2 << 2 << 6 << 4;

    bvar::Maxer<int>* maxer3 = my_mmaxer.get_stats(labels_value3);
    ASSERT_TRUE(maxer3);
    *maxer3 << 9 << 6 << 3;

    bvar::MultiDimension<bvar::Miner<int> > my_mminer("dump_miner", labels);
    bvar::Miner<int>* miner1 = my_mminer.get_stats(labels_value1);
    ASSERT_TRUE(miner1);
    *miner1 << 3 << 1 << 5;

    bvar::Miner<int>* miner2 = my_mminer.get_stats(labels_value2);
    ASSERT_TRUE(miner2);
    *miner2 << 2 << 6 << 4;

    bvar::Miner<int>* miner3 = my_mminer.get_stats(labels_value3);
    ASSERT_TRUE(miner3);
    *miner3 << 9 << 6 << 3;

    bvar::MultiDimension<bvar::LatencyRecorder> my_mlatencyrecorder("dump_latencyrecorder", labels);
    bvar::LatencyRecorder* my_latencyrecorder1 = my_mlatencyrecorder.get_stats(labels_value1);
    ASSERT_TRUE(my_latencyrecorder1);
    *my_latencyrecorder1 << 1 << 3 << 5;
    *my_latencyrecorder1 << 2 << 4 << 6;
    *my_latencyrecorder1 << 3 << 6 << 9;
    sleep(2);
    
    GFLAGS_NAMESPACE::SetCommandLineOption("bvar_dump_interval", old_bvar_dump_interval.c_str());
    GFLAGS_NAMESPACE::SetCommandLineOption("mbvar_dump", old_mbvar_dump.c_str());
    GFLAGS_NAMESPACE::SetCommandLineOption("mbvar_dump_prefix", old_mbvar_dump_prefix.c_str());
    GFLAGS_NAMESPACE::SetCommandLineOption("mbvar_dump_format", old_mbvar_dump_format.c_str());
}

TEST_F(MVariableTest, test_describe_exposed) {
    std::string bvar_name("request_count_describe");
    bvar::MultiDimension<bvar::Adder<int> > my_madder1(bvar_name, labels);

    std::string describe_str = bvar::MVariableBase::describe_exposed(bvar_name);

    std::ostringstream describe_oss;
    ASSERT_EQ(0, bvar::MVariableBase::describe_exposed(bvar_name, describe_oss));
    ASSERT_STREQ(describe_str.c_str(), describe_oss.str().c_str());
}

// A composite metric of our own: it exports a single gauge family whose
// samples carry a `region` label of their own. The reserved-label check in
// MultiDimension reads MetricFamily::reserved_labels, so it applies to any
// composite metric and is not hardcoded for Histogram's `le' or
// LatencyRecorder's `quantile'. This type is here to prove that.
class RegionedGauge {
public:
    explicit RegionedGauge(const std::string& region = "north")
        : _region(region) {}
    void set(int64_t value) { _value = value; }

    static std::vector<bvar::MetricFamily> list_metric_families() {
        return {{"", "gauge", {"region"}, {}}};
    }

    bool dump_samples(bvar::Dumper* dumper, size_t /*family_index*/,
                      const std::string& name,
                      butil::StringPiece labels) const {
        std::string key(name);
        key.push_back('{');
        if (!labels.empty()) {
            key.append(labels.data(), labels.size());
            key.push_back(',');
        }
        key.append("region=\"");
        key.append(_region);
        key.append("\"}");
        return dumper->dump_mvar(key, butil::Int64ToString(_value));
    }

private:
    std::string _region;
    int64_t _value{0};
};


// When a label of the enclosing MultiDimension collides with one reserved by
// the composite metric, the configuration is rejected: the constructor does
// not expose it, get_stats returns nothing and expose returns -1. The builtin
// Histogram (reserves `le'), LatencyRecorder (reserves `quantile') and the
// custom RegionedGauge (reserves `region') all go through the same check.
TEST_F(MVariableTest, multi_dimension_rejects_reserved_labels) {
    size_t nexposed = bvar::MVariableBase::count_exposed();

    // Precondition: RegionedGauge really is detected as a composite metric,
    // otherwise the reserved-label check never runs and the collision cases
    // below would prove nothing.
    ASSERT_TRUE(bvar::detail::IsCompositeMetric<RegionedGauge>::value);

    bvar::MultiDimension<bvar::Histogram> mhist(
        "hist_reserved_label_test", {"method", "le"},
        bvar::Histogram::BucketSchema({10, 20}));
    ASSERT_TRUE(mhist.name().empty());
    ASSERT_EQ(nullptr, mhist.get_stats({"echo", "10"}));
    ASSERT_EQ(-1, mhist.expose("hist_reserved_label_test"));
    ASSERT_TRUE(mhist.name().empty());

    // The guard lives in the shared expose_impl, so base-class pointers take
    // the same path.
    bvar::MVariableBase* mvariable_base = &mhist;
    ASSERT_EQ(-1, mvariable_base->expose("hist_reserved_label_base_test"));
    ASSERT_TRUE(mhist.name().empty());

    bvar::MVariable<std::list<std::string> >* typed_base = &mhist;
    ASSERT_EQ(-1, typed_base->expose_as("hist", "reserved_label_typed_base_test"));
    ASSERT_TRUE(mhist.name().empty());

    bvar::MultiDimension<bvar::LatencyRecorder> mlr(
        "latency_reserved_label_test", {"method", "quantile"});
    ASSERT_TRUE(mlr.name().empty());
    ASSERT_EQ(nullptr, mlr.get_stats({"echo", "0.99"}));
    ASSERT_EQ(-1, mlr.expose("latency_reserved_label_test"));

    // `region' is taken by RegionedGauge itself, so the enclosing
    // MultiDimension cannot use it as a label either.
    bvar::MultiDimension<RegionedGauge> conflict(
        "custom_reserved_label_test", {"idc", "region"});
    ASSERT_TRUE(conflict.name().empty());
    ASSERT_EQ(nullptr, conflict.get_stats({"bj", "north"}));
    ASSERT_EQ(-1, conflict.expose("custom_reserved_label_test"));
    ASSERT_TRUE(conflict.name().empty());

    // Reserved labels only block a same-named outer label: any other label
    // name works fine, and the reserved labels of the two builtin types are
    // independent of each other.
    bvar::MultiDimension<bvar::Histogram> valid_histogram(
        {"quantile"}, bvar::Histogram::BucketSchema({10, 20}));
    ASSERT_NE(nullptr, valid_histogram.get_stats({"0.99"}));
    bvar::MultiDimension<bvar::LatencyRecorder> valid_recorder({"le"});
    ASSERT_NE(nullptr, valid_recorder.get_stats({"10"}));
    bvar::MultiDimension<RegionedGauge> ok(
        "custom_reserved_label_ok", {"idc", "method"});
    ASSERT_NE(nullptr, ok.get_stats({"bj", "get"}));
    ASSERT_STREQ("custom_reserved_label_ok", ok.name().c_str());

    ASSERT_TRUE(ok.hide());
    ASSERT_EQ(nexposed, bvar::MVariableBase::count_exposed());
}
