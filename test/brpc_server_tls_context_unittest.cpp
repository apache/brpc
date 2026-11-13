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

#include <errno.h>
#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>
#include "bthread/bthread.h"
#include "bthread/countdown_event.h"
#include "butil/time.h"
#include "brpc/details/server_tls_context.h"

namespace {
using brpc::details::ServerTLSContext;
using Context = ServerTLSContext;
using Lease = Context::Lease;

static_assert(!std::is_copy_constructible<Lease>::value,
              "A lease must have exactly one owner");
static_assert(!std::is_copy_assignable<Lease>::value,
              "A lease must not duplicate its count");

// Start two competing operations together; neither depends on a sleep or on
// one particular scheduler outcome. Joining keeps captured state alive.
void RunTogether(const std::function<void()>& first,
                 const std::function<void()>& second) {
    std::mutex mutex;
    std::condition_variable condition;
    int ready = 0;
    bool start = false;
    auto run = [&](const std::function<void()>& operation) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            ++ready;
            condition.notify_all();
            condition.wait(lock, [&] { return start; });
        }
        operation();
    };
    std::thread a(run, std::cref(first));
    std::thread b(run, std::cref(second));
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return ready == 2; });
        start = true;
        condition.notify_all();
    }
    a.join();
    b.join();
}

TEST(ServerTLSContextTest, empty_generation_closes_without_a_release) {
    for (bool commit : {false, true}) {
        scoped_refptr<Context> context = new Context;
        EXPECT_EQ(Context::Phase::INITIALIZING, context->GetSnapshot().phase);
        if (commit) {
            ASSERT_EQ(0, context->CommitStartup());
        }
        context->BeginDrain();
        EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
        EXPECT_EQ(commit, context->GetSnapshot().startup_committed);
        const timespec deadline = butil::seconds_from_now(5);
        ASSERT_EQ(0, context->WaitSealed(&deadline));
        ASSERT_EQ(0, context->Retire());
        EXPECT_EQ(0, context->Retire());
        context->BeginDrain();
        EXPECT_EQ(Context::Phase::RETIRED, context->GetSnapshot().phase);
        EXPECT_EQ(ECANCELED, context->CommitStartup());
        Lease rejected;
        EXPECT_EQ(ECANCELED,
                  context->AdmitIndependent(Context::LeaseKind::TASK, &rejected));
    }
}

TEST(ServerTLSContextTest, startup_waits_for_tasks_but_can_retain_producers) {
    scoped_refptr<Context> context = new Context;
    Lease task;
    Lease producer;
    Lease rejected;
    EXPECT_EQ(ECANCELED, context->AdmitIndependent(Context::LeaseKind::TASK, &rejected));
    ASSERT_EQ(0, context->AdmitBootstrap(Context::LeaseKind::TASK, &task));
    ASSERT_EQ(0, context->AdmitBootstrap(Context::LeaseKind::PRODUCER, &producer));
    EXPECT_EQ(EINVAL, context->AdmitBootstrap(Context::LeaseKind::REQUEST, &rejected));
    EXPECT_EQ(ECANCELED, task.Derive(Context::LeaseKind::TASK, &rejected));
    EXPECT_EQ(EBUSY, context->CommitStartup());
    task.Reset();
    ASSERT_EQ(0, context->CommitStartup());
    EXPECT_EQ(EALREADY, context->CommitStartup());
    EXPECT_EQ(ECANCELED, context->AdmitBootstrap(Context::LeaseKind::TASK, &rejected));
    EXPECT_EQ(1u, context->GetSnapshot().lease_count);
    producer.Reset();
    EXPECT_EQ(Context::Phase::OPEN, context->GetSnapshot().phase);
    EXPECT_EQ(0u, context->GetSnapshot().lease_count);
}

TEST(ServerTLSContextTest, zero_bootstrap_users_does_not_commit_or_seal) {
    scoped_refptr<Context> context = new Context;
    Lease task;
    ASSERT_EQ(0, context->AdmitBootstrap(Context::LeaseKind::TASK, &task));
    task.Reset();
    EXPECT_EQ(Context::Phase::INITIALIZING, context->GetSnapshot().phase);
    EXPECT_FALSE(context->GetSnapshot().startup_committed);
    EXPECT_EQ(0, context->AdmitBootstrap(Context::LeaseKind::TASK, &task));
}

