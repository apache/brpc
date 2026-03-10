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

#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits.h>
#include <new>
#include <pthread.h>
#include <stdint.h>
#include <vector>

#include <gtest/gtest.h>
#include <gflags/gflags.h>

#include "butil/atomicops.h"
#include "butil/process_util.h"
#include "butil/time.h"
#include "bthread/bthread.h"
#include "bthread/butex.h"
#include "bthread/task_control.h"
#include "bthread/task_group.h"
#include "bthread/unstable.h"

namespace bthread {
DECLARE_int32(bthread_active_task_poll_every_nswitch);
DECLARE_int64(bthread_active_task_idle_wait_ns);
}
DECLARE_int32(task_group_runqueue_capacity);

namespace {

enum TestMode {
    TEST_MODE_IDLE = 0,
    TEST_MODE_IDLE_WAIT_INTERVAL = 1,
    TEST_MODE_BUTEX_WAKE_WITHIN = 2,
    TEST_MODE_BUTEX_WAKE_WITHIN_NULL = 3,
    TEST_MODE_BUTEX_WAKE_WITHIN_NO_WAITER = 4,
    TEST_MODE_BUTEX_WAKE_WITHIN_PTHREAD_WAITER = 5,
    TEST_MODE_BUSY_PERIODIC_POLL_WAKE = 6,
    TEST_MODE_SCENARIO_REQ_WAKE = 7,
    TEST_MODE_SCENARIO_REQ_WAKE_BUSY_PERIODIC = 8,
    TEST_MODE_BUTEX_WAKE_WITHIN_STRICT_CROSS_WORKER_REJECT = 9,
    TEST_MODE_BUTEX_WAKE_WITHIN_EAGAIN_WHEN_PINNED_RQ_FULL = 10,
};

enum GenericWakeVariant {
    GENERIC_WAKE = 0,
    GENERIC_WAKE_N = 1,
    GENERIC_WAKE_EXCEPT = 2,
    GENERIC_REQUEUE = 3,
};

struct PerWorkerState {
};

struct MockReqCtx {
    MockReqCtx()
        : butex(NULL)
        , result_ready(0)
        , wake_rc(0)
        , wake_errno(0)
        , waiter_ready(0)
        , waiter_done(0)
        , resume_saw_result_ready(0)
        , wait_rc(0)
        , wait_errno(0)
        , waiter_worker_pthread(0)
        , resume_worker_pthread(0)
        , hook_worker_pthread(0)
        , completion_published(0) {}

    void* butex;
    std::atomic<int> result_ready;
    std::atomic<int> wake_rc;
    std::atomic<int> wake_errno;
    std::atomic<int> waiter_ready;
    std::atomic<int> waiter_done;
    std::atomic<int> resume_saw_result_ready;
    std::atomic<int> wait_rc;
    std::atomic<int> wait_errno;
    std::atomic<uint64_t> waiter_worker_pthread;
    std::atomic<uint64_t> resume_worker_pthread;
    std::atomic<uint64_t> hook_worker_pthread;
    std::atomic<int> completion_published;
};

struct ActiveTaskTestState {
    ActiveTaskTestState()
        : mode(TEST_MODE_IDLE)
        , init_calls(0)
        , destroy_calls(0)
        , harvest_calls(0)
        , butex_ptr(0)
        , butex_ptr_aux1(0)
        , butex_ptr_aux2(0)
        , pending_req_ptr(0)
        , target_hook_worker_pthread(0)
        , butex_expected_waiters(0)
        , butex_wake_started(0)
        , butex_wake_completed(0)
        , butex_wake_rc(0)
        , butex_wake_errno(0)
        , butex_wake_rc_aux1(0)
        , butex_wake_errno_aux1(0)
        , butex_wake_rc_aux2(0)
        , butex_wake_errno_aux2(0)
        , hook_wake_harvest_calls(0)
        , hook_action_inflight(0)
        , butex_waiter_ready_count(0)
        , butex_waiter_done_count(0)
        , butex_waiter_resume_count(0)
        , butex_waiter_worker_pthread(0)
        , butex_waiter_resume_worker_pthread(0)
        , pthread_waiter_ready_count(0)
        , pthread_waiter_done_count(0)
        , busy_task_started(0)
        , busy_task_stop(0)
        , busy_task_switches(0) {}
    std::atomic<int> mode;
    std::atomic<int> init_calls;
    std::atomic<int> destroy_calls;
    std::atomic<int> harvest_calls;
    std::atomic<uintptr_t> butex_ptr;
    std::atomic<uintptr_t> butex_ptr_aux1;
    std::atomic<uintptr_t> butex_ptr_aux2;
    std::atomic<uintptr_t> pending_req_ptr;
    std::atomic<uint64_t> target_hook_worker_pthread;
    std::atomic<int> butex_expected_waiters;
    std::atomic<int> butex_wake_started;
    std::atomic<int> butex_wake_completed;
    std::atomic<int> butex_wake_rc;
    std::atomic<int> butex_wake_errno;
    std::atomic<int> butex_wake_rc_aux1;
    std::atomic<int> butex_wake_errno_aux1;
    std::atomic<int> butex_wake_rc_aux2;
    std::atomic<int> butex_wake_errno_aux2;
    std::atomic<int> hook_wake_harvest_calls;
    std::atomic<int> hook_action_inflight;
    std::atomic<int> butex_waiter_ready_count;
    std::atomic<int> butex_waiter_done_count;
    std::atomic<int> butex_waiter_resume_count;
    std::atomic<uint64_t> butex_waiter_worker_pthread;
    std::atomic<uint64_t> butex_waiter_resume_worker_pthread;
    std::atomic<int> pthread_waiter_ready_count;
    std::atomic<int> pthread_waiter_done_count;
    std::atomic<int> busy_task_started;
    std::atomic<int> busy_task_stop;
    std::atomic<int> busy_task_switches;
};

struct PinnedWaitCtx {
    PinnedWaitCtx()
        : butex(NULL)
        , use_timeout(false)
        , timeout_ms(0)
        , ready(0)
        , done(0)
        , wait_rc(0)
        , wait_errno(0)
        , pinned_worker_pthread(0)
        , resume_worker_pthread(0) {}

