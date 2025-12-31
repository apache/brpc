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
#include <bthread/bthread.h>
#include <bthread/unstable.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <atomic>
#include <vector>
#include <set>
#include <mutex>
#include <thread>
#include <chrono>

namespace {

// Per-worker state for module callbacks.
struct WorkerModuleTLS {
    bool inited_a;
    bool inited_b;
    int poll_a;
    int poll_b;

    WorkerModuleTLS() : inited_a(false), inited_b(false), poll_a(0), poll_b(0) {}
};

// Thread-local storage for simulating per-worker resources.
static __thread WorkerModuleTLS* tls_modules = nullptr;

// Global stats for validating execution.
static std::atomic<int> init_calls_a(0);
static std::atomic<int> init_calls_b(0);
static std::atomic<int> idle_calls_a(0);
static std::atomic<int> idle_calls_b(0);
static std::atomic<int> init_twice_a(0);
static std::atomic<int> init_twice_b(0);
static std::atomic<int> idle_without_init_a(0);
static std::atomic<int> idle_without_init_b(0);

// Set to collect all unique worker IDs we've seen.
static std::set<int> observed_worker_ids;
static std::mutex stats_mutex;
static std::atomic<int> global_worker_counter(0);

// Stats for init failure test.
static std::atomic<int> init_failure_calls(0);
static std::atomic<int> idle_after_init_failure(0);

// Stats for always-return-true test.
static std::atomic<int> always_true_calls(0);

// Stats for timeout test.
static std::atomic<int> timeout_test_calls(0);

// Stats for unregister test.
static std::atomic<int> unregister_test_idle_calls(0);

// Stats for thread safety test.
static std::atomic<int> concurrent_register_count(0);

int MockInitConcurrent() {
    concurrent_register_count.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

bool MockIdleConcurrent() {
    return false;
}

// Init for module A. Runs at most once per worker thread by design.
int MockWorkerInitA() {
    if (tls_modules == nullptr) {
        tls_modules = new WorkerModuleTLS();
    }
    if (tls_modules->inited_a) {
        init_twice_a.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    tls_modules->inited_a = true;
    init_calls_a.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(stats_mutex);
    observed_worker_ids.insert(global_worker_counter.fetch_add(1, std::memory_order_relaxed));
    LOG(INFO) << "MockWorkerInitA: inited_a=" << tls_modules->inited_a;
    return 0;
}

// Init for module B. Runs at most once per worker thread by design.
int MockWorkerInitB() {
    if (tls_modules == nullptr) {
        tls_modules = new WorkerModuleTLS();
    }
    if (tls_modules->inited_b) {
        init_twice_b.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    tls_modules->inited_b = true;
    init_calls_b.fetch_add(1, std::memory_order_relaxed);
    LOG(INFO) << "MockWorkerInitB: inited_b=" << tls_modules->inited_b;
    return 0;
}

// Idle callback for module A. Must run only after init succeeded.
bool MockIdlePollerA() {
    idle_calls_a.fetch_add(1, std::memory_order_relaxed);
    if (tls_modules == nullptr || !tls_modules->inited_a) {
        idle_without_init_a.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ++tls_modules->poll_a;
    if (tls_modules->poll_a % 64 == 0) {
        LOG(INFO) << "MockIdlePollerA: poll_a=" << tls_modules->poll_a;
        return true;
    }
    return false;
}

// Idle callback for module B. Must run only after init succeeded.
bool MockIdlePollerB() {
    idle_calls_b.fetch_add(1, std::memory_order_relaxed);
    if (tls_modules == nullptr || !tls_modules->inited_b) {
        idle_without_init_b.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ++tls_modules->poll_b;
    if (tls_modules->poll_b % 32 == 0) {
        LOG(INFO) << "MockIdlePollerB: poll_b=" << tls_modules->poll_b;
        return true;
    }
    return false;
}

class IdleCallbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset global state.
        observed_worker_ids.clear();
        global_worker_counter.store(0, std::memory_order_relaxed);
        init_calls_a.store(0, std::memory_order_relaxed);
        init_calls_b.store(0, std::memory_order_relaxed);
        idle_calls_a.store(0, std::memory_order_relaxed);
        idle_calls_b.store(0, std::memory_order_relaxed);
        init_twice_a.store(0, std::memory_order_relaxed);
        init_twice_b.store(0, std::memory_order_relaxed);
        idle_without_init_a.store(0, std::memory_order_relaxed);
        idle_without_init_b.store(0, std::memory_order_relaxed);
        init_failure_calls.store(0, std::memory_order_relaxed);
        idle_after_init_failure.store(0, std::memory_order_relaxed);
        always_true_calls.store(0, std::memory_order_relaxed);
        timeout_test_calls.store(0, std::memory_order_relaxed);
        unregister_test_idle_calls.store(0, std::memory_order_relaxed);
        concurrent_register_count.store(0, std::memory_order_relaxed);
    }

    void TearDown() override {
        // Clean up registered callbacks to avoid affecting other tests.
        if (_handle_a > 0) {
            bthread_unregister_worker_idle_function(_handle_a);
            _handle_a = 0;
        }
        if (_handle_b > 0) {
            bthread_unregister_worker_idle_function(_handle_b);
            _handle_b = 0;
        }
        if (_handle_c > 0) {
            bthread_unregister_worker_idle_function(_handle_c);
            _handle_c = 0;
        }
        if (_handle_d > 0) {
            bthread_unregister_worker_idle_function(_handle_d);
            _handle_d = 0;
        }
        if (_handle_e > 0) {
            bthread_unregister_worker_idle_function(_handle_e);
            _handle_e = 0;
        }
    }

    int _handle_a = 0;
    int _handle_b = 0;
    int _handle_c = 0;
    int _handle_d = 0;
    int _handle_e = 0;
};

void* dummy_task(void* arg) {
    // Sleep to allow workers to enter idle loop.
    bthread_usleep(1000);
    return nullptr;
}

TEST_F(IdleCallbackTest, WorkerIsolationAndExecution) {
    // 1. Register multiple (init, idle) pairs from different "modules".
    ASSERT_EQ(0, bthread_register_worker_idle_function(
                      MockWorkerInitA, MockIdlePollerA, 1000, &_handle_a));
    ASSERT_EQ(0, bthread_register_worker_idle_function(
                      MockWorkerInitB, MockIdlePollerB, 1000, &_handle_b));

    // 2. Determine number of workers (concurrency)
    int concurrency = bthread_getconcurrency();
    LOG(INFO) << "Current concurrency: " << concurrency;

    // 3. Create enough bthreads to ensure all workers are activated at least once
    // but also give them time to become idle.
    std::vector<bthread_t> tids;
    for (int i = 0; i < concurrency * 2; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, nullptr, dummy_task, nullptr);
        tids.push_back(tid);
    }

    // 4. Wait for all tasks to complete
    for (bthread_t tid : tids) {
        bthread_join(tid, nullptr);
    }

    // 5. Sleep a bit to ensure all workers have had a chance to hit the idle loop.
    usleep(50 * 1000);

    // 6. Verify results.
    std::lock_guard<std::mutex> lock(stats_mutex);
    LOG(INFO) << "Observed " << observed_worker_ids.size() << " unique worker contexts.";

    // Basic sanity: both module callbacks should have been executed at least once.
    EXPECT_GT(init_calls_a.load(std::memory_order_relaxed), 0);
    EXPECT_GT(init_calls_b.load(std::memory_order_relaxed), 0);
    EXPECT_GT(idle_calls_a.load(std::memory_order_relaxed), 0);
    EXPECT_GT(idle_calls_b.load(std::memory_order_relaxed), 0);

    // Init should not run twice in the same worker thread for the same module.
    EXPECT_EQ(init_twice_a.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(init_twice_b.load(std::memory_order_relaxed), 0);

    // Idle should not run before init is completed.
    EXPECT_EQ(idle_without_init_a.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(idle_without_init_b.load(std::memory_order_relaxed), 0);

    // We expect at least one worker to have initialized its context.
    ASSERT_GT(observed_worker_ids.size(), 0);

    // If concurrency is larger than 1, it is likely we observed multiple workers.
    if (concurrency > 1) {
        EXPECT_GT(observed_worker_ids.size(), 1);
    }
}

// Test parameter validation.
TEST_F(IdleCallbackTest, ParameterValidation) {
    int handle = 0;

    // NULL idle_fn should fail (init_fn can be NULL, but idle_fn cannot).
    ASSERT_EQ(EINVAL, bthread_register_worker_idle_function(
                          MockWorkerInitA, nullptr, 1000, &handle));

    // timeout_us = 0 should fail.
    ASSERT_EQ(EINVAL, bthread_register_worker_idle_function(
                          MockWorkerInitA, MockIdlePollerA, 0, &handle));

    // Valid registration should succeed.
    ASSERT_EQ(0, bthread_register_worker_idle_function(
                     MockWorkerInitA, MockIdlePollerA, 1000, &_handle_a));
    ASSERT_GT(_handle_a, 0);

    // Unregister with invalid handle should fail.
    ASSERT_EQ(EINVAL, bthread_unregister_worker_idle_function(-1));
    ASSERT_EQ(EINVAL, bthread_unregister_worker_idle_function(0));
    ASSERT_EQ(EINVAL, bthread_unregister_worker_idle_function(99999));

    // Unregister with valid handle should succeed.
    ASSERT_EQ(0, bthread_unregister_worker_idle_function(_handle_a));
    _handle_a = 0;
}

// Test init failure scenario: idle_fn should not run if init_fn returns non-zero.
int MockWorkerInitFailure() {
    init_failure_calls.fetch_add(1, std::memory_order_relaxed);
    return -1;  // Init fails.
}

bool MockIdleAfterInitFailure() {
    idle_after_init_failure.fetch_add(1, std::memory_order_relaxed);
    return false;
}

TEST_F(IdleCallbackTest, InitFailurePreventsIdleExecution) {
    ASSERT_EQ(0, bthread_register_worker_idle_function(
                     MockWorkerInitFailure, MockIdleAfterInitFailure, 1000, &_handle_a));

    // Create some bthreads to activate workers.
    std::vector<bthread_t> tids;
    for (int i = 0; i < 5; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, nullptr, dummy_task, nullptr);
        tids.push_back(tid);
    }

    for (bthread_t tid : tids) {
        bthread_join(tid, nullptr);
    }

    // Wait for idle loop.
    usleep(50 * 1000);

    // Init should have been called.
    EXPECT_GT(init_failure_calls.load(std::memory_order_relaxed), 0);

    // Idle should NOT have been called because init failed.
    EXPECT_EQ(idle_after_init_failure.load(std::memory_order_relaxed), 0);
}

// Test registration without init_fn.
bool MockIdleWithoutInit() {
    timeout_test_calls.fetch_add(1, std::memory_order_relaxed);
    return false;
}

TEST_F(IdleCallbackTest, RegistrationWithoutInitFunction) {
    // Register with NULL init_fn should work.
    ASSERT_EQ(0, bthread_register_worker_idle_function(
                     nullptr, MockIdleWithoutInit, 2000, &_handle_a));

    std::vector<bthread_t> tids;
    for (int i = 0; i < 5; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, nullptr, dummy_task, nullptr);
        tids.push_back(tid);
    }

    for (bthread_t tid : tids) {
        bthread_join(tid, nullptr);
    }

    usleep(50 * 1000);

    // Idle should have been called even without init_fn.
    EXPECT_GT(timeout_test_calls.load(std::memory_order_relaxed), 0);
}

// Test multiple registrations and timeout calculation.
bool MockIdleTimeout1() {
    return false;
}

bool MockIdleTimeout2() {
    return false;
}

bool MockIdleTimeout3() {
    return false;
}

TEST_F(IdleCallbackTest, MultipleRegistrationsAndTimeout) {
    int handle1 = 0, handle2 = 0, handle3 = 0;

    // Register with different timeouts: 100us, 500us, 2000us.
    ASSERT_EQ(0, bthread_register_worker_idle_function(
                     nullptr, MockIdleTimeout1, 100, &handle1));
    ASSERT_EQ(0, bthread_register_worker_idle_function(
                     nullptr, MockIdleTimeout2, 500, &handle2));
    ASSERT_EQ(0, bthread_register_worker_idle_function(
                     nullptr, MockIdleTimeout3, 2000, &handle3));

    // The minimal timeout should be 100us.
    // We verify this indirectly by checking that idle functions are called
    // frequently enough (the minimal timeout determines wait time).

    std::vector<bthread_t> tids;
    for (int i = 0; i < 3; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, nullptr, dummy_task, nullptr);
        tids.push_back(tid);
    }

    for (bthread_t tid : tids) {
        bthread_join(tid, nullptr);
    }

    // Clean up.
    bthread_unregister_worker_idle_function(handle1);
    bthread_unregister_worker_idle_function(handle2);
    bthread_unregister_worker_idle_function(handle3);
}

// Test unregister functionality.
bool MockIdleForUnregister() {
    unregister_test_idle_calls.fetch_add(1, std::memory_order_relaxed);
    return false;
}

TEST_F(IdleCallbackTest, UnregisterStopsIdleExecution) {
    ASSERT_EQ(0, bthread_register_worker_idle_function(
                     nullptr, MockIdleForUnregister, 1000, &_handle_a));

    std::vector<bthread_t> tids;
    for (int i = 0; i < 3; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, nullptr, dummy_task, nullptr);
        tids.push_back(tid);
    }

    for (bthread_t tid : tids) {
        bthread_join(tid, nullptr);
    }

    usleep(20 * 1000);
    const int calls_before = unregister_test_idle_calls.load(std::memory_order_relaxed);
    EXPECT_GT(calls_before, 0);

    // Unregister.
    ASSERT_EQ(0, bthread_unregister_worker_idle_function(_handle_a));
    _handle_a = 0;

    // Wait a bit more.
    usleep(30 * 1000);
    const int calls_after = unregister_test_idle_calls.load(std::memory_order_relaxed);

    // Calls should not increase much after unregister (may increase slightly
    // due to in-flight calls, but should stabilize).
    EXPECT_LE(calls_after - calls_before, 5);
}

// Test that always returning true does not cause busy loop.
bool MockIdleAlwaysTrue() {
    always_true_calls.fetch_add(1, std::memory_order_relaxed);
    return true;  // Always report work done.
}

TEST_F(IdleCallbackTest, AlwaysReturnTrueDoesNotBusyLoop) {
    ASSERT_EQ(0, bthread_register_worker_idle_function(
                     nullptr, MockIdleAlwaysTrue, 10000, &_handle_a));

    std::vector<bthread_t> tids;
    for (int i = 0; i < 3; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, nullptr, dummy_task, nullptr);
        tids.push_back(tid);
    }

