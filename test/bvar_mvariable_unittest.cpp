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
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"
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

class MyStringView {
public:
    MyStringView() : _ptr(NULL), _len(0) {}
    MyStringView(const char* str)
        : _ptr(str),
          _len(str == NULL ? 0 : strlen(str)) {}
#if __cplusplus >= 201703L
    MyStringView(const std::string_view& str)
        : _ptr(str.data()), _len(str.size()) {}
#endif // __cplusplus >= 201703L
    MyStringView(const std::string& str)
        : _ptr(str.data()), _len(str.size()) {}
    MyStringView(const char* offset, size_t len)
        : _ptr(offset), _len(len) {}

    const char* data() const { return _ptr; }
    size_t size() const { return _len; }

    // Converts to `std::basic_string`.
    explicit operator std::string() const {
        if (NULL == _ptr) {
            return {};
        }
        return {_ptr, size()};
    }

    // Converts to butil::StringPiece.
    explicit operator butil::StringPiece() const {
        if (NULL == _ptr) {
            return {};
        }
        return {_ptr, size()};
    }

private:
    const char* _ptr;
    size_t _len;
};

bool operator==(const MyStringView& x, const std::string& y) {
    if (x.size() != y.size()) {
        return false;
    }

    return butil::StringPiece::wordmemcmp(x.data(), y.data(), x.size()) == 0;
}

bool operator==(const std::string& x, const MyStringView& y) {
    if (x.size() != y.size()) {
        return false;
    }

    return butil::StringPiece::wordmemcmp(x.data(), y.data(), x.size()) == 0;
}

static int g_exposed_count = 0;

template <typename KeyType, typename ValueType>
static void TestLabels() {
    std::string mbvar_name = butil::string_printf("my_madder_%d", g_exposed_count);
    KeyType labels{"idc", "method", "status"};
    bvar::MultiDimension<bvar::Adder<int>, KeyType> my_madder(mbvar_name, labels);
    ASSERT_EQ(labels.size(), my_madder.count_labels());
    ASSERT_STREQ(mbvar_name.c_str(), my_madder.name().c_str());
    ASSERT_EQ(labels, my_madder.labels());

    using ItemType = typename ValueType::value_type;
    ValueType labels_value{ItemType("cv"), ItemType("post"), ItemType("200")};
    bvar::Adder<int>* adder = my_madder.get_stats(labels_value);
    ASSERT_NE(nullptr, adder);
    ASSERT_TRUE(my_madder.has_stats(labels_value));
    ASSERT_EQ((size_t)1, my_madder.count_stats());
    {
        // Compatible with old API.
        bvar::Adder<int>* temp = my_madder.get_stats({"cv", "post", "200"});
        ASSERT_EQ(adder, temp);
    }
    *adder << g_exposed_count;
    ASSERT_EQ(g_exposed_count, adder->get_value());
    my_madder.delete_stats(labels_value);
    ASSERT_FALSE(my_madder.has_stats(labels_value));
    ASSERT_EQ((size_t)0, my_madder.count_stats());
}

TEST_F(MVariableTest, labels) {
    TestLabels<std::list<std::string>, std::list<std::string>>();
    TestLabels<std::list<std::string>, std::vector<std::string>>();
    TestLabels<std::list<std::string>, std::array<std::string, 3>>();

    TestLabels<std::vector<std::string>, std::list<std::string>>();
    TestLabels<std::vector<std::string>, std::vector<std::string>>();
    TestLabels<std::vector<std::string>, std::array<std::string, 3>>();

#if __cplusplus >= 201703L
    TestLabels<std::list<std::string>, std::list<std::string_view>>();
    TestLabels<std::list<std::string>, std::vector<std::string_view>>();
    TestLabels<std::list<std::string>, std::array<std::string_view, 3>>();
#endif // __cplusplus >= 201703L

    TestLabels<std::vector<std::string>, std::list<butil::StringPiece>>();
    TestLabels<std::vector<std::string>, std::vector<butil::StringPiece>>();
    TestLabels<std::vector<std::string>, std::array<butil::StringPiece, 3>>();

    TestLabels<std::list<std::string>, std::list<MyStringView>>();
    TestLabels<std::list<std::string>, std::vector<MyStringView>>();
    TestLabels<std::list<std::string>, std::array<MyStringView, 3>>();

    TestLabels<std::vector<std::string>, std::list<MyStringView>>();
    TestLabels<std::vector<std::string>, std::vector<MyStringView>>();
    TestLabels<std::vector<std::string>, std::array<MyStringView, 3>>();
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
    std::list<std::string> labels_value1 {"bj", "get", "200"};
    std::string bvar_name("request_count_describe");
    bvar::MultiDimension<bvar::Adder<int> > my_madder1(bvar_name, labels);

    std::string describe_str = bvar::MVariableBase::describe_exposed(bvar_name);

    std::ostringstream describe_oss;
    ASSERT_EQ(0, bvar::MVariableBase::describe_exposed(bvar_name, describe_oss));
    ASSERT_STREQ(describe_str.c_str(), describe_oss.str().c_str());
}
