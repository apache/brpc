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
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <vector>

#include "bthread/bthread.h"
#include "bthread/countdown_event.h"
#include "bthread/unstable.h"
#include "butil/atomicops.h"
#include "butil/memory/scope_guard.h"
#include "butil/time.h"
#include "brpc/details/server_tls_context.h"
#include "brpc/details/server_tls_task_binding.h"

DECLARE_int32(task_group_ntags);

namespace {
using brpc::details::ServerTLSContext;
using brpc::details::RegisterServerTLSTaskPool;
using brpc::details::RetireServerTLSTaskPool;
using Context = ServerTLSContext;

struct BlockingTask {
    Context* context = nullptr;
    bthread_key_t key = INVALID_BTHREAD_KEY;
    bool use_tls = false;
    bool exit_explicitly = false;
    bool function_released = false;
    bool destructor_released = false;
    butil::atomic<int> destroyed{0};
    bthread::CountdownEvent started;
    bthread::CountdownEvent finish_function;
    bthread::CountdownEvent destructor_started;
    bthread::CountdownEvent finish_destructor;

    void ReleaseFunction() {
        if (!function_released) {
            function_released = true;
            finish_function.signal();
        }
    }
    void ReleaseDestructor() {
        if (!destructor_released) {
            destructor_released = true;
            finish_destructor.signal();
        }
    }
    static void Destroy(void* arg) {
        auto* self = static_cast<BlockingTask*>(arg);
        EXPECT_EQ(Context::Phase::DRAINING, self->context->GetSnapshot().phase);
        EXPECT_GE(self->context->GetSnapshot().lease_count, 1u);
        self->destructor_started.signal();
        EXPECT_EQ(0, self->finish_destructor.timed_wait(
            butil::seconds_from_now(30)));
        self->destroyed.fetch_add(1, butil::memory_order_relaxed);
    }
    static void* Run(void* arg) {
        auto* self = static_cast<BlockingTask*>(arg);
        // The lease must already exist when the task executes, not only at exit.
        EXPECT_GE(self->context->GetSnapshot().lease_count, 1u);
        if (self->use_tls) {
            EXPECT_EQ(0, bthread_setspecific(self->key, self));
        }
        self->started.signal();
        EXPECT_EQ(0, self->finish_function.timed_wait(
            butil::seconds_from_now(30)));
        if (self->exit_explicitly) {
            bthread_exit(nullptr);
        }
        return nullptr;
    }
};

class ManagedPoolTest : public testing::Test {
protected:
    void SetUp() override {
        context = new Context;
        // Managed pool identities are retained even after retirement. Do not
        // use a stack pool or recycle this address for another generation.
        pool = new bthread_keytable_pool_t;
        ASSERT_EQ(0, bthread_keytable_pool_init(pool));
        initialized = true;
        ASSERT_EQ(0, bthread_key_create(&key, BlockingTask::Destroy));
        ASSERT_EQ(0, RegisterServerTLSTaskPool(pool, context));
        registered = true;
        ASSERT_EQ(0, context->CommitStartup());
    }
    void TearDown() override {
        context->BeginDrain();
        const timespec deadline = butil::seconds_from_now(5);
        ASSERT_EQ(0, context->WaitSealed(&deadline));
        if (initialized && !pool_destroyed) {
            EXPECT_EQ(0, bthread_keytable_pool_destroy(pool));
        }
        if (key != INVALID_BTHREAD_KEY) {
            EXPECT_EQ(0, bthread_key_delete(key));
        }
        EXPECT_EQ(0, context->Retire());
        if (registered) {
            EXPECT_EQ(0, RetireServerTLSTaskPool(pool));
        }
        // Keep the header and its rwlock valid for stale copied attributes.
    }
    bthread_attr_t Attr(bool pthread_stack = false, int tag = 0) const {
        bthread_attr_t attr = pthread_stack ? BTHREAD_ATTR_PTHREAD
                                           : BTHREAD_ATTR_NORMAL;
        attr.keytable_pool = pool;
        attr.tag = tag;
        return attr;
    }
    scoped_refptr<Context> context;
    bthread_keytable_pool_t* pool = nullptr;
    bthread_key_t key = INVALID_BTHREAD_KEY;
    bool initialized = false;
    bool pool_destroyed = false;
    bool registered = false;
};

int Start(bool urgent, bthread_t* tid, const bthread_attr_t* attr,
          void* (*fn)(void*), void* arg) {
    return urgent ? bthread_start_urgent(tid, attr, fn, arg)
                  : bthread_start_background(tid, attr, fn, arg);
}

struct Options {
    int caller;  // 0: native pthread; 1: same-tag bthread; 2: cross-tag bthread.
    bool urgent;
    bool pthread_stack;
};

struct Launcher {
    Launcher(Options options_in, bthread_attr_t attr_in, void* data_in,
             void* (*fn_in)(void*) = BlockingTask::Run)
        : options(options_in), attr(attr_in), data(data_in), fn(fn_in) {}
    Options options;
    bthread_attr_t attr;
    void* data;
    void* (*fn)(void*);
    bthread_t child = INVALID_BTHREAD;
    int result = -1;
    bthread::CountdownEvent completed;
    static void* Run(void* arg) {
        auto* self = static_cast<Launcher*>(arg);
        self->result = Start(self->options.urgent, &self->child, &self->attr,
                             self->fn, self->data);
        self->completed.signal();
        return nullptr;
    }
};

class ManagedTaskTest : public ManagedPoolTest,
                        public testing::WithParamInterface<Options> {
protected:
    void CheckExitBoundary(bool use_tls, bool exit_explicitly = false) {
        const Options options = GetParam();
        BlockingTask data;
        data.context = context.get();
        data.key = key;
        data.use_tls = use_tls;
        data.exit_explicitly = exit_explicitly;
        Launcher launcher{options,
                          Attr(options.pthread_stack, options.caller == 2 ? 1 : 0),
                          &data};
        bthread_t driver = INVALID_BTHREAD;
        auto cleanup = butil::MakeScopeGuard([&] {
            data.ReleaseFunction();
            data.ReleaseDestructor();
            // Join driver before reading its output even on a fatal assertion.
            if (driver != INVALID_BTHREAD) {
                EXPECT_EQ(0, bthread_join(driver, nullptr));
            }
            if (launcher.child != INVALID_BTHREAD) {
                EXPECT_EQ(0, bthread_join(launcher.child, nullptr));
            }
        });
        if (options.caller == 0) {
            Launcher::Run(&launcher);
        } else {
            bthread_attr_t driver_attr = BTHREAD_ATTR_NORMAL;
            driver_attr.tag = 0;
            ASSERT_EQ(0, bthread_start_background(
                &driver, &driver_attr, Launcher::Run, &launcher));
        }
        ASSERT_EQ(0, launcher.completed.timed_wait(butil::seconds_from_now(5)));
        ASSERT_EQ(0, launcher.result);
        ASSERT_EQ(0, data.started.timed_wait(butil::seconds_from_now(5)));
        EXPECT_EQ(1u, context->GetSnapshot().lease_count);
        context->BeginDrain();
        if (use_tls) {
            // Force task exit to destroy its held table instead of caching it.
            // The key and destructor state remain alive until the task is joined.
            ASSERT_EQ(0, bthread_keytable_pool_destroy(pool));
            pool_destroyed = true;
        }
        EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
        data.ReleaseFunction();
        if (use_tls) {
            ASSERT_EQ(0, data.destructor_started.timed_wait(
                butil::seconds_from_now(5)));
            EXPECT_EQ(1u, context->GetSnapshot().lease_count);
            EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
            bthread_attr_t running;
            EXPECT_EQ(0, bthread_getattr(launcher.child, &running));
            EXPECT_EQ(0, data.destroyed.load(butil::memory_order_relaxed));
            data.ReleaseDestructor();
        }
        ASSERT_EQ(0, bthread_join(launcher.child, nullptr));
        const timespec deadline = butil::seconds_from_now(5);
        ASSERT_EQ(0, context->WaitSealed(&deadline));
        EXPECT_EQ(0u, context->GetSnapshot().lease_count);
        EXPECT_EQ(use_tls ? 1 : 0,
                  data.destroyed.load(butil::memory_order_relaxed));
    }
};

TEST_P(ManagedTaskTest, drain_waits_for_blocked_tls_destructor) {
    CheckExitBoundary(true);
}

TEST_P(ManagedTaskTest, task_without_keytable_still_releases_lease) {
    CheckExitBoundary(false);
}

TEST_P(ManagedTaskTest, explicit_bthread_exit_still_waits_for_tls_destructor) {
    CheckExitBoundary(true, true);
}

struct DestructorParent {
    Context* context;
    bthread_key_t key;
    bthread_attr_t child_attr;
    bool urgent;
    BlockingTask child;
    bthread_t child_tid = INVALID_BTHREAD;
    int child_result = -1;
    butil::atomic<int> destroyed{0};
    bthread::CountdownEvent started;
    bthread::CountdownEvent finish_function;
    bthread::CountdownEvent child_created;