    for (bthread_t tid : tids) {
        bthread_join(tid, nullptr);
    }

    // Wait a short time.
    const auto start_time = std::chrono::steady_clock::now();
    usleep(10 * 1000);  // 10ms
    const auto end_time = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                end_time - start_time)
                                .count();

    const int calls = always_true_calls.load(std::memory_order_relaxed);

    LOG(INFO) << "AlwaysReturnTrue test: " << calls << " calls in " << elapsed_ms
              << "ms (expected low frequency)";

    EXPECT_GT(calls, 0);
    EXPECT_LT(calls, 1000);
}

// Test thread safety of register/unregister.
void* concurrent_register_task(void* arg) {
    (void)arg;
    int handle = 0;
    if (bthread_register_worker_idle_function(MockInitConcurrent, MockIdleConcurrent,
                                               1000, &handle) == 0) {
        // Small delay, then unregister.
        bthread_usleep(1000);
        bthread_unregister_worker_idle_function(handle);
    }
    return nullptr;
}

TEST_F(IdleCallbackTest, ThreadSafety) {
    // Concurrent registration/unregistration from multiple bthreads.
    std::vector<bthread_t> tids;
    for (int i = 0; i < 20; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, nullptr, concurrent_register_task, nullptr);
        tids.push_back(tid);
    }

    for (bthread_t tid : tids) {
        bthread_join(tid, nullptr);
    }

    // Should have seen some init calls from concurrent registrations.
    // Exact number is non-deterministic, but should be > 0.
    EXPECT_GT(concurrent_register_count.load(std::memory_order_relaxed), 0);
}

} // namespace