    void* butex;
    bool use_timeout;
    int timeout_ms;
    std::atomic<int> ready;
    std::atomic<int> done;
    std::atomic<int> wait_rc;
    std::atomic<int> wait_errno;
    std::atomic<uint64_t> pinned_worker_pthread;
    std::atomic<uint64_t> resume_worker_pthread;
};

ActiveTaskTestState g_state;
std::atomic<int> g_register_rc(-1);
std::atomic<int> g_register_once(0);
bthread::TaskControl* g_shared_tc = NULL;
std::atomic<int> g_shared_tc_once(0);

void ResetState() {
    g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
    g_state.init_calls.store(0, std::memory_order_relaxed);
    g_state.destroy_calls.store(0, std::memory_order_relaxed);
    g_state.harvest_calls.store(0, std::memory_order_relaxed);
    g_state.butex_ptr.store(0, std::memory_order_relaxed);
    g_state.butex_ptr_aux1.store(0, std::memory_order_relaxed);
    g_state.butex_ptr_aux2.store(0, std::memory_order_relaxed);
    g_state.pending_req_ptr.store(0, std::memory_order_relaxed);
    g_state.target_hook_worker_pthread.store(0, std::memory_order_relaxed);
    g_state.butex_expected_waiters.store(0, std::memory_order_relaxed);
    g_state.butex_wake_started.store(0, std::memory_order_relaxed);
    g_state.butex_wake_completed.store(0, std::memory_order_relaxed);
    g_state.butex_wake_rc.store(0, std::memory_order_relaxed);
    g_state.butex_wake_errno.store(0, std::memory_order_relaxed);
    g_state.butex_wake_rc_aux1.store(0, std::memory_order_relaxed);
    g_state.butex_wake_errno_aux1.store(0, std::memory_order_relaxed);
    g_state.butex_wake_rc_aux2.store(0, std::memory_order_relaxed);
    g_state.butex_wake_errno_aux2.store(0, std::memory_order_relaxed);
    g_state.hook_wake_harvest_calls.store(0, std::memory_order_relaxed);
    g_state.butex_waiter_ready_count.store(0, std::memory_order_relaxed);
    g_state.butex_waiter_done_count.store(0, std::memory_order_relaxed);
    g_state.butex_waiter_resume_count.store(0, std::memory_order_relaxed);
    g_state.butex_waiter_worker_pthread.store(0, std::memory_order_relaxed);
    g_state.butex_waiter_resume_worker_pthread.store(0, std::memory_order_relaxed);
    g_state.pthread_waiter_ready_count.store(0, std::memory_order_relaxed);
    g_state.pthread_waiter_done_count.store(0, std::memory_order_relaxed);
    g_state.busy_task_started.store(0, std::memory_order_relaxed);
    g_state.busy_task_stop.store(0, std::memory_order_relaxed);
    g_state.busy_task_switches.store(0, std::memory_order_relaxed);
}

uint64_t PthreadToU64(pthread_t tid) {
    uint64_t v = 0;
    memcpy(&v, &tid, std::min(sizeof(v), sizeof(tid)));
    return v;
}

bool WaitAtomicAtLeast(const std::atomic<int>& value, int expected, int timeout_ms) {
    for (int i = 0; i < timeout_ms; ++i) {
        if (value.load(std::memory_order_relaxed) >= expected) {
            return true;
        }
        usleep(1000);
    }
    return value.load(std::memory_order_relaxed) >= expected;
}

bool WaitAtomicEqual(const std::atomic<int>& value, int expected, int timeout_ms) {
    for (int i = 0; i < timeout_ms; ++i) {
        if (value.load(std::memory_order_relaxed) == expected) {
            return true;
        }
        usleep(1000);
    }
    return value.load(std::memory_order_relaxed) == expected;
}

bool WaitPinnedButexQueued(void* butex, int timeout_ms) {
    for (int i = 0; i < timeout_ms; ++i) {
        errno = 0;
        const int rc = bthread::butex_wake(butex, true);
        const int err = errno;
        if (rc == -1 && err == EINVAL) {
            return true;
        }
        if (rc == 0) {
            usleep(1000);
            continue;
        }
        return false;
    }
    return false;
}

void DrainHookActions() {
    ASSERT_TRUE(WaitAtomicEqual(g_state.hook_action_inflight, 0, 5000));
}

void QuiesceHookActionsAfterModeIdle() {
    DrainHookActions();
    usleep(1000);
    DrainHookActions();
}

void PrepareForCase() {
    g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
    QuiesceHookActionsAfterModeIdle();
    ResetState();
}

bthread::TaskControl& GetSharedTaskControl() {
    int expected = 0;
    if (g_shared_tc_once.compare_exchange_strong(
            expected, 1, std::memory_order_relaxed)) {
        g_shared_tc = new bthread::TaskControl();
        CHECK(g_shared_tc != NULL);
        // Keep one TaskControl instance in this process. Multiple TaskControl
        // instances expose fixed-name bvars and conflict in CI builds with
        // BRPC_BTHREAD_TRACER enabled.
        CHECK_EQ(0, g_shared_tc->init(2));
        CHECK(WaitAtomicAtLeast(g_state.init_calls, 2, 5000));
    }
    CHECK(g_shared_tc != NULL);
    return *g_shared_tc;
}

bthread::TaskControl& GetSharedSingleWorkerTaskControl() {
    return GetSharedTaskControl();
}

bthread::TaskControl& GetSharedTwoWorkerTaskControl() {
    return GetSharedTaskControl();
}

void* TestButexWaitTask(void*) {
    void* butex = reinterpret_cast<void*>(
        g_state.butex_ptr.load(std::memory_order_relaxed));
    if (butex == NULL) {
        g_state.butex_waiter_done_count.fetch_add(1, std::memory_order_relaxed);
        return NULL;
    }
    g_state.butex_waiter_worker_pthread.store(PthreadToU64(pthread_self()),
                                              std::memory_order_relaxed);
    g_state.butex_waiter_ready_count.fetch_add(1, std::memory_order_relaxed);
    const int rc = bthread::butex_wait(butex, 0, NULL);
    if (rc == 0) {
        g_state.butex_waiter_resume_count.fetch_add(1, std::memory_order_relaxed);
        g_state.butex_waiter_resume_worker_pthread.store(PthreadToU64(pthread_self()),
                                                         std::memory_order_relaxed);
    } else if (errno == EWOULDBLOCK) {
        // Value changed before waiter was queued; still resumed from caller's view.
        g_state.butex_waiter_resume_count.fetch_add(1, std::memory_order_relaxed);
        g_state.butex_waiter_resume_worker_pthread.store(PthreadToU64(pthread_self()),
                                                         std::memory_order_relaxed);
    }
    g_state.butex_waiter_done_count.fetch_add(1, std::memory_order_relaxed);
    return NULL;
}

void* TestBusyYieldTask(void*) {
    g_state.busy_task_started.fetch_add(1, std::memory_order_relaxed);
    while (!g_state.busy_task_stop.load(std::memory_order_relaxed)) {
        g_state.busy_task_switches.fetch_add(1, std::memory_order_relaxed);
        bthread_yield();
    }
    return NULL;
}

void* TestPthreadButexWait(void*) {
    void* butex = reinterpret_cast<void*>(
        g_state.butex_ptr.load(std::memory_order_relaxed));
    if (butex == NULL) {
        g_state.pthread_waiter_done_count.fetch_add(1, std::memory_order_relaxed);
        return NULL;
    }
    g_state.pthread_waiter_ready_count.fetch_add(1, std::memory_order_relaxed);
    const int rc = bthread::butex_wait(butex, 0, NULL);
    if (rc != 0 && errno != EWOULDBLOCK) {
        // Still count completion; assertions are on within wake behavior.
    }
    g_state.pthread_waiter_done_count.fetch_add(1, std::memory_order_relaxed);
    return NULL;
}

void* TestRequestWaitTask(void* arg) {
    MockReqCtx* req = static_cast<MockReqCtx*>(arg);
    if (req == NULL || req->butex == NULL) {
        return NULL;
    }
    req->waiter_worker_pthread.store(PthreadToU64(pthread_self()),
                                     std::memory_order_relaxed);
    req->waiter_ready.store(1, std::memory_order_release);
    errno = 0;
    const int rc = bthread_butex_wait_local(req->butex, 0, NULL);
    const int err = errno;
    req->wait_rc.store(rc, std::memory_order_relaxed);
    req->wait_errno.store(err, std::memory_order_relaxed);
    req->resume_worker_pthread.store(PthreadToU64(pthread_self()),
                                     std::memory_order_relaxed);
    if (req->result_ready.load(std::memory_order_acquire) == 1) {
        req->resume_saw_result_ready.store(1, std::memory_order_relaxed);
    }
    req->waiter_done.store(1, std::memory_order_release);
    return NULL;
}

void* TestPinnedButexLocalWaitTask(void*) {
    void* butex = reinterpret_cast<void*>(
        g_state.butex_ptr.load(std::memory_order_relaxed));
    if (butex == NULL) {
        g_state.butex_waiter_done_count.fetch_add(1, std::memory_order_relaxed);
        return NULL;
    }
    const uint64_t home = PthreadToU64(pthread_self());
    g_state.butex_waiter_worker_pthread.store(home, std::memory_order_relaxed);
    g_state.butex_waiter_ready_count.fetch_add(1, std::memory_order_relaxed);
    const int rc = bthread_butex_wait_local(butex, 0, NULL);
    if (rc == 0 || errno == EWOULDBLOCK) {
        g_state.butex_waiter_resume_count.fetch_add(1, std::memory_order_relaxed);
        g_state.butex_waiter_resume_worker_pthread.store(PthreadToU64(pthread_self()),
                                                         std::memory_order_relaxed);
    }
    g_state.butex_waiter_done_count.fetch_add(1, std::memory_order_relaxed);
    return NULL;
}

void* TestPinnedWaitTask(void* arg) {
    PinnedWaitCtx* ctx = static_cast<PinnedWaitCtx*>(arg);
    if (ctx == NULL || ctx->butex == NULL) {
        if (ctx) {
            ctx->done.store(1, std::memory_order_release);
        }
        return NULL;
    }

    const uint64_t home = PthreadToU64(pthread_self());
    ctx->pinned_worker_pthread.store(home, std::memory_order_relaxed);
    ctx->ready.store(1, std::memory_order_release);

    timespec abstime;
    const timespec* pabstime = NULL;
    if (ctx->use_timeout) {
        abstime = butil::milliseconds_from_now(ctx->timeout_ms);
        pabstime = &abstime;
    }
    errno = 0;
    const int wait_rc = bthread_butex_wait_local(ctx->butex, 0, pabstime);
    const int wait_errno = errno;
    ctx->wait_rc.store(wait_rc, std::memory_order_relaxed);
    ctx->wait_errno.store(wait_errno, std::memory_order_relaxed);
    ctx->resume_worker_pthread.store(PthreadToU64(pthread_self()),
                                     std::memory_order_relaxed);
    ctx->done.store(1, std::memory_order_release);
    return NULL;
}

bool MaybeRunWithinWakeFromHook(const bthread_active_task_ctx_t* ctx,
                                int mode,
                                bool* skip_park_out) {
    struct ScopedHookInflight {
        ScopedHookInflight() {
            g_state.hook_action_inflight.fetch_add(1, std::memory_order_relaxed);
        }
        ~ScopedHookInflight() {
            g_state.hook_action_inflight.fetch_sub(1, std::memory_order_relaxed);
        }
    } scoped_inflight;

    if (mode != TEST_MODE_BUTEX_WAKE_WITHIN &&
        mode != TEST_MODE_BUTEX_WAKE_WITHIN_NULL &&
        mode != TEST_MODE_BUTEX_WAKE_WITHIN_NO_WAITER &&
        mode != TEST_MODE_BUTEX_WAKE_WITHIN_PTHREAD_WAITER &&
        mode != TEST_MODE_BUSY_PERIODIC_POLL_WAKE &&
        mode != TEST_MODE_SCENARIO_REQ_WAKE &&
        mode != TEST_MODE_SCENARIO_REQ_WAKE_BUSY_PERIODIC &&
        mode != TEST_MODE_BUTEX_WAKE_WITHIN_STRICT_CROSS_WORKER_REJECT &&
        mode != TEST_MODE_BUTEX_WAKE_WITHIN_EAGAIN_WHEN_PINNED_RQ_FULL) {
        return false;
    }

    const uint64_t target_worker = g_state.target_hook_worker_pthread.load(
        std::memory_order_relaxed);
    if (target_worker != 0 && PthreadToU64(ctx->worker_pthread) != target_worker) {
        return false;
    }

    const bool is_scenario_req_wake =
        (mode == TEST_MODE_SCENARIO_REQ_WAKE ||
         mode == TEST_MODE_SCENARIO_REQ_WAKE_BUSY_PERIODIC);

    if (mode == TEST_MODE_BUTEX_WAKE_WITHIN ||
        mode == TEST_MODE_BUTEX_WAKE_WITHIN_STRICT_CROSS_WORKER_REJECT ||
        mode == TEST_MODE_BUTEX_WAKE_WITHIN_EAGAIN_WHEN_PINNED_RQ_FULL ||
        mode == TEST_MODE_BUSY_PERIODIC_POLL_WAKE) {
        const int expected_waiters =
            g_state.butex_expected_waiters.load(std::memory_order_relaxed);
        if (g_state.butex_waiter_ready_count.load(std::memory_order_relaxed) <
            expected_waiters) {
            return true;
        }
    } else if (mode == TEST_MODE_BUTEX_WAKE_WITHIN_PTHREAD_WAITER) {
        if (g_state.pthread_waiter_ready_count.load(std::memory_order_relaxed) < 1) {
            return true;
        }
    } else if (is_scenario_req_wake) {
        MockReqCtx* req = reinterpret_cast<MockReqCtx*>(
            g_state.pending_req_ptr.load(std::memory_order_acquire));
        if (req == NULL ||
            req->waiter_ready.load(std::memory_order_acquire) < 1 ||
            req->completion_published.load(std::memory_order_acquire) == 0) {
            return true;
        }
    }

    int expected = 0;
    if (!g_state.butex_wake_started.compare_exchange_strong(
            expected, 1, std::memory_order_relaxed)) {
        return true;
    }

    g_state.hook_wake_harvest_calls.fetch_add(1, std::memory_order_relaxed);

    void* butex = NULL;
    MockReqCtx* req = NULL;
    if (mode == TEST_MODE_BUTEX_WAKE_WITHIN_EAGAIN_WHEN_PINNED_RQ_FULL) {
        void* butex0 = reinterpret_cast<void*>(
            g_state.butex_ptr.load(std::memory_order_relaxed));
        void* butex1 = reinterpret_cast<void*>(
            g_state.butex_ptr_aux1.load(std::memory_order_relaxed));
        void* butex2 = reinterpret_cast<void*>(
            g_state.butex_ptr_aux2.load(std::memory_order_relaxed));
        errno = 0;
        const int rc0 = bthread_butex_wake_within(ctx, butex0);
        const int err0 = errno;
        errno = 0;
        const int rc1 = bthread_butex_wake_within(ctx, butex1);
        const int err1 = errno;
        errno = 0;
        const int rc2 = bthread_butex_wake_within(ctx, butex2);
        const int err2 = errno;

        g_state.butex_wake_rc.store(rc0, std::memory_order_relaxed);
        g_state.butex_wake_errno.store(err0, std::memory_order_relaxed);
        g_state.butex_wake_rc_aux1.store(rc1, std::memory_order_relaxed);
        g_state.butex_wake_errno_aux1.store(err1, std::memory_order_relaxed);
        g_state.butex_wake_rc_aux2.store(rc2, std::memory_order_relaxed);
        g_state.butex_wake_errno_aux2.store(err2, std::memory_order_relaxed);

        if (!(rc0 == 1 && rc1 == 1 && rc2 == -1 && err2 == EAGAIN)) {
            // setup race: retry in next harvest round.
            g_state.butex_wake_started.store(0, std::memory_order_relaxed);
            return true;
        }
        g_state.butex_wake_completed.fetch_add(1, std::memory_order_relaxed);
        if (skip_park_out) {
            *skip_park_out = true;
        }
        return true;
    }

    if (is_scenario_req_wake) {
        req = reinterpret_cast<MockReqCtx*>(
            g_state.pending_req_ptr.load(std::memory_order_acquire));
        if (req == NULL) {
            g_state.butex_wake_started.store(0, std::memory_order_relaxed);
            return true;
        }
        req->hook_worker_pthread.store(PthreadToU64(ctx->worker_pthread),
                                       std::memory_order_relaxed);
        req->result_ready.store(1, std::memory_order_release);
        butex = req->butex;
    } else {
        butex = reinterpret_cast<void*>(
            g_state.butex_ptr.load(std::memory_order_relaxed));
        if (mode == TEST_MODE_BUTEX_WAKE_WITHIN_NULL) {
            butex = NULL;
        }
    }

    errno = 0;
    const int rc = bthread_butex_wake_within(ctx, butex);
    const int err = errno;
    g_state.butex_wake_rc.store(rc, std::memory_order_relaxed);
    g_state.butex_wake_errno.store(err, std::memory_order_relaxed);

    bool done = true;
    if (is_scenario_req_wake) {
        done = (rc == 1);
    } else if (mode == TEST_MODE_BUTEX_WAKE_WITHIN ||
        mode == TEST_MODE_BUTEX_WAKE_WITHIN_STRICT_CROSS_WORKER_REJECT ||
        mode == TEST_MODE_BUSY_PERIODIC_POLL_WAKE) {
        const int expected_waiters =
            g_state.butex_expected_waiters.load(std::memory_order_relaxed);
        if (mode == TEST_MODE_BUTEX_WAKE_WITHIN_STRICT_CROSS_WORKER_REJECT) {
            done = (rc == -1 && err == EINVAL);
        } else if (expected_waiters == 1) {
            done = (rc == 1);
        } else if (expected_waiters > 1) {
            done = (rc == -1 && err == EINVAL) || (rc == 1);
        }
    } else if (mode == TEST_MODE_BUTEX_WAKE_WITHIN_PTHREAD_WAITER) {
        done = (rc == -1 && err == EINVAL);
    }

    if (!done) {
        g_state.butex_wake_started.store(0, std::memory_order_relaxed);
        return true;
    }

    if (req != NULL) {
        req->wake_rc.store(rc, std::memory_order_relaxed);
        req->wake_errno.store(err, std::memory_order_relaxed);
    }
    g_state.butex_wake_completed.fetch_add(1, std::memory_order_relaxed);
    if (skip_park_out) {
        *skip_park_out = true;
    }
    return true;
}

int ActiveTaskWorkerInit(void** worker_local,
                         const bthread_active_task_ctx_t* ctx,
                         void*) {
    (void)ctx;
    PerWorkerState* s = new (std::nothrow) PerWorkerState;
    if (s == NULL) {
        return ENOMEM;
    }
    *worker_local = s;
    g_state.init_calls.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

void ActiveTaskWorkerDestroy(void* worker_local,
                             const bthread_active_task_ctx_t*,
                             void*) {
    delete static_cast<PerWorkerState*>(worker_local);
    g_state.destroy_calls.fetch_add(1, std::memory_order_relaxed);
}

int ActiveTaskHarvest(void* worker_local,
                      const bthread_active_task_ctx_t* ctx) {
    g_state.harvest_calls.fetch_add(1, std::memory_order_relaxed);
    (void)worker_local;
    const int mode = g_state.mode.load(std::memory_order_acquire);
    if (mode == TEST_MODE_IDLE_WAIT_INTERVAL) {
        return 0;
    }
    bool skip_park = false;
    if (MaybeRunWithinWakeFromHook(ctx, mode, &skip_park)) {
        return skip_park ? 1 : 0;
    }
    return 0;
}

int RegisterTestActiveTaskType() {
    bthread_active_task_type_t type;
    memset(&type, 0, sizeof(type));
    type.struct_size = sizeof(type);
    type.name = "active_task_unittest";
    type.worker_init = ActiveTaskWorkerInit;
    type.worker_destroy = ActiveTaskWorkerDestroy;
    type.harvest = ActiveTaskHarvest;
    return bthread_register_active_task_type(&type);
}

void* JustExit(void*) {
    return NULL;
}

int ChildCheckRegisterAfterInitRejected() {
    bthread_t tid = INVALID_BTHREAD;
    if (bthread_start_background(&tid, NULL, JustExit, NULL) != 0) {
        return 2;
    }
    bthread_join(tid, NULL);

    bthread_active_task_type_t type;
    memset(&type, 0, sizeof(type));
    type.struct_size = sizeof(type);
    type.name = "active_task_after_init";
    type.harvest = ActiveTaskHarvest;
    const int rc = bthread_register_active_task_type(&type);
    return (rc == EPERM ? 0 : 3);
}

int ChildCheckLocalWorkerInitDestroyAndIdleWaitInterval() {
    if (g_register_rc.load(std::memory_order_relaxed) != 0) {
        return 20;
    }
    PrepareForCase();
    {
        bthread::TaskControl tc;
        if (tc.init(2) != 0) {
            return 21;
        }
        if (!WaitAtomicAtLeast(g_state.init_calls, 2, 5000)) {
            return 22;
        }
        const int harvest_snapshot = g_state.harvest_calls.load(std::memory_order_relaxed);
        const int64_t old_idle_wait_ns = bthread::FLAGS_bthread_active_task_idle_wait_ns;
        bthread::FLAGS_bthread_active_task_idle_wait_ns = 1000 * 1000;  // 1ms
        g_state.mode.store(TEST_MODE_IDLE_WAIT_INTERVAL, std::memory_order_release);
        tc.signal_task(2, BTHREAD_TAG_DEFAULT);
        if (!WaitAtomicAtLeast(g_state.harvest_calls, harvest_snapshot + 3, 5000)) {
            bthread::FLAGS_bthread_active_task_idle_wait_ns = old_idle_wait_ns;
            return 23;
        }
        bthread::FLAGS_bthread_active_task_idle_wait_ns = old_idle_wait_ns;
        g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
        if (!WaitAtomicEqual(g_state.hook_action_inflight, 0, 5000)) {
            return 25;
        }
    }
    return (g_state.destroy_calls.load(std::memory_order_relaxed) == 2 ? 0 : 24);
}

int ChildCheckWakeWithinEagainWhenPinnedRqFull() {
    if (g_register_rc.load(std::memory_order_relaxed) != 0) {
        return 30;
    }
    const int32_t old_runqueue_capacity = FLAGS_task_group_runqueue_capacity;
    FLAGS_task_group_runqueue_capacity = 2;

    int ret = 0;
    PrepareForCase();
    {
        bthread::TaskControl tc;
        if (tc.init(1) != 0) {
            ret = 31;
        } else if (!WaitAtomicAtLeast(g_state.init_calls, 1, 5000)) {
            ret = 32;
        } else {
            bthread::TaskGroup* tg = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
            if (tg == NULL) {
                ret = 33;
            } else {
                PinnedWaitCtx ctx[3];
                bthread_t tids[3] = {
                    INVALID_BTHREAD, INVALID_BTHREAD, INVALID_BTHREAD };
                void* butexes[3] = { NULL, NULL, NULL };
                for (int i = 0; i < 3; ++i) {
                    butexes[i] = bthread::butex_create();
                    if (butexes[i] == NULL) {
                        ret = 34;
                        break;
                    }
                    static_cast<butil::atomic<int>*>(butexes[i])->store(
                        0, butil::memory_order_relaxed);
                    ctx[i].butex = butexes[i];
                    if (tg->start_background<true>(&tids[i], NULL,
                                                   TestPinnedWaitTask, &ctx[i]) != 0) {
                        ret = 35;
                        break;
                    }
                    tg->flush_nosignal_tasks();
                    if (!WaitAtomicAtLeast(ctx[i].ready, 1, 5000)) {
                        ret = 36;
                        break;
                    }
                    if (!WaitPinnedButexQueued(butexes[i], 5000)) {
                        ret = 37;
                        break;
                    }
                }

                if (ret == 0) {
                    const uint64_t home = ctx[0].pinned_worker_pthread.load(
                        std::memory_order_relaxed);
                    if (home == 0) {
                        ret = 38;
                    } else {
                        g_state.butex_ptr.store(reinterpret_cast<uintptr_t>(butexes[0]),
                                                std::memory_order_relaxed);
                        g_state.butex_ptr_aux1.store(reinterpret_cast<uintptr_t>(butexes[1]),
                                                     std::memory_order_relaxed);
                        g_state.butex_ptr_aux2.store(reinterpret_cast<uintptr_t>(butexes[2]),
                                                     std::memory_order_relaxed);
                        g_state.butex_expected_waiters.store(0, std::memory_order_relaxed);
                        g_state.target_hook_worker_pthread.store(home,
                                                                 std::memory_order_relaxed);
                        g_state.mode.store(
                            TEST_MODE_BUTEX_WAKE_WITHIN_EAGAIN_WHEN_PINNED_RQ_FULL,
                            std::memory_order_release);
                        tc.signal_task(1, BTHREAD_TAG_DEFAULT);
                        if (!WaitAtomicAtLeast(g_state.butex_wake_completed, 1, 5000)) {
                            ret = 39;
                        } else if (g_state.butex_wake_rc.load(std::memory_order_relaxed) != 1 ||
                                   g_state.butex_wake_rc_aux1.load(std::memory_order_relaxed) != 1 ||
                                   g_state.butex_wake_rc_aux2.load(std::memory_order_relaxed) != -1 ||
                                   g_state.butex_wake_errno_aux2.load(std::memory_order_relaxed) != EAGAIN) {
                            ret = 40;
                        } else {
                            g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
                            QuiesceHookActionsAfterModeIdle();
                            g_state.butex_wake_started.store(0, std::memory_order_relaxed);
                            g_state.butex_wake_completed.store(0, std::memory_order_relaxed);
                            g_state.butex_wake_rc.store(0, std::memory_order_relaxed);
                            g_state.butex_wake_errno.store(0, std::memory_order_relaxed);
                            g_state.target_hook_worker_pthread.store(home,
                                                                     std::memory_order_relaxed);
                            g_state.butex_ptr.store(
                                reinterpret_cast<uintptr_t>(butexes[2]),
                                std::memory_order_relaxed);
                            g_state.mode.store(TEST_MODE_BUTEX_WAKE_WITHIN,
                                               std::memory_order_release);
                            tc.signal_task(1, BTHREAD_TAG_DEFAULT);
                            if (!WaitAtomicAtLeast(g_state.butex_wake_completed, 1, 5000)) {
                                ret = 41;
                            } else if (g_state.butex_wake_rc.load(std::memory_order_relaxed) != 1) {
                                ret = 42;
                            }
                        }
                    }
                }

                g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
                g_state.target_hook_worker_pthread.store(0, std::memory_order_relaxed);
                QuiesceHookActionsAfterModeIdle();
                for (int i = 0; i < 3; ++i) {
                    if (tids[i] != INVALID_BTHREAD) {
                        if (bthread_join(tids[i], NULL) != 0 && ret == 0) {
                            ret = 43;
                        }
                    }
                    if (butexes[i] != NULL) {
                        bthread::butex_destroy(butexes[i]);
                    }
                }
                g_state.butex_ptr.store(0, std::memory_order_relaxed);
                g_state.butex_ptr_aux1.store(0, std::memory_order_relaxed);
                g_state.butex_ptr_aux2.store(0, std::memory_order_relaxed);
            }
        }
    }
    FLAGS_task_group_runqueue_capacity = old_runqueue_capacity;
    return ret;
}

bool GetSelfExecutablePath(char* buf, size_t len) {
    if (buf == NULL || len == 0) {
        return false;
    }
    const ssize_t n = butil::GetProcessAbsolutePath(buf, len);
    if (n <= 0 || static_cast<size_t>(n) >= len) {
        return false;
    }
    buf[n] = '\0';
    return true;
}

bool GetArgv0Fallback(char* buf, size_t len) {
    if (buf == NULL || len == 0) {
        return false;
    }
    const ssize_t n = butil::ReadCommandLine(buf, len, false);
    if (n <= 0 || static_cast<size_t>(n) >= len) {
        return false;
    }
    buf[n] = '\0';
    return true;
}

int RunChildMode(const char* mode) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        char self_path[PATH_MAX];
        char argv0[PATH_MAX];
        const bool have_self_path = GetSelfExecutablePath(self_path, sizeof(self_path));
        const bool have_argv0 = GetArgv0Fallback(argv0, sizeof(argv0));
        if (!have_self_path && !have_argv0) {
            _exit(4);
        }
        setenv("BRPC_ACTIVE_TASK_UT_CHILD_MODE", mode, 1);
        if (have_self_path) {
            char* const argv[] = { self_path, NULL };
            execv(self_path, argv);
        }
        if (have_argv0) {
            char* const argv[] = { argv0, NULL };
            execvp(argv0, argv);
        }
        _exit(5);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        return -1;
    }
    if (!WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

void RunButexWakeWithinCase(int waiter_count,
                            int expected_wake_rc,
                            int expected_wake_errno) {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedSingleWorkerTaskControl();
    bthread::TaskGroup* tg = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), tg);
    ASSERT_GT(waiter_count, 0);
    const bool allow_setup_race_retry = (waiter_count > 1 && expected_wake_rc < 0);
    const int max_attempts = allow_setup_race_retry ? 10 : 1;

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        PrepareForCase();

