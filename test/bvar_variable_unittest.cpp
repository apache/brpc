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

// Date: Fri Jul 24 17:19:40 CST 2015

#include <pthread.h>                                // pthread_*

#include <cstddef>
#include <memory>
#include <iostream>
#include <sstream>
#include "butil/time.h"
#include "butil/macros.h"

#include "bvar/bvar.h"

#include <gflags/gflags.h>
#include <gtest/gtest.h>

namespace bvar {
DECLARE_bool(bvar_log_dumpped);
}

namespace {

// overloading for operator<< does not work for gflags>=2.1
template <typename T>
std::string vec2string(const std::vector<T>& vec) {
    std::ostringstream os;
    os << '[';
    if (!vec.empty()) {
        os << vec[0];
        for (size_t i = 1; i < vec.size(); ++i) {
            os << ',' << vec[i];
        }
    }
    os << ']';
    return os.str();
}

class VariableTest : public testing::Test {
protected:
    void SetUp() {
    }
    void TearDown() {
        ASSERT_EQ(0UL, bvar::Variable::count_exposed());
    }
};

TEST_F(VariableTest, status) {
    bvar::Status<int> st1;
    st1.set_value(9);
    ASSERT_TRUE(st1.is_hidden());
#ifdef BAIDU_INTERNAL
    boost::any v1;
    st1.get_value(&v1);
    ASSERT_EQ(9, boost::any_cast<int>(v1));
#endif
    ASSERT_EQ(0, st1.expose("var1"));
    ASSERT_FALSE(st1.is_hidden());
    ASSERT_EQ("9", bvar::Variable::describe_exposed("var1"));
    std::vector<std::string> vars;
    bvar::Variable::list_exposed(&vars);
    ASSERT_EQ(1UL, vars.size());
    ASSERT_EQ("var1", vars[0]);
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());

    bvar::Status<int> st2;
    st2.set_value(10);
    ASSERT_TRUE(st2.is_hidden());
    ASSERT_EQ(-1, st2.expose("var1"));
    ASSERT_TRUE(st2.is_hidden());
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());
    ASSERT_EQ("10", st2.get_description());
    ASSERT_EQ("9", bvar::Variable::describe_exposed("var1"));
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());

    ASSERT_TRUE(st1.hide());
    ASSERT_TRUE(st1.is_hidden());
    ASSERT_EQ(0UL, bvar::Variable::count_exposed());
    ASSERT_EQ("", bvar::Variable::describe_exposed("var1"));
    ASSERT_EQ(0, st1.expose("var1"));
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());
    ASSERT_EQ("9", bvar::Variable::describe_exposed("var1"));
    
    ASSERT_EQ(0, st2.expose("var2"));
    ASSERT_EQ(2UL, bvar::Variable::count_exposed());
    ASSERT_EQ("9", bvar::Variable::describe_exposed("var1"));
    ASSERT_EQ("10", bvar::Variable::describe_exposed("var2"));
    bvar::Variable::list_exposed(&vars);
    ASSERT_EQ(2UL, vars.size());
    ASSERT_EQ("var1", vars[0]);
    ASSERT_EQ("var2", vars[1]);

    ASSERT_TRUE(st2.hide());
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());
    ASSERT_EQ("", bvar::Variable::describe_exposed("var2"));
    bvar::Variable::list_exposed(&vars);
    ASSERT_EQ(1UL, vars.size());
    ASSERT_EQ("var1", vars[0]);

    ASSERT_EQ(0, st2.expose("Var2 Again"));
    ASSERT_EQ("", bvar::Variable::describe_exposed("Var2 Again"));
    ASSERT_EQ("10", bvar::Variable::describe_exposed("var2_again"));
    bvar::Variable::list_exposed(&vars);
    ASSERT_EQ(2UL, vars.size());
    ASSERT_EQ("var1", vars[0]);
    ASSERT_EQ("var2_again", vars[1]);
    ASSERT_EQ(2UL, bvar::Variable::count_exposed());        

    bvar::Status<int> st3("var3", 11);
    ASSERT_EQ("var3", st3.name());
    ASSERT_EQ(3UL, bvar::Variable::count_exposed());
    ASSERT_EQ("11", bvar::Variable::describe_exposed("var3"));
    bvar::Variable::list_exposed(&vars);
    ASSERT_EQ(3UL, vars.size());
    ASSERT_EQ("var1", vars[0]);
    ASSERT_EQ("var3", vars[1]);
    ASSERT_EQ("var2_again", vars[2]);
    ASSERT_EQ(3UL, bvar::Variable::count_exposed());

    bvar::Status<int> st4("var4", 12);
    ASSERT_EQ("var4", st4.name());
    ASSERT_EQ(4UL, bvar::Variable::count_exposed());