TEST(ServerTLSContextTest, startup_abort_revokes_bootstrap_derivation) {
    scoped_refptr<Context> context = new Context;
    Lease task;
    Lease producer;
    ASSERT_EQ(0, context->AdmitBootstrap(Context::LeaseKind::TASK, &task));
    ASSERT_EQ(0, context->AdmitBootstrap(Context::LeaseKind::PRODUCER, &producer));
    context->BeginDrain();
    Lease rejected;
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    EXPECT_FALSE(context->GetSnapshot().startup_committed);
    EXPECT_EQ(ECANCELED, task.Derive(Context::LeaseKind::TASK, &rejected));
    EXPECT_EQ(ECANCELED, producer.Derive(Context::LeaseKind::TASK, &rejected));
    EXPECT_EQ(ECANCELED, context->AdmitBootstrap(Context::LeaseKind::TASK, &rejected));
    EXPECT_EQ(ECANCELED, context->CommitStartup());
    task.Reset();
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    producer.Reset();
    EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
}

TEST(ServerTLSContextTest, drain_blocks_roots_but_keeps_existing_work_live) {
    scoped_refptr<Context> context = new Context;
    ASSERT_EQ(0, context->CommitStartup());
    Lease parent;
    ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::TASK, &parent));
    context->BeginDrain();
    context->BeginDrain();
    EXPECT_EQ(EBUSY, context->Retire());
    for (Context::LeaseKind kind : {Context::LeaseKind::TASK, Context::LeaseKind::PRODUCER,
                                    Context::LeaseKind::REQUEST}) {
        Lease rejected;
        EXPECT_EQ(ECANCELED, context->AdmitIndependent(kind, &rejected));
        EXPECT_FALSE(rejected.valid());
    }
    Lease child;
    ASSERT_EQ(0, parent.Derive(Context::LeaseKind::TASK, &child));
    parent.Reset();
    EXPECT_EQ(1u, context->GetSnapshot().lease_count);
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    child.Reset();
    EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
}

TEST(ServerTLSContextTest, connection_cannot_admit_fresh_request_during_drain) {
    scoped_refptr<Context> context = new Context;
    ASSERT_EQ(0, context->CommitStartup());
    Lease producer;
    ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::PRODUCER, &producer));
    context->BeginDrain();
    Lease request;
    EXPECT_EQ(ECANCELED, context->AdmitIndependent(Context::LeaseKind::REQUEST, &request));
    EXPECT_NE(0, producer.Derive(Context::LeaseKind::REQUEST, &request));
    Lease progress;
    ASSERT_EQ(0, producer.Derive(Context::LeaseKind::TASK, &progress));
    producer.Reset();
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    progress.Reset();
    EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
}

TEST(ServerTLSContextTest, async_request_can_resume_after_original_task_exits) {
    scoped_refptr<Context> context = new Context;
    ASSERT_EQ(0, context->CommitStartup());
    Lease request;
    Lease task;
    ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::REQUEST, &request));
    ASSERT_EQ(0, request.Derive(Context::LeaseKind::TASK, &task));
    task.Reset();
    context->BeginDrain();
    Lease continuation;
    ASSERT_EQ(0, request.Derive(Context::LeaseKind::TASK, &continuation));
    Lease rejected;
    EXPECT_NE(0, request.Derive(Context::LeaseKind::PRODUCER, &rejected));
    EXPECT_NE(0, request.Derive(Context::LeaseKind::REQUEST, &rejected));
    request.Reset();
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    continuation.Reset();
    EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
}

TEST(ServerTLSContextTest, failed_creation_reservation_releases_on_scope_exit) {
    scoped_refptr<Context> context = new Context;
    ASSERT_EQ(0, context->CommitStartup());
    {
        Lease unpublished;
        ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::TASK, &unpublished));
        context->BeginDrain();
        EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
        // Simulate a failure before task publication: the reservation owns
        // rollback even though no task and no KeyTable was ever constructed.
    }
    EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
    EXPECT_EQ(0u, context->GetSnapshot().lease_count);
}

TEST(ServerTLSContextTest, moving_a_lease_transfers_exactly_one_reservation) {
    scoped_refptr<Context> first = new Context;
    scoped_refptr<Context> second = new Context;
    ASSERT_EQ(0, first->CommitStartup());
    ASSERT_EQ(0, second->CommitStartup());
    Lease a;
    Lease b;
    ASSERT_EQ(0, first->AdmitIndependent(Context::LeaseKind::TASK, &a));
    ASSERT_EQ(0, second->AdmitIndependent(Context::LeaseKind::TASK, &b));
    first->BeginDrain();
    second->BeginDrain();
    Lease moved(std::move(a));
    EXPECT_FALSE(a.valid());
    EXPECT_TRUE(moved.valid());
    b = std::move(moved);
    EXPECT_FALSE(moved.valid());
    EXPECT_EQ(Context::Phase::SEALED, second->GetSnapshot().phase);
    EXPECT_EQ(1u, first->GetSnapshot().lease_count);
    b.Reset();
    b.Reset();
    EXPECT_EQ(Context::Phase::SEALED, first->GetSnapshot().phase);
}