    static void* Run(void* arg) {
        auto* self = static_cast<DestructorParent*>(arg);
        EXPECT_EQ(0, bthread_setspecific(self->key, self));
        self->started.signal();
        EXPECT_EQ(0, self->finish_function.timed_wait(
            butil::seconds_from_now(30)));
        return nullptr;
    }
    static void Destroy(void* arg) {
        auto* self = static_cast<DestructorParent*>(arg);
        EXPECT_EQ(Context::Phase::DRAINING, self->context->GetSnapshot().phase);
        EXPECT_EQ(1u, self->context->GetSnapshot().lease_count);
        // This must derive from the exiting task while its KeyTable is being
        // destroyed. Moving or clearing the lifecycle alias early rejects it.
        self->child_result = Start(self->urgent, &self->child_tid,
                                   &self->child_attr, BlockingTask::Run,
                                   &self->child);
        self->destroyed.fetch_add(1, butil::memory_order_relaxed);
        self->child_created.signal();
    }
};

TEST_P(ManagedTaskTest, tls_destructor_child_keeps_generation_draining) {
    ASSERT_EQ(0, bthread_key_delete(key));
    key = INVALID_BTHREAD_KEY;
    ASSERT_EQ(0, bthread_key_create(&key, DestructorParent::Destroy));
    const Options options = GetParam();
    DestructorParent data;
    data.context = context.get();
    data.key = key;
    data.child_attr = Attr(options.pthread_stack);
    data.urgent = options.urgent;
    data.child.context = context.get();
    Launcher launcher(options,
                      Attr(options.pthread_stack, options.caller == 2 ? 1 : 0),
                      &data, DestructorParent::Run);
    bthread_t driver = INVALID_BTHREAD;
    bool parent_released = false;
    auto cleanup = butil::MakeScopeGuard([&] {
        // Also flush any cached value on a failed assertion while data/key
        // still exist. A callback must never be deferred to fixture teardown.
        context->BeginDrain();
        if (!pool_destroyed) {
            EXPECT_EQ(0, bthread_keytable_pool_destroy(pool));
            pool_destroyed = true;
        }
        if (!parent_released) {
            data.finish_function.signal();
        }
        data.child.ReleaseFunction();
        if (driver != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(driver, nullptr));
        }
        if (launcher.child != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(launcher.child, nullptr));
        }
        // Joining the parent publishes all destructor outputs, including on
        // failure paths where child_created was never observed.
        if (data.child_tid != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(data.child_tid, nullptr));
        }
    });
    if (options.caller == 0) {
        Launcher::Run(&launcher);
    } else {
        bthread_attr_t driver_attr = BTHREAD_ATTR_NORMAL;
        driver_attr.tag = 0;
        ASSERT_EQ(0, bthread_start_background(
            &driver, &driver_attr, Launcher::Run, &launcher));
    }
    ASSERT_EQ(0, launcher.completed.timed_wait(butil::seconds_from_now(5)));
    ASSERT_EQ(0, launcher.result);
    ASSERT_EQ(0, data.started.timed_wait(butil::seconds_from_now(5)));
    context->BeginDrain();
    ASSERT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    ASSERT_EQ(0, bthread_keytable_pool_destroy(pool));
    pool_destroyed = true;
    data.finish_function.signal();
    parent_released = true;
    ASSERT_EQ(0, data.child_created.timed_wait(butil::seconds_from_now(5)));
    ASSERT_EQ(0, data.child_result);
    ASSERT_EQ(0, data.child.started.timed_wait(butil::seconds_from_now(5)));
    ASSERT_EQ(0, bthread_join(launcher.child, nullptr));
    EXPECT_EQ(1, data.destroyed.load(butil::memory_order_relaxed));
    // join can precede the parent's final lease release, so do not assume an
    // exact count here. The blocked child must prevent sealing in either order.
    EXPECT_GE(context->GetSnapshot().lease_count, 1u);
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    const timespec expired = butil::seconds_from_now(-1);
    EXPECT_EQ(ETIMEDOUT, context->WaitSealed(&expired));
    data.child.ReleaseFunction();
    ASSERT_EQ(0, bthread_join(data.child_tid, nullptr));
    const timespec deadline = butil::seconds_from_now(5);
    ASSERT_EQ(0, context->WaitSealed(&deadline));
    EXPECT_EQ(Context::Phase::SEALED, context->GetSnapshot().phase);
    EXPECT_EQ(0u, context->GetSnapshot().lease_count);
}