        void* butex = bthread::butex_create();
        ASSERT_NE(static_cast<void*>(NULL), butex);
        static_cast<butil::atomic<int>*>(butex)->store(0, butil::memory_order_relaxed);
        g_state.butex_ptr.store(reinterpret_cast<uintptr_t>(butex),
                                std::memory_order_relaxed);
        g_state.butex_expected_waiters.store(waiter_count, std::memory_order_relaxed);

        std::vector<bthread_t> tids(waiter_count, INVALID_BTHREAD);
        bool retry_case = false;

        for (int i = 0; i < waiter_count; ++i) {
            ASSERT_EQ(0, tg->start_background<true>(&tids[i], NULL,
                                                    TestButexWaitTask, NULL));
        }
        tg->flush_nosignal_tasks();
        ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_waiter_ready_count, waiter_count, 5000));
        if (waiter_count == 1) {
            const uint64_t waiter_worker =
                g_state.butex_waiter_worker_pthread.load(std::memory_order_relaxed);
            ASSERT_NE(0u, waiter_worker);
            g_state.target_hook_worker_pthread.store(waiter_worker,
                                                     std::memory_order_relaxed);
        }
        g_state.mode.store(TEST_MODE_BUTEX_WAKE_WITHIN, std::memory_order_release);
        tc.signal_task(2, BTHREAD_TAG_DEFAULT);
        ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_wake_started, 1, 5000));
        int observed_wake_rc = g_state.butex_wake_rc.load(std::memory_order_relaxed);
        int observed_wake_errno = g_state.butex_wake_errno.load(std::memory_order_relaxed);
        if (expected_wake_rc < 0) {
            ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_wake_completed, 1, 5000))
                << "waiters=" << waiter_count
                << " ready=" << g_state.butex_waiter_ready_count.load(std::memory_order_relaxed)
                << " started=" << g_state.butex_wake_started.load(std::memory_order_relaxed)
                << " rc=" << g_state.butex_wake_rc.load(std::memory_order_relaxed)
                << " errno=" << g_state.butex_wake_errno.load(std::memory_order_relaxed);
            observed_wake_rc = g_state.butex_wake_rc.load(std::memory_order_relaxed);
            observed_wake_errno = g_state.butex_wake_errno.load(std::memory_order_relaxed);
        }
        if (expected_wake_rc < 0) {
            static_cast<butil::atomic<int>*>(butex)->store(1, butil::memory_order_release);
            for (int kick = 0;
                 kick < 20 &&
                 g_state.butex_waiter_done_count.load(std::memory_order_relaxed) < waiter_count;
                 ++kick) {
                ASSERT_GE(bthread::butex_wake_all(butex, true), 0);
                usleep(1000);
            }
        }

        ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_waiter_done_count, waiter_count, 5000))
            << "wake_rc=" << observed_wake_rc
            << " wake_errno=" << observed_wake_errno
            << " ready=" << g_state.butex_waiter_ready_count.load(std::memory_order_relaxed)
            << " resumed=" << g_state.butex_waiter_resume_count.load(std::memory_order_relaxed)
            << " harvest_calls=" << g_state.harvest_calls.load(std::memory_order_relaxed)
            << " attempt=" << attempt;
        for (int i = 0; i < waiter_count; ++i) {
            ASSERT_EQ(0, bthread_join(tids[i], NULL));
        }
        ASSERT_EQ(waiter_count,
                  g_state.butex_waiter_resume_count.load(std::memory_order_relaxed));
        g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
        g_state.target_hook_worker_pthread.store(0, std::memory_order_relaxed);
        QuiesceHookActionsAfterModeIdle();
        const int final_wake_rc = g_state.butex_wake_rc.load(std::memory_order_relaxed);

        if (allow_setup_race_retry && observed_wake_rc == 1 && attempt < max_attempts) {
            retry_case = true;
        } else {
            if (expected_wake_rc < 0) {
                ASSERT_EQ(expected_wake_rc, observed_wake_rc);
                ASSERT_EQ(expected_wake_errno, observed_wake_errno);
            } else {
                ASSERT_EQ(expected_wake_rc, final_wake_rc);
            }
        }
        bthread::butex_destroy(butex);
        g_state.butex_ptr.store(0, std::memory_order_relaxed);
        if (!retry_case) {
            return;
        }
    }
}

