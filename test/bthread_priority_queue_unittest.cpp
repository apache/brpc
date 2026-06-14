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
#include <sched.h>
#include "bthread/bthread.h"
#include "bthread/task_group.h"

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
    static void SetUpTestSuite() {
        google::SetCommandLineOption("enable_bthread_priority_queue", "true");
        google::SetCommandLineOption("event_dispatcher_num", "4");
    }
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
            TaskArg* arg = new TaskArg{i / 2};
            ASSERT_EQ(0, bthread_start_background(&tid, &priority_attr,
                                                   priority_task_fn, arg));
        } else {
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

// Test 3: start_foreground (bthread_start_urgent) with GLOBAL_PRIORITY.
// Simulates ED calling StartInputEvent: ED bthread calls start_urgent,
// gets preempted into PQ via priority_to_run, child runs and ends,
// ending_sched steals ED from PQ to resume.
TEST_F(PriorityQueueTest, start_foreground_priority_to_run) {
    const int N = 200;

    struct EDSimArg {
        int n_tasks;
    };
    EDSimArg ed_arg{N};

    auto ed_fn = [](void* arg) -> void* {
        EDSimArg* ea = static_cast<EDSimArg*>(arg);
        bthread::TaskMeta* meta =
            bthread::TaskGroup::address_meta(bthread_self());
        meta->priority_index = 0;

        for (int i = 0; i < ea->n_tasks; ++i) {
            TaskArg* ta = new TaskArg{i};
            bthread_t child;
            bthread_start_urgent(&child, NULL, priority_task_fn, ta);
        }
        return NULL;
    };

    bthread_attr_t priority_attr = BTHREAD_ATTR_NORMAL;
    priority_attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    bthread_t ed_tid;
    ASSERT_EQ(0, bthread_start_background(&ed_tid, &priority_attr,
                                           ed_fn, &ed_arg));
    bthread_join(ed_tid, NULL);

    ASSERT_EQ(N, g_priority_count.load());
    std::lock_guard<std::mutex> lk(g_tid_mutex);
    ASSERT_EQ((size_t)N, g_executed_ids.size());
}

// Test 4: Multiple ED-like bthreads concurrently calling start_urgent.
// Verifies PQ correctness under concurrent preemption from multiple EDs.
TEST_F(PriorityQueueTest, multiple_eds_concurrent_preempt) {
    const int NUM_EDS = 4;
    const int TASKS_PER_ED = 50;
    const int TOTAL = NUM_EDS * TASKS_PER_ED;
    std::atomic<int> resume_count(0);

    struct EDArg {
        int ed_index;
        int n_children;
        std::atomic<int>* resume_count;
    };

    auto ed_fn = [](void* arg) -> void* {
        EDArg* ea = static_cast<EDArg*>(arg);
        bthread::TaskMeta* meta =
            bthread::TaskGroup::address_meta(bthread_self());
        meta->priority_index = ea->ed_index;

        std::vector<bthread_t> children;
        children.reserve(ea->n_children);
        for (int i = 0; i < ea->n_children; ++i) {
            int id = ea->ed_index * ea->n_children + i;
            TaskArg* ta = new TaskArg{id};
            bthread_t child;
            bthread_start_urgent(&child, NULL, priority_task_fn, ta);
            children.push_back(child);
            ea->resume_count->fetch_add(1, std::memory_order_relaxed);
        }
        for (auto c : children) {
            bthread_join(c, NULL);
        }
        return NULL;
    };

    bthread_attr_t priority_attr = BTHREAD_ATTR_NORMAL;
    priority_attr.flags |= BTHREAD_GLOBAL_PRIORITY;

    std::vector<EDArg> ed_args(NUM_EDS);
    std::vector<bthread_t> ed_tids(NUM_EDS);
    for (int i = 0; i < NUM_EDS; ++i) {
        ed_args[i] = {i, TASKS_PER_ED, &resume_count};
        ASSERT_EQ(0, bthread_start_background(&ed_tids[i], &priority_attr,
                                               ed_fn, &ed_args[i]));
    }

    for (int i = 0; i < NUM_EDS; ++i) {
        bthread_join(ed_tids[i], NULL);
    }

    ASSERT_EQ(TOTAL, g_priority_count.load());
    ASSERT_EQ(TOTAL, resume_count.load());
    std::lock_guard<std::mutex> lk(g_tid_mutex);
    ASSERT_EQ((size_t)TOTAL, g_executed_ids.size());
}

} // namespace
