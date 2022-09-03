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

#include <pthread.h>
#include <cstddef>
#include <memory>
#include <iostream>
#include <set>
#include <string>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "bvar/bvar.h"
#include "bvar/multi_dimension.h"
#include "butil/third_party/rapidjson/rapidjson.h"
#include "butil/third_party/rapidjson/document.h"

const size_t OPS_PER_THREAD = 200000;

const size_t OPS_PER_THREAD_INTRECORDER = 2000;

static const int num_thread = 24;

static const int idc_count = 20;
static const int method_count = 20;
static const int status_count = 50;

static const std::list<std::string> labels = {"idc", "method", "status"};

static void *thread_adder(void *arg) {
    bvar::Adder<uint64_t> *reducer = (bvar::Adder<uint64_t> *)arg;
    butil::Timer timer;
    timer.start();
    for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
        (*reducer) << 2;
    }
    timer.stop();
    return (void *)(timer.n_elapsed());
}

static long start_perf_test_with_madder(size_t num_thread, bvar::Adder<uint64_t>* adder) {
    EXPECT_TRUE(adder->valid());
    pthread_t threads[num_thread];
    for (size_t i = 0; i < num_thread; ++i) {
        pthread_create(&threads[i], NULL, &thread_adder, (void *)adder);
    }
    long totol_time = 0;
    for (size_t i = 0; i < num_thread; ++i) {
        void *ret = NULL; 
        pthread_join(threads[i], &ret);
        totol_time += (long)ret;
    }
    long avg_time = totol_time / (OPS_PER_THREAD * num_thread);
    EXPECT_EQ(2ul * num_thread * OPS_PER_THREAD, adder->get_value());
    return avg_time;
}

static void *thread_maxer(void *arg) {
    bvar::Maxer<uint64_t> *reducer = (bvar::Maxer<uint64_t> *)arg;
    butil::Timer timer;
    timer.start();
    for (size_t i = 1; i <= OPS_PER_THREAD; ++i) {
        (*reducer) << 2 * i * OPS_PER_THREAD;
    }
    timer.stop();
    return (void *)(timer.n_elapsed());
}

static long start_perf_test_with_mmaxer(size_t num_thread, bvar::Maxer<uint64_t>* maxer) {
    EXPECT_TRUE(maxer->valid());
    pthread_t threads[num_thread];
    for (size_t i = 0; i < num_thread; ++i) {
        pthread_create(&threads[i], NULL, &thread_maxer, (void *)maxer);
    }
    long totol_time = 0;
    for (size_t i = 0; i < num_thread; ++i) {
        void *ret = NULL; 
        pthread_join(threads[i], &ret);
        totol_time += (long)ret;
    }
    long avg_time = totol_time / (OPS_PER_THREAD * num_thread);
    EXPECT_EQ(2ul * OPS_PER_THREAD * OPS_PER_THREAD, maxer->get_value());
    return avg_time;
}

static void *thread_miner(void *arg) {
    bvar::Miner<uint64_t> *reducer = (bvar::Miner<uint64_t> *)arg;
    butil::Timer timer;
    timer.start();
    for (size_t i = 1; i <= OPS_PER_THREAD; ++i) {
        (*reducer) << -2 * i * OPS_PER_THREAD;
    }
    timer.stop();
    return (void *)(timer.n_elapsed());
}

static long start_perf_test_with_mminer(size_t num_thread, bvar::Miner<uint64_t>* miner) {
    EXPECT_TRUE(miner->valid());
    pthread_t threads[num_thread];
    for (size_t i = 0; i < num_thread; ++i) {
        pthread_create(&threads[i], NULL, &thread_miner, (void *)miner);
    }
    long totol_time = 0;
    for (size_t i = 0; i < num_thread; ++i) {
        void *ret = NULL; 
        pthread_join(threads[i], &ret);
        totol_time += (long)ret;
    }
    long avg_time = totol_time / (OPS_PER_THREAD * num_thread);
    EXPECT_EQ(-2ul * OPS_PER_THREAD * OPS_PER_THREAD, miner->get_value());
    return avg_time;
}

