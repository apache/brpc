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

// Date: 2018/09/19 14:51:06

#include <pthread.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "butil/macros.h"
#include "bthread/bthread.h"
#include "brpc/circuit_breaker.h"
#include "brpc/socket.h"
#include "brpc/server.h"
#include "echo.pb.h"

namespace {
void initialize_random() {
    srand(time(0));
}

const int kShortWindowSize = 500;
const int kLongWindowSize = 1000;
const int kShortWindowErrorPercent = 10;
const int kLongWindowErrorPercent = 5;
const int kMinIsolationDurationMs = 10;
const int kMaxIsolationDurationMs = 200;
const int kErrorCodeForFailed = 131;
const int kErrorCodeForSucc = 0;
const int kErrorCost = 1000;
const int kLatency = 1000;
const int kThreadNum = 3;
const int kHalfWindowSize = 0;
} // namespace

namespace brpc {
DECLARE_int32(circuit_breaker_short_window_size);
DECLARE_int32(circuit_breaker_long_window_size);
DECLARE_int32(circuit_breaker_short_window_error_percent);
DECLARE_int32(circuit_breaker_long_window_error_percent);
DECLARE_int32(circuit_breaker_min_isolation_duration_ms);
DECLARE_int32(circuit_breaker_max_isolation_duration_ms);
DECLARE_int32(circuit_breaker_half_open_window_size);
} // namespace brpc

int main(int argc, char* argv[]) {
    brpc::FLAGS_circuit_breaker_short_window_size = kShortWindowSize;
    brpc::FLAGS_circuit_breaker_long_window_size = kLongWindowSize;
    brpc::FLAGS_circuit_breaker_short_window_error_percent = kShortWindowErrorPercent;
    brpc::FLAGS_circuit_breaker_long_window_error_percent = kLongWindowErrorPercent;
    brpc::FLAGS_circuit_breaker_min_isolation_duration_ms = kMinIsolationDurationMs;
    brpc::FLAGS_circuit_breaker_max_isolation_duration_ms = kMaxIsolationDurationMs;
    brpc::FLAGS_circuit_breaker_half_open_window_size = kHalfWindowSize;
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

pthread_once_t initialize_random_control = PTHREAD_ONCE_INIT;

struct FeedbackControl {
    FeedbackControl(int req_num, int error_percent,
                 brpc::CircuitBreaker* circuit_breaker)
        : _req_num(req_num)
        , _error_percent(error_percent)
        , _circuit_breaker(circuit_breaker)
        , _healthy_cnt(0)
        , _unhealthy_cnt(0)
        , _healthy(true) {}
    int _req_num;
    int _error_percent;
    brpc::CircuitBreaker* _circuit_breaker;
    int _healthy_cnt;
    int _unhealthy_cnt;
    bool _healthy;
};

class CircuitBreakerTest : public ::testing::Test {
protected:
    CircuitBreakerTest() {
        pthread_once(&initialize_random_control, initialize_random);
    };

    virtual ~CircuitBreakerTest() {};
    virtual void SetUp() {};
    virtual void TearDown() {};

    static void* feed_back_thread(void* data) {
        FeedbackControl* fc = static_cast<FeedbackControl*>(data);
        for (int i = 0; i < fc->_req_num; ++i) {
            bool healthy = false;
            if (rand() % 100 < fc->_error_percent) {
                healthy = fc->_circuit_breaker->OnCallEnd(kErrorCodeForFailed, kErrorCost);
            } else {
                healthy = fc->_circuit_breaker->OnCallEnd(kErrorCodeForSucc, kLatency);
            }
            fc->_healthy = healthy;
            if (healthy) {
                ++fc->_healthy_cnt;
            } else {
                ++fc->_unhealthy_cnt;
            }
        }
        return fc;
    }

    void StartFeedbackThread(std::vector<pthread_t>* thread_list,
                             std::vector<std::unique_ptr<FeedbackControl>>* fc_list,
                             int error_percent) {
        thread_list->clear();
        fc_list->clear();
        for (int i = 0; i < kThreadNum; ++i) {
            pthread_t tid = 0;
            FeedbackControl* fc =
                new FeedbackControl(2 * kLongWindowSize, error_percent, &_circuit_breaker);
            fc_list->emplace_back(fc);
            pthread_create(&tid, nullptr, feed_back_thread, fc);
            thread_list->push_back(tid);
        }
    }

    brpc::CircuitBreaker _circuit_breaker;
};

TEST_F(CircuitBreakerTest, should_not_isolate) {
    std::vector<pthread_t> thread_list;
    std::vector<std::unique_ptr<FeedbackControl>> fc_list;
    StartFeedbackThread(&thread_list, &fc_list, 3);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = nullptr;
        ASSERT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_EQ(fc->_unhealthy_cnt, 0);
        EXPECT_TRUE(fc->_healthy);
    }
}

