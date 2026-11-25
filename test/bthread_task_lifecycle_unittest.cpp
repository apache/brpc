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

#include <chrono>
#include <condition_variable>
#include <errno.h>
#include <functional>
#include <mutex>
#include <new>
#include <utility>

#include <gtest/gtest.h>

#include "bthread/bthread.h"
#include "bthread/task_lifecycle.h"
#include "bthread/unstable.h"
#include "butil/memory/scope_guard.h"

namespace {

enum MockMode {
    RETURN_TOKEN,
    RETURN_NULL_TOKEN,
    FAIL_AFTER_TOKEN,
    BLOCK_TASK_RELEASE
};

struct MockState {
    std::mutex mutex;
    std::condition_variable condition;
    MockMode mode = RETURN_TOKEN;
    int admits = 0;
    int releases = 0;
    int active = 0;
    bool release_entered = false;
    bool allow_release = false;
    bool release_done = false;
};

struct MockToken {
    MockState* state;
    bool block_release;
};

MockState g_state;

void ResetMock(MockMode mode) {
    std::lock_guard<std::mutex> lock(g_state.mutex);
    ASSERT_EQ(0, g_state.active);
    g_state.mode = mode;
    g_state.admits = 0;
    g_state.releases = 0;
    g_state.release_entered = false;
    g_state.allow_release = false;
    g_state.release_done = false;
}

int MockAdmit(bthread_keytable_pool_t*, const void*, void** output) {
    if (output == nullptr) {
        return EINVAL;
    }
    *output = nullptr;
    std::lock_guard<std::mutex> lock(g_state.mutex);
    ++g_state.admits;
    if (g_state.mode == RETURN_NULL_TOKEN) {
        return 0;
    }
    MockToken* const token = new (std::nothrow) MockToken{
        &g_state, g_state.mode == BLOCK_TASK_RELEASE};
    if (token == nullptr) {
        return ENOMEM;
    }
    ++g_state.active;
    *output = token;
    return g_state.mode == FAIL_AFTER_TOKEN ? ENOMEM : 0;
}

void MockRelease(void* opaque_token) {
    MockToken* const token = static_cast<MockToken*>(opaque_token);
    MockState* const state = token->state;
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        ++state->releases;
        if (token->block_release) {
            state->release_entered = true;
            state->condition.notify_all();
            // Deliberately block this native worker in the isolated test hook.
            // Production hooks are required to return without yielding.
            state->condition.wait(lock, [&] { return state->allow_release; });
        }
        --state->active;
        state->release_done = true;
        state->condition.notify_all();
    }
    delete token;
}

const bthread::TaskLifecycleHooks kMockHooks = {
    MockAdmit,
    MockRelease
};

const bthread::TaskLifecycleHooks kOtherHooks = {
    MockAdmit,
    MockRelease
};

void InstallMockHooks() {
    ASSERT_EQ(0, bthread::InstallTaskLifecycleHooks(&kMockHooks));
}

bool WaitFor(const std::function<bool()>& predicate) {
    std::unique_lock<std::mutex> lock(g_state.mutex);
    return g_state.condition.wait_for(
        lock, std::chrono::seconds(5), predicate);
}

void* DoNothing(void*) {
    return nullptr;
}

class PoolGuard {
public:
    PoolGuard() { EXPECT_EQ(0, bthread_keytable_pool_init(&_pool)); }
    ~PoolGuard() {
        EXPECT_EQ(0, bthread_keytable_pool_destroy(&_pool));
        EXPECT_EQ(0, pthread_rwlock_destroy(&_pool.rwlock));
    }

    bthread_keytable_pool_t* get() { return &_pool; }

private:
    bthread_keytable_pool_t _pool;
};