static void *thread_intrecorder(void *arg) {
    bvar::IntRecorder *reducer = (bvar::IntRecorder *)arg;
    butil::Timer timer;
    timer.start();
    for (size_t i = 1; i <= OPS_PER_THREAD_INTRECORDER; ++i) {
        (*reducer) << 2 * i * OPS_PER_THREAD_INTRECORDER;
    }
    timer.stop();
    return (void *)(timer.n_elapsed());
}

static long start_perf_test_with_mintrecorder(size_t num_thread, bvar::IntRecorder* intrecorder) {
    EXPECT_TRUE(intrecorder->valid());
    pthread_t threads[num_thread];
    for (size_t i = 0; i < num_thread; ++i) {
        pthread_create(&threads[i], NULL, &thread_intrecorder, (void *)intrecorder);
    }
    long totol_time = 0;
    for (size_t i = 0; i < num_thread; ++i) {
        void *ret = NULL; 
        pthread_join(threads[i], &ret);
        totol_time += (long)ret;
    }
    long avg_time = totol_time / (OPS_PER_THREAD_INTRECORDER * num_thread);
    EXPECT_EQ(2ul * (1 + OPS_PER_THREAD_INTRECORDER) / 2 * OPS_PER_THREAD_INTRECORDER, intrecorder->average());
    return avg_time;
}

class MultiDimensionTest : public testing::Test {
protected:
    void SetUp() {}
    void TearDown() {
    }
};

TEST_F(MultiDimensionTest, madder) {
    std::list<std::string> labels_value = {"bj", "get", "200"};
    bvar::MultiDimension<bvar::Adder<uint32_t> > my_madder1("request_count_madder_uint32_t", labels);
    bvar::Adder<uint32_t>* my_adder1 = my_madder1.get_stats(labels_value);
    ASSERT_TRUE(my_adder1);
    ASSERT_TRUE(my_adder1->valid());
    *my_adder1 << 2 << 4;
    ASSERT_EQ(6u, my_adder1->get_value());

    bvar::MultiDimension<bvar::Adder<double> > my_madder2("request_count_madder_double", labels);
    bvar::Adder<double>* my_adder2 = my_madder2.get_stats(labels_value);
    ASSERT_TRUE(my_adder2);
    ASSERT_TRUE(my_adder2->valid());
    *my_adder2 <<  2.0 << 4.0;
    ASSERT_EQ(6.0, my_adder2->get_value());

    bvar::MultiDimension<bvar::Adder<int> > my_madder3("request_count_madder_int", labels);
    bvar::Adder<int>* my_adder3 = my_madder3.get_stats(labels_value);
    ASSERT_TRUE(my_adder3);
    ASSERT_TRUE(my_adder3->valid());
    *my_adder3 << -9 << 1 << 0 << 3;
    ASSERT_EQ(-5, my_adder3->get_value());

    bvar::MultiDimension<bvar::Adder<std::string> > my_madder_str("my_string", labels);
    bvar::Adder<std::string> *my_str1 = my_madder_str.get_stats(labels_value);
    ASSERT_TRUE(my_str1);
    std::string str1 = "world";
    *my_str1 << "hello " << str1;
    ASSERT_STREQ("hello world", my_str1->get_value().c_str());
}

TEST_F(MultiDimensionTest, mmadder_perf) {
    std::list<std::string> labels_value = {"bj", "get", "200"};
    bvar::MultiDimension<bvar::Adder<uint64_t> > my_madder1("request_count_madder_uint64_t", labels);
    bvar::Adder<uint64_t>* my_adder = my_madder1.get_stats(labels_value);
    ASSERT_TRUE(my_adder);
    
    std::ostringstream oss;
    for (size_t i = 1; i <= num_thread; ++i) { 
        my_adder->reset();
        oss << i << '\t' << start_perf_test_with_madder(i, my_adder) << '\n';
    }
    LOG(INFO) << "Adder performance:\n" << oss.str();
}

TEST_F(MultiDimensionTest, mmaxer) {
    bvar::MultiDimension<bvar::Maxer<int> > my_mmaxer("request_count_mmaxer", labels);
    std::list<std::string> labels_value = {"bj", "get", "200"};
    bvar::Maxer<int>* my_maxer = my_mmaxer.get_stats(labels_value);
    ASSERT_TRUE(my_maxer);
    *my_maxer << 1 << 2 << 3;
    ASSERT_EQ(3, my_maxer->get_value());
}