#ifdef BAIDU_INTERNAL
    boost::any v4;
    st4.get_value(&v4);
    ASSERT_EQ(12, boost::any_cast<int>(v4));
#endif
    ASSERT_EQ("12", bvar::Variable::describe_exposed("var4"));
    bvar::Variable::list_exposed(&vars);
    ASSERT_EQ(4UL, vars.size());
    ASSERT_EQ("var1", vars[0]);
    ASSERT_EQ("var3", vars[1]);
    ASSERT_EQ("var4", vars[2]);
    ASSERT_EQ("var2_again", vars[3]);

    bvar::Status<void*> st5((void*)19UL);
    LOG(INFO) << st5;
#ifdef BAIDU_INTERNAL
    boost::any v5;
    st5.get_value(&v5);
    ASSERT_EQ((void*)19UL, boost::any_cast<void*>(v5));
#endif
    ASSERT_EQ("0x13", st5.get_description());
}

namespace foo {
namespace bar {
class Apple {};
class BaNaNa {};
class Car_Rot {};
class RPCTest {};
class HELLO {};
}
}

TEST_F(VariableTest, expose) {
    bvar::Status<int> c1;
    ASSERT_EQ(0, c1.expose_as("foo::bar::Apple", "c1"));
    ASSERT_EQ("foo_bar_apple_c1", c1.name());
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());

    ASSERT_EQ(0, c1.expose_as("foo.bar::BaNaNa", "c1"));
    ASSERT_EQ("foo_bar_ba_na_na_c1", c1.name());
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());

    ASSERT_EQ(0, c1.expose_as("foo::bar.Car_Rot", "c1"));
    ASSERT_EQ("foo_bar_car_rot_c1", c1.name());
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());

    ASSERT_EQ(0, c1.expose_as("foo-bar-RPCTest", "c1"));
    ASSERT_EQ("foo_bar_rpctest_c1", c1.name());
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());

    ASSERT_EQ(0, c1.expose_as("foo-bar-HELLO", "c1"));
    ASSERT_EQ("foo_bar_hello_c1", c1.name());
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());
    
    ASSERT_EQ(0, c1.expose("c1"));
    ASSERT_EQ("c1", c1.name());
    ASSERT_EQ(1UL, bvar::Variable::count_exposed());
}

class MyDumper : public bvar::Dumper {
public:
    bool dump(const std::string& name,
              const butil::StringPiece& description) {
        _list.push_back(std::make_pair(name, description.as_string()));
        return true;
    }

    std::vector<std::pair<std::string, std::string> > _list;
};

int print_int(void*) {
    return 5;
}

