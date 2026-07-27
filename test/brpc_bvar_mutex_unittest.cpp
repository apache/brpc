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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// brpc - A framework to host and access services throughout Baidu.

// Date: Mon Jul 27 15:58:00 CST 2026

#include <atomic>
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "bthread/bthread.h"
#include "butil/compat.h"
#include "butil/time.h"
#include "bvar/bvar.h"

namespace {

const char kVariableName[] = "bvar_mutex_worker_starvation";
const char kRecursiveOuterName[] = "bvar_recursive_outer";
const char kRecursiveInnerName[] = "bvar_recursive_inner25";
const int64_t kWatchdogTimeoutUs = 1000000;
const int64_t kWaiterSettleUs = 100000;
const int64_t kProbeTimeoutUs = 1000000;
const int64_t kChildTimeoutUs = 3000000;
const int kWaiterCount = BTHREAD_MIN_CONCURRENCY * 4;

struct TestState;
int BlockingPassiveGetter(void* raw);
struct RecursiveTestState;
int RecursiveOuterGetter(void* raw);

struct TestState {
    std::atomic<bool> holder_in_describe{false};
    std::atomic<uint64_t> holder_worker{0};
    std::atomic<bool> release_holder{false};
    std::atomic<bool> holder_finished{false};
    std::atomic<int> waiter_started{0};
    std::atomic<bool> waiters_may_contend{false};
    std::atomic<int> waiter_finished{0};
    std::atomic<bool> probe_started{false};
    std::atomic<bool> probe_finished{false};
    std::atomic<int> failures{0};
    bvar::BasicPassiveStatus<int> variable;

    TestState() : variable(kVariableName, BlockingPassiveGetter, this) {}
};

struct RecursiveTestState {
    std::atomic<bool> inner_described{false};
    std::atomic<bool> outer_described{false};
    bvar::BasicPassiveStatus<int> inner;
    bvar::BasicPassiveStatus<int> outer;

    RecursiveTestState()
        : inner(kRecursiveInnerName, [](void*) { return 7; }, nullptr),
          outer(kRecursiveOuterName, RecursiveOuterGetter, this) {}
};

int RecursiveOuterGetter(void* raw) {
    auto* state = static_cast<RecursiveTestState*>(raw);
    bthread_usleep(1000);
    if (bvar::Variable::describe_exposed(kRecursiveInnerName) == "7") {
        state->inner_described.store(true, std::memory_order_release);
    }
    return 9;
}

int BlockingPassiveGetter(void* raw) {
    auto* state = static_cast<TestState*>(raw);
    state->holder_worker.store(pthread_numeric_id(), std::memory_order_release);
    state->holder_in_describe.store(true, std::memory_order_release);
    while (!state->release_holder.load(std::memory_order_acquire)) {
        bthread_usleep(1000);
    }
    return 1;
}

void* DescribeAsBthreadHolder(void* raw) {
    auto* state = static_cast<TestState*>(raw);
    if (bvar::Variable::describe_exposed(kVariableName) != "1") {
        state->failures.fetch_add(1, std::memory_order_relaxed);
    }
    state->holder_finished.store(true, std::memory_order_release);
    return nullptr;
}

void* DescribeAsBthreadWaiter(void* raw) {
    auto* state = static_cast<TestState*>(raw);
    state->waiter_started.fetch_add(1, std::memory_order_release);
    while (!state->waiters_may_contend.load(std::memory_order_acquire)) {
        bthread_usleep(1000);
    }
    while (!state->release_holder.load(std::memory_order_acquire) &&
           pthread_numeric_id() ==
               state->holder_worker.load(std::memory_order_acquire)) {
        bthread_yield();
    }
    if (bvar::Variable::describe_exposed(kVariableName) != "1") {
        state->failures.fetch_add(1, std::memory_order_relaxed);
    }
    state->waiter_finished.fetch_add(1, std::memory_order_release);
    return nullptr;
}

void* RunProbe(void* raw) {
    auto* state = static_cast<TestState*>(raw);
    while (!state->release_holder.load(std::memory_order_acquire) &&
           pthread_numeric_id() ==
               state->holder_worker.load(std::memory_order_acquire)) {
        bthread_yield();
    }
    state->probe_started.store(true, std::memory_order_release);
    state->probe_finished.store(true, std::memory_order_release);
    return nullptr;
}

void* WarmupBthreadRuntime(void*) {
    return nullptr;
}

template <typename T>
bool WaitForAtLeast(const std::atomic<T>& value, T expected,
                    int64_t timeout_us) {
    const int64_t deadline_us = butil::gettimeofday_us() + timeout_us;
    while (value.load(std::memory_order_acquire) < expected &&
           butil::gettimeofday_us() < deadline_us) {
        usleep(1000);
    }
    return value.load(std::memory_order_acquire) >= expected;
}

bool WaitForTrue(const std::atomic<bool>& value, int64_t timeout_us) {
    const int64_t deadline_us = butil::gettimeofday_us() + timeout_us;
    while (!value.load(std::memory_order_acquire) &&
           butil::gettimeofday_us() < deadline_us) {
        usleep(1000);
    }
    return value.load(std::memory_order_acquire);
}

void WriteStage(const char* stage) {
    dprintf(STDERR_FILENO, "[bvar-mutex-test] %s\n", stage);
}

bool RunWorkerStarvationScenario() {
    // The holder yields as a bthread while owning the VarMap shard lock.
    // Waiters that contend on the pthread mutex block their worker pthreads.
    // Once all workers are blocked, neither the holder nor an unrelated probe
    // can resume.
    WriteStage("set concurrency");
    if (bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY) != 0) {
        WriteStage("set concurrency failed");
        return false;
    }

