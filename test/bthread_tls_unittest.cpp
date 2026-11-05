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

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

#include "bthread/context.h"
#include "bthread/task_meta.h"
#include "butil/containers/doubly_buffered_data.h"
#include "butil/debug/address_annotations.h"
#include "butil/find_cstr.h"
#include "butil/iobuf.h"
#include "butil/object_pool.h"
#include "butil/resource_pool.h"
#include "butil/thread_local.h"
#include "bvar/detail/agent_group.h"

namespace {

BUTIL_THREAD_LOCAL(int, tls_number, 7);
using Buffer = char[16];
BUTIL_THREAD_LOCAL(Buffer, tls_buffer, {});

struct Noncopyable {
    Noncopyable() = default;
    Noncopyable(const Noncopyable&) = delete;
    Noncopyable* operator&() { return nullptr; }
    ~Noncopyable() { ++destroyed; }
    int value = 17;
    static std::atomic<int> destroyed;
};
std::atomic<int> Noncopyable::destroyed{0};
BUTIL_THREAD_LOCAL(Noncopyable, tls_object, {});

struct Agent {
    int owner = -1;
};
using Group = bvar::detail::AgentGroup<Agent>;
using Data = butil::DoublyBufferedData<int, int>;
struct Helper {};
struct Object {};
struct Resource {};

// Exercise a static member of a class template without an out-of-class TLS
// definition. Each specialization must have its own storage.
template <typename T>
struct Member {
    static BUTIL_THREAD_LOCAL(T, value, {});
};

struct Snapshot {
    int* number;
    Buffer* buffer;
    int member;
    Agent* agent;
    int agent_owner;
    int* wrapper;
    int wrapper_owner;
    Helper* helper;
    Object* object;
    Resource* resource;
    bthread::LocalStorage* bls;
    butil::iobuf::TLSData* iobuf;
    butil::StringMapThreadLocalTemp* stringmap;
};

BUTIL_FORCE_INLINE Snapshot snapshot(Data& data) {
    Data::ScopedPtr ptr;
    if (data.Read(&ptr) != 0) {
        std::abort();
    }
    Object* object = butil::get_object<Object>();
    butil::return_object(object);
    butil::ResourceId<Resource> id;
    Resource* resource = butil::get_resource(&id);
    butil::return_resource(id);
    Agent* agent = Group::get_tls_agent(0);
    return {&tls_number(), &tls_buffer(), Member<int>::value(),
            agent, agent ? agent->owner : -1, &ptr.tls(), ptr.tls(),
            butil::get_thread_local<Helper>(), object, resource,
            &bthread::tls_bls(), butil::iobuf::get_g_tls_data(),
            &butil::tls_stringmap_temp()};
}

// Use the same context switch as the scheduler, but explicitly hand the stack
// between two pthreads. This guarantees migration on EVERY suspension, without
// sleeps or depending on the scheduler's work-stealing decisions.
constexpr int kRounds = 128;
struct AsanStack {
    void* fake = nullptr;
    const void* bottom = nullptr;
    size_t size = 0;
};
struct Migration {
    std::mutex mutex;
    std::condition_variable changed;
    int ready = 0;
    int worker = 0;
    bool done = false;
    bthread_fcontext_t fiber = nullptr;
    bthread_fcontext_t native[2] = {};
    AsanStack fiber_stack;
    AsanStack native_stack[2];
    Data data;
    Snapshot expected[2];
    Snapshot actual[kRounds];
    int numbers[kRounds] = {};
};

void migrating_fiber(intptr_t arg) {
    auto* state = reinterpret_cast<Migration*>(arg);
    BUTIL_ASAN_FINISH_SWITCH_FIBER(nullptr,
        &state->native_stack[state->worker].bottom,
        &state->native_stack[state->worker].size);
    for (int i = 0; i < kRounds; ++i) {
        state->actual[i] = snapshot(state->data);
        state->numbers[i] = ++tls_number();
        tls_buffer()[0] = static_cast<char>(state->worker + 1);
        BUTIL_ASAN_START_SWITCH_FIBER(&state->fiber_stack.fake,
            state->native_stack[state->worker].bottom,
            state->native_stack[state->worker].size);
        bthread_jump_fcontext(&state->fiber, state->native[state->worker], 0);
        BUTIL_ASAN_FINISH_SWITCH_FIBER(state->fiber_stack.fake,
            &state->native_stack[state->worker].bottom,
            &state->native_stack[state->worker].size);
    }
    state->done = true;
    BUTIL_ASAN_START_SWITCH_FIBER(nullptr,
        state->native_stack[state->worker].bottom,
        state->native_stack[state->worker].size);
    bthread_jump_fcontext(&state->fiber, state->native[state->worker], 0);
    std::abort();  // A completed context must never be resumed.
}

TEST(ThreadLocalMigrationTest, reacquire_after_every_suspend) {
    Migration state;
    constexpr size_t stack_size = 128 * 1024;
    std::unique_ptr<char[]> stack(new char[stack_size]);
    state.fiber_stack.bottom = stack.get();
    state.fiber_stack.size = stack_size;
    state.fiber = bthread_make_fcontext(stack.get() + stack_size,
                                        stack_size, migrating_fiber);
    ASSERT_NE(nullptr, state.fiber);
    auto run = [&state](int worker) {
        std::unique_lock<std::mutex> lock(state.mutex);
        tls_number() = worker * 1000;
        Member<int>::value() = worker;
        Group::get_or_create_tls_agent(0)->owner = worker;
        {
            Data::ScopedPtr ptr;
            if (state.data.Read(&ptr) != 0) {
                std::abort();
            }
            ptr.tls() = worker;
        }
        state.expected[worker] = snapshot(state.data);
        ++state.ready;
        state.changed.notify_all();
        int block = 32;
        for (;;) {
            state.changed.wait(lock, [&state, worker] {
                return state.done || (state.ready == 2 && state.worker == worker);
            });
            if (state.done) {
                break;
            }
            // Grow the source pthread's vector while the fiber is suspended.
            // A cached TLS slot must not make the next destination access it.
            Group::get_or_create_tls_agent(block++ * Group::ELEMENTS_PER_BLOCK);
            BUTIL_ASAN_START_SWITCH_FIBER(&state.native_stack[worker].fake,
                state.fiber_stack.bottom, state.fiber_stack.size);
            bthread_jump_fcontext(&state.native[worker], state.fiber,
                                  reinterpret_cast<intptr_t>(&state));
            BUTIL_ASAN_FINISH_SWITCH_FIBER(state.native_stack[worker].fake,
                                          nullptr, nullptr);
            state.worker = 1 - worker;
            state.changed.notify_all();
        }
    };
    std::thread first(run, 0);
    std::thread second(run, 1);
    first.join();
    second.join();

    ASSERT_NE(state.expected[0].number, state.expected[1].number);
    ASSERT_NE(state.expected[0].agent, state.expected[1].agent);
    ASSERT_NE(state.expected[0].wrapper, state.expected[1].wrapper);
    for (int i = 0; i < kRounds; ++i) {
        SCOPED_TRACE(i);
        const Snapshot& expected = state.expected[i % 2];
        const Snapshot& actual = state.actual[i];
        EXPECT_EQ(expected.number, actual.number);
        EXPECT_EQ(expected.buffer, actual.buffer);
        EXPECT_EQ(expected.member, actual.member);
        EXPECT_EQ(expected.agent, actual.agent);
        EXPECT_EQ(i % 2, actual.agent_owner);
        EXPECT_EQ(expected.wrapper, actual.wrapper);
        EXPECT_EQ(i % 2, actual.wrapper_owner);
        EXPECT_EQ(expected.helper, actual.helper);
        EXPECT_EQ(expected.object, actual.object);
        EXPECT_EQ(expected.resource, actual.resource);
        EXPECT_EQ(expected.bls, actual.bls);
        EXPECT_EQ(expected.iobuf, actual.iobuf);
        EXPECT_EQ(expected.stringmap, actual.stringmap);
        EXPECT_EQ((i % 2) * 1000 + i / 2 + 1, state.numbers[i]);
    }
}

TEST(ThreadLocalMigrationTest, accessor_initialization_and_types) {
    EXPECT_EQ(7, tls_number());
    EXPECT_EQ(0, tls_buffer()[0]);
    EXPECT_EQ(0, Member<int>::value());
    EXPECT_EQ(0L, Member<long>::value());
    Member<int>::value() = 42;
    EXPECT_EQ(0L, Member<long>::value());
    Member<int>::value() = 0;
}

TEST(ThreadLocalMigrationTest, noncopyable_object_destroyed_on_thread_exit) {
    const int before = Noncopyable::destroyed.load();
    std::thread worker([] {
        EXPECT_EQ(17, tls_object().value);
        tls_object().value = 42;
        EXPECT_EQ(42, tls_object().value);
        EXPECT_NE(nullptr, std::addressof(tls_object()));
    });
    worker.join();
    EXPECT_EQ(before + 1, Noncopyable::destroyed.load());
}

}  // namespace
