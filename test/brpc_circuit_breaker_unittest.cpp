// brpc - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu, Inc.

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
const int kMinIsolationDurationMs = 100;
const int kMaxIsolationDurationMs = 1000;
const int kErrorCodeForFailed = 131;
const int kErrorCodeForSucc = 0;
const int kErrorCost = 1000;
const int kLatency = 1000;
const int kThreadNum = 3;
} // namespace

namespace brpc {
DECLARE_int32(circuit_breaker_short_window_size);
DECLARE_int32(circuit_breaker_long_window_size);
DECLARE_int32(circuit_breaker_short_window_error_percent);
DECLARE_int32(circuit_breaker_long_window_error_percent);
DECLARE_int32(circuit_breaker_min_isolation_duration_ms);
DECLARE_int32(circuit_breaker_max_isolation_duration_ms);
} // namespace brpc

int main(int argc, char* argv[]) {
    brpc::FLAGS_circuit_breaker_short_window_size = kShortWindowSize;
    brpc::FLAGS_circuit_breaker_long_window_size = kLongWindowSize;
    brpc::FLAGS_circuit_breaker_short_window_error_percent = kShortWindowErrorPercent;
    brpc::FLAGS_circuit_breaker_long_window_error_percent = kLongWindowErrorPercent;
    brpc::FLAGS_circuit_breaker_min_isolation_duration_ms = kMinIsolationDurationMs;
    brpc::FLAGS_circuit_breaker_max_isolation_duration_ms = kMaxIsolationDurationMs;
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
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
            pthread_create(&tid, NULL, feed_back_thread, fc);
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
        void* ret_data = NULL;
        EXPECT_EQ(pthread_join(thread_list[i], &ret_data), 0);
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
        void* ret_data = NULL;
        EXPECT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
        EXPECT_FALSE(fc->_healthy);
    }
}

TEST_F(CircuitBreakerTest, isolation_duration_grow) {
    _circuit_breaker.Reset();
    std::vector<pthread_t> thread_list;
    std::vector<std::unique_ptr<FeedbackControl>> fc_list;
    StartFeedbackThread(&thread_list, &fc_list, 100);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = NULL;
        EXPECT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_FALSE(fc->_healthy);
        EXPECT_LE(fc->_healthy_cnt, kShortWindowSize);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
    }
    EXPECT_EQ(_circuit_breaker.isolation_duration_ms(), kMinIsolationDurationMs * 2);

    _circuit_breaker.Reset();
    bthread_usleep(kMinIsolationDurationMs * 1000);
    StartFeedbackThread(&thread_list, &fc_list, 100);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = NULL;
        EXPECT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_FALSE(fc->_healthy);
        EXPECT_LE(fc->_healthy_cnt, kShortWindowSize);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
    }
    EXPECT_EQ(_circuit_breaker.isolation_duration_ms(), kMinIsolationDurationMs * 4);

    _circuit_breaker.Reset();
    bthread_usleep((kMaxIsolationDurationMs + kMinIsolationDurationMs) * 1000);
    StartFeedbackThread(&thread_list, &fc_list, 100);
    for (int  i = 0; i < kThreadNum; ++i) {
        void* ret_data = NULL;
        EXPECT_EQ(pthread_join(thread_list[i], &ret_data), 0);
        FeedbackControl* fc = static_cast<FeedbackControl*>(ret_data);
        EXPECT_FALSE(fc->_healthy);
        EXPECT_LE(fc->_healthy_cnt, kShortWindowSize);
        EXPECT_GT(fc->_unhealthy_cnt, 0);
    }
    EXPECT_EQ(_circuit_breaker.isolation_duration_ms(), kMinIsolationDurationMs);
}