void RunHookOnlyWakeCase(TestMode mode, void* butex, int expected_rc, int expected_errno) {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedSingleWorkerTaskControl();
    PrepareForCase();
    g_state.butex_ptr.store(reinterpret_cast<uintptr_t>(butex),
                            std::memory_order_relaxed);
    g_state.butex_expected_waiters.store(0, std::memory_order_relaxed);

    g_state.mode.store(mode, std::memory_order_release);
    tc.signal_task(1, BTHREAD_TAG_DEFAULT);
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_wake_completed, 1, 5000));
    ASSERT_EQ(expected_rc, g_state.butex_wake_rc.load(std::memory_order_relaxed));
    if (expected_rc < 0) {
        ASSERT_EQ(expected_errno, g_state.butex_wake_errno.load(std::memory_order_relaxed));
    }
    g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
    QuiesceHookActionsAfterModeIdle();
    g_state.butex_ptr.store(0, std::memory_order_relaxed);
}

void RunPthreadWaiterRejectedCase() {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedSingleWorkerTaskControl();
    PrepareForCase();

    void* butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), butex);
    static_cast<butil::atomic<int>*>(butex)->store(0, butil::memory_order_relaxed);
    g_state.butex_ptr.store(reinterpret_cast<uintptr_t>(butex),
                            std::memory_order_relaxed);

    pthread_t th;
    ASSERT_EQ(0, pthread_create(&th, NULL, TestPthreadButexWait, NULL));

    ASSERT_TRUE(WaitAtomicAtLeast(g_state.pthread_waiter_ready_count, 1, 5000));
    g_state.mode.store(TEST_MODE_BUTEX_WAKE_WITHIN_PTHREAD_WAITER,
                       std::memory_order_release);
    tc.signal_task(1, BTHREAD_TAG_DEFAULT);
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_wake_completed, 1, 5000));
    ASSERT_EQ(-1, g_state.butex_wake_rc.load(std::memory_order_relaxed));
    ASSERT_EQ(EINVAL, g_state.butex_wake_errno.load(std::memory_order_relaxed));
    g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
    QuiesceHookActionsAfterModeIdle();

    static_cast<butil::atomic<int>*>(butex)->store(1, butil::memory_order_release);
    for (int kick = 0;
         kick < 20 &&
         g_state.pthread_waiter_done_count.load(std::memory_order_relaxed) < 1;
         ++kick) {
        ASSERT_GE(bthread::butex_wake_all(butex, true), 0);
        usleep(1000);
    }
    ASSERT_EQ(0, pthread_join(th, NULL));
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.pthread_waiter_done_count, 1, 5000));

    bthread::butex_destroy(butex);
    g_state.butex_ptr.store(0, std::memory_order_relaxed);
}