std::vector<Options> AllOptions() {
    std::vector<Options> result;
    for (int caller = 0; caller != 3; ++caller) {
        for (bool urgent : {false, true}) {
            for (bool pthread_stack : {false, true}) {
                result.push_back({caller, urgent, pthread_stack});
            }
        }
    }
    return result;
}
INSTANTIATE_TEST_SUITE_P(StartRoutes, ManagedTaskTest,
                         testing::ValuesIn(AllOptions()));

void* CountRun(void* arg) {
    static_cast<butil::atomic<int>*>(arg)->fetch_add(1, butil::memory_order_relaxed);
    return nullptr;
}

TEST_F(ManagedPoolTest, closed_pool_rejects_before_tid_publication_or_execution) {
    context->BeginDrain();
    butil::atomic<int> runs{0};
    for (bool urgent : {false, true}) {
        bthread_t tid = 12345;
        const bthread_attr_t attr = Attr();
        EXPECT_EQ(ECANCELED, Start(urgent, &tid, &attr, CountRun, &runs));
        EXPECT_EQ(12345u, tid);
    }
    EXPECT_EQ(0, runs.load(butil::memory_order_relaxed));
    EXPECT_EQ(0u, context->GetSnapshot().lease_count);
}

struct IndependentRootProbe {
    Context* context;
    bthread_attr_t attr;
    butil::atomic<int> runs{0};

