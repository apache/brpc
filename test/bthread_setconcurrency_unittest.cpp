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

#include <climits>
#include <thread>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "butil/atomicops.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/logging.h"
#include "butil/thread_local.h"
#include <bthread/butex.h>
#include "butil/logging.h"
#include "bthread/bthread.h"
#include "bthread/task_control.h"

namespace bthread {
    extern TaskControl* g_task_control;
}

namespace {
void* dummy(void*) {
    return nullptr;
}

TEST(BthreadTest, setconcurrency) {
    ASSERT_EQ(8, bthread_getconcurrency());
    ASSERT_EQ(EINVAL, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY - 1));
    ASSERT_EQ(EINVAL, bthread_setconcurrency(0));
    ASSERT_EQ(EINVAL, bthread_setconcurrency(-1));
    ASSERT_EQ(EINVAL, bthread_setconcurrency(BTHREAD_MAX_CONCURRENCY + 1));
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY, bthread_getconcurrency());
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY + 1));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 1, bthread_getconcurrency());
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY));  // smaller value
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(&th, nullptr, dummy, nullptr));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 1, bthread_getconcurrency());
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY + 5));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 5, bthread_getconcurrency());
    ASSERT_EQ(EPERM, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY + 1));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 5, bthread_getconcurrency());
}

static butil::atomic<int> *odd;
static butil::atomic<int> *even;

static butil::atomic<int> nbthreads(0);
static butil::atomic<int> npthreads(0);
static BAIDU_THREAD_LOCAL bool counted = false;
static butil::atomic<bool> stop (false);

static void *odd_thread(void *) {
    nbthreads.fetch_add(1);
    while (!stop) {
        if (!counted) {
            counted = true;
            npthreads.fetch_add(1);
        }
        bthread::butex_wake_all(even);
        bthread::butex_wait(odd, 0, nullptr);
    }
    return nullptr;
}

static void *even_thread(void *) {
    nbthreads.fetch_add(1);
    while (!stop) {
        if (!counted) {
            counted = true;
            npthreads.fetch_add(1);
        }
        bthread::butex_wake_all(odd);
        bthread::butex_wait(even, 0, nullptr);
    }
    return nullptr;
}

TEST(BthreadTest, setconcurrency_with_running_bthread) {
    odd = bthread::butex_create_checked<butil::atomic<int> >();
    even = bthread::butex_create_checked<butil::atomic<int> >();
    ASSERT_TRUE(odd != nullptr && even != nullptr);
    *odd = 0;
    *even = 0;
    std::vector<bthread_t> tids;
    const int N = 200;
    for (int i = 0; i < N; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, odd_thread, nullptr);
        tids.push_back(tid);
        bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, even_thread, nullptr);
        tids.push_back(tid);
    }
    for (int i = 100; i <= N; ++i) {
        ASSERT_EQ(0, bthread_setconcurrency(i));
        ASSERT_EQ(i, bthread_getconcurrency());
    }
    usleep(1000 * N);
    *odd = 1;
    *even = 1;
    stop =  true;
    bthread::butex_wake_all(odd);
    bthread::butex_wake_all(even);
    for (size_t i = 0; i < tids.size(); ++i) {
        bthread_join(tids[i], nullptr);
    }
    LOG(INFO) << "All bthreads has quit";
    ASSERT_EQ(2*N, nbthreads);
    // This is not necessarily true, not all workers need to run sth.
    //ASSERT_EQ(N, npthreads);
    LOG(INFO) << "Touched pthreads=" << npthreads;
}

void* sleep_proc(void*) {
    usleep(100000);
    return nullptr;
}

void* add_concurrency_proc(void*) {
    bthread_t tid;
    bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, sleep_proc, nullptr);
    bthread_join(tid, nullptr);
    return nullptr;
}

bool set_min_concurrency(int num) {
    std::stringstream ss;
    ss << num;
    std::string ret = GFLAGS_NAMESPACE::SetCommandLineOption("bthread_min_concurrency", ss.str().c_str());
    return !ret.empty();
}

int get_min_concurrency() {
    std::string ret;
    GFLAGS_NAMESPACE::GetCommandLineOption("bthread_min_concurrency", &ret);
    return atoi(ret.c_str());
}

