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

namespace {

const size_t SEM_COUNT = 10000;

void* sem_waiter(void* arg) {
    bthread_usleep(10 * 1000);
    auto sem = (bthread_sem_t*)arg;
    for (size_t i = 0; i < SEM_COUNT; ++i) {
        bthread_sem_wait(sem);
    }
    return NULL;
}

void* sem_poster(void* arg) {
    bthread_usleep(10 * 1000);
    auto sem = (bthread_sem_t*)arg;
    for (size_t i = 0; i < SEM_COUNT; ++i) {
        bthread_sem_post(sem);
    }
    return NULL;
}

TEST(SemaphoreTest, sanity) {
    bthread_sem_t sem;
    ASSERT_EQ(0, bthread_sem_init(&sem, 1));
    ASSERT_EQ(0, bthread_sem_wait(&sem));
    ASSERT_EQ(0, bthread_sem_post(&sem));
    ASSERT_EQ(0, bthread_sem_wait(&sem));

    bthread_t waiter_th;
    bthread_t poster_th;
    ASSERT_EQ(0, bthread_start_urgent(&waiter_th, NULL, sem_waiter, &sem));
    ASSERT_EQ(0, bthread_start_urgent(&poster_th, NULL, sem_poster, &sem));
    ASSERT_EQ(0, bthread_join(waiter_th, NULL));
    ASSERT_EQ(0, bthread_join(poster_th, NULL));

    ASSERT_EQ(0, bthread_sem_destroy(&sem));
}



TEST(SemaphoreTest, used_in_pthread) {
    bthread_sem_t sem;
    ASSERT_EQ(0, bthread_sem_init(&sem, 0));

    pthread_t waiter_th[8];
    pthread_t poster_th[8];
    for (auto& th : waiter_th) {
        ASSERT_EQ(0, pthread_create(&th, NULL, sem_waiter, &sem));
    }
    for (auto& th : poster_th) {
        ASSERT_EQ(0, pthread_create(&th, NULL, sem_poster, &sem));
    }
    for (auto& th : waiter_th) {
        pthread_join(th, NULL);
    }
    for (auto& th : poster_th) {
        pthread_join(th, NULL);
    }

    ASSERT_EQ(0, bthread_sem_destroy(&sem));
}

void* do_timedwait(void *arg) {
    struct timespec t = { -2, 0 };
    EXPECT_EQ(ETIMEDOUT, bthread_sem_timedwait((bthread_sem_t*)arg, &t));
    return NULL;
}

TEST(SemaphoreTest, timedwait) {
    bthread_sem_t sem;
    ASSERT_EQ(0, bthread_sem_init(&sem, 0));
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, do_timedwait, &sem));
    ASSERT_EQ(0, bthread_join(th, NULL));
    ASSERT_EQ(0, bthread_sem_destroy(&sem));
}


struct TryWaitArgs {
    bthread_sem_t* sem;
    int rc;
};

void* do_trywait(void *arg) {
    auto trylock_args = (TryWaitArgs*)arg;
    EXPECT_EQ(trylock_args->rc, bthread_sem_trywait(trylock_args->sem));
    return NULL;
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
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, do_trywait, &args));
    ASSERT_EQ(0, bthread_join(th, NULL));
    args.rc = EAGAIN;
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, do_trywait, &args));
    ASSERT_EQ(0, bthread_join(th, NULL));

    ASSERT_EQ(0, bthread_sem_destroy(&sem));
}

bool g_started = false;
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
    return NULL;
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
        ASSERT_EQ(0, pthread_create(&pthreads[i], NULL, loop_until_stopped, &args.back()));
    }

    for (int i = 0; i < M; ++i) {
        if (i % 2 == 0) {
            args.push_back({ &sem, wait_op });
        } else {
            args.push_back({ &sem, post_op });
        }
        const bthread_attr_t* attr = i % 2 ? NULL : &BTHREAD_ATTR_PTHREAD;
        ASSERT_EQ(0, bthread_start_urgent(&bthreads[i], attr, loop_until_stopped, &args.back()));
    }
    for (bthread_t bthread : bthreads) {
        bthread_join(bthread, NULL);
    }
    for (pthread_t pthread : pthreads) {
        pthread_join(pthread, NULL);
    }

    ASSERT_EQ(0, bthread_sem_destroy(&sem));
}

} // namespace