TEST(TaskLifecycleTest, installs_one_process_lifetime_hook_table) {
    EXPECT_EQ(EINVAL, bthread::InstallTaskLifecycleHooks(nullptr));
    const bthread::TaskLifecycleHooks missing_admit = {nullptr, MockRelease};
    const bthread::TaskLifecycleHooks missing_release = {MockAdmit, nullptr};
    EXPECT_EQ(EINVAL, bthread::InstallTaskLifecycleHooks(&missing_admit));
    EXPECT_EQ(EINVAL, bthread::InstallTaskLifecycleHooks(&missing_release));
    InstallMockHooks();
    EXPECT_EQ(0, bthread::InstallTaskLifecycleHooks(&kMockHooks));
    EXPECT_EQ(EBUSY, bthread::InstallTaskLifecycleHooks(&kOtherHooks));
}

TEST(TaskLifecycleTest, acquire_reset_and_scope_exit_release_once) {
    InstallMockHooks();
    ResetMock(RETURN_TOKEN);
    PoolGuard pool;
    {
        bthread::TaskLifecycle lifecycle;
        ASSERT_EQ(0, lifecycle.Acquire(pool.get(), nullptr));
        EXPECT_TRUE(lifecycle.valid());
        lifecycle.Reset();
        lifecycle.Reset();
        EXPECT_FALSE(lifecycle.valid());
    }
    std::lock_guard<std::mutex> lock(g_state.mutex);
    EXPECT_EQ(1, g_state.admits);
    EXPECT_EQ(1, g_state.releases);
    EXPECT_EQ(0, g_state.active);
}

TEST(TaskLifecycleTest, move_transfers_exactly_one_release) {
    InstallMockHooks();
    ResetMock(RETURN_TOKEN);
    PoolGuard pool;
    {
        bthread::TaskLifecycle first;
        ASSERT_EQ(0, first.Acquire(pool.get(), nullptr));
        bthread::TaskLifecycle second(std::move(first));
        EXPECT_FALSE(first.valid());
        EXPECT_TRUE(second.valid());
        bthread::TaskLifecycle third;
        third = std::move(second);
        EXPECT_FALSE(second.valid());
        EXPECT_TRUE(third.valid());
    }
    std::lock_guard<std::mutex> lock(g_state.mutex);
    EXPECT_EQ(1, g_state.admits);
    EXPECT_EQ(1, g_state.releases);
    EXPECT_EQ(0, g_state.active);
}

TEST(TaskLifecycleTest, null_token_keeps_pool_legacy) {
    InstallMockHooks();
    ResetMock(RETURN_NULL_TOKEN);
    PoolGuard pool;
    bthread::TaskLifecycle lifecycle;
    ASSERT_EQ(0, lifecycle.Acquire(pool.get(), nullptr));
    EXPECT_FALSE(lifecycle.valid());
    std::lock_guard<std::mutex> lock(g_state.mutex);
    EXPECT_EQ(1, g_state.admits);
    EXPECT_EQ(0, g_state.releases);
    EXPECT_EQ(0, g_state.active);
}

TEST(TaskLifecycleTest, failed_admission_rolls_back_returned_token) {
    InstallMockHooks();
    ResetMock(FAIL_AFTER_TOKEN);
    PoolGuard pool;
    bthread::TaskLifecycle lifecycle;
    EXPECT_EQ(ENOMEM, lifecycle.Acquire(pool.get(), nullptr));
    EXPECT_FALSE(lifecycle.valid());
    std::lock_guard<std::mutex> lock(g_state.mutex);
    EXPECT_EQ(1, g_state.admits);
    EXPECT_EQ(1, g_state.releases);
    EXPECT_EQ(0, g_state.active);
}

TEST(TaskLifecycleTest, task_start_propagates_admission_failure_and_rolls_back) {
    InstallMockHooks();
    ResetMock(FAIL_AFTER_TOKEN);
    PoolGuard pool;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.keytable_pool = pool.get();
    bthread_t tid = 123;
    EXPECT_EQ(ENOMEM,
              bthread_start_background(&tid, &attr, DoNothing, nullptr));
    EXPECT_EQ(123UL, tid);
    std::lock_guard<std::mutex> lock(g_state.mutex);
    EXPECT_EQ(1, g_state.admits);
    EXPECT_EQ(1, g_state.releases);
    EXPECT_EQ(0, g_state.active);
}