TEST(BthreadTest, min_concurrency) {
    ASSERT_EQ(1, set_min_concurrency(-1)); // set min success
    ASSERT_EQ(1, set_min_concurrency(0)); // set min success
    ASSERT_EQ(0, get_min_concurrency());
    int conn = bthread_getconcurrency();
    int add_conn = 50;

    ASSERT_EQ(0, set_min_concurrency(conn + 1)); // set min failed
    ASSERT_EQ(0, get_min_concurrency());

    ASSERT_EQ(1, set_min_concurrency(conn - 1)); // set min success
    ASSERT_EQ(conn - 1, get_min_concurrency());

    ASSERT_EQ(EINVAL, bthread_setconcurrency(conn - 2)); // set max failed
    ASSERT_EQ(0, bthread_setconcurrency(conn + add_conn + 1)); // set max success
    ASSERT_EQ(0, bthread_setconcurrency(conn + add_conn)); // set max success
    ASSERT_EQ(conn + add_conn, bthread_getconcurrency());
    ASSERT_EQ(conn, bthread::g_task_control->normal_concurrency());

    ASSERT_EQ(1, set_min_concurrency(conn + 1)); // set min success
    ASSERT_EQ(conn + 1, get_min_concurrency());
    ASSERT_EQ(conn + 1, bthread::g_task_control->normal_concurrency());

    std::vector<bthread_t> tids;
    for (int i = 0; i < conn; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, sleep_proc, nullptr);
        tids.push_back(tid);
    }
    for (int i = 0; i < add_conn; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, add_concurrency_proc, nullptr);
        tids.push_back(tid);
    }
    for (size_t i = 0; i < tids.size(); ++i) {
        bthread_join(tids[i], nullptr);
    }
    ASSERT_EQ(conn + add_conn, bthread_getconcurrency());
    ASSERT_EQ(conn + add_conn, bthread::g_task_control->normal_concurrency());
}

int current_tag(int tag) {
    std::stringstream ss;
    ss << tag;
    std::string ret = GFLAGS_NAMESPACE::SetCommandLineOption("bthread_current_tag", ss.str().c_str());
    return !(ret.empty());
}

TEST(BthreadTest, current_tag) {
    ASSERT_EQ(false, current_tag(-2));
    ASSERT_EQ(true, current_tag(0));
    ASSERT_EQ(false, current_tag(1));
}

int concurrency_by_tag(int num) {
    std::stringstream ss;
    ss << num;
    std::string ret =
        GFLAGS_NAMESPACE::SetCommandLineOption("bthread_concurrency_by_tag", ss.str().c_str());
    return !(ret.empty());
}

TEST(BthreadTest, concurrency_by_tag) {
    ASSERT_EQ(concurrency_by_tag(1), false);
    auto con = bthread_getconcurrency();
    ASSERT_EQ(concurrency_by_tag(con), true);
    ASSERT_EQ(concurrency_by_tag(con + 1), true);
    ASSERT_EQ(bthread_getconcurrency(), con+1);
    bthread_setconcurrency(con + 1);
    ASSERT_EQ(concurrency_by_tag(con + 1), true);
}

// The tests below only assert on deltas: concurrency can never be reduced, so
// the tests above leave it at an unspecified value. Keep them last in the file
// as they make `concurrency()` exceed `bthread_getconcurrency()`.

void initialize_task_control() {
    ASSERT_TRUE(set_min_concurrency(0));
    bthread_t tid;
    ASSERT_EQ(0, bthread_start_background(&tid, nullptr, dummy, nullptr));
    ASSERT_EQ(0, bthread_join(tid, nullptr));
    ASSERT_NE(nullptr, bthread::g_task_control);
}

