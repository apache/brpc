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

#include <algorithm>
#include <gtest/gtest.h>
#include <bthread/bthread.h>
#include <bthread/butex.h>
#include <bthread/countdown_event.h>
#include <butil/atomicops.h>
#include <butil/time.h>

namespace {

const size_t SEM_COUNT = 10000;

void* sem_waiter(void* arg) {
    bthread_usleep(10 * 1000);
    auto sem = (bthread_sem_t*)arg;
    for (size_t i = 0; i < SEM_COUNT; ++i) {
        bthread_sem_wait(sem);
    }
    return nullptr;
}

void* sem_poster(void* arg) {
    bthread_usleep(10 * 1000);
    auto sem = (bthread_sem_t*)arg;
    for (size_t i = 0; i < SEM_COUNT; ++i) {
        bthread_sem_post(sem);
    }
    return nullptr;
}

TEST(SemaphoreTest, sanity) {
    bthread_sem_t sem;
    ASSERT_EQ(0, bthread_sem_init(&sem, 1));
    ASSERT_EQ(0, bthread_sem_wait(&sem));
    ASSERT_EQ(0, bthread_sem_post(&sem));
    ASSERT_EQ(0, bthread_sem_wait(&sem));

    bthread_t waiter_th;
    bthread_t poster_th;
    ASSERT_EQ(0, bthread_start_urgent(&waiter_th, nullptr, sem_waiter, &sem));
    ASSERT_EQ(0, bthread_start_urgent(&poster_th, nullptr, sem_poster, &sem));
    ASSERT_EQ(0, bthread_join(waiter_th, nullptr));
    ASSERT_EQ(0, bthread_join(poster_th, nullptr));

    ASSERT_EQ(0, bthread_sem_destroy(&sem));
}



TEST(SemaphoreTest, used_in_pthread) {
    bthread_sem_t sem;
    ASSERT_EQ(0, bthread_sem_init(&sem, 0));

    pthread_t waiter_th[8];
    pthread_t poster_th[8];
    for (auto& th : waiter_th) {
        ASSERT_EQ(0, pthread_create(&th, nullptr, sem_waiter, &sem));
    }
    for (auto& th : poster_th) {
        ASSERT_EQ(0, pthread_create(&th, nullptr, sem_poster, &sem));
    }
    for (auto& th : waiter_th) {
        pthread_join(th, nullptr);
    }
    for (auto& th : poster_th) {
        pthread_join(th, nullptr);
    }

    ASSERT_EQ(0, bthread_sem_destroy(&sem));
}

void* do_timedwait(void *arg) {
    struct timespec t = { -2, 0 };
    EXPECT_EQ(ETIMEDOUT, bthread_sem_timedwait((bthread_sem_t*)arg, &t));
    return nullptr;
}

TEST(SemaphoreTest, timedwait) {
    bthread_sem_t sem;
    ASSERT_EQ(0, bthread_sem_init(&sem, 0));
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(&th, nullptr, do_timedwait, &sem));
    ASSERT_EQ(0, bthread_join(th, nullptr));
    ASSERT_EQ(0, bthread_sem_destroy(&sem));
}


struct TryWaitArgs {
    bthread_sem_t* sem;
    int rc;
};

void* do_trywait(void *arg) {
    auto trylock_args = (TryWaitArgs*)arg;
    EXPECT_EQ(trylock_args->rc, bthread_sem_trywait(trylock_args->sem));
    return nullptr;
}

TEST(SemaphoreTest, trywait) {
    bthread_sem_t sem;
    ASSERT_EQ(0, bthread_sem_init(&sem, 0));

    ASSERT_EQ(EAGAIN, bthread_sem_trywait(&sem));
    ASSERT_EQ(0, bthread_sem_post(&sem));
    ASSERT_EQ(0, bthread_sem_trywait(&sem));
    ASSERT_EQ(EAGAIN, bthread_sem_trywait(&sem));

    ASSERT_EQ(0, bthread_sem_post(&sem));
    bthread_t th;
    TryWaitArgs args{ &sem, 0};
    ASSERT_EQ(0, bthread_start_urgent(&th, nullptr, do_trywait, &args));
    ASSERT_EQ(0, bthread_join(th, nullptr));
    args.rc = EAGAIN;
    ASSERT_EQ(0, bthread_start_urgent(&th, nullptr, do_trywait, &args));
    ASSERT_EQ(0, bthread_join(th, nullptr));

    ASSERT_EQ(0, bthread_sem_destroy(&sem));
}

struct SemButexWaitArgs {
    bthread_sem_t* sem;
    int expected_value;
    bthread::CountdownEvent* ready;
    butil::atomic<int> nwoken{0};
};

void* sem_butex_waiter(void* arg) {
    auto args = static_cast<SemButexWaitArgs*>(arg);
    args->ready->signal();
    const timespec deadline = butil::seconds_from_now(10);
    // Wait only once so that excess wakeups cannot be hidden by sem_wait's
    // retry loop. Do not consume the posted permits.
    const int rc = bthread::butex_wait(args->sem->butex, args->expected_value,
                                      &deadline);
    if (rc == 0) {
        args->nwoken.fetch_add(1, butil::memory_order_relaxed);
    } else {
        // The post may race with a waiter that has not yet been queued.
        EXPECT_EQ(EWOULDBLOCK, errno);
    }
    return nullptr;
}