    // TaskControl initialization exposes bthread's own bvars. It must finish
    // before the holder owns a VarMap lock, otherwise bthread_start_background
    // blocks during runtime setup rather than exercising mutex contention.
    bthread_t warmup;
    if (bthread_start_background(&warmup, nullptr, WarmupBthreadRuntime,
                                 nullptr) != 0 ||
        bthread_join(warmup, nullptr) != 0) {
        WriteStage("bthread runtime warmup failed");
        return false;
    }
    WriteStage("bthread runtime warmed up");

    auto* state = new TestState;
    WriteStage("state created");
    bthread_t holder;
    bthread_t waiters[kWaiterCount];
    bthread_t probe;
    bool holder_created = false;
    int waiters_created = 0;
    bool probe_created = false;

    if (bthread_start_background(&holder, nullptr, DescribeAsBthreadHolder,
                                 state) != 0) {
        WriteStage("holder create failed");
        return false;
    }
    holder_created = true;
    if (!WaitForTrue(state->holder_in_describe, kWatchdogTimeoutUs)) {
        WriteStage("holder did not enter describe");
        state->release_holder.store(true, std::memory_order_release);
        bthread_join(holder, nullptr);
        return false;
    }
    WriteStage("holder entered describe");

    for (int i = 0; i < kWaiterCount; ++i) {
        if (bthread_start_background(&waiters[i], nullptr,
                                     DescribeAsBthreadWaiter, state) != 0) {
            WriteStage("waiter create failed");
            state->release_holder.store(true, std::memory_order_release);
            bthread_join(holder, nullptr);
            return false;
        }
        ++waiters_created;
    }
    if (!WaitForAtLeast(state->waiter_started, kWaiterCount,
                        kWatchdogTimeoutUs)) {
        WriteStage("waiters did not start");
        state->release_holder.store(true, std::memory_order_release);
        bthread_join(holder, nullptr);
        return false;
    }
    WriteStage("waiters started");
    state->waiters_may_contend.store(true, std::memory_order_release);

    // Release all waiters together and give them time to occupy every worker
    // in the contended pthread mutex before enqueueing the unrelated probe.
    usleep(kWaiterSettleUs);
    if (bthread_start_background(&probe, nullptr, RunProbe, state) != 0) {
        WriteStage("probe create failed");
        state->release_holder.store(true, std::memory_order_release);
        bthread_join(holder, nullptr);
        return false;
    }
    probe_created = true;

    // On the unfixed implementation, all workers are blocked in the VarMap
    // pthread mutex while the holder is suspended, so the unrelated probe
    // cannot run. With bthread_mutex_t, the waiters park and the probe runs.
    const bool probe_ran_before_release =
        WaitForTrue(state->probe_started, kProbeTimeoutUs);
    WriteStage(probe_ran_before_release ? "probe ran" : "probe did not run");

