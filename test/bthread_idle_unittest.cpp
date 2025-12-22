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
#include <set>
#include <mutex>

namespace {

// Mock context to simulate per-thread state (e.g., io_uring ring)
struct MockWorkerContext {
    int worker_id;
    int poll_count;

    MockWorkerContext() : worker_id(-1), poll_count(0) {}
};

// Thread-local storage to simulate "shared-nothing" architecture
// In a real scenario, this would hold something like an io_uring instance.
static __thread MockWorkerContext* tls_context = nullptr;

// Set to collect all unique worker IDs we've seen
static std::set<int> observed_worker_ids;
static std::mutex stats_mutex;

// The idle callback function
// Access per-worker resources via TLS (simulating io_uring per worker)
bool MockIdlePoller() {
    if (!tls_context) {
        tls_context = new MockWorkerContext();
        // Use pthread_self or a counter to assign a unique ID
        static std::atomic<int> global_worker_counter(0);
        tls_context->worker_id = global_worker_counter.fetch_add(1);
        
        std::lock_guard<std::mutex> lock(stats_mutex);
        observed_worker_ids.insert(tls_context->worker_id);
        LOG(INFO) << "Worker thread " << pthread_self() << " initialized with ID " << tls_context->worker_id;
    }

    tls_context->poll_count++;
    
    // Simulate some work occasionally to wake up the worker immediately
    // For this test, we mostly want to verify it runs and has correct context
    if (tls_context->poll_count % 100 == 0) {
        return true; // Pretend we found work
    }

    return false; // Sleep with timeout
}

class IdleCallbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset state
        observed_worker_ids.clear();
    }

    void TearDown() override {
        // Clean up global callback to avoid affecting other tests
        bthread_set_worker_idle_callback(nullptr, 0);
    }
};

void* dummy_task(void* arg) {
    bthread_usleep(1000); // Sleep 1ms to allow workers to go idle
    return nullptr;
}

TEST_F(IdleCallbackTest, WorkerIsolationAndExecution) {
    // 1. Set the idle callback with a short timeout (e.g., 1ms)
    ASSERT_EQ(0, bthread_set_worker_idle_callback(MockIdlePoller, 1000));

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

    // 5. Sleep a bit to ensure all workers have had a chance to hit the idle loop
    usleep(50 * 1000); // 50ms

    // 6. Verify results
    std::lock_guard<std::mutex> lock(stats_mutex);
    LOG(INFO) << "Observed " << observed_worker_ids.size() << " unique worker contexts.";
    
    // We expect at least one worker to have initialized its context.
    // In a highly concurrent test environment, usually most workers will initialize.
    ASSERT_GT(observed_worker_ids.size(), 0);
    
    // Check that we saw different IDs if concurrency > 1 (though not strictly guaranteed 
    // that ALL workers will run if the OS scheduler is quirky, but >1 is highly likely)
    if (concurrency > 1) {
         EXPECT_GT(observed_worker_ids.size(), 1);
    }
}

} // namespace
