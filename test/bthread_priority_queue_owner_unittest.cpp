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

// Tests for B2 priority queue owner dynamic changes:
//   1. New workers bind to available shards correctly
//   2. Priority tasks survive concurrency increases
//   3. Stress: concurrent priority tasks + concurrency scaling

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <atomic>
#include <vector>
#include <set>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <sched.h>
#include "bthread/bthread.h"

namespace {

std::atomic<int> g_count(0);
std::mutex g_mu;
std::set<int> g_ids;

void reset() {
    g_count.store(0);
    std::lock_guard<std::mutex> lk(g_mu);
    g_ids.clear();
}

struct Arg {
    int id;
    int sleep_us;  // simulate work
};

void* priority_fn(void* a) {
    Arg* arg = static_cast<Arg*>(a);
    if (arg->sleep_us > 0) {
        bthread_usleep(arg->sleep_us);
    }
    g_count.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_ids.insert(arg->id);
    }
    delete arg;
    return NULL;
}

class OwnerDynamicTest : public ::testing::Test {
protected:
    void SetUp() override {
        reset();
    }
};

// Test 1: Add workers while priority tasks are in-flight.
// New workers should bind to available priority shards and help process tasks.
TEST_F(OwnerDynamicTest, add_workers_during_priority_tasks) {
    int initial_concurrency = bthread_getconcurrency();

    const int N = 500;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    std::vector<bthread_t> tids;
    tids.reserve(N);

    // Submit first batch of priority tasks (slow tasks to keep them in-flight)
    for (int i = 0; i < N / 2; ++i) {
        bthread_t tid;
        Arg* arg = new Arg{i, 1000};  // 1ms sleep each
        ASSERT_EQ(0, bthread_start_background(&tid, &attr, priority_fn, arg));
        tids.push_back(tid);
    }

    // Increase concurrency — this creates new workers that should
    // bind to priority shards
    int new_concurrency = initial_concurrency + 4;
    if (new_concurrency <= 1024) {
        bthread_setconcurrency(new_concurrency);
    }

    // Submit second batch
    for (int i = N / 2; i < N; ++i) {
        bthread_t tid;
        Arg* arg = new Arg{i, 500};  // 0.5ms sleep
        ASSERT_EQ(0, bthread_start_background(&tid, &attr, priority_fn, arg));
        tids.push_back(tid);
    }

    // Join all
    for (auto tid : tids) {
        bthread_join(tid, NULL);
    }

    EXPECT_EQ(N, g_count.load());
    {
        std::lock_guard<std::mutex> lk(g_mu);
        EXPECT_EQ((size_t)N, g_ids.size());
        for (int i = 0; i < N; ++i) {
            EXPECT_TRUE(g_ids.count(i)) << "Missing task " << i;
        }
    }
}

// Test 2: Rapidly submit priority tasks while scaling concurrency up.
// Tests that no tasks are lost during owner binding transitions.
TEST_F(OwnerDynamicTest, rapid_submit_with_scaling) {
    int cur = bthread_getconcurrency();

    const int ROUNDS = 5;
    const int TASKS_PER_ROUND = 100;
    const int TOTAL = ROUNDS * TASKS_PER_ROUND;

    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    std::vector<bthread_t> all_tids;
    all_tids.reserve(TOTAL);

    for (int r = 0; r < ROUNDS; ++r) {
        // Submit a batch of priority tasks
        for (int i = 0; i < TASKS_PER_ROUND; ++i) {
            bthread_t tid;
            int id = r * TASKS_PER_ROUND + i;
            Arg* arg = new Arg{id, 200};
            ASSERT_EQ(0, bthread_start_background(&tid, &attr, priority_fn, arg));
            all_tids.push_back(tid);
        }

        // Try to scale up (will silently fail if already at max)
        int next = cur + 2;
        if (next <= 1024) {
            bthread_setconcurrency(next);
            cur = bthread_getconcurrency();
        }
    }

    for (auto tid : all_tids) {
        bthread_join(tid, NULL);
    }

    EXPECT_EQ(TOTAL, g_count.load());
    {
        std::lock_guard<std::mutex> lk(g_mu);
        EXPECT_EQ((size_t)TOTAL, g_ids.size());
    }
}

// Test 3: Mixed priority and normal tasks with concurrent worker scaling.
// Producer threads submit tasks while main thread scales concurrency.
TEST_F(OwnerDynamicTest, concurrent_submit_and_scale) {
    const int N_PRODUCERS = 3;
    const int TASKS_PER_PRODUCER = 200;
    const int TOTAL = N_PRODUCERS * TASKS_PER_PRODUCER;

    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    std::atomic<int> started(0);
    std::vector<bthread_t> all_tids[N_PRODUCERS];
    std::vector<std::thread> producers;

    for (int p = 0; p < N_PRODUCERS; ++p) {
        all_tids[p].resize(TASKS_PER_PRODUCER);
        producers.emplace_back([&, p]() {
            started.fetch_add(1);
            while (started.load() < N_PRODUCERS) {
                sched_yield();
            }
            for (int i = 0; i < TASKS_PER_PRODUCER; ++i) {
                int id = p * TASKS_PER_PRODUCER + i;
                Arg* arg = new Arg{id, 100};
                bthread_start_background(&all_tids[p][i], &attr, priority_fn, arg);
            }
        });
    }

    // Concurrently scale workers while producers are running
    int cur = bthread_getconcurrency();
    for (int i = 0; i < 3; ++i) {
        usleep(5000);  // 5ms
        int next = cur + 2;
        if (next <= 1024) {
            bthread_setconcurrency(next);
            cur = bthread_getconcurrency();
        }
    }

    for (auto& t : producers) {
        t.join();
    }

    // Join all bthreads
    for (int p = 0; p < N_PRODUCERS; ++p) {
        for (auto tid : all_tids[p]) {
            bthread_join(tid, NULL);
        }
    }

    EXPECT_EQ(TOTAL, g_count.load());
    {
        std::lock_guard<std::mutex> lk(g_mu);
        EXPECT_EQ((size_t)TOTAL, g_ids.size());
    }
}

// Test 4: Stress test — high volume of priority tasks with scaling.
TEST_F(OwnerDynamicTest, stress_priority_with_scaling) {
    const int N = 3000;

    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    std::vector<bthread_t> tids(N);
    int cur = bthread_getconcurrency();

    for (int i = 0; i < N; ++i) {
        Arg* arg = new Arg{i, 0};  // no sleep, pure throughput
        ASSERT_EQ(0, bthread_start_background(&tids[i], &attr, priority_fn, arg));

        // Scale up every 500 tasks
        if (i % 500 == 499) {
            int next = cur + 2;
            if (next <= 1024) {
                bthread_setconcurrency(next);
                cur = bthread_getconcurrency();
            }
        }
    }

    for (int i = 0; i < N; ++i) {
        bthread_join(tids[i], NULL);
    }

    EXPECT_EQ(N, g_count.load());
    {
        std::lock_guard<std::mutex> lk(g_mu);
        EXPECT_EQ((size_t)N, g_ids.size());
    }
}

} // namespace

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    google::SetCommandLineOption("enable_bthread_priority_queue", "true");
    google::SetCommandLineOption("priority_queue_shards", "4");
    return RUN_ALL_TESTS();
}