TEST_F(VariableTest, dump) {
    MyDumper d;

    // Nothing to dump yet.
    bvar::FLAGS_bvar_log_dumpped = true;
    ASSERT_EQ(0, bvar::Variable::dump_exposed(&d, NULL));
    ASSERT_TRUE(d._list.empty());

    bvar::Adder<int> v2("var2");
    v2 << 2;
    bvar::Status<int> v1("var1", 1);
    bvar::Status<int> v1_2("var1", 12);
    bvar::Status<int> v3("foo.bar.Apple", "var3", 3);
    bvar::Adder<int> v4("foo.bar.BaNaNa", "var4");
    v4 << 4;
    bvar::BasicPassiveStatus<int> v5(
        "foo::bar::Car_Rot", "var5", print_int, NULL);
    
    ASSERT_EQ(5, bvar::Variable::dump_exposed(&d, NULL));
    ASSERT_EQ(5UL, d._list.size());
    int i = 0;
    ASSERT_EQ("foo_bar_apple_var3", d._list[i++ / 2].first);
    ASSERT_EQ("3", d._list[i++ / 2].second);
    ASSERT_EQ("foo_bar_ba_na_na_var4", d._list[i++ / 2].first);
    ASSERT_EQ("4", d._list[i++ / 2].second);
    ASSERT_EQ("foo_bar_car_rot_var5", d._list[i++ / 2].first);
    ASSERT_EQ("5", d._list[i++ / 2].second);
    ASSERT_EQ("var1", d._list[i++ / 2].first);
    ASSERT_EQ("1", d._list[i++ / 2].second);
    ASSERT_EQ("var2", d._list[i++ / 2].first);
    ASSERT_EQ("2", d._list[i++ / 2].second);

    d._list.clear();
    bvar::DumpOptions opts;
    opts.white_wildcards = "foo_bar_*";
    opts.black_wildcards = "*var5";
    ASSERT_EQ(2, bvar::Variable::dump_exposed(&d, &opts));
    ASSERT_EQ(2UL, d._list.size());
    i = 0;
    ASSERT_EQ("foo_bar_apple_var3", d._list[i++ / 2].first);
    ASSERT_EQ("3", d._list[i++ / 2].second);
    ASSERT_EQ("foo_bar_ba_na_na_var4", d._list[i++ / 2].first);
    ASSERT_EQ("4", d._list[i++ / 2].second);

    d._list.clear();
    opts = bvar::DumpOptions();
    opts.white_wildcards = "*?rot*";
    ASSERT_EQ(1, bvar::Variable::dump_exposed(&d, &opts));
    ASSERT_EQ(1UL, d._list.size());
    i = 0;
    ASSERT_EQ("foo_bar_car_rot_var5", d._list[i++ / 2].first);
    ASSERT_EQ("5", d._list[i++ / 2].second);

    d._list.clear();
    opts = bvar::DumpOptions();
    opts.white_wildcards = "";
    opts.black_wildcards = "var2;var1";
    ASSERT_EQ(3, bvar::Variable::dump_exposed(&d, &opts));
    ASSERT_EQ(3UL, d._list.size());
    i = 0;
    ASSERT_EQ("foo_bar_apple_var3", d._list[i++ / 2].first);
    ASSERT_EQ("3", d._list[i++ / 2].second);
    ASSERT_EQ("foo_bar_ba_na_na_var4", d._list[i++ / 2].first);
    ASSERT_EQ("4", d._list[i++ / 2].second);
    ASSERT_EQ("foo_bar_car_rot_var5", d._list[i++ / 2].first);
    ASSERT_EQ("5", d._list[i++ / 2].second);

    d._list.clear();
    opts = bvar::DumpOptions();
    opts.white_wildcards = "";
    opts.black_wildcards = "f?o_b?r_*;not_exist";
    ASSERT_EQ(2, bvar::Variable::dump_exposed(&d, &opts));
    ASSERT_EQ(2UL, d._list.size());
    i = 0;
    ASSERT_EQ("var1", d._list[i++ / 2].first);
    ASSERT_EQ("1", d._list[i++ / 2].second);
    ASSERT_EQ("var2", d._list[i++ / 2].first);
    ASSERT_EQ("2", d._list[i++ / 2].second);

    d._list.clear();
    opts = bvar::DumpOptions();
    opts.question_mark = '$';
    opts.white_wildcards = "";
    opts.black_wildcards = "f$o_b$r_*;not_exist";
    ASSERT_EQ(2, bvar::Variable::dump_exposed(&d, &opts));
    ASSERT_EQ(2UL, d._list.size());
    i = 0;
    ASSERT_EQ("var1", d._list[i++ / 2].first);
    ASSERT_EQ("1", d._list[i++ / 2].second);
    ASSERT_EQ("var2", d._list[i++ / 2].first);
    ASSERT_EQ("2", d._list[i++ / 2].second);

    d._list.clear();
    opts = bvar::DumpOptions();
    opts.white_wildcards = "not_exist";
    ASSERT_EQ(0, bvar::Variable::dump_exposed(&d, &opts));
    ASSERT_EQ(0UL, d._list.size());

    d._list.clear();
    opts = bvar::DumpOptions();
    opts.white_wildcards = "not_exist;f??o_bar*";
    ASSERT_EQ(0, bvar::Variable::dump_exposed(&d, &opts));
    ASSERT_EQ(0UL, d._list.size());
}