TEST(BthreadTest, reserve_concurrency_invalid_args) {
    ASSERT_NO_FATAL_FAILURE(initialize_task_control());
    ASSERT_EQ(0, bthread_add_reserved_concurrency_by_tag(
                      1, BTHREAD_TAG_DEFAULT));
    int total = bthread::g_task_control->concurrency();
    int reserved = bthread::g_task_control->reserved_concurrency();
    ASSERT_EQ(EINVAL, bthread_add_reserved_concurrency_by_tag(
                      0, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(EINVAL, bthread_add_reserved_concurrency_by_tag(
                      -1, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(EINVAL, bthread_add_reserved_concurrency_by_tag(
                      1, BTHREAD_TAG_DEFAULT - 1));
    // Only one tag by default.
    ASSERT_EQ(EINVAL, bthread_add_reserved_concurrency_by_tag(
                      1, BTHREAD_TAG_DEFAULT + 1));
    ASSERT_EQ(EINVAL, bthread_add_reserved_concurrency_by_tag(
                      BTHREAD_MAX_RESERVED_CONCURRENCY + 1, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(EINVAL, bthread_add_reserved_concurrency_by_tag(
                      INT_MAX, BTHREAD_TAG_DEFAULT));
    // A valid individual request must also fit the accumulated reservation.
    int room = BTHREAD_MAX_RESERVED_CONCURRENCY -
        bthread::g_task_control->reserved_concurrency(BTHREAD_TAG_DEFAULT);
    ASSERT_GT(room, 0);
    ASSERT_EQ(EINVAL, bthread_add_reserved_concurrency_by_tag(
                      room + 1, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(total, bthread::g_task_control->concurrency());
    ASSERT_EQ(reserved, bthread::g_task_control->reserved_concurrency());
}

// Reserved workers are extra: they raise the number of worker pthreads but not
// the concurrency configured by the user.
TEST(BthreadTest, reserve_concurrency_is_not_counted) {
    ASSERT_NO_FATAL_FAILURE(initialize_task_control());
    int normal = bthread_getconcurrency();
    int total = bthread::g_task_control->concurrency();
    int reserved = bthread::g_task_control->reserved_concurrency();
    ASSERT_EQ(normal, bthread::g_task_control->normal_concurrency());
    ASSERT_EQ(normal + reserved, total);
    // Only one tag by default, so the per-tag counters must match the totals.
    ASSERT_EQ(normal, bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(reserved, bthread::g_task_control->reserved_concurrency(
                           BTHREAD_TAG_DEFAULT));

    int k = 3;
    ASSERT_EQ(0, bthread_add_reserved_concurrency_by_tag(
                      k, BTHREAD_TAG_DEFAULT));
    // Reservations update only the reserved count. The total is its sum with
    // the normal count. These values hold as soon as `add_workers()` returns,
    // without waiting for workers to register their `TaskGroup`.
    ASSERT_EQ(total + k, bthread::g_task_control->concurrency());
    ASSERT_EQ(reserved + k, bthread::g_task_control->reserved_concurrency());
    ASSERT_EQ(reserved + k, bthread::g_task_control->reserved_concurrency(
                               BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(normal, bthread_getconcurrency());
    ASSERT_EQ(normal, bthread::g_task_control->normal_concurrency());
    ASSERT_EQ(normal, bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT));
}

// Asking for `N` workers must yield `N` runnable ones no matter whether the
// reservation happens before or after the request.
TEST(BthreadTest, reserve_concurrency_is_order_independent) {
    ASSERT_NO_FATAL_FAILURE(initialize_task_control());
    int k = 2;
    int delta = 10;

    // reserve -> setconcurrency
    int normal = bthread_getconcurrency();
    int reserved = bthread::g_task_control->reserved_concurrency();
    ASSERT_EQ(0, bthread_add_reserved_concurrency_by_tag(
                      k, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(0, bthread_setconcurrency(normal + delta));
    ASSERT_EQ(normal + delta, bthread_getconcurrency());
    ASSERT_EQ(reserved + k, bthread::g_task_control->reserved_concurrency());
    ASSERT_EQ(normal + delta, bthread::g_task_control->normal_concurrency());
    ASSERT_EQ(normal + delta + reserved + k,
              bthread::g_task_control->concurrency());

    // setconcurrency -> reserve, same outcome.
    normal = bthread_getconcurrency();
    reserved = bthread::g_task_control->reserved_concurrency();
    ASSERT_EQ(0, bthread_setconcurrency(normal + delta));
    ASSERT_EQ(0, bthread_add_reserved_concurrency_by_tag(
                      k, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(normal + delta, bthread_getconcurrency());
    ASSERT_EQ(reserved + k, bthread::g_task_control->reserved_concurrency());
    ASSERT_EQ(normal + delta, bthread::g_task_control->normal_concurrency());
    ASSERT_EQ(normal + delta + reserved + k,
              bthread::g_task_control->concurrency());
}

// Reloading `-bthread_concurrency` with the same value must not make the worker
// count creep upwards by the reserved amount each time.
TEST(BthreadTest, reserve_concurrency_no_creep_on_flag_reload) {
    ASSERT_NO_FATAL_FAILURE(initialize_task_control());
    ASSERT_EQ(0, bthread_add_reserved_concurrency_by_tag(
                      2, BTHREAD_TAG_DEFAULT));
    int normal = bthread_getconcurrency();
    int total = bthread::g_task_control->concurrency();
    for (int i = 0; i < 3; ++i) {
        std::stringstream ss;
        ss << normal;
        ASSERT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
                     "bthread_concurrency", ss.str().c_str()).empty());
        ASSERT_EQ(normal, bthread_getconcurrency());
        ASSERT_EQ(total, bthread::g_task_control->concurrency());
    }
}

TEST(BthreadTest, setconcurrency_by_tag_no_creep) {
    ASSERT_NO_FATAL_FAILURE(initialize_task_control());
    int target = bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT) + 5;
    ASSERT_EQ(0, bthread_setconcurrency_by_tag(target, BTHREAD_TAG_DEFAULT));
    int total = bthread::g_task_control->concurrency();
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(0, bthread_setconcurrency_by_tag(target, BTHREAD_TAG_DEFAULT));
        ASSERT_EQ(target, bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT));
        ASSERT_EQ(total, bthread::g_task_control->concurrency());
    }
}

TEST(BthreadTest, reserve_concurrency_during_setconcurrency) {
    ASSERT_NO_FATAL_FAILURE(initialize_task_control());
    int normal = bthread_getconcurrency();
    int reserved = bthread::g_task_control->reserved_concurrency();
    butil::atomic<bool> stop(false);
    butil::atomic<int> ready(0);
    butil::atomic<int> failures(0);
    int nsetters = 8;
    int nreserve = 128;
    std::vector<std::thread> setters;
    for (int i = 0; i < nsetters; ++i) {
        setters.emplace_back([&]() {
            ready.fetch_add(1, butil::memory_order_release);
            while (!stop.load(butil::memory_order_relaxed)) {
                if (bthread_setconcurrency(normal) != 0) {
                    failures.fetch_add(1, butil::memory_order_relaxed);
                }
            }
        });
    }
    while (ready.load(butil::memory_order_acquire) != nsetters) {
        usleep(1000);
    }
    int rc = bthread_add_reserved_concurrency_by_tag(
        nreserve, BTHREAD_TAG_DEFAULT);
    stop.store(true, butil::memory_order_relaxed);
    for (auto& setter : setters) {
        setter.join();
    }
    ASSERT_EQ(0, rc);
    ASSERT_EQ(0, failures.load(butil::memory_order_relaxed));
    ASSERT_EQ(normal, bthread_getconcurrency());
    ASSERT_EQ(normal, bthread::g_task_control->normal_concurrency());
    ASSERT_EQ(reserved + nreserve,
              bthread::g_task_control->reserved_concurrency());
}

// Keep this last, it maxes out the normal concurrency of the default tag.
TEST(BthreadTest, concurrency_limit_excludes_reserved_workers) {
    ASSERT_NO_FATAL_FAILURE(initialize_task_control());
    ASSERT_EQ(0, bthread_add_reserved_concurrency_by_tag(
                      1, BTHREAD_TAG_DEFAULT));
    int reserved =
        bthread::g_task_control->reserved_concurrency(BTHREAD_TAG_DEFAULT);
    ASSERT_GT(reserved, 0);
    // Reserving first must leave the entire normal budget available.
    ASSERT_EQ(0, bthread_setconcurrency_by_tag(
        BTHREAD_MAX_CONCURRENCY, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(BTHREAD_MAX_CONCURRENCY + reserved,
              bthread::g_task_control->concurrency(BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(BTHREAD_MAX_CONCURRENCY,
              bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MAX_CONCURRENCY));
    // Filling the normal budget first must still allow reservations.
    ASSERT_EQ(0, bthread_add_reserved_concurrency_by_tag(
                      1, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(BTHREAD_MAX_CONCURRENCY + reserved + 1,
              bthread::g_task_control->concurrency(BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(BTHREAD_MAX_CONCURRENCY,
              bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(EINVAL, bthread_setconcurrency_by_tag(
        BTHREAD_MAX_CONCURRENCY + 1, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(BTHREAD_MAX_CONCURRENCY + reserved + 1,
              bthread::g_task_control->concurrency(BTHREAD_TAG_DEFAULT));
}

void check_reserved_concurrency_limit() {
    ASSERT_NO_FATAL_FAILURE(initialize_task_control());
    int normal = bthread_getconcurrency();
    ASSERT_EQ(0, bthread_add_reserved_concurrency_by_tag(
        BTHREAD_MAX_RESERVED_CONCURRENCY - 1, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(0, bthread_add_reserved_concurrency_by_tag(
                      1, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(BTHREAD_MAX_RESERVED_CONCURRENCY,
              bthread::g_task_control->reserved_concurrency(
                  BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(normal, bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT));

    // A full reserved budget must not prevent adding normal workers.
    ASSERT_EQ(0, bthread_setconcurrency(normal + 1));
    ASSERT_EQ(normal + 1, bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT));
    int total = bthread::g_task_control->concurrency();
    ASSERT_EQ(normal + 1 + BTHREAD_MAX_RESERVED_CONCURRENCY, total);
    ASSERT_EQ(EINVAL, bthread_add_reserved_concurrency_by_tag(
                      1, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(total, bthread::g_task_control->concurrency());
    ASSERT_EQ(BTHREAD_MAX_RESERVED_CONCURRENCY,
              bthread::g_task_control->reserved_concurrency(
                  BTHREAD_TAG_DEFAULT));
}

void set_threadsafe_death_test_style() {
#ifdef GTEST_FLAG_SET
    GTEST_FLAG_SET(death_test_style, "threadsafe");
#else
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
#endif
}

TEST(BthreadConcurrencyDeathTest, reserved_limit_is_independent) {
    // Isolate the full reservation so it cannot consume later tests' budgets.
    set_threadsafe_death_test_style();
    ASSERT_EXIT({
        check_reserved_concurrency_limit();
        _exit(::testing::Test::HasFailure() ? 1 : 0);
    }, ::testing::ExitedWithCode(0), "");
}

void check_minimum_concurrency_with_reservation(bool lazy) {
    // Blocking loops reserve workers on top of the four normal workers.
    ASSERT_EQ(0, bthread_setconcurrency(4));
    ASSERT_EQ(EINVAL, bthread_setconcurrency(3));
    ASSERT_TRUE(set_min_concurrency(lazy ? 4 : 0));
    ASSERT_FALSE(set_min_concurrency(3));
    ASSERT_EQ(lazy ? 4 : 0, get_min_concurrency());

    // The first reservation initializes the scheduler at the minimum.
    ASSERT_EQ(0, bthread_add_reserved_concurrency_by_tag(
                      1, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(0, bthread_setconcurrency_by_tag(4, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(EINVAL, bthread_setconcurrency_by_tag(3, BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(4, bthread_getconcurrency());
    ASSERT_EQ(4, bthread_getconcurrency_by_tag(BTHREAD_TAG_DEFAULT));
    ASSERT_EQ(1, bthread::g_task_control->reserved_concurrency());
    ASSERT_EQ(5, bthread::g_task_control->concurrency());

    bthread_t tid;
    ASSERT_EQ(0, bthread_start_background(&tid, nullptr, dummy, nullptr));
    ASSERT_EQ(0, bthread_join(tid, nullptr));
}

TEST(BthreadConcurrencyDeathTest, minimum_concurrency_excludes_reserved_workers) {
    set_threadsafe_death_test_style();
    for (bool lazy : {false, true}) {
        SCOPED_TRACE(lazy);
        ASSERT_EXIT({
            check_minimum_concurrency_with_reservation(lazy);
            _exit(::testing::Test::HasFailure() ? 1 : 0);
        }, ::testing::ExitedWithCode(0), "");
    }
}

} // namespace