TEST(ServerTLSContextTest, lease_keeps_context_alive_after_owner_pin_is_dropped) {
    scoped_refptr<Context> context = new Context;
    ASSERT_EQ(0, context->CommitStartup());
    Lease parent;
    ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::TASK, &parent));
    context->BeginDrain();
    context = nullptr;
    Lease child;
    ASSERT_EQ(0, parent.Derive(Context::LeaseKind::TASK, &child));
    parent.Reset();
    EXPECT_TRUE(child.valid());
    child.Reset();
}

TEST(ServerTLSContextTest, invalid_output_does_not_add_or_replace_a_lease) {
    scoped_refptr<Context> context = new Context;
    ASSERT_EQ(0, context->CommitStartup());
    Lease held;
    ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::TASK, &held));
    EXPECT_EQ(EINVAL, context->AdmitIndependent(Context::LeaseKind::TASK, nullptr));
    EXPECT_EQ(EINVAL, context->AdmitIndependent(Context::LeaseKind::TASK, &held));
    EXPECT_EQ(EINVAL, held.Derive(Context::LeaseKind::TASK, &held));
    EXPECT_EQ(EINVAL, held.Derive(Context::LeaseKind::TASK, nullptr));
    EXPECT_EQ(1u, context->GetSnapshot().lease_count);
    Lease empty;
    EXPECT_NE(0, empty.Derive(Context::LeaseKind::TASK, &empty));
}

TEST(ServerTLSContextTest, saturated_count_rejects_without_publishing_or_wrapping) {
    for (bool commit : {false, true}) {
        scoped_refptr<Context> context = new Context;
        Lease owner;
        if (commit) {
            ASSERT_EQ(0, context->CommitStartup());
            ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::TASK, &owner));
        } else {
            ASSERT_EQ(0, context->AdmitBootstrap(Context::LeaseKind::PRODUCER, &owner));
        }
        // Inject an unreachable-in-practice boundary using the test build's
        // existing -fno-access-control, without adding a production test knob.
        // No other thread accesses this context during the injection.
        context->_lease_count = std::numeric_limits<uint64_t>::max();
        Lease rejected;
        const int result = commit
            ? context->AdmitIndependent(Context::LeaseKind::TASK, &rejected)
            : context->AdmitBootstrap(Context::LeaseKind::TASK, &rejected);
        EXPECT_EQ(EOVERFLOW, result);
        if (commit) {
            EXPECT_EQ(EOVERFLOW, owner.Derive(Context::LeaseKind::TASK, &rejected));
        }
        EXPECT_FALSE(rejected.valid());
        EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
                  context->GetSnapshot().lease_count);
        context->_lease_count = 1;
        owner.Reset();
        if (!commit) {
            // A failed bootstrap admission must not leave an init-task count.
            EXPECT_EQ(0, context->CommitStartup());
        }
        context->BeginDrain();
        EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
    }
}

TEST(ServerTLSContextTest, independent_admission_races_stop_without_escape) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        scoped_refptr<Context> context = new Context;
        ASSERT_EQ(0, context->CommitStartup());
        Lease admitted;
        int result = -1;
        RunTogether([&] {
            result = context->AdmitIndependent(Context::LeaseKind::REQUEST, &admitted);
        }, [&] { context->BeginDrain(); });
        ASSERT_TRUE(result == 0 || result == ECANCELED);
        if (result == 0) {
            EXPECT_TRUE(admitted.valid());
            EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
            EXPECT_EQ(1u, context->GetSnapshot().lease_count);
        } else {
            EXPECT_FALSE(admitted.valid());
            EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
            EXPECT_EQ(0u, context->GetSnapshot().lease_count);
        }
        admitted.Reset();
        EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
    }
}

TEST(ServerTLSContextTest, request_continuation_races_stop_and_ownership_handoff) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        scoped_refptr<Context> context = new Context;
        ASSERT_EQ(0, context->CommitStartup());
        Lease request;
        ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::REQUEST, &request));
        Lease child;
        int result = -1;
        // Only this lane owns request. Its Derive and Reset must never race
        // each other; Stop can race the handoff to its new continuation.
        RunTogether([&] {
            result = request.Derive(Context::LeaseKind::TASK, &child);
            request.Reset();
        }, [&] { context->BeginDrain(); });
        ASSERT_EQ(0, result);
        EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
        EXPECT_EQ(1u, context->GetSnapshot().lease_count);
        child.Reset();
        EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
    }
}