    static void* Run(void* arg) {
        auto* self = static_cast<IndependentRootProbe*>(arg);
        EXPECT_EQ(Context::Phase::DRAINING, self->context->GetSnapshot().phase);
        EXPECT_EQ(1u, self->context->GetSnapshot().lease_count);
        for (bool urgent : {false, true}) {
            for (bool pthread_stack : {false, true}) {
                for (int tag : {0, 1}) {
                    bthread_attr_t attr = self->attr;
                    attr.stack_type = pthread_stack ? BTHREAD_STACKTYPE_PTHREAD
                                                   : BTHREAD_STACKTYPE_NORMAL;
                    attr.tag = tag;
                    bthread_t tid = 12345;
                    const int rc = Start(urgent, &tid, &attr, CountRun, &self->runs);
                    EXPECT_EQ(ECANCELED, rc);
                    EXPECT_EQ(12345u, tid);
                    // A regression must still finish any accidentally admitted
                    // task before its argument goes out of scope.
                    if (rc == 0) {
                        EXPECT_EQ(0, bthread_join(tid, nullptr));
                    }
                    EXPECT_EQ(1u, self->context->GetSnapshot().lease_count);
                    EXPECT_EQ(Context::Phase::DRAINING, self->context->GetSnapshot().phase);
                }
            }
        }
        EXPECT_EQ(0, self->runs.load(butil::memory_order_relaxed));
        return nullptr;
    }
};