    state->release_holder.store(true, std::memory_order_release);
    WriteStage("holder released");
    const bool completed =
        WaitForTrue(state->holder_finished, kWatchdogTimeoutUs) &&
        WaitForAtLeast(state->waiter_finished, kWaiterCount,
                       kWatchdogTimeoutUs) &&
        WaitForTrue(state->probe_finished, kWatchdogTimeoutUs);
    WriteStage(completed ? "workers completed" : "workers did not complete");

    if (holder_created) {
        bthread_join(holder, nullptr);
    }
    for (int i = 0; i < waiters_created; ++i) {
        bthread_join(waiters[i], nullptr);
    }
    if (probe_created) {
        bthread_join(probe, nullptr);
    }
    WriteStage("workers joined");

    const bool ok = probe_ran_before_release && completed &&
                    state->failures.load(std::memory_order_relaxed) == 0;
    delete state;
    return ok;
}

void* DescribeRecursiveVariable(void* raw) {
    auto* state = static_cast<RecursiveTestState*>(raw);
    const std::string description =
        bvar::Variable::describe_exposed(kRecursiveOuterName);
    const bool inner_described =
        state->inner_described.load(std::memory_order_acquire);
    state->outer_described.store(description == "9" && inner_described,
                                 std::memory_order_release);
    return nullptr;
}

bool RunRecursiveDescribeScenario() {
    RecursiveTestState state;
    bthread_t thread;
    if (bthread_start_background(&thread, nullptr, DescribeRecursiveVariable,
                                 &state) != 0) {
        return false;
    }
    return bthread_join(thread, nullptr) == 0 &&
           state.outer_described.load(std::memory_order_acquire);
}

bool RunChildAndWait(const char* child_argument, const char* failure_message) {
    const pid_t pid = fork();
    if (pid == -1) {
        return false;
    }
    if (pid == 0) {
        execl("/proc/self/exe", "brpc_bvar_mutex_unittest", child_argument,
              nullptr);
        _exit(127);
    }

    int status = 0;
    pid_t child = 0;
    const int64_t deadline_us = butil::gettimeofday_us() + kChildTimeoutUs;
    while ((child = waitpid(pid, &status, WNOHANG)) == 0 &&
           butil::gettimeofday_us() < deadline_us) {
        usleep(1000);
    }
    if (child == 0) {
        kill(pid, SIGKILL);
        EXPECT_EQ(pid, waitpid(pid, &status, 0));
        ADD_FAILURE() << failure_message;
        return false;
    }
    EXPECT_EQ(pid, child);
    EXPECT_TRUE(WIFEXITED(status));
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

TEST(BvarMutexTest, DoesNotStarveWorkersWhenBthreadDescribeYields) {
    EXPECT_TRUE(RunChildAndWait(
        "--bvar-mutex-worker-starvation-child",
        "bvar worker-starvation child did not finish before watchdog; "
        "bthread workers could not make progress while the VarMap lock was "
        "held"));
}

TEST(BvarMutexTest, SupportsRecursiveDescribeAfterBthreadYield) {
    EXPECT_TRUE(RunChildAndWait(
        "--bvar-mutex-recursive-describe-child",
        "bvar recursive describe child did not finish before watchdog; "
        "VarMap lock did not preserve recursive bthread ownership"));
}

TEST(BvarMutexTest, SupportsRecursiveDescribeFromPthread) {
    RecursiveTestState state;
    EXPECT_EQ("9", bvar::Variable::describe_exposed(kRecursiveOuterName));
    EXPECT_TRUE(state.inner_described.load(std::memory_order_acquire));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 &&
        strcmp(argv[1], "--bvar-mutex-worker-starvation-child") == 0) {
        _exit(RunWorkerStarvationScenario() ? 0 : 1);
    }
    if (argc == 2 &&
        strcmp(argv[1], "--bvar-mutex-recursive-describe-child") == 0) {
        _exit(RunRecursiveDescribeScenario() ? 0 : 1);
    }
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    const int rc = RUN_ALL_TESTS();
    _exit(rc);
}