TEST_F(CircuitBreakerTest, should_isolate) {
    std::vector<pthread_t> thread_list;
    std::vector<std::unique_ptr<FeedbackControl>> fc_list;
    StartFeedbackThread(&thread_list, &fc_list, 50);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = nullptr;
        ASSERT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
        EXPECT_FALSE(fc->_healthy);
    }
}

TEST_F(CircuitBreakerTest, should_isolate_with_half_open) {
    std::vector<pthread_t> thread_list;
    std::vector<std::unique_ptr<FeedbackControl>> fc_list;
    StartFeedbackThread(&thread_list, &fc_list, 100);
    int total_failed = 0;
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = nullptr;
        ASSERT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
        EXPECT_FALSE(fc->_healthy);
        total_failed += fc->_unhealthy_cnt;
    }
    _circuit_breaker.Reset();

    int total_failed1 = 0;
    StartFeedbackThread(&thread_list, &fc_list, 100);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = nullptr;
        ASSERT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_FALSE(fc->_healthy);
        EXPECT_LE(fc->_healthy_cnt, kShortWindowSize);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
        total_failed1 += fc->_unhealthy_cnt;
    }

    // Enable the half-open state.
    // The first request cause _broken = true immediately.
    brpc::FLAGS_circuit_breaker_half_open_window_size = 10;
    _circuit_breaker.Reset();
    int total_failed2 = 0;
    StartFeedbackThread(&thread_list, &fc_list, 100);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = nullptr;
        ASSERT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_FALSE(fc->_healthy);
        EXPECT_LE(fc->_healthy_cnt, kShortWindowSize);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
        total_failed2 += fc->_unhealthy_cnt;
    }
    brpc::FLAGS_circuit_breaker_half_open_window_size = 0;

    EXPECT_EQ(kLongWindowSize * 2 * kThreadNum -
                  kShortWindowSize *
                      brpc::FLAGS_circuit_breaker_short_window_error_percent /
                      100,
              total_failed);

    EXPECT_EQ(total_failed1, total_failed);
    EXPECT_EQ(kLongWindowSize * 2 * kThreadNum, total_failed2);
}

TEST_F(CircuitBreakerTest, isolation_duration_grow_and_reset) {
    std::vector<pthread_t> thread_list;
    std::vector<std::unique_ptr<FeedbackControl>> fc_list;
    StartFeedbackThread(&thread_list, &fc_list, 100);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = nullptr;
        ASSERT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_FALSE(fc->_healthy);
        EXPECT_LE(fc->_healthy_cnt, kShortWindowSize);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
    }
    EXPECT_EQ(_circuit_breaker.isolation_duration_ms(), kMinIsolationDurationMs);

    _circuit_breaker.Reset();
    StartFeedbackThread(&thread_list, &fc_list, 100);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = nullptr;
        ASSERT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_FALSE(fc->_healthy);
        EXPECT_LE(fc->_healthy_cnt, kShortWindowSize);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
    }
    EXPECT_EQ(_circuit_breaker.isolation_duration_ms(), kMinIsolationDurationMs * 2);

    _circuit_breaker.Reset();
    StartFeedbackThread(&thread_list, &fc_list, 100);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = nullptr;
        ASSERT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_FALSE(fc->_healthy);
        EXPECT_LE(fc->_healthy_cnt, kShortWindowSize);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
    }
    EXPECT_EQ(_circuit_breaker.isolation_duration_ms(), kMinIsolationDurationMs * 4);

    _circuit_breaker.Reset();
    ::usleep((kMaxIsolationDurationMs + kMinIsolationDurationMs) * 1000);
    StartFeedbackThread(&thread_list, &fc_list, 100);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = nullptr;
        ASSERT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_FALSE(fc->_healthy);
        EXPECT_LE(fc->_healthy_cnt, kShortWindowSize);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
    }
    EXPECT_EQ(_circuit_breaker.isolation_duration_ms(), kMinIsolationDurationMs);

}

TEST_F(CircuitBreakerTest, maximum_isolation_duration) {
    brpc::FLAGS_circuit_breaker_max_isolation_duration_ms =
        brpc::FLAGS_circuit_breaker_min_isolation_duration_ms + 1;
    ASSERT_LT(brpc::FLAGS_circuit_breaker_max_isolation_duration_ms,
              2 * brpc::FLAGS_circuit_breaker_min_isolation_duration_ms);
    std::vector<pthread_t> thread_list;
    std::vector<std::unique_ptr<FeedbackControl>> fc_list;

    _circuit_breaker.Reset();
    StartFeedbackThread(&thread_list, &fc_list, 100);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = nullptr;
        ASSERT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_FALSE(fc->_healthy);
        EXPECT_LE(fc->_healthy_cnt, kShortWindowSize);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
    }
    EXPECT_EQ(_circuit_breaker.isolation_duration_ms(),
              brpc::FLAGS_circuit_breaker_max_isolation_duration_ms);
}