TEST_F(ManagedPoolTest, draining_with_live_task_rejects_native_independent_roots) {
    BlockingTask blocker;
    blocker.context = context.get();
    bthread_t tid = INVALID_BTHREAD;
    auto cleanup = butil::MakeScopeGuard([&] {
        blocker.ReleaseFunction();
        if (tid != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(tid, nullptr));
        }
    });
    const bthread_attr_t attr = Attr();
    ASSERT_EQ(0, bthread_start_background(&tid, &attr, BlockingTask::Run, &blocker));
    ASSERT_EQ(0, blocker.started.timed_wait(butil::seconds_from_now(5)));
    context->BeginDrain();
    ASSERT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    ASSERT_EQ(1u, context->GetSnapshot().lease_count);
    IndependentRootProbe probe;
    probe.context = context.get();
    probe.attr = Attr();
    IndependentRootProbe::Run(&probe);
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
}

TEST_F(ManagedPoolTest, draining_with_live_task_rejects_foreign_independent_roots) {
    scoped_refptr<Context> source = new Context;
    auto* source_pool = new bthread_keytable_pool_t;
    ASSERT_EQ(0, bthread_keytable_pool_init(source_pool));
    bool source_registered = false;
    BlockingTask blocker;
    blocker.context = context.get();
    bthread_t blocker_tid = INVALID_BTHREAD;
    bthread_t caller_tid = INVALID_BTHREAD;
    IndependentRootProbe probe;
    probe.context = context.get();
    probe.attr = Attr();
    auto cleanup = butil::MakeScopeGuard([&] {
        // Keep the target blocker alive throughout all probes, even if a
        // rendezvous fails and the caller has not finished yet.
        if (caller_tid != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(caller_tid, nullptr));
        }
        blocker.ReleaseFunction();
        if (blocker_tid != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(blocker_tid, nullptr));
        }
        source->BeginDrain();
        const timespec deadline = butil::seconds_from_now(5);
        EXPECT_EQ(0, source->WaitSealed(&deadline));
        EXPECT_EQ(0, bthread_keytable_pool_destroy(source_pool));
        EXPECT_EQ(0, source->Retire());
        if (source_registered) {
            EXPECT_EQ(0, RetireServerTLSTaskPool(source_pool));
        }
    });
    ASSERT_EQ(0, RegisterServerTLSTaskPool(source_pool, source));
    source_registered = true;
    ASSERT_EQ(0, source->CommitStartup());
    const bthread_attr_t attr = Attr();
    ASSERT_EQ(0, bthread_start_background(
        &blocker_tid, &attr, BlockingTask::Run, &blocker));
    ASSERT_EQ(0, blocker.started.timed_wait(butil::seconds_from_now(5)));
    context->BeginDrain();
    ASSERT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    ASSERT_EQ(1u, context->GetSnapshot().lease_count);
    bthread_attr_t source_attr = BTHREAD_ATTR_NORMAL;
    source_attr.keytable_pool = source_pool;
    source_attr.tag = 0;
    ASSERT_EQ(0, bthread_start_background(
        &caller_tid, &source_attr, IndependentRootProbe::Run, &probe));
    ASSERT_EQ(0, bthread_join(caller_tid, nullptr));
    EXPECT_EQ(0, probe.runs.load(butil::memory_order_relaxed));
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    EXPECT_EQ(1u, context->GetSnapshot().lease_count);
}