TEST_F(MultiDimensionTest, mmaxer_perf) {
    bvar::MultiDimension<bvar::Maxer<uint64_t> > my_mmaxer("request_count_mmaxer", labels);
    std::list<std::string> labels_value = {"bj", "get", "200"};
    bvar::Maxer<uint64_t>* my_maxer = my_mmaxer.get_stats(labels_value);
    ASSERT_TRUE(my_maxer);
    
    std::ostringstream oss;
    for (size_t i = 1; i <= num_thread; ++i) { 
        my_maxer->reset();
        oss << i << '\t' << start_perf_test_with_mmaxer(i, my_maxer) << '\n';
    }
    LOG(INFO) << "Maxer performance:\n" << oss.str();
}

TEST_F(MultiDimensionTest, mminer) {
    bvar::MultiDimension<bvar::Miner<int> > my_mminer("client_request_count_mminer", labels);
    std::list<std::string> labels_value = {"bj", "get", "200"};
    bvar::Miner<int>* my_miner = my_mminer.get_stats(labels_value);
    ASSERT_TRUE(my_miner);
    *my_miner << 1 << 2 << 3;
    ASSERT_EQ(1, my_miner->get_value());
}

TEST_F(MultiDimensionTest, mminer_perf) {
    bvar::MultiDimension<bvar::Miner<uint64_t> > my_mminer("request_count_mminer", labels);
    std::list<std::string> labels_value = {"bj", "get", "200"};
    bvar::Miner<uint64_t>* my_miner = my_mminer.get_stats(labels_value);
    ASSERT_TRUE(my_miner);
    
    std::ostringstream oss;
    for (size_t i = 1; i <= num_thread; ++i) { 
        my_miner->reset();
        oss << i << '\t' << start_perf_test_with_mminer(i, my_miner) << '\n';
    }
    LOG(INFO) << "Miner performance:\n" << oss.str();
}


TEST_F(MultiDimensionTest, mintrecoder) {
    bvar::MultiDimension<bvar::IntRecorder> my_mintrecorder("client_request_count_mintrecorder", labels);
    std::list<std::string> labels_value = {"bj", "get", "200"};
    bvar::IntRecorder* my_intrecorder = my_mintrecorder.get_stats(labels_value);
    ASSERT_TRUE(my_intrecorder);
    *my_intrecorder << 1 << 2 << 3;
    ASSERT_EQ(2, my_intrecorder->average());
}

TEST_F(MultiDimensionTest, mintrecorder_perf) {
    bvar::MultiDimension<bvar::IntRecorder> my_mintrecorder("request_count_mintrecorder", labels);
    std::list<std::string> labels_value = {"bj", "get", "200"};
    bvar::IntRecorder* my_intrecorder = my_mintrecorder.get_stats(labels_value);
    ASSERT_TRUE(my_intrecorder);
    
    std::ostringstream oss;
    for (size_t i = 1; i <= num_thread; ++i) { 
        my_intrecorder->reset();
        oss << i << '\t' << start_perf_test_with_mintrecorder(i, my_intrecorder) << '\n';
    }
    LOG(INFO) << "IntRecorder performance:\n" << oss.str();
}