struct ScopedPollEveryNSwitch {
    explicit ScopedPollEveryNSwitch(int32_t value)
        : old_(bthread::FLAGS_bthread_active_task_poll_every_nswitch) {
        bthread::FLAGS_bthread_active_task_poll_every_nswitch = value;
    }
    ~ScopedPollEveryNSwitch() {
        bthread::FLAGS_bthread_active_task_poll_every_nswitch = old_;
    }
    int32_t old_;
};

void RunBusyPeriodicPollWakeCase() {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedSingleWorkerTaskControl();
    bthread::TaskGroup* tg = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), tg);
    PrepareForCase();

    void* butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), butex);
    static_cast<butil::atomic<int>*>(butex)->store(0, butil::memory_order_relaxed);
    g_state.butex_ptr.store(reinterpret_cast<uintptr_t>(butex),
                            std::memory_order_relaxed);
    g_state.butex_expected_waiters.store(1, std::memory_order_relaxed);

    ScopedPollEveryNSwitch guard(1);
    bthread_t busy_tid = INVALID_BTHREAD;
    bthread_t waiter_tid = INVALID_BTHREAD;

    ASSERT_EQ(0, tg->start_background<true>(&busy_tid, NULL, TestBusyYieldTask, NULL));
    tg->flush_nosignal_tasks();
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.busy_task_started, 1, 5000));
    ASSERT_EQ(0, tg->start_background<true>(&waiter_tid, NULL, TestButexWaitTask, NULL));
    tg->flush_nosignal_tasks();
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_waiter_ready_count, 1, 5000));
    const uint64_t waiter_worker =
        g_state.butex_waiter_worker_pthread.load(std::memory_order_relaxed);
    ASSERT_NE(0u, waiter_worker);
    g_state.target_hook_worker_pthread.store(waiter_worker, std::memory_order_relaxed);
    g_state.mode.store(TEST_MODE_BUSY_PERIODIC_POLL_WAKE, std::memory_order_release);

    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_wake_started, 1, 5000));
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_waiter_done_count, 1, 5000));
    ASSERT_EQ(1, g_state.butex_wake_rc.load(std::memory_order_relaxed));
    ASSERT_GE(g_state.hook_wake_harvest_calls.load(std::memory_order_relaxed), 1);
    ASSERT_GE(g_state.harvest_calls.load(std::memory_order_relaxed), 1);
    ASSERT_GT(g_state.busy_task_switches.load(std::memory_order_relaxed), 0);

    g_state.busy_task_stop.store(1, std::memory_order_relaxed);
    ASSERT_EQ(0, bthread_join(waiter_tid, NULL));
    ASSERT_EQ(0, bthread_join(busy_tid, NULL));
    g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
    g_state.target_hook_worker_pthread.store(0, std::memory_order_relaxed);
    QuiesceHookActionsAfterModeIdle();

    bthread::butex_destroy(butex);
    g_state.butex_ptr.store(0, std::memory_order_relaxed);
}

bool ChooseTwoDistinctGroups(bthread::TaskControl& tc,
                             bthread::TaskGroup** g1,
                             bthread::TaskGroup** g2) {
    if (g1 == NULL || g2 == NULL) {
        return false;
    }
    *g1 = NULL;
    *g2 = NULL;
    if (tc.concurrency(BTHREAD_TAG_DEFAULT) < 2) {
        return false;
    }
    for (int i = 0; i < 256; ++i) {
        bthread::TaskGroup* a = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
        bthread::TaskGroup* b = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
        if (a != NULL && b != NULL && a != b) {
            *g1 = a;
            *g2 = b;
            return true;
        }
    }
    return false;
}

