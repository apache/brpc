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
#include <memory>
#include <string>
#include <vector>

#include <bthread/bthread.h>
#include <bthread/countdown_event.h>
#include "butil/time.h"

DECLARE_int32(task_group_runqueue_capacity);

namespace bthread {
DECLARE_int32(bthread_concurrency);
DECLARE_int32(bthread_min_concurrency);
}  // namespace bthread

namespace {

const int kRunQueueCapacity = 8;
const int kMaxDepth = kRunQueueCapacity + 8;
const int kNumRounds = 3;
const int kWaitSeconds = 10;

struct RunState;

struct TaskArg {
    RunState* state;
    int index;
    int next_index;
    int leaf_index;
};

struct RunState {
    RunState(int num_chains, bool yield_between_starts)
        : tasks_per_chain(2 * kMaxDepth + 1)
        , task_count(num_chains * tasks_per_chain)
        , args(task_count)
        , started(new std::atomic<int>[task_count])
        , finished(new std::atomic<int>[task_count])
        , done(task_count)
        , yield_between_starts(yield_between_starts) {
        for (int chain = 0; chain < num_chains; ++chain) {
            const int base = chain * tasks_per_chain;
            for (int depth = 0; depth <= kMaxDepth; ++depth) {
                const int index = base + depth;
                args[index].state = this;
                args[index].index = index;
                args[index].next_index = -1;
                args[index].leaf_index = -1;
                if (depth < kMaxDepth) {
                    args[index].next_index = index + 1;
                    args[index].leaf_index = base + kMaxDepth + 1 + depth;

                    const int leaf = args[index].leaf_index;
                    args[leaf].state = this;
                    args[leaf].index = leaf;
                    args[leaf].next_index = -1;
                    args[leaf].leaf_index = -1;
                }
            }
        }
        for (int i = 0; i < task_count; ++i) {
            started[i].store(0);
            finished[i].store(0);
        }
    }

    static void* run_task(void* void_arg) {
        TaskArg* arg = static_cast<TaskArg*>(void_arg);
        RunState* state = arg->state;
        if (state->started[arg->index].fetch_add(1) != 0) {
            state->duplicate_starts.fetch_add(1);
        }
        state->total_started.fetch_add(1);

        if (state->yield_between_starts) {
            bthread_yield();
        }
        if (arg->next_index >= 0) {
            state->start_child(arg->next_index);
        }
        if (state->yield_between_starts) {
            bthread_yield();
        }
        if (arg->leaf_index >= 0) {
            state->start_child(arg->leaf_index);
        }

        state->finished[arg->index].fetch_add(1);
        state->total_finished.fetch_add(1);
        state->done.signal();
        return nullptr;
    }

    void start_child(int index) {
        bthread_t tid;
        const int rc = bthread_start_urgent(
            &tid, nullptr, run_task, &args[index]);
        if (rc != 0) {
            start_errors.fetch_add(1);
        }
    }

    void start_roots(int num_chains) {
        for (int chain = 0; chain < num_chains; ++chain) {
            bthread_t tid;
            const int rc = bthread_start_background(
                &tid, nullptr, run_task, &args[chain * tasks_per_chain]);
            if (rc != 0) {
                start_errors.fetch_add(1);
            }
        }
    }

    bool wait_and_check() {
        const int rc = done.timed_wait(butil::seconds_from_now(kWaitSeconds));
        if (rc != 0) {
            ADD_FAILURE() << "runqueue saturation did not finish, rc=" << rc
                          << " tasks=" << task_count
                          << " started=" << total_started.load()
                          << " finished=" << total_finished.load()
                          << " start_errors=" << start_errors.load();
            return false;
        }

        int bad_task_states = 0;
        for (int i = 0; i < task_count; ++i) {
            if (started[i].load() != 1 || finished[i].load() != 1) {
                ++bad_task_states;
            }
        }
        EXPECT_EQ(0, start_errors.load());
        EXPECT_EQ(0, duplicate_starts.load());
        EXPECT_EQ(task_count, total_started.load());
        EXPECT_EQ(task_count, total_finished.load());
        EXPECT_EQ(0, bad_task_states);
        return start_errors.load() == 0 && duplicate_starts.load() == 0 &&
               total_started.load() == task_count &&
               total_finished.load() == task_count && bad_task_states == 0;
    }

    const int tasks_per_chain;
    const int task_count;
    std::vector<TaskArg> args;
    std::unique_ptr<std::atomic<int>[]> started;
    std::unique_ptr<std::atomic<int>[]> finished;
    std::atomic<int> start_errors{0};
    std::atomic<int> duplicate_starts{0};
    std::atomic<int> total_started{0};
    std::atomic<int> total_finished{0};
    bthread::CountdownEvent done;
    const bool yield_between_starts;
};

bool run_round(int num_chains, bool yield_between_starts) {
    std::unique_ptr<RunState> state(
        new RunState(num_chains, yield_between_starts));
    state->start_roots(num_chains);
    if (state->wait_and_check()) {
        return true;
    }
    // Timed-out workers may still access their arguments. Keep the state alive
    // until the test runner terminates the process.
    state.release();
    return false;
}

TEST(RunQueueFullTest, ForegroundSaturation) {
    ASSERT_EQ(1, bthread_getconcurrency());
    for (int round = 0; round < kNumRounds; ++round) {
        SCOPED_TRACE("single-worker round " + std::to_string(round));
        ASSERT_TRUE(run_round(1, false));
    }

    ASSERT_EQ(0, bthread_setconcurrency(4));
    ASSERT_EQ(4, bthread_getconcurrency());
    for (int round = 0; round < kNumRounds; ++round) {
        SCOPED_TRACE("multi-worker round " + std::to_string(round));
        ASSERT_TRUE(run_round(4, true));
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    FLAGS_task_group_runqueue_capacity = kRunQueueCapacity;
    bthread::FLAGS_bthread_concurrency = 1;
    bthread::FLAGS_bthread_min_concurrency = 0;
    return RUN_ALL_TESTS();
}
