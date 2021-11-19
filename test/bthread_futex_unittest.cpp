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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/errno.h"
#include <limits.h>                            // INT_MAX
#include "butil/atomicops.h"
#include "bthread/bthread.h"
#include <bthread/sys_futex.h>
#include <bthread/processor.h>

namespace {
volatile bool stop = false;

butil::atomic<int> nthread(0);

void* read_thread(void* arg) {
    butil::atomic<int>* m = (butil::atomic<int>*)arg;
    int njob = 0;
    while (!stop) {
        int x;
        while (!stop && (x = *m) != 0) {
            if (x > 0) {
                while ((x = m->fetch_sub(1)) > 0) {
                    ++njob;
                    const long start = butil::cpuwide_time_ns();
                    while (butil::cpuwide_time_ns() < start + 10000) {
                    }
                    if (stop) {
                        return new int(njob);
                    }
                }
                m->fetch_add(1);
            } else {
                cpu_relax();
            }
        }

        ++nthread;
        bthread::futex_wait_private(m/*lock1*/, 0/*consumed_njob*/, NULL);
        --nthread;
    }
    return new int(njob);
}

TEST(FutexTest, rdlock_performance) {
    const size_t N = 100000;
    butil::atomic<int> lock1(0);
    pthread_t rth[8];
    for (size_t i = 0; i < ARRAY_SIZE(rth); ++i) {
        ASSERT_EQ(0, pthread_create(&rth[i], NULL, read_thread, &lock1));
    }

    const int64_t t1 = butil::cpuwide_time_ns();
    for (size_t i = 0; i < N; ++i) {
        if (nthread) {
            lock1.fetch_add(1);
            bthread::futex_wake_private(&lock1, 1);
        } else {
            lock1.fetch_add(1);
            if (nthread) {
                bthread::futex_wake_private(&lock1, 1);
            }
        }
    }
    const int64_t t2 = butil::cpuwide_time_ns();

    bthread_usleep(3000000);
    stop = true;
    for (int i = 0; i < 10; ++i) {
        bthread::futex_wake_private(&lock1, INT_MAX);
        sched_yield();
    }

    int njob = 0;
    int* res;
    for (size_t i = 0; i < ARRAY_SIZE(rth); ++i) {
        pthread_join(rth[i], (void**)&res);
        njob += *res;
        delete res;
    }
    printf("wake %lu times, %" PRId64 "ns each, lock1=%d njob=%d\n",
           N, (t2-t1)/N, lock1.load(), njob);
    ASSERT_EQ(N, (size_t)(lock1.load() + njob));
}

TEST(FutexTest, futex_wake_before_wait) {
    int lock1 = 0;
    timespec timeout = { 1, 0 };
    ASSERT_EQ(0, bthread::futex_wake_private(&lock1, INT_MAX));
    ASSERT_EQ(-1, bthread::futex_wait_private(&lock1, 0, &timeout));
    ASSERT_EQ(ETIMEDOUT, errno);
}

void* dummy_waiter(void* lock) {
    bthread::futex_wait_private(lock, 0, NULL);
    return NULL;
}

TEST(FutexTest, futex_wake_many_waiters_perf) {
    
    int lock1 = 0;
    size_t N = 0;
    pthread_t th;
    for (; N < 1000 && !pthread_create(&th, NULL, dummy_waiter, &lock1); ++N) {}
    
    sleep(1);
    int nwakeup = 0;
    butil::Timer tm;
    tm.start();
    for (size_t i = 0; i < N; ++i) {
        nwakeup += bthread::futex_wake_private(&lock1, 1);
    }
    tm.stop();
    printf("N=%lu, futex_wake a thread = %" PRId64 "ns\n", N, tm.n_elapsed() / N);
    ASSERT_EQ(N, (size_t)nwakeup);

    sleep(2);
    const size_t REP = 10000;
    nwakeup = 0;
    tm.start();
    for (size_t i = 0; i < REP; ++i) {
        nwakeup += bthread::futex_wake_private(&lock1, 1);
    }
    tm.stop();
    ASSERT_EQ(0, nwakeup);
    printf("futex_wake nop = %" PRId64 "ns\n", tm.n_elapsed() / REP);
}

butil::atomic<int> nevent(0);

void* waker(void* lock) {
    bthread_usleep(10000);
    const size_t REP = 100000;
    int nwakeup = 0;
    butil::Timer tm;
    tm.start();
    for (size_t i = 0; i < REP; ++i) {
        nwakeup += bthread::futex_wake_private(lock, 1);
    }
    tm.stop();
    EXPECT_EQ(0, nwakeup);
    printf("futex_wake nop = %" PRId64 "ns\n", tm.n_elapsed() / REP);
    return NULL;
} 

void* batch_waker(void* lock) {
    bthread_usleep(10000);
    const size_t REP = 100000;
    int nwakeup = 0;
    butil::Timer tm;
    tm.start();
    for (size_t i = 0; i < REP; ++i) {
        if (nevent.fetch_add(1, butil::memory_order_relaxed) == 0) {
            nwakeup += bthread::futex_wake_private(lock, 1);
            int expected = 1;
            while (1) {
                int last_expected = expected;
                if (nevent.compare_exchange_strong(expected, 0, butil::memory_order_relaxed)) {
                    break;
                }
                nwakeup += bthread::futex_wake_private(lock, expected - last_expected);
            }
        }
    }
    tm.stop();
    EXPECT_EQ(0, nwakeup);
    printf("futex_wake nop = %" PRId64 "ns\n", tm.n_elapsed() / REP);
    return NULL;
} 

TEST(FutexTest, many_futex_wake_nop_perf) {
    pthread_t th[8];
    int lock1;
    std::cout << "[Direct wake]" << std::endl;
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, waker, &lock1));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], NULL));
    }
    std::cout << "[Batch wake]" << std::endl;
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, batch_waker, &lock1));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], NULL));
    }
}
} // namespace