struct ParentTask {
    Context* context;
    bthread_attr_t attr;
    BlockingTask child;
    bthread_t child_tid = INVALID_BTHREAD;
    int result = -1;
    bthread::CountdownEvent started;
    bthread::CountdownEvent derive;
    bthread::CountdownEvent created;
    static void* Unmanaged(void* arg) {
        auto* self = static_cast<ParentTask*>(arg);
        EXPECT_EQ(1u, self->context->GetSnapshot().lease_count);
        bthread_usleep(1);  // Exercise saving/restoring the LocalStorage alias.
        EXPECT_EQ(1u, self->context->GetSnapshot().lease_count);
        return nullptr;
    }
    static void* Run(void* arg) {
        auto* self = static_cast<ParentTask*>(arg);
        self->started.signal();
        EXPECT_EQ(0, self->derive.timed_wait(butil::seconds_from_now(30)));
        bthread_t untracked = INVALID_BTHREAD;
        const int rc = bthread_start_urgent(&untracked, nullptr, Unmanaged, self);
        EXPECT_EQ(0, rc);
        if (rc == 0) {
            EXPECT_EQ(0, bthread_join(untracked, nullptr));
        }
        // Switch tags so the child routes through start_background<true> on
        // another TaskGroup. Its parent authority must still be this task.
        self->result = bthread_start_urgent(
            &self->child_tid, &self->attr, BlockingTask::Run, &self->child);
        self->created.signal();
        return nullptr;
    }
};

TEST_F(ManagedPoolTest, same_generation_child_survives_parent_exit_during_drain) {
    ParentTask data;
    data.context = context.get();
    data.attr = Attr(false, 1);
    data.child.context = context.get();
    bthread_t parent = INVALID_BTHREAD;
    bool derived = false;
    auto cleanup = butil::MakeScopeGuard([&] {
        if (!derived) {
            data.derive.signal();
        }
        data.child.ReleaseFunction();
        if (parent != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(parent, nullptr));
        }
        if (data.child_tid != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(data.child_tid, nullptr));
        }
    });
    const bthread_attr_t attr = Attr(false, 0);
    ASSERT_EQ(0, bthread_start_background(&parent, &attr, ParentTask::Run, &data));
    ASSERT_EQ(0, data.started.timed_wait(butil::seconds_from_now(5)));
    context->BeginDrain();
    data.derive.signal();
    derived = true;
    ASSERT_EQ(0, data.created.timed_wait(butil::seconds_from_now(5)));
    ASSERT_EQ(0, data.result);
    ASSERT_EQ(0, data.child.started.timed_wait(butil::seconds_from_now(5)));
    ASSERT_EQ(0, bthread_join(parent, nullptr));
    // join may return just before the parent's final lease release. The child
    // must keep drain open in either ordering; WaitSealed below is the barrier
    // for completion of all final releases.
    EXPECT_GE(context->GetSnapshot().lease_count, 1u);
    EXPECT_EQ(Context::Phase::DRAINING, context->GetSnapshot().phase);
    data.child.ReleaseFunction();
    ASSERT_EQ(0, bthread_join(data.child_tid, nullptr));
    const timespec deadline = butil::seconds_from_now(5);
    EXPECT_EQ(0, context->WaitSealed(&deadline));
}

TEST_F(ManagedPoolTest, retired_binding_is_a_tombstone_not_a_legacy_pool) {
    context->BeginDrain();
    ASSERT_EQ(0, context->Retire());
    ASSERT_EQ(0, RetireServerTLSTaskPool(pool));
    EXPECT_EQ(0, RetireServerTLSTaskPool(pool));
    scoped_refptr<Context> replacement = new Context;
    EXPECT_EQ(EEXIST, RegisterServerTLSTaskPool(pool, replacement));
    const bthread_attr_t attr = Attr();
    bthread_t tid = 12345;
    butil::atomic<int> runs{0};
    EXPECT_EQ(ECANCELED,
              bthread_start_background(&tid, &attr, CountRun, &runs));
    EXPECT_EQ(12345u, tid);
    EXPECT_EQ(0, runs.load(butil::memory_order_relaxed));
}