void check_post_wakeup_count(unsigned initial_value, size_t num, bool batch) {
    SCOPED_TRACE(initial_value);
    const int N = 8;
    // Retry if too few waiters were queued before posting. This avoids relying
    // on a fixed sleep to establish that the waiters have entered butex_wait.
    for (int attempt = 0; attempt < 10; ++attempt) {
        bthread_sem_t sem;
        ASSERT_EQ(0, bthread_sem_init(&sem, initial_value));
        bthread::CountdownEvent ready(2 * N);
        SemButexWaitArgs args{&sem, static_cast<int>(initial_value), &ready};
        bthread_t bthreads[N];
        pthread_t pthreads[N];
        for (int i = 0; i < N; ++i) {
            ASSERT_EQ(0, bthread_start_background(
                &bthreads[i], nullptr, sem_butex_waiter, &args));
            ASSERT_EQ(0, pthread_create(
                &pthreads[i], nullptr, sem_butex_waiter, &args));
        }
        EXPECT_EQ(0, ready.timed_wait(butil::seconds_from_now(10)));

        EXPECT_EQ(0, batch ? bthread_sem_post_n(&sem, num)
                           : bthread_sem_post(&sem));
        auto value = reinterpret_cast<butil::atomic<unsigned>*>(sem.butex);
        EXPECT_EQ(initial_value + num, value->load(butil::memory_order_relaxed));
        // Also change the value for post_n(0), so late arrivals cannot enqueue
        // after cleanup and block forever.
        value->fetch_add(1, butil::memory_order_relaxed);
        const int remaining = bthread::butex_wake_all(sem.butex);
        for (int i = 0; i < N; ++i) {
            EXPECT_EQ(0, bthread_join(bthreads[i], nullptr));
            EXPECT_EQ(0, pthread_join(pthreads[i], nullptr));
        }
        const int queued = args.nwoken.load(butil::memory_order_relaxed);
        // Successful waits include both post and cleanup wakeups; late
        // arrivals that returned EWOULDBLOCK are excluded.
        EXPECT_EQ(std::min(num, static_cast<size_t>(queued)),
                  static_cast<size_t>(queued - remaining));
        ASSERT_EQ(0, bthread_sem_destroy(&sem));
        if (queued >= N) {
            return;
        }
    }
    FAIL() << "Too few waiters were queued to check the wakeup limit";
}

TEST(SemaphoreTest, post_wakes_one_waiter) {
    check_post_wakeup_count(0, 1, false);
    check_post_wakeup_count(4, 1, false);
}

TEST(SemaphoreTest, post_n_wakes_requested_waiters) {
    check_post_wakeup_count(0, 3, true);
    check_post_wakeup_count(1, 3, true);
    check_post_wakeup_count(8, 3, true);
}

TEST(SemaphoreTest, post_n_zero_does_not_wake_waiters) {
    check_post_wakeup_count(0, 0, true);
    check_post_wakeup_count(5, 0, true);
}

bool g_stopped = false;

void wait_op(bthread_sem_t* sem, int64_t sleep_us) {
    ASSERT_EQ(0, bthread_sem_wait(sem));
    if (0 != sleep_us) {
        bthread_usleep(sleep_us);
    }
}

void post_op(bthread_sem_t* rw, int64_t sleep_us) {
    ASSERT_EQ(0, bthread_sem_post(rw));
    if (0 != sleep_us) {
        bthread_usleep(sleep_us);
    }
}

typedef void (*OP)(bthread_sem_t* sem, int64_t sleep_us);

struct MixThreadArg {
    bthread_sem_t* sem;
    OP op;
};

void* loop_until_stopped(void* arg) {
    auto args = (MixThreadArg*)arg;
    for (size_t i = 0; i < SEM_COUNT; ++i) {
        args->op(args->sem, 20);
    }
    return nullptr;
}

TEST(SemaphoreTest, mix_thread_types) {
    g_stopped = false;
    bthread_sem_t sem;
    ASSERT_EQ(0, bthread_sem_init(&sem, 0));

    const int N = 16;
    const int M = N * 2;
    pthread_t pthreads[N];
    bthread_t bthreads[M];
    // reserve enough workers for test. This is a must since we have
    // BTHREAD_ATTR_PTHREAD bthreads which may cause deadlocks (the
    // bhtread_usleep below can't be scheduled and g_stopped is never
    // true, thus loop_until_stopped spins forever)
    bthread_setconcurrency(M);
    std::vector<MixThreadArg> args;
    args.reserve(N + M);
    for (int i = 0; i < N; ++i) {
        if (i % 2 == 0) {
            args.push_back({ &sem, wait_op });
        } else {
            args.push_back({ &sem, post_op });
        }
        ASSERT_EQ(0, pthread_create(&pthreads[i], nullptr, loop_until_stopped, &args.back()));
    }

    for (int i = 0; i < M; ++i) {
        if (i % 2 == 0) {
            args.push_back({ &sem, wait_op });
        } else {
            args.push_back({ &sem, post_op });
        }
        const bthread_attr_t* attr = i % 2 ? nullptr : &BTHREAD_ATTR_PTHREAD;
        ASSERT_EQ(0, bthread_start_urgent(&bthreads[i], attr, loop_until_stopped, &args.back()));
    }
    for (bthread_t bthread : bthreads) {
        bthread_join(bthread, nullptr);
    }
    for (pthread_t pthread : pthreads) {
        pthread_join(pthread, nullptr);
    }

    ASSERT_EQ(0, bthread_sem_destroy(&sem));
}

} // namespace