void RunStrictCrossWorkerRejectCase() {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    PrepareForCase();

    bthread::TaskControl& tc = GetSharedTwoWorkerTaskControl();

    bthread::TaskGroup* group_a = NULL;
    bthread::TaskGroup* group_b = NULL;
    ASSERT_TRUE(ChooseTwoDistinctGroups(tc, &group_a, &group_b));
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), group_a);
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), group_b);
    ASSERT_NE(group_a, group_b);
    ASSERT_NE(0u, PthreadToU64(group_a->tid()));
    ASSERT_NE(0u, PthreadToU64(group_b->tid()));

    void* butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), butex);
    static_cast<butil::atomic<int>*>(butex)->store(0, butil::memory_order_relaxed);
    g_state.butex_ptr.store(reinterpret_cast<uintptr_t>(butex),
                            std::memory_order_relaxed);
    g_state.butex_expected_waiters.store(1, std::memory_order_relaxed);

    ScopedPollEveryNSwitch poll_guard(1);
    bthread_t busy_b = INVALID_BTHREAD;
    bthread_t waiter_tid = INVALID_BTHREAD;

    ASSERT_EQ(0, group_a->start_background<true>(&waiter_tid, NULL,
                                                 TestButexWaitTask, NULL));
    group_a->flush_nosignal_tasks();
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_waiter_ready_count, 1, 5000));
    const uint64_t waiter_exec_worker =
        g_state.butex_waiter_worker_pthread.load(std::memory_order_relaxed);
    ASSERT_NE(0u, waiter_exec_worker);
    bthread::TaskGroup* waiter_group = NULL;
    bthread::TaskGroup* wrong_waker_group = NULL;
    if (waiter_exec_worker == PthreadToU64(group_a->tid())) {
        waiter_group = group_a;
        wrong_waker_group = group_b;
    } else if (waiter_exec_worker == PthreadToU64(group_b->tid())) {
        waiter_group = group_b;
        wrong_waker_group = group_a;
    } else {
        FAIL() << "waiter ran on unexpected worker pthread=" << waiter_exec_worker
               << " group_a=" << PthreadToU64(group_a->tid())
               << " group_b=" << PthreadToU64(group_b->tid());
    }
    ASSERT_EQ(0, wrong_waker_group->start_background<true>(&busy_b, NULL,
                                                           TestBusyYieldTask, NULL));
    wrong_waker_group->flush_nosignal_tasks();
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.busy_task_started, 1, 5000));

    g_state.target_hook_worker_pthread.store(PthreadToU64(wrong_waker_group->tid()),
                                             std::memory_order_relaxed);
    g_state.mode.store(TEST_MODE_BUTEX_WAKE_WITHIN_STRICT_CROSS_WORKER_REJECT,
                       std::memory_order_release);
    tc.signal_task(2, BTHREAD_TAG_DEFAULT);
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_wake_completed, 1, 5000))
        << "target_wrong=" << PthreadToU64(wrong_waker_group->tid())
        << " waiter_group=" << PthreadToU64(waiter_group->tid())
        << " waiter_exec=" << waiter_exec_worker
        << " busy_started=" << g_state.busy_task_started.load(std::memory_order_relaxed)
        << " harvest_calls=" << g_state.harvest_calls.load(std::memory_order_relaxed)
        << " hook_wake_harvest_calls="
        << g_state.hook_wake_harvest_calls.load(std::memory_order_relaxed)
        << " rc=" << g_state.butex_wake_rc.load(std::memory_order_relaxed)
        << " errno=" << g_state.butex_wake_errno.load(std::memory_order_relaxed)
        << " waiter_done=" << g_state.butex_waiter_done_count.load(std::memory_order_relaxed)
        << " waiter_resumed="
        << g_state.butex_waiter_resume_count.load(std::memory_order_relaxed);
    ASSERT_EQ(-1, g_state.butex_wake_rc.load(std::memory_order_relaxed));
    ASSERT_EQ(EINVAL, g_state.butex_wake_errno.load(std::memory_order_relaxed));
    ASSERT_EQ(0, g_state.butex_waiter_done_count.load(std::memory_order_relaxed));

    g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
    g_state.target_hook_worker_pthread.store(0, std::memory_order_relaxed);
    QuiesceHookActionsAfterModeIdle();
    g_state.butex_wake_started.store(0, std::memory_order_relaxed);
    g_state.butex_wake_completed.store(0, std::memory_order_relaxed);
    g_state.butex_wake_rc.store(0, std::memory_order_relaxed);
    g_state.butex_wake_errno.store(0, std::memory_order_relaxed);
    g_state.target_hook_worker_pthread.store(PthreadToU64(waiter_group->tid()),
                                             std::memory_order_relaxed);
    g_state.mode.store(TEST_MODE_BUTEX_WAKE_WITHIN, std::memory_order_release);
    tc.signal_task(2, BTHREAD_TAG_DEFAULT);

    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_wake_completed, 1, 5000));
    ASSERT_EQ(1, g_state.butex_wake_rc.load(std::memory_order_relaxed));
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_waiter_done_count, 1, 5000));
    ASSERT_EQ(1, g_state.butex_waiter_resume_count.load(std::memory_order_relaxed));

    g_state.busy_task_stop.store(1, std::memory_order_relaxed);
    ASSERT_EQ(0, bthread_join(waiter_tid, NULL));
    ASSERT_EQ(0, bthread_join(busy_b, NULL));

    g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
    g_state.target_hook_worker_pthread.store(0, std::memory_order_relaxed);
    QuiesceHookActionsAfterModeIdle();
    g_state.butex_ptr.store(0, std::memory_order_relaxed);
    bthread::butex_destroy(butex);
}

void RunRequestFlowCase(bool busy_periodic) {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedSingleWorkerTaskControl();
    bthread::TaskGroup* tg = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), tg);
    PrepareForCase();

    MockReqCtx* req = new MockReqCtx;
    ASSERT_NE(static_cast<MockReqCtx*>(NULL), req);
    req->butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), req->butex);
    static_cast<butil::atomic<int>*>(req->butex)->store(0, butil::memory_order_relaxed);
    g_state.pending_req_ptr.store(reinterpret_cast<uintptr_t>(req),
                                  std::memory_order_release);

    bthread_t waiter_tid = INVALID_BTHREAD;
    bthread_t busy_tid = INVALID_BTHREAD;
    int32_t old_poll_every_nswitch = 0;
    bool poll_overridden = false;
    if (busy_periodic) {
        old_poll_every_nswitch = bthread::FLAGS_bthread_active_task_poll_every_nswitch;
        bthread::FLAGS_bthread_active_task_poll_every_nswitch = 1;
        poll_overridden = true;
        ASSERT_EQ(0, tg->start_background<true>(&busy_tid, NULL, TestBusyYieldTask, NULL));
        ASSERT_TRUE(WaitAtomicAtLeast(g_state.busy_task_started, 1, 5000));
    }

    ASSERT_EQ(0, tg->start_background<true>(&waiter_tid, NULL, TestRequestWaitTask, req));
    ASSERT_TRUE(WaitAtomicAtLeast(req->waiter_ready, 1, 5000));

    req->completion_published.store(1, std::memory_order_release);
    g_state.mode.store(busy_periodic ? TEST_MODE_SCENARIO_REQ_WAKE_BUSY_PERIODIC
                                     : TEST_MODE_SCENARIO_REQ_WAKE,
                       std::memory_order_release);
    if (!busy_periodic) {
        tc.signal_task(1, BTHREAD_TAG_DEFAULT);
    }

    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_wake_started, 1, 5000));
    ASSERT_TRUE(WaitAtomicAtLeast(req->waiter_done, 1, 5000))
        << "wake_rc=" << req->wake_rc.load(std::memory_order_relaxed)
        << " wake_errno=" << req->wake_errno.load(std::memory_order_relaxed)
        << " hook_worker=" << req->hook_worker_pthread.load(std::memory_order_relaxed)
        << " waiter_worker=" << req->waiter_worker_pthread.load(std::memory_order_relaxed)
        << " resume_worker=" << req->resume_worker_pthread.load(std::memory_order_relaxed);

    if (busy_periodic) {
        ASSERT_GE(g_state.hook_wake_harvest_calls.load(std::memory_order_relaxed), 1);
        ASSERT_GE(g_state.harvest_calls.load(std::memory_order_relaxed), 1);
        ASSERT_GT(g_state.busy_task_switches.load(std::memory_order_relaxed), 0);
    }

    ASSERT_EQ(1, req->wake_rc.load(std::memory_order_relaxed));
    ASSERT_EQ(1, req->result_ready.load(std::memory_order_acquire));
    ASSERT_EQ(1, req->resume_saw_result_ready.load(std::memory_order_relaxed));
    ASSERT_EQ(0, req->wait_rc.load(std::memory_order_relaxed));
    const uint64_t waiter_worker = req->waiter_worker_pthread.load(std::memory_order_relaxed);
    const uint64_t hook_worker = req->hook_worker_pthread.load(std::memory_order_relaxed);
    const uint64_t resume_worker = req->resume_worker_pthread.load(std::memory_order_relaxed);
    ASSERT_NE(0u, waiter_worker);
    ASSERT_NE(0u, hook_worker);
    ASSERT_NE(0u, resume_worker);
    ASSERT_EQ(waiter_worker, hook_worker);
    ASSERT_EQ(waiter_worker, resume_worker);

    if (busy_periodic) {
        g_state.busy_task_stop.store(1, std::memory_order_relaxed);
    }
    ASSERT_EQ(0, bthread_join(waiter_tid, NULL));
    if (busy_periodic) {
        ASSERT_EQ(0, bthread_join(busy_tid, NULL));
    }
    g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
    QuiesceHookActionsAfterModeIdle();
    g_state.pending_req_ptr.store(0, std::memory_order_release);

    if (poll_overridden) {
        bthread::FLAGS_bthread_active_task_poll_every_nswitch = old_poll_every_nswitch;
    }
    bthread::butex_destroy(req->butex);
    delete req;
}

void StartTaskOnGroupAndFlush(bthread::TaskGroup* g,
                              bthread_t* tid,
                              void* (*fn)(void*),
                              void* arg) {
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), g);
    ASSERT_NE(static_cast<bthread_t*>(NULL), tid);
    *tid = INVALID_BTHREAD;
    ASSERT_EQ(0, g->start_background<true>(tid, NULL, fn, arg));
    g->flush_nosignal_tasks();
}

void StartPinnedWaitTaskAndWaitReady(bthread::TaskGroup* g,
                                     PinnedWaitCtx* ctx,
                                     bthread_t* tid,
                                     uint64_t* home_worker) {
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), g);
    ASSERT_NE(static_cast<PinnedWaitCtx*>(NULL), ctx);
    ASSERT_NE(static_cast<bthread_t*>(NULL), tid);
    ASSERT_NE(static_cast<uint64_t*>(NULL), home_worker);
    StartTaskOnGroupAndFlush(g, tid, TestPinnedWaitTask, ctx);
    ASSERT_TRUE(WaitAtomicAtLeast(ctx->ready, 1, 5000));
    *home_worker = ctx->pinned_worker_pthread.load(std::memory_order_relaxed);
    ASSERT_NE(0u, *home_worker);
}

void WaitJoinPinnedWaitTaskAndAssert(PinnedWaitCtx* ctx,
                                     bthread_t tid,
                                     int expected_wait_rc,
                                     int expected_wait_errno,
                                     uint64_t expected_home_worker) {
    ASSERT_NE(static_cast<PinnedWaitCtx*>(NULL), ctx);
    ASSERT_TRUE(WaitAtomicAtLeast(ctx->done, 1, 5000));
    ASSERT_EQ(0, bthread_join(tid, NULL));
    ASSERT_EQ(expected_wait_rc, ctx->wait_rc.load(std::memory_order_relaxed));
    if (expected_wait_rc < 0) {
        ASSERT_EQ(expected_wait_errno, ctx->wait_errno.load(std::memory_order_relaxed));
    }
    ASSERT_EQ(expected_home_worker,
              ctx->resume_worker_pthread.load(std::memory_order_relaxed));
}

int CallGenericWakeVariant(GenericWakeVariant variant, void* butex,
                           void* requeue_target) {
    switch (variant) {
    case GENERIC_WAKE:
        return bthread::butex_wake(butex, true);
    case GENERIC_WAKE_N:
        return bthread::butex_wake_n(butex, 1, true);
    case GENERIC_WAKE_EXCEPT:
        return bthread::butex_wake_except(butex, INVALID_BTHREAD);
    case GENERIC_REQUEUE:
        CHECK(requeue_target != NULL);
        return bthread::butex_requeue(butex, requeue_target);
    default:
        CHECK(false) << "unknown generic wake variant=" << variant;
        return -1;
    }
}

