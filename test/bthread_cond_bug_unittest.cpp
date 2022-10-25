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

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <unistd.h>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "bthread/bthread.h"
#include "bthread/condition_variable.h"
#include "bthread/mutex.h"
#include "butil/logging.h"
#include "butil/macros.h"
#include "bvar/bvar.h"

DEFINE_int64(wait_us, 5, "wait us");
typedef std::unique_lock<bthread::Mutex> Lock;
typedef bthread::ConditionVariable Condition;
bthread::Mutex      g_mutex;
Condition           g_cond;
std::deque<int32_t> g_que;
const size_t        g_capacity = 2000;
const int PRODUCER_NUM = 5;
struct ProducerStat {
    std::atomic<int> loop_count;
    bvar::Adder<int> wait_count;
    bvar::Adder<int> wait_timeout_count;
    bvar::Adder<int> wait_success_count;
};
ProducerStat g_stat[PRODUCER_NUM];

void* print_func(void* arg) {
    int last_loop[PRODUCER_NUM] = {0};
    for (int j = 0; j < 10; j++) {
        usleep(1000000);
        for (int i = 0; i < PRODUCER_NUM; i++) {
            if (g_stat[i].loop_count.load() <= last_loop[i]) {
                LOG(ERROR) << "producer thread:" << i << " stopped";
                return nullptr;
            }
            LOG(INFO) << "producer stat idx:" << i
                      << " wait:" << g_stat[i].wait_count
                      << " wait_timeout:" << g_stat[i].wait_timeout_count
                      << " wait_success:" << g_stat[i].wait_success_count;
            g_stat[i].loop_count = g_stat[i].loop_count.load();
        }
    }
    return (void*)1;
}

void* produce_func(void* arg) {
    const int64_t wait_us = FLAGS_wait_us;
    LOG(INFO) << "wait us:" << wait_us;
    int64_t idx = (int64_t)(arg);
    int32_t i = 0;
    while (!bthread_stopped(bthread_self())) {
        //LOG(INFO) << "come to a new round " << idx << "round[" << i << "]";
        {
            Lock lock(g_mutex); 
            while (g_que.size() >= g_capacity && !bthread_stopped(bthread_self())) {
                g_stat[idx].wait_count << 1;
                //LOG(INFO) << "wait begin " << idx;
                int ret = g_cond.wait_for(lock, wait_us);
                if (ret == ETIMEDOUT) {
                    g_stat[idx].wait_timeout_count << 1;
                    //LOG_EVERY_SECOND(INFO) << "wait timeout " << idx;
                } else {
                    g_stat[idx].wait_success_count << 1;
                    //LOG_EVERY_SECOND(INFO) << "wait early " << idx;
                }
            }
            g_que.push_back(++i);
            //LOG(INFO) << "push back " << idx << " data[" << i << "]";
        }
        usleep(rand() % 20 + 5);
        g_stat[idx].loop_count.fetch_add(1);
    }
    LOG(INFO) << "producer func return, idx:" << idx;
    return nullptr;
}

void* consume_func(void* arg) {
    while (!bthread_stopped(bthread_self())) {
        bool need_notify = false;
        {
            Lock lock(g_mutex);
            need_notify = (g_que.size() == g_capacity);
            if (!g_que.empty()) {
                g_que.pop_front();
                LOG_EVERY_SECOND(INFO) << "pop a data";
            } else {
                LOG_EVERY_SECOND(INFO) << "que is empty";
            }
        }
        usleep(rand() % 300 + 500);
        if (need_notify) {
            //g_cond.notify_all();
            //LOG(WARNING) << "notify";
        }
    }
    LOG(INFO) << "consumer func return";
    return nullptr;
}

TEST(BthreadCondBugTest, test_bug) {
    bthread_t tids[PRODUCER_NUM];
    for (int i = 0; i < PRODUCER_NUM; i++) {
        bthread_start_background(&tids[i], NULL, produce_func, (void*)(int64_t)i);
    }
    bthread_t tid;
    bthread_start_background(&tid, NULL, consume_func, NULL);

    int64_t ret = (int64_t)print_func(nullptr);

    bthread_stop(tid);
    bthread_join(tid, nullptr);
    for (int i = 0; i < PRODUCER_NUM; i++) {
        bthread_stop(tids[i]);
        bthread_join(tids[i], nullptr);
    }

    ASSERT_EQ(ret, 1);
}