TEST_F(ManagedPoolTest, another_generations_parent_cannot_bypass_closed_admission) {
    scoped_refptr<Context> other = new Context;
    auto* other_pool = new bthread_keytable_pool_t;
    ASSERT_EQ(0, bthread_keytable_pool_init(other_pool));
    ASSERT_EQ(0, RegisterServerTLSTaskPool(other_pool, other));
    ASSERT_EQ(0, other->CommitStartup());
    other->BeginDrain();
    ParentTask data;
    data.context = context.get();
    data.attr = Attr(false, 1);
    data.attr.keytable_pool = other_pool;
    data.child.context = other.get();
    bthread_t parent = INVALID_BTHREAD;
    auto cleanup = butil::MakeScopeGuard([&] {
        data.child.ReleaseFunction();
        if (parent != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(parent, nullptr));
        }
        if (data.child_tid != INVALID_BTHREAD) {
            EXPECT_EQ(0, bthread_join(data.child_tid, nullptr));
        }
        const timespec deadline = butil::seconds_from_now(5);
        EXPECT_EQ(0, other->WaitSealed(&deadline));
        EXPECT_EQ(0, bthread_keytable_pool_destroy(other_pool));
        EXPECT_EQ(0, other->Retire());
        EXPECT_EQ(0, RetireServerTLSTaskPool(other_pool));
    });
    data.derive.signal();
    const bthread_attr_t attr = Attr(false, 0);
    ASSERT_EQ(0, bthread_start_background(&parent, &attr, ParentTask::Run, &data));
    ASSERT_EQ(0, data.created.timed_wait(butil::seconds_from_now(5)));
    EXPECT_EQ(ECANCELED, data.result);
    EXPECT_EQ(INVALID_BTHREAD, data.child_tid);
    EXPECT_EQ(0u, other->GetSnapshot().lease_count);
}

TEST_F(ManagedPoolTest, unmanaged_pool_keeps_legacy_task_creation) {
    bthread_keytable_pool_t legacy;
    ASSERT_EQ(0, bthread_keytable_pool_init(&legacy));
    auto cleanup = butil::MakeScopeGuard([&] {
        EXPECT_EQ(0, bthread_keytable_pool_destroy(&legacy));
        EXPECT_EQ(0, pthread_rwlock_destroy(&legacy.rwlock));
    });
    context->BeginDrain();
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.keytable_pool = &legacy;
    bthread_t tid;
    butil::atomic<int> runs{0};
    ASSERT_EQ(0, bthread_start_background(&tid, &attr, CountRun, &runs));
    ASSERT_EQ(0, bthread_join(tid, nullptr));
    EXPECT_EQ(1, runs.load(butil::memory_order_relaxed));
    EXPECT_EQ(0u, context->GetSnapshot().lease_count);
}

TEST(ServerTLSTaskBindingTest, initializing_public_roots_are_not_bootstrap_work) {
    scoped_refptr<Context> context = new Context;
    auto* pool = new bthread_keytable_pool_t;
    ASSERT_EQ(0, bthread_keytable_pool_init(pool));
    ASSERT_EQ(0, RegisterServerTLSTaskPool(pool, context));
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.keytable_pool = pool;
    butil::atomic<int> runs{0};
    bthread_t tid = 12345;
    EXPECT_EQ(ECANCELED, bthread_start_urgent(&tid, &attr, CountRun, &runs));
    EXPECT_EQ(12345u, tid);
    EXPECT_EQ(0, runs.load(butil::memory_order_relaxed));
    EXPECT_EQ(0u, context->GetSnapshot().lease_count);
    context->BeginDrain();
    EXPECT_EQ(0, bthread_keytable_pool_destroy(pool));
    EXPECT_EQ(0, context->Retire());
    EXPECT_EQ(0, RetireServerTLSTaskPool(pool));
}
}  // namespace

int main(int argc, char** argv) {
    // Configure both groups before the first TaskControl is created.
    FLAGS_task_group_ntags = 2;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
