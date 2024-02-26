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

#include <gtest/gtest.h>
#include "bthread/bthread.h"
#include "bthread/singleton_on_bthread_once.h"
#include "bthread/task_control.h"

namespace bthread {
extern TaskControl* g_task_control;
}

namespace {

bthread_once_t g_bthread_once_control;
bool g_bthread_once_started = false;
butil::atomic<int> g_bthread_once_count(0);

void init_routine() {
    bthread_usleep(2000 * 1000);
    g_bthread_once_count.fetch_add(1, butil::memory_order_relaxed);
}

void bthread_once_task() {
    bthread_once(&g_bthread_once_control, init_routine);
    //  `init_routine' only be called once.
    ASSERT_EQ(1, g_bthread_once_count.load(butil::memory_order_relaxed));
}

void* first_bthread_once_task(void*) {
    g_bthread_once_started = true;
    bthread_once_task();
    return NULL;
}


void* other_bthread_once_task(void*) {
    bthread_once_task();
    return NULL;
}

TEST(BthreadOnceTest, once) {
    bthread_t bid;
    ASSERT_EQ(0, bthread_start_background(
        &bid, NULL, first_bthread_once_task, NULL));
    while (!g_bthread_once_started) {
        bthread_usleep(1000);
    }
    ASSERT_NE(nullptr, bthread::g_task_control);
    int concurrency = bthread::g_task_control->concurrency();
    LOG(INFO) << "concurrency: " << concurrency;
    ASSERT_GT(concurrency, 0);
    std::vector<bthread_t> bids(concurrency * 100);
    for (auto& id : bids) {
        ASSERT_EQ(0, bthread_start_background(
            &id, NULL, other_bthread_once_task, NULL));
    }
    bthread_once_task();

    for (auto& id : bids) {
        bthread_join(id, NULL);
    }
    bthread_join(bid, NULL);
}

bool g_bthread_started = false;
butil::atomic<int> g_bthread_singleton_count(0);

class BthreadSingleton {
public:
    BthreadSingleton() {
        bthread_usleep(2000 * 1000);
        g_bthread_singleton_count.fetch_add(1, butil::memory_order_relaxed);
    }
};

void get_bthread_singleton() {
    auto instance = bthread::get_leaky_singleton<BthreadSingleton>();
    ASSERT_NE(nullptr, instance);
    // Only one BthreadSingleton instance has been created.
    ASSERT_EQ(1, g_bthread_singleton_count.load(butil::memory_order_relaxed));
}

void* first_get_bthread_singleton(void*) {
    g_bthread_started = true;
    get_bthread_singleton();
    return NULL;
}


void* get_bthread_singleton(void*) {
    get_bthread_singleton();
    return NULL;
}

// Singleton will definitely not cause deadlock,
// even if constructor of T will hang the bthread.
TEST(BthreadOnceTest, singleton) {
    bthread_t bid;
    ASSERT_EQ(0, bthread_start_background(
        &bid, NULL, first_get_bthread_singleton, NULL));
    while (!g_bthread_started) {
        bthread_usleep(1000);
    }
    ASSERT_NE(nullptr, bthread::g_task_control);
    int concurrency = bthread::g_task_control->concurrency();
    LOG(INFO) << "concurrency: " << concurrency;
    ASSERT_GT(concurrency, 0);
    std::vector<bthread_t> bids(concurrency * 100);
    for (auto& id : bids) {
        ASSERT_EQ(0, bthread_start_background(
            &id, NULL, get_bthread_singleton, NULL));
    }
    get_bthread_singleton();

    for (auto& id : bids) {
        bthread_join(id, NULL);
    }
    bthread_join(bid, NULL);
}

}