void RunPinnedGenericWakeRejectedCase(GenericWakeVariant variant) {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedTwoWorkerTaskControl();
    bthread::TaskGroup* g1 = NULL;
    bthread::TaskGroup* g2 = NULL;
    ASSERT_TRUE(ChooseTwoDistinctGroups(tc, &g1, &g2));
    PrepareForCase();

    PinnedWaitCtx ctx;
    ctx.butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), ctx.butex);
    static_cast<butil::atomic<int>*>(ctx.butex)->store(0, butil::memory_order_relaxed);
    void* requeue_butex = NULL;
    if (variant == GENERIC_REQUEUE) {
        requeue_butex = bthread::butex_create();
        ASSERT_NE(static_cast<void*>(NULL), requeue_butex);
        static_cast<butil::atomic<int>*>(requeue_butex)->store(
            0, butil::memory_order_relaxed);
    }

    bthread_t tid = INVALID_BTHREAD;
    uint64_t home = 0;
    StartPinnedWaitTaskAndWaitReady(g1, &ctx, &tid, &home);
    bool rejected = false;
    for (int i = 0; i < 50; ++i) {
        errno = 0;
        const int rc = CallGenericWakeVariant(variant, ctx.butex, requeue_butex);
        const int err = errno;
        if (rc == -1 && err == EINVAL) {
            rejected = true;
            break;
        }
        ASSERT_EQ(0, rc);
        usleep(1000);
    }
    ASSERT_TRUE(rejected);
    ASSERT_EQ(0, ctx.done.load(std::memory_order_acquire));

    bool interrupt_sent = false;
    for (int i = 0; i < 20 && ctx.done.load(std::memory_order_acquire) == 0; ++i) {
        ASSERT_EQ(0, bthread_interrupt(tid));
        interrupt_sent = true;
        if (WaitAtomicAtLeast(ctx.done, 1, 50)) {
            break;
        }
    }
    ASSERT_TRUE(interrupt_sent);
    WaitJoinPinnedWaitTaskAndAssert(&ctx, tid, -1, EINTR, home);

    if (requeue_butex != NULL) {
        bthread::butex_destroy(requeue_butex);
    }
    bthread::butex_destroy(ctx.butex);
}

void RunPinnedTimeoutCase() {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedTwoWorkerTaskControl();
    bthread::TaskGroup* g = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), g);
    PrepareForCase();

    PinnedWaitCtx ctx;
    ctx.butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), ctx.butex);
    static_cast<butil::atomic<int>*>(ctx.butex)->store(0, butil::memory_order_relaxed);
    ctx.use_timeout = true;
    ctx.timeout_ms = 20;

    bthread_t tid = INVALID_BTHREAD;
    uint64_t home = 0;
    StartPinnedWaitTaskAndWaitReady(g, &ctx, &tid, &home);
    WaitJoinPinnedWaitTaskAndAssert(&ctx, tid, -1, ETIMEDOUT, home);

    bthread::butex_destroy(ctx.butex);
}

void RunPinnedInterruptCase() {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedTwoWorkerTaskControl();
    bthread::TaskGroup* g = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), g);
    PrepareForCase();

    PinnedWaitCtx ctx;
    ctx.butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), ctx.butex);
    static_cast<butil::atomic<int>*>(ctx.butex)->store(0, butil::memory_order_relaxed);

    bthread_t tid = INVALID_BTHREAD;
    uint64_t home = 0;
    StartPinnedWaitTaskAndWaitReady(g, &ctx, &tid, &home);
    bool interrupt_sent = false;
    for (int i = 0; i < 20 &&
                    ctx.done.load(std::memory_order_acquire) == 0; ++i) {
        ASSERT_EQ(0, bthread_interrupt(tid));
        interrupt_sent = true;
        if (WaitAtomicAtLeast(ctx.done, 1, 50)) {
            break;
        }
    }
    ASSERT_TRUE(interrupt_sent);

    WaitJoinPinnedWaitTaskAndAssert(&ctx, tid, -1, EINTR, home);

    bthread::butex_destroy(ctx.butex);
}

void RunButexWaitLocalImmediateEwouldblockCase() {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedTwoWorkerTaskControl();
    bthread::TaskGroup* g = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), g);
    PrepareForCase();

    PinnedWaitCtx ctx;
    ctx.butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), ctx.butex);
    static_cast<butil::atomic<int>*>(ctx.butex)->store(1, butil::memory_order_relaxed);

    bthread_t tid = INVALID_BTHREAD;
    uint64_t home = 0;
    StartPinnedWaitTaskAndWaitReady(g, &ctx, &tid, &home);
    WaitJoinPinnedWaitTaskAndAssert(&ctx, tid, -1, EWOULDBLOCK, home);

    bthread::butex_destroy(ctx.butex);
}

void RunPinnedTimeoutThenWithinWakeReturnsZeroCase() {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedTwoWorkerTaskControl();
    bthread::TaskGroup* g = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), g);
    PrepareForCase();

    PinnedWaitCtx ctx;
    ctx.butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), ctx.butex);
    static_cast<butil::atomic<int>*>(ctx.butex)->store(0, butil::memory_order_relaxed);
    ctx.use_timeout = true;
    ctx.timeout_ms = 20;

    bthread_t tid = INVALID_BTHREAD;
    uint64_t home = 0;
    StartPinnedWaitTaskAndWaitReady(g, &ctx, &tid, &home);
    WaitJoinPinnedWaitTaskAndAssert(&ctx, tid, -1, ETIMEDOUT, home);

    RunHookOnlyWakeCase(TEST_MODE_BUTEX_WAKE_WITHIN_NO_WAITER, ctx.butex, 0, 0);
    bthread::butex_destroy(ctx.butex);
}

void RunPinnedGenericWakeRejectedThenWithinWakeCase() {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedTwoWorkerTaskControl();
    bthread::TaskGroup* g1 = NULL;
    bthread::TaskGroup* g2 = NULL;
    ASSERT_TRUE(ChooseTwoDistinctGroups(tc, &g1, &g2));
    PrepareForCase();

    void* butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), butex);
    static_cast<butil::atomic<int>*>(butex)->store(0, butil::memory_order_relaxed);
    g_state.butex_ptr.store(reinterpret_cast<uintptr_t>(butex), std::memory_order_relaxed);
    g_state.butex_expected_waiters.store(1, std::memory_order_relaxed);

    bthread_t waiter_tid = INVALID_BTHREAD;
    StartTaskOnGroupAndFlush(g1, &waiter_tid, TestPinnedButexLocalWaitTask, NULL);
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_waiter_ready_count, 1, 5000));
    const uint64_t waiter_worker =
        g_state.butex_waiter_worker_pthread.load(std::memory_order_relaxed);
    ASSERT_NE(0u, waiter_worker);

    bool rejected = false;
    for (int i = 0; i < 50; ++i) {
        errno = 0;
        const int rc = bthread::butex_wake(butex, true);
        const int err = errno;
        if (rc == -1 && err == EINVAL) {
            rejected = true;
            break;
        }
        ASSERT_EQ(0, rc);
        usleep(1000);
    }
    ASSERT_TRUE(rejected);
    ASSERT_EQ(0, g_state.butex_waiter_done_count.load(std::memory_order_relaxed));

    g_state.target_hook_worker_pthread.store(waiter_worker, std::memory_order_relaxed);
    g_state.mode.store(TEST_MODE_BUTEX_WAKE_WITHIN, std::memory_order_release);
    tc.signal_task(2, BTHREAD_TAG_DEFAULT);
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_wake_completed, 1, 5000));
    ASSERT_EQ(1, g_state.butex_wake_rc.load(std::memory_order_relaxed));
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_waiter_done_count, 1, 5000));
    ASSERT_EQ(1, g_state.butex_waiter_resume_count.load(std::memory_order_relaxed));
    ASSERT_EQ(waiter_worker,
              g_state.butex_waiter_resume_worker_pthread.load(std::memory_order_relaxed));

    ASSERT_EQ(0, bthread_join(waiter_tid, NULL));
    g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
    g_state.target_hook_worker_pthread.store(0, std::memory_order_relaxed);
    QuiesceHookActionsAfterModeIdle();
    g_state.butex_ptr.store(0, std::memory_order_relaxed);
    bthread::butex_destroy(butex);
}

void RunPinnedInterruptThenWithinWakeReturnsZeroCase() {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedTwoWorkerTaskControl();
    bthread::TaskGroup* g = tc.choose_one_group(BTHREAD_TAG_DEFAULT);
    ASSERT_NE(static_cast<bthread::TaskGroup*>(NULL), g);
    PrepareForCase();

    PinnedWaitCtx ctx;
    ctx.butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), ctx.butex);
    static_cast<butil::atomic<int>*>(ctx.butex)->store(0, butil::memory_order_relaxed);

    bthread_t tid = INVALID_BTHREAD;
    uint64_t home = 0;
    StartPinnedWaitTaskAndWaitReady(g, &ctx, &tid, &home);
    bool interrupt_sent = false;
    for (int i = 0; i < 20 && ctx.done.load(std::memory_order_acquire) == 0; ++i) {
        ASSERT_EQ(0, bthread_interrupt(tid));
        interrupt_sent = true;
        if (WaitAtomicAtLeast(ctx.done, 1, 50)) {
            break;
        }
    }
    ASSERT_TRUE(interrupt_sent);
    WaitJoinPinnedWaitTaskAndAssert(&ctx, tid, -1, EINTR, home);

    RunHookOnlyWakeCase(TEST_MODE_BUTEX_WAKE_WITHIN_NO_WAITER, ctx.butex, 0, 0);
    bthread::butex_destroy(ctx.butex);
}