TEST_F(MultiDimensionTest, stats) {
    std::vector<std::list<std::string> > vec_labels;
    std::vector<std::list<std::string> > vec_labels_no_sort;
    bvar::MultiDimension<bvar::Adder<int> > my_madder("test_stats", labels);
    std::list<std::string> labels_value1 = {"tc", "get", "200"};
    vec_labels.push_back(labels_value1);
    vec_labels_no_sort.push_back(labels_value1);
    bvar::Adder<int>* adder1 = my_madder.get_stats(labels_value1);
    ASSERT_TRUE(adder1);
    std::vector<std::list<std::string> > ret_labels;
    my_madder.list_stats(&ret_labels);
    ASSERT_EQ(vec_labels, ret_labels);

    std::list<std::string> labels_value2 = {"nj", "get", "200"};
    bvar::Adder<int>* adder2 = my_madder.get_stats(labels_value2);
    ASSERT_TRUE(adder2);
    vec_labels.push_back(labels_value2);
    vec_labels_no_sort.push_back(labels_value2);
    my_madder.list_stats(&ret_labels);
    sort(vec_labels.begin(), vec_labels.end());
    sort(ret_labels.begin(), ret_labels.end());
    ASSERT_EQ(vec_labels, ret_labels);

    std::list<std::string> labels_value3 = {"hz", "post", "500"};
    bvar::Adder<int>* adder3 = my_madder.get_stats(labels_value3);
    ASSERT_TRUE(adder3);
    vec_labels.push_back(labels_value3);
    vec_labels_no_sort.push_back(labels_value3);
    my_madder.list_stats(&ret_labels);
    sort(vec_labels.begin(), vec_labels.end());
    sort(ret_labels.begin(), ret_labels.end());
    ASSERT_EQ(vec_labels, ret_labels);

    std::list<std::string> labels_value4 = {"gz", "post", "500"};
    bvar::Adder<int>* adder4 = my_madder.get_stats(labels_value4);
    ASSERT_TRUE(adder4);
    ASSERT_EQ(4, my_madder.count_stats());
    vec_labels.push_back(labels_value4);
    vec_labels_no_sort.push_back(labels_value4);
    my_madder.list_stats(&ret_labels);
    sort(vec_labels.begin(), vec_labels.end());
    sort(ret_labels.begin(), ret_labels.end());
    ASSERT_EQ(vec_labels, ret_labels);

    my_madder.delete_stats(labels_value4);
    ASSERT_EQ(3, my_madder.count_stats());
    vec_labels_no_sort.pop_back();
    my_madder.list_stats(&ret_labels);
    sort(vec_labels_no_sort.begin(), vec_labels_no_sort.end());
    sort(ret_labels.begin(), ret_labels.end());
    ASSERT_EQ(vec_labels_no_sort, ret_labels);
}

TEST_F(MultiDimensionTest, get_description) {
    bvar::MultiDimension<bvar::Adder<int> > my_madder("test_get_description", labels);
    std::list<std::string> labels_value1 = {"gz", "post", "200"};
    bvar::Adder<int>* adder1 = my_madder.get_stats(labels_value1);
    ASSERT_TRUE(adder1);
    *adder1 << 1;
    std::list<std::string> labels_value2 = {"tc", "post", "200"};
    bvar::Adder<int>* adder2 = my_madder.get_stats(labels_value2);
    ASSERT_TRUE(adder2);
    *adder2 << 2;

    const std::string description = my_madder.get_description();
    LOG(INFO) << "description=" << description;
    BUTIL_RAPIDJSON_NAMESPACE::Document doc;
    doc.Parse(description.c_str());
    ASSERT_FALSE(doc.HasParseError());
    ASSERT_TRUE(doc.IsObject());
    ASSERT_TRUE(doc.HasMember("name"));
    ASSERT_TRUE(doc["name"].IsString());
    ASSERT_STREQ("test_get_description", doc["name"].GetString());

    ASSERT_TRUE(doc.HasMember("stats_count"));
    ASSERT_TRUE(doc["stats_count"].IsInt());
    ASSERT_EQ(2, doc["stats_count"].GetInt());

    ASSERT_TRUE(doc.HasMember("labels"));
    ASSERT_TRUE(doc["labels"].IsArray());

    BUTIL_RAPIDJSON_NAMESPACE::Value& labels = doc["labels"];
    ASSERT_EQ(3, labels.Size());
    ASSERT_STREQ(labels[0].GetString(), "idc");
    ASSERT_STREQ(labels[1].GetString(), "method");
    ASSERT_STREQ(labels[2].GetString(), "status");
}