TEST_F(VariableTest, latency_recorder) {
    bvar::LatencyRecorder rec;
    rec << 1 << 2 << 3;
    ASSERT_EQ(3, rec.count());
    ASSERT_EQ(-1, rec.expose(""));
    ASSERT_EQ(-1, rec.expose("latency"));
    ASSERT_EQ(-1, rec.expose("Latency"));

    
    ASSERT_EQ(0, rec.expose("FooBar__latency"));
    std::vector<std::string> names;
    bvar::Variable::list_exposed(&names);
    std::sort(names.begin(), names.end());
    ASSERT_EQ(11UL, names.size()) << vec2string(names);
    ASSERT_EQ("foo_bar_count", names[0]);
    ASSERT_EQ("foo_bar_latency", names[1]);
    ASSERT_EQ("foo_bar_latency_80", names[2]);
    ASSERT_EQ("foo_bar_latency_90", names[3]);
    ASSERT_EQ("foo_bar_latency_99", names[4]);
    ASSERT_EQ("foo_bar_latency_999", names[5]);
    ASSERT_EQ("foo_bar_latency_9999", names[6]);
    ASSERT_EQ("foo_bar_latency_cdf", names[7]);
    ASSERT_EQ("foo_bar_latency_percentiles", names[8]);
    ASSERT_EQ("foo_bar_max_latency", names[9]);
    ASSERT_EQ("foo_bar_qps", names[10]);

    ASSERT_EQ(0, rec.expose("ApplePie"));
    bvar::Variable::list_exposed(&names);
    std::sort(names.begin(), names.end());
    ASSERT_EQ(11UL, names.size());
    ASSERT_EQ("apple_pie_count", names[0]);
    ASSERT_EQ("apple_pie_latency", names[1]);
    ASSERT_EQ("apple_pie_latency_80", names[2]);
    ASSERT_EQ("apple_pie_latency_90", names[3]);
    ASSERT_EQ("apple_pie_latency_99", names[4]);
    ASSERT_EQ("apple_pie_latency_999", names[5]);
    ASSERT_EQ("apple_pie_latency_9999", names[6]);
    ASSERT_EQ("apple_pie_latency_cdf", names[7]);
    ASSERT_EQ("apple_pie_latency_percentiles", names[8]);
    ASSERT_EQ("apple_pie_max_latency", names[9]);
    ASSERT_EQ("apple_pie_qps", names[10]);

    ASSERT_EQ(0, rec.expose("BaNaNa::Latency"));
    bvar::Variable::list_exposed(&names);
    std::sort(names.begin(), names.end());
    ASSERT_EQ(11UL, names.size());
    ASSERT_EQ("ba_na_na_count", names[0]);
    ASSERT_EQ("ba_na_na_latency", names[1]);
    ASSERT_EQ("ba_na_na_latency_80", names[2]);
    ASSERT_EQ("ba_na_na_latency_90", names[3]);
    ASSERT_EQ("ba_na_na_latency_99", names[4]);
    ASSERT_EQ("ba_na_na_latency_999", names[5]);
    ASSERT_EQ("ba_na_na_latency_9999", names[6]);
    ASSERT_EQ("ba_na_na_latency_cdf", names[7]);
    ASSERT_EQ("ba_na_na_latency_percentiles", names[8]);
    ASSERT_EQ("ba_na_na_max_latency", names[9]);
    ASSERT_EQ("ba_na_na_qps", names[10]);
}

TEST_F(VariableTest, recursive_mutex) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    butil::Timer timer;
    const size_t N = 1000000;
    timer.start();
    for (size_t i = 0; i < N; ++i) {
        BAIDU_SCOPED_LOCK(mutex);
    }
    timer.stop();
    LOG(INFO) << "Each recursive mutex lock/unlock pair take "
              << timer.n_elapsed() / N << "ns";
}
} // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}