void RunPinnedWithinWakeNoStealCase() {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
    bthread::TaskControl& tc = GetSharedTwoWorkerTaskControl();
    PrepareForCase();

    bthread::TaskGroup* group_a = NULL;
    bthread::TaskGroup* group_b = NULL;
    ASSERT_TRUE(ChooseTwoDistinctGroups(tc, &group_a, &group_b));

    void* butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), butex);
    static_cast<butil::atomic<int>*>(butex)->store(0, butil::memory_order_relaxed);
    g_state.butex_ptr.store(reinterpret_cast<uintptr_t>(butex),
                            std::memory_order_relaxed);
    g_state.butex_expected_waiters.store(1, std::memory_order_relaxed);

    bthread_t waiter_tid = INVALID_BTHREAD;
    StartTaskOnGroupAndFlush(group_a, &waiter_tid, TestPinnedButexLocalWaitTask, NULL);
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_waiter_ready_count, 1, 5000));
    const uint64_t waiter_worker =
        g_state.butex_waiter_worker_pthread.load(std::memory_order_relaxed);
    ASSERT_NE(0u, waiter_worker);
    g_state.target_hook_worker_pthread.store(waiter_worker, std::memory_order_relaxed);
    g_state.mode.store(TEST_MODE_BUTEX_WAKE_WITHIN, std::memory_order_release);
    tc.signal_task(2, BTHREAD_TAG_DEFAULT);
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_wake_completed, 1, 5000));
    ASSERT_EQ(1, g_state.butex_wake_rc.load(std::memory_order_relaxed));
    ASSERT_TRUE(WaitAtomicAtLeast(g_state.butex_waiter_done_count, 1, 5000));
    ASSERT_EQ(1, g_state.butex_waiter_resume_count.load(std::memory_order_relaxed));
    ASSERT_EQ(waiter_worker,
              g_state.butex_waiter_resume_worker_pthread.load(
                  std::memory_order_relaxed));

    ASSERT_EQ(0, bthread_join(waiter_tid, NULL));
    g_state.mode.store(TEST_MODE_IDLE, std::memory_order_release);
    g_state.target_hook_worker_pthread.store(0, std::memory_order_relaxed);
    QuiesceHookActionsAfterModeIdle();
    g_state.butex_ptr.store(0, std::memory_order_relaxed);
    bthread::butex_destroy(butex);
}

void RunPinnedWaiterNotStealableStressCase(int rounds) {
    ASSERT_GT(rounds, 0);
    for (int i = 0; i < rounds; ++i) {
        RunPinnedWithinWakeNoStealCase();
    }
}

class ActiveTaskTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        const char* child_mode = getenv("BRPC_ACTIVE_TASK_UT_CHILD_MODE");
        int expected = 0;
        if (g_register_once.compare_exchange_strong(expected, 1,
                                                    std::memory_order_relaxed)) {
            g_register_rc.store(RegisterTestActiveTaskType(), std::memory_order_relaxed);
        }
        if (child_mode != NULL) {
            if (strcmp(child_mode, "register_after_init") == 0) {
                _exit(ChildCheckRegisterAfterInitRejected());
            }
            if (strcmp(child_mode, "local_worker_init_destroy_and_idle_wait_interval") == 0) {
                _exit(ChildCheckLocalWorkerInitDestroyAndIdleWaitInterval());
            }
            if (strcmp(child_mode, "wake_within_eagain_when_pinned_rq_full") == 0) {
                _exit(ChildCheckWakeWithinEagainWhenPinnedRqFull());
            }
            _exit(100);
        }
    }
};

::testing::Environment* const g_active_task_env =
    ::testing::AddGlobalTestEnvironment(new ActiveTaskTestEnvironment);

}  // namespace

TEST(BthreadActiveTaskTest, register_before_init_succeeds) {
    ASSERT_EQ(0, g_register_rc.load(std::memory_order_relaxed));
}

TEST(BthreadActiveTaskTest, butex_wake_within_outside_hook_is_rejected) {
    void* butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), butex);
    bthread_active_task_ctx_t fake_ctx;
    memset(&fake_ctx, 0, sizeof(fake_ctx));
    fake_ctx.struct_size = sizeof(fake_ctx);
    errno = 0;
    ASSERT_EQ(-1, bthread_butex_wake_within(&fake_ctx, butex));
    ASSERT_EQ(EPERM, errno);
    bthread::butex_destroy(butex);
}

TEST(BthreadActiveTaskTest, butex_wake_within_local_wakes_waiter) {
    RunButexWakeWithinCase(1, 1, 0);
}

TEST(BthreadActiveTaskTest, butex_wake_within_null_butex_is_rejected_in_hook) {
    RunHookOnlyWakeCase(TEST_MODE_BUTEX_WAKE_WITHIN_NULL, NULL, -1, EINVAL);
}

TEST(BthreadActiveTaskTest, butex_wake_within_no_waiter_returns_zero_in_hook) {
    void* butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), butex);
    static_cast<butil::atomic<int>*>(butex)->store(0, butil::memory_order_relaxed);
    RunHookOnlyWakeCase(TEST_MODE_BUTEX_WAKE_WITHIN_NO_WAITER, butex, 0, 0);
    bthread::butex_destroy(butex);
}

TEST(BthreadActiveTaskTest, butex_wake_within_returns_eagain_when_pinned_rq_full) {
    ASSERT_EQ(0, RunChildMode("wake_within_eagain_when_pinned_rq_full"));
}

TEST(BthreadActiveTaskTest, butex_wake_within_multiple_waiters_rejected) {
    RunButexWakeWithinCase(2, -1, EINVAL);
}

TEST(BthreadActiveTaskTest, butex_wake_within_pthread_waiter_rejected) {
    RunPthreadWaiterRejectedCase();
}

TEST(BthreadActiveTaskTest, busy_periodic_poll_can_wake_waiter_without_parking) {
    RunBusyPeriodicPollWakeCase();
}

TEST(BthreadActiveTaskTest, request_flow_private_butex_is_passed_and_woken_locally) {
    RunRequestFlowCase(false);
}

TEST(BthreadActiveTaskTest, request_flow_busy_periodic_poll_wakes_waiter) {
    RunRequestFlowCase(true);
}

TEST(BthreadActiveTaskTest, local_worker_init_destroy_and_idle_wait_interval) {
    ASSERT_EQ(0, RunChildMode("local_worker_init_destroy_and_idle_wait_interval"));
}

TEST(BthreadActiveTaskTest, register_after_global_init_is_rejected) {
    ASSERT_EQ(0, RunChildMode("register_after_init"));
}

TEST(BthreadActiveTaskTest, stress_single_waiter_local_wake) {
    for (int i = 0; i < 50; ++i) {
        RunButexWakeWithinCase(1, 1, 0);
    }
}

TEST(BthreadActiveTaskTest, stress_multiple_waiters_rejected) {
    for (int i = 0; i < 10; ++i) {
        RunButexWakeWithinCase(2, -1, EINVAL);
    }
}

TEST(BthreadActiveTaskTest, strict_same_worker_rejects_cross_worker_wake) {
    RunStrictCrossWorkerRejectCase();
}

TEST(BthreadActiveTaskTest, butex_wait_local_outside_bthread_rejected) {
    void* butex = bthread::butex_create();
    ASSERT_NE(static_cast<void*>(NULL), butex);
    static_cast<butil::atomic<int>*>(butex)->store(0, butil::memory_order_relaxed);
    errno = 0;
    ASSERT_EQ(-1, bthread_butex_wait_local(butex, 0, NULL));
    ASSERT_EQ(EPERM, errno);
    bthread::butex_destroy(butex);
}

TEST(BthreadActiveTaskTest, butex_wait_local_immediate_ewouldblock_uses_implicit_wait_scope_pin) {
    RunButexWaitLocalImmediateEwouldblockCase();
}

TEST(BthreadActiveTaskTest, pinned_waiter_active_task_within_resumes_on_home_worker) {
    RunPinnedWithinWakeNoStealCase();
}

TEST(BthreadActiveTaskTest, pinned_waiter_active_task_within_not_stealable_stress) {
    RunPinnedWaiterNotStealableStressCase(20);
}

TEST(BthreadActiveTaskTest, pinned_waiter_timeout_resumes_on_home_worker) {
    RunPinnedTimeoutCase();
}

TEST(BthreadActiveTaskTest, pinned_waiter_interrupt_resumes_on_home_worker) {
    RunPinnedInterruptCase();
}

TEST(BthreadActiveTaskTest, generic_butex_wake_on_pinned_waiter_is_rejected) {
    RunPinnedGenericWakeRejectedCase(GENERIC_WAKE);
}

TEST(BthreadActiveTaskTest, generic_butex_wake_n_on_pinned_waiter_is_rejected) {
    RunPinnedGenericWakeRejectedCase(GENERIC_WAKE_N);
}

TEST(BthreadActiveTaskTest, generic_butex_wake_except_on_pinned_waiter_is_rejected) {
    RunPinnedGenericWakeRejectedCase(GENERIC_WAKE_EXCEPT);
}

TEST(BthreadActiveTaskTest, generic_butex_requeue_on_pinned_waiter_is_rejected) {
    RunPinnedGenericWakeRejectedCase(GENERIC_REQUEUE);
}

TEST(BthreadActiveTaskTest, wake_within_returns_zero_after_timeout_competition) {
    RunPinnedTimeoutThenWithinWakeReturnsZeroCase();
}

TEST(BthreadActiveTaskTest, wake_within_returns_zero_after_interrupt_competition) {
    RunPinnedInterruptThenWithinWakeReturnsZeroCase();
}

TEST(BthreadActiveTaskTest, within_wake_succeeds_after_generic_wake_rejected) {
    RunPinnedGenericWakeRejectedThenWithinWakeCase();
}
