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
#include <vector>
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/errno.h"
#include <limits.h>                            // INT_MAX
#include "butil/atomicops.h"
#include "bthread/bthread.h"
#include <bthread/sys_futex.h>
#include <bthread/processor.h>

namespace {
butil::atomic<bool> stop(false);

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
        // A stop between the loop condition and futex_wait must not leave
        // this worker asleep forever. Periodically recheck the stop flag.
        timespec timeout = butil::milliseconds_to_timespec(100);
        bthread::futex_wait_private(m/*lock1*/, 0/*consumed_njob*/, &timeout);
        --nthread;
    }
    return new int(njob);
}

TEST(FutexTest, rdlock_performance) {
    stop = false;
    nthread = 0;
    size_t N = 100000;
    butil::atomic<int> lock1(0);
    pthread_t rth[8];
    for (size_t i = 0; i < ARRAY_SIZE(rth); ++i) {
        ASSERT_EQ(0, pthread_create(&rth[i], nullptr, read_thread, &lock1));
    }

    int64_t t1 = butil::cpuwide_time_ns();
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
    int64_t t2 = butil::cpuwide_time_ns();

    bthread_usleep(3000000);
    stop = true;
    bthread::futex_wake_private(&lock1, INT_MAX);

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

struct DummyWaiterArg {
    butil::atomic<int>* lock;
    butil::atomic<int>* registered;
    butil::atomic<bool>* cleaning_up;
};

void* dummy_waiter(void* void_arg) {
    DummyWaiterArg* arg = static_cast<DummyWaiterArg*>(void_arg);
    timespec timeout = butil::seconds_to_timespec(10);
    // Publish readiness before entering the wait so the controller does not
    // start measuring wakeups while threads are still being created.
    arg->registered->fetch_add(1, butil::memory_order_release);
    int rc;
    do {
        rc = bthread::futex_wait_private(arg->lock, 0, &timeout);
    } while (rc != 0 && errno == EINTR);
    if (arg->cleaning_up->load(butil::memory_order_acquire) &&
        rc == -1 && errno == EWOULDBLOCK) {
        return nullptr;
    }
    EXPECT_EQ(0, rc);
    return nullptr;
}

TEST(FutexTest, futex_wake_many_waiters_perf) {
    butil::atomic<int> lock1(0);
    butil::atomic<int> registered(0);
    butil::atomic<bool> cleaning_up(false);
    DummyWaiterArg arg = { &lock1, &registered, &cleaning_up };
    std::vector<pthread_t> threads;
    for (size_t i = 0; i < 1000; ++i) {
        pthread_t th;
        if (pthread_create(&th, nullptr, dummy_waiter, &arg) != 0) {
            break;
        }
        threads.push_back(th);
    }
    ASSERT_FALSE(threads.empty());
    size_t N = threads.size();
    const int64_t registration_deadline =
        butil::cpuwide_time_us() + 10000000L;
    while (registered.load(butil::memory_order_acquire) !=
               static_cast<int>(N) &&
           butil::cpuwide_time_us() < registration_deadline) {
        usleep(1000);
    }
    const bool all_registered =
        registered.load(butil::memory_order_acquire) == static_cast<int>(N);

    int nwakeup = 0;
    int64_t wake_ns = 0;
    const int64_t wake_deadline = butil::cpuwide_time_us() + 10000000L;
    butil::Timer tm;
    if (all_registered) {
        while (static_cast<size_t>(nwakeup) < N &&
               butil::cpuwide_time_us() < wake_deadline) {
            tm.start();
            int rc = bthread::futex_wake_private(&lock1, 1);
            tm.stop();
            EXPECT_GE(rc, 0);
            if (rc > 0) {
                nwakeup += rc;
                wake_ns += tm.n_elapsed();
            } else {
                usleep(1000);
            }
        }
    }
    // Also release waiters if a wake assertion fails; a wake alone is not
    // persistent, and a worker that races with this store gets EWOULDBLOCK.
    cleaning_up.store(true, butil::memory_order_release);
    lock1.store(1);
    bthread::futex_wake_private(&lock1, INT_MAX);
    for (pthread_t th : threads) {
        EXPECT_EQ(0, pthread_join(th, nullptr));
    }
    EXPECT_TRUE(all_registered)
        << "Timed out waiting for all futex waiters to register";
    EXPECT_EQ(N, static_cast<size_t>(nwakeup));
    if (nwakeup != 0) {
        printf("N=%lu, futex_wake a thread = %" PRId64 "ns\n", N,
               wake_ns / nwakeup);
    }

    size_t REP = 10000;
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
    return nullptr;
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
    return nullptr;
} 

TEST(FutexTest, many_futex_wake_nop_perf) {
    pthread_t th[8];
    int lock1 = 0;
    std::cout << "[Direct wake]" << std::endl;
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], nullptr, waker, &lock1));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], nullptr));
    }
    std::cout << "[Batch wake]" << std::endl;
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], nullptr, batch_waker, &lock1));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], nullptr));
    }
}
} // namespace