TEST(TaskLifecycleTest, null_pool_bypasses_installed_hooks) {
    InstallMockHooks();
    ResetMock(RETURN_TOKEN);
    bthread::TaskLifecycle lifecycle;
    EXPECT_EQ(0, lifecycle.Acquire(nullptr, nullptr));
    EXPECT_FALSE(lifecycle.valid());
    std::lock_guard<std::mutex> lock(g_state.mutex);
    EXPECT_EQ(0, g_state.admits);
}

TEST(TaskLifecycleTest, null_function_does_not_admit_or_modify_tid) {
    InstallMockHooks();
    ResetMock(RETURN_TOKEN);
    PoolGuard pool;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.keytable_pool = pool.get();
    bthread_t tid = 123;
    EXPECT_EQ(EINVAL, bthread_start_background(&tid, &attr, nullptr, nullptr));
    EXPECT_EQ(123UL, tid);
    EXPECT_EQ(EINVAL, bthread_start_urgent(&tid, &attr, nullptr, nullptr));
    EXPECT_EQ(123UL, tid);
    std::lock_guard<std::mutex> lock(g_state.mutex);
    EXPECT_EQ(0, g_state.admits);
    EXPECT_EQ(0, g_state.active);
}

TEST(TaskLifecycleTest, public_task_start_releases_root_lifecycle) {
    InstallMockHooks();
    ResetMock(RETURN_TOKEN);
    PoolGuard pool;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.keytable_pool = pool.get();
    bthread_t background_tid = INVALID_BTHREAD;
    bthread_t urgent_tid = INVALID_BTHREAD;
    auto cleanup = butil::MakeScopeGuard([&] {
        if (background_tid != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(background_tid, nullptr));
        }
        if (urgent_tid != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(urgent_tid, nullptr));
        }
        EXPECT_TRUE(WaitFor([] { return g_state.active == 0; }));
    });
    ASSERT_EQ(0, bthread_start_background(
        &background_tid, &attr, DoNothing, nullptr));
    ASSERT_EQ(0, bthread_start_urgent(
        &urgent_tid, &attr, DoNothing, nullptr));
    ASSERT_EQ(0, bthread_join(background_tid, nullptr));
    ASSERT_EQ(0, bthread_join(urgent_tid, nullptr));
    ASSERT_TRUE(WaitFor([] { return g_state.releases == 2; }));
    std::lock_guard<std::mutex> lock(g_state.mutex);
    EXPECT_EQ(2, g_state.admits);
    EXPECT_EQ(2, g_state.releases);
    EXPECT_EQ(0, g_state.active);
}

TEST(TaskLifecycleTest, join_wakes_before_task_lifecycle_release_finishes) {
    InstallMockHooks();
    ResetMock(BLOCK_TASK_RELEASE);
    PoolGuard pool;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.keytable_pool = pool.get();
    bthread_t tid = INVALID_BTHREAD;
    bool started = false;
    auto unblock = butil::MakeScopeGuard([&] {
        if (!started) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_state.mutex);
            g_state.allow_release = true;
            g_state.condition.notify_all();
        }
        bthread_join(tid, nullptr);
        WaitFor([] { return g_state.release_done; });
    });

    ASSERT_EQ(0, bthread_start_background(&tid, &attr, DoNothing, nullptr));
    started = true;
    ASSERT_TRUE(WaitFor([] { return g_state.release_entered; }));

    // The release hook is reached only after the task version was published
    // and joiners were woken. The task id is already dead although its opaque
    // lifecycle token is deliberately still held by the hook.
    bthread_attr_t dead_attr;
    EXPECT_NE(0, bthread_getattr(tid, &dead_attr));
    ASSERT_EQ(0, bthread_join(tid, nullptr));
    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        EXPECT_EQ(1, g_state.active);
        EXPECT_FALSE(g_state.release_done);
    }

    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        g_state.allow_release = true;
        g_state.condition.notify_all();
    }
    ASSERT_TRUE(WaitFor([] { return g_state.release_done; }));
    std::lock_guard<std::mutex> lock(g_state.mutex);
    EXPECT_EQ(0, g_state.active);
}

}  // namespace