TEST(ServerTLSContextTest, final_release_races_waiter_without_missing_wakeup) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        scoped_refptr<Context> context = new Context;
        ASSERT_EQ(0, context->CommitStartup());
        Lease task;
        ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::TASK, &task));
        context->BeginDrain();
        int result = -1;
        RunTogether([&] {
            const timespec deadline = butil::seconds_from_now(5);
            result = context->WaitSealed(&deadline);
        }, [&] { task.Reset(); });
        EXPECT_EQ(0, result);
        EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
    }
}

TEST(ServerTLSContextTest,
     final_release_wake_outlives_external_owner_and_returning_waiter) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        scoped_refptr<Context> owner = new Context;
        ASSERT_EQ(0, owner->CommitStartup());
        Lease last_task;
        ASSERT_EQ(0, owner->AdmitIndependent(Context::LeaseKind::TASK, &last_task));
        owner->BeginDrain();

        // After dropping owner, only the waiter and the final lease keep the
        // context alive. The waiter releases its pin immediately after it has
        // captured the successful final state; the lease release path must
        // retain its own pin through publishing SEALED and the complete wake.
        scoped_refptr<Context> waiter_pin = owner;
        owner = nullptr;
        int wait_result = -1;
        Context::Phase observed_phase = Context::Phase::INITIALIZING;
        RunTogether([&] {
            const timespec deadline = butil::seconds_from_now(5);
            wait_result = waiter_pin->WaitSealed(&deadline);
            if (wait_result == 0) {
                observed_phase = waiter_pin->GetSnapshot().phase;
            }
            waiter_pin = nullptr;
        }, [&] { last_task.Reset(); });

        EXPECT_EQ(0, wait_result);
        EXPECT_EQ(Context::Phase::SEALED, observed_phase);
        EXPECT_FALSE(waiter_pin);
        EXPECT_FALSE(last_task.valid());
    }

    // This is combined lifetime/race coverage. It does not deterministically
    // pause inside butex_wake and does not by itself prove wake internals.
}

struct BthreadWaiter {
    scoped_refptr<Context> context;
    bthread::CountdownEvent entered;
    int result = -1;
    static void* Run(void* arg) {
        auto* self = static_cast<BthreadWaiter*>(arg);
        self->entered.signal();
        const timespec deadline = butil::seconds_from_now(5);
        self->result = self->context->WaitSealed(&deadline);
        return nullptr;
    }
};

TEST(ServerTLSContextTest, bthread_waits_for_last_task_and_producer) {
    scoped_refptr<Context> context = new Context;
    ASSERT_EQ(0, context->CommitStartup());
    Lease task;
    Lease producer;
    ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::TASK, &task));
    ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::PRODUCER, &producer));
    context->BeginDrain();
    BthreadWaiter waiter;
    waiter.context = context;
    bthread_t tid;
    ASSERT_EQ(0, bthread_start_background(&tid, nullptr, BthreadWaiter::Run,
                                         &waiter));
    EXPECT_EQ(0, waiter.entered.timed_wait(butil::seconds_from_now(5)));
    task.Reset();
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    producer.Reset();
    EXPECT_EQ(0, bthread_join(tid, nullptr));
    EXPECT_EQ(0, waiter.result);
}

TEST(ServerTLSContextTest, diagnostic_timeout_never_retires_or_closes_a_user) {
    scoped_refptr<Context> context = new Context;
    ASSERT_EQ(0, context->CommitStartup());
    Lease task;
    ASSERT_EQ(0, context->AdmitIndependent(Context::LeaseKind::TASK, &task));
    context->BeginDrain();
    const timespec expired = butil::seconds_from_now(-1);
    EXPECT_EQ(ETIMEDOUT, context->WaitSealed(&expired));
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    EXPECT_EQ(1u, context->GetSnapshot().lease_count);
    EXPECT_TRUE(task.valid());
    task.Reset();
    EXPECT_EQ(0, context->WaitSealed(&expired));
}

TEST(ServerTLSContextTest, retired_generation_never_reopens_on_restart) {
    scoped_refptr<Context> old_generation = new Context;
    ASSERT_EQ(0, old_generation->CommitStartup());
    old_generation->BeginDrain();
    ASSERT_EQ(0, old_generation->Retire());
    scoped_refptr<Context> fresh_generation = new Context;
    ASSERT_EQ(0, fresh_generation->CommitStartup());
    Lease fresh;
    Lease stale;
    EXPECT_EQ(ECANCELED,
              old_generation->AdmitIndependent(Context::LeaseKind::TASK, &stale));
    EXPECT_EQ(0, fresh_generation->AdmitIndependent(Context::LeaseKind::TASK, &fresh));
    EXPECT_EQ(Context::Phase::RETIRED, old_generation->GetSnapshot().phase);
    EXPECT_EQ(Context::Phase::OPEN, fresh_generation->GetSnapshot().phase);
}
}  // namespace
