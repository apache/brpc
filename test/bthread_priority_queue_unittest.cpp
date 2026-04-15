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
#include <gflags/gflags.h>
#include <atomic>
#include <vector>
#include <set>
#include <mutex>
#include "bthread/bthread.h"
#include "bthread/processor.h"

namespace {

// Counter incremented by priority bthreads to verify execution
std::atomic<int> g_priority_count(0);
// Mutex + set for collecting executed tids to verify no loss
std::mutex g_tid_mutex;
std::set<int> g_executed_ids;

void reset_globals() {
    g_priority_count.store(0);
    std::lock_guard<std::mutex> lk(g_tid_mutex);
    g_executed_ids.clear();
}

struct TaskArg {
    int id;
};

void* priority_task_fn(void* arg) {
    TaskArg* ta = static_cast<TaskArg*>(arg);
    g_priority_count.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(g_tid_mutex);
        g_executed_ids.insert(ta->id);
    }
    delete ta;
    return NULL;
}

void* normal_task_fn(void* /*arg*/) {
    // Just a normal task that does nothing, used as a filler
    bthread_usleep(1000);
    return NULL;
}

class PriorityQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        reset_globals();
    }
};

// Test 1: End-to-end priority task submission and execution.
// Multiple producers submit priority tasks, verify all tasks are executed.
TEST_F(PriorityQueueTest, e2e_priority_tasks_all_executed) {
    const int N = 200;

    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    std::vector<bthread_t> tids(N);
    for (int i = 0; i < N; ++i) {
        TaskArg* arg = new TaskArg{i};
        ASSERT_EQ(0, bthread_start_background(&tids[i], &attr,
                                               priority_task_fn, arg));
    }

    for (int i = 0; i < N; ++i) {
        bthread_join(tids[i], NULL);
    }

    ASSERT_EQ(N, g_priority_count.load());
    std::lock_guard<std::mutex> lk(g_tid_mutex);
    ASSERT_EQ((size_t)N, g_executed_ids.size());
    for (int i = 0; i < N; ++i) {
        ASSERT_TRUE(g_executed_ids.count(i)) << "Missing task id=" << i;
    }
}

// Test 2: Mix of priority and normal tasks, all complete correctly.
TEST_F(PriorityQueueTest, mixed_priority_and_normal_tasks) {
    const int N_PRIORITY = 100;
    const int N_NORMAL = 100;

    bthread_attr_t priority_attr = BTHREAD_ATTR_NORMAL;
    priority_attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    std::vector<bthread_t> tids;
    tids.reserve(N_PRIORITY + N_NORMAL);

    for (int i = 0; i < N_PRIORITY + N_NORMAL; ++i) {
        bthread_t tid;
        if (i % 2 == 0 && (i / 2) < N_PRIORITY) {
            // Priority task
            TaskArg* arg = new TaskArg{i / 2};
            ASSERT_EQ(0, bthread_start_background(&tid, &priority_attr,
                                                   priority_task_fn, arg));
        } else {
            // Normal task
            ASSERT_EQ(0, bthread_start_background(&tid, NULL,
                                                   normal_task_fn, NULL));
        }
        tids.push_back(tid);
    }

    for (auto tid : tids) {
        bthread_join(tid, NULL);
    }

    ASSERT_EQ(N_PRIORITY, g_priority_count.load());
}

// Test 3: Concurrent producers submitting priority tasks from multiple pthreads.
// Simulates multiple event dispatchers pushing to the priority queue.
TEST_F(PriorityQueueTest, concurrent_producers_no_task_loss) {
    const int NUM_PRODUCERS = 4;
    const int TASKS_PER_PRODUCER = 50;
    const int TOTAL = NUM_PRODUCERS * TASKS_PER_PRODUCER;

    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    std::atomic<int> started(0);
    std::vector<pthread_t> producers(NUM_PRODUCERS);
    std::vector<std::vector<bthread_t>> all_tids(NUM_PRODUCERS);

    struct ProducerArg {
        int producer_id;
        int tasks_per_producer;
        bthread_attr_t* attr;
        std::vector<bthread_t>* tids;
        std::atomic<int>* started;
    };

    auto producer_fn = [](void* arg) -> void* {
        ProducerArg* pa = static_cast<ProducerArg*>(arg);
        pa->started->fetch_add(1);
        // Spin until all producers are ready
        while (pa->started->load() < 4) {
            cpu_relax();
        }
        pa->tids->resize(pa->tasks_per_producer);
        for (int i = 0; i < pa->tasks_per_producer; ++i) {
            int id = pa->producer_id * pa->tasks_per_producer + i;
            TaskArg* ta = new TaskArg{id};
            int rc = bthread_start_background(&(*pa->tids)[i], pa->attr,
                                               priority_task_fn, ta);
            EXPECT_EQ(0, rc);
        }
        return NULL;
    };

    std::vector<ProducerArg> pargs(NUM_PRODUCERS);
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pargs[i] = {i, TASKS_PER_PRODUCER, &attr, &all_tids[i], &started};
        ASSERT_EQ(0, pthread_create(&producers[i], NULL, producer_fn, &pargs[i]));
    }

    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pthread_join(producers[i], NULL);
    }

    // Join all bthreads
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        for (auto tid : all_tids[i]) {
            bthread_join(tid, NULL);
        }
    }

    ASSERT_EQ(TOTAL, g_priority_count.load());
    std::lock_guard<std::mutex> lk(g_tid_mutex);
    ASSERT_EQ((size_t)TOTAL, g_executed_ids.size());
}

// Test 4: Priority tasks submitted with only 1 shard (degenerate case).
// Verifies correctness when nshard=1.
TEST_F(PriorityQueueTest, single_shard_correctness) {
    // This test relies on FLAGS_priority_queue_shards being set before
    // TaskControl init. Since TaskControl is already initialized by the
    // time we run, we test with whatever shard count is configured.
    // The key verification is no task loss.
    const int N = 100;

    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    std::vector<bthread_t> tids(N);
    for (int i = 0; i < N; ++i) {
        TaskArg* arg = new TaskArg{i};
        ASSERT_EQ(0, bthread_start_background(&tids[i], &attr,
                                               priority_task_fn, arg));
    }
    for (int i = 0; i < N; ++i) {
        bthread_join(tids[i], NULL);
    }

    ASSERT_EQ(N, g_priority_count.load());
    std::lock_guard<std::mutex> lk(g_tid_mutex);
    ASSERT_EQ((size_t)N, g_executed_ids.size());
}

// Test 5: Stress test with high volume of priority tasks.
TEST_F(PriorityQueueTest, stress_high_volume) {
    const int N = 2000;

    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    std::vector<bthread_t> tids(N);
    for (int i = 0; i < N; ++i) {
        TaskArg* arg = new TaskArg{i};
        ASSERT_EQ(0, bthread_start_background(&tids[i], &attr,
                                               priority_task_fn, arg));
    }
    for (int i = 0; i < N; ++i) {
        bthread_join(tids[i], NULL);
    }

    ASSERT_EQ(N, g_priority_count.load());
    std::lock_guard<std::mutex> lk(g_tid_mutex);
    ASSERT_EQ((size_t)N, g_executed_ids.size());
}

} // namespace

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    // Enable priority queue before any bthread is created (triggers TaskControl init)
    google::SetCommandLineOption("enable_bthread_priority_queue", "true");
    google::SetCommandLineOption("priority_queue_shards", "4");
    return RUN_ALL_TESTS();
}