TEST_F(MultiDimensionTest, mlatencyrecorder) {
    std::string old_bvar_dump_interval;
    std::string old_mbvar_dump;
    std::string old_bvar_latency_p1;
    std::string old_bvar_latency_p2;
    std::string old_bvar_latency_p3;

    GFLAGS_NS::GetCommandLineOption("bvar_dump_interval", &old_bvar_dump_interval);
    GFLAGS_NS::GetCommandLineOption("mbvar_dump", &old_mbvar_dump);
    GFLAGS_NS::GetCommandLineOption("bvar_latency_p1", &old_bvar_latency_p1);
    GFLAGS_NS::GetCommandLineOption("bvar_latency_p2", &old_bvar_latency_p2);
    GFLAGS_NS::GetCommandLineOption("bvar_latency_p3", &old_bvar_latency_p3);

    GFLAGS_NS::SetCommandLineOption("bvar_dump_interval", "1");
    GFLAGS_NS::SetCommandLineOption("mbvar_dump", "true");
    GFLAGS_NS::SetCommandLineOption("bvar_latency_p1", "60");
    GFLAGS_NS::SetCommandLineOption("bvar_latency_p2", "70");
    GFLAGS_NS::SetCommandLineOption("bvar_latency_p3", "80");

    bvar::MultiDimension<bvar::LatencyRecorder> my_mlatencyrecorder("client_request_count_mlatencyrecorder", labels);
    std::list<std::string> labels_value = {"tc", "get", "200"};
    bvar::LatencyRecorder* my_latencyrecorder = my_mlatencyrecorder.get_stats(labels_value);
    ASSERT_TRUE(my_latencyrecorder);
    *my_latencyrecorder << 1 << 2 << 3 << 4 << 5 << 6 << 7;
    sleep(1);
    ASSERT_EQ(4, my_latencyrecorder->latency());
    ASSERT_EQ(7, my_latencyrecorder->max_latency());
    ASSERT_LE(7, my_latencyrecorder->qps());
    ASSERT_EQ(7, my_latencyrecorder->count());

    GFLAGS_NS::SetCommandLineOption("bvar_dump_interval", old_bvar_dump_interval.c_str());
    GFLAGS_NS::SetCommandLineOption("mbvar_dump", old_mbvar_dump.c_str());
    GFLAGS_NS::SetCommandLineOption("bvar_latency_p1", old_bvar_latency_p1.c_str());
    GFLAGS_NS::SetCommandLineOption("bvar_latency_p2", old_bvar_latency_p2.c_str());
    GFLAGS_NS::SetCommandLineOption("bvar_latency_p3", old_bvar_latency_p3.c_str());
}

TEST_F(MultiDimensionTest, mstatus) {
    bvar::MultiDimension<bvar::Status<int> > my_mstatus("my_mstatus", labels);
    std::list<std::string> labels_value {"tc", "get", "200"};
    bvar::Status<int>* my_status = my_mstatus.get_stats(labels_value);
    ASSERT_TRUE(my_status);
    my_status->set_value(1);
    ASSERT_EQ(1, my_status->get_value());
}

typedef size_t (*hash_fun)(const std::list<std::string>& labels_name);

static uint64_t perf_hash(hash_fun fn) {
    uint64_t cost = 0;
    for (int i = 0; i < idc_count; i++) {
        std::ostringstream oss_idc;
        oss_idc << "idc" << i;
        for (int j = 0; j < method_count; j++) {
            std::ostringstream oss_method;
            oss_method << "method" << j;
            for (int k = 0; k < status_count; k++) {
                std::ostringstream oss_status;
                oss_status << "status" << k;
                std::list<std::string> labels_value {oss_idc.str(), oss_method.str(), oss_status.str()};
                butil::Timer timer(butil::Timer::STARTED);
                size_t hash_code = fn(labels_value);
                EXPECT_NE(0, hash_code);
                timer.stop();
                cost += timer.n_elapsed();
            }
        }
    }
    return cost;
}

static size_t hash_fun1(const std::list<std::string>& labels_value) {
    size_t hash_value = 0;
    for (auto &k : labels_value) {
        hash_value += std::hash<std::string>()(k);
    }
    return hash_value;
}

static size_t hash_fun2(const std::list<std::string>& labels_value) {
    std::string hash_str;
    for (auto &k : labels_value) {
        hash_str.append(k);
    }
    return std::hash<std::string>()(hash_str);
}

static size_t hash_fun3(const std::list<std::string>& labels_value) {
    std::ostringstream oss;
    for (auto &k : labels_value) {
        oss << k;
    }
    return std::hash<std::string>()(oss.str());
}

TEST_F(MultiDimensionTest, test_hash) {
    std::ostringstream oss;
    oss << "hash_fun1 \t" << perf_hash(hash_fun1) << "\n"
        << "hash_fun2 \t" << perf_hash(hash_fun2) << "\n"
        << "hash_fun3 \t" << perf_hash(hash_fun3) << "\n";
    LOG(INFO) << "Hash fun performance:\n" << oss.str();
}

