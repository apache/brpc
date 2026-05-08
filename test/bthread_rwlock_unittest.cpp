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
#include "gperftools_helper.h"
#include "butil/atomicops.h"
#include <bthread/rwlock.h>

namespace {

long start_time = butil::cpuwide_time_ms();
int c = 0;
void* rdlocker(void* arg) {
    auto rw = (bthread_rwlock_t*)arg;
    bthread_rwlock_rdlock(rw);
    LOG(INFO) << butil::string_printf("[%" PRIu64 "] I'm rdlocker, %d, %" PRId64 "ms\n",
                                      pthread_numeric_id(), ++c,
                                      butil::cpuwide_time_ms() - start_time);
    bthread_usleep(10000);
    bthread_rwlock_unlock(rw);
    return NULL;
}

void* wrlocker(void* arg) {
    auto rw = (bthread_rwlock_t*)arg;
    bthread_rwlock_wrlock(rw);
    LOG(INFO) << butil::string_printf("[%" PRIu64 "] I'm wrlocker, %d, %" PRId64 "ms\n",
                                      pthread_numeric_id(), ++c,
                                      butil::cpuwide_time_ms() - start_time);
    bthread_usleep(10000);
    bthread_rwlock_unlock(rw);
    return NULL;
}

TEST(RWLockTest, sanity) {
    bthread_rwlock_t rw;
    ASSERT_EQ(0, bthread_rwlock_init(&rw, NULL));
    ASSERT_EQ(0, bthread_rwlock_rdlock(&rw));
    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));
    ASSERT_EQ(0, bthread_rwlock_wrlock(&rw));
    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));

    bthread_t rdth;
    bthread_t rwth;
    ASSERT_EQ(0, bthread_start_urgent(&rdth, NULL, rdlocker, &rw));
    ASSERT_EQ(0, bthread_start_urgent(&rwth, NULL, wrlocker, &rw));

    ASSERT_EQ(0, bthread_join(rdth, NULL));
    ASSERT_EQ(0, bthread_join(rwth, NULL));
    ASSERT_EQ(0, bthread_rwlock_destroy(&rw));
}

TEST(RWLockTest, used_in_pthread) {
    bthread_rwlock_t rw;
    ASSERT_EQ(0, bthread_rwlock_init(&rw, NULL));
    pthread_t rdth[8];
    pthread_t wrth[8];
    for (size_t i = 0; i < ARRAY_SIZE(rdth); ++i) {
        ASSERT_EQ(0, pthread_create(&rdth[i], NULL, rdlocker, &rw));
    }
    for (size_t i = 0; i < ARRAY_SIZE(wrth); ++i) {
        ASSERT_EQ(0, pthread_create(&wrth[i], NULL, wrlocker, &rw));
    }

    for (size_t i = 0; i < ARRAY_SIZE(rdth); ++i) {
        pthread_join(rdth[i], NULL);
    }
    for (size_t i = 0; i < ARRAY_SIZE(rdth); ++i) {
        pthread_join(wrth[i], NULL);
    }
    ASSERT_EQ(0, bthread_rwlock_destroy(&rw));
}

void* do_timedrdlock(void *arg) {
    struct timespec t = { -2, 0 };
    EXPECT_EQ(ETIMEDOUT, bthread_rwlock_timedrdlock((bthread_rwlock_t*)arg, &t));
    return NULL;
}

void* do_timedwrlock(void *arg) {
    struct timespec t = { -2, 0 };
    EXPECT_EQ(ETIMEDOUT, bthread_rwlock_timedwrlock((bthread_rwlock_t*)arg, &t));
    LOG(INFO) << 10;
    return NULL;
}

TEST(RWLockTest, timedlock) {
    bthread_rwlock_t rw;
    ASSERT_EQ(0, bthread_rwlock_init(&rw, NULL));

    ASSERT_EQ(0, bthread_rwlock_rdlock(&rw));
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, do_timedwrlock, &rw));
    ASSERT_EQ(0, bthread_join(th, NULL));
    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));

    ASSERT_EQ(0, bthread_rwlock_wrlock(&rw));
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, do_timedwrlock, &rw));
    ASSERT_EQ(0, bthread_join(th, NULL));
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, do_timedrdlock, &rw));
    ASSERT_EQ(0, bthread_join(th, NULL));
    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));
    ASSERT_EQ(0, bthread_rwlock_destroy(&rw));
}

struct TrylockArgs {
    bthread_rwlock_t* rw;
    int rc;
};

void* do_tryrdlock(void *arg) {
    auto trylock_args = (TrylockArgs*)arg;
    EXPECT_EQ(trylock_args->rc, bthread_rwlock_tryrdlock(trylock_args->rw));
    if (0 != trylock_args->rc) {
        return NULL;
    }
    EXPECT_EQ(trylock_args->rc, bthread_rwlock_unlock(trylock_args->rw));
    return NULL;
}

void* do_trywrlock(void *arg) {
    auto trylock_args = (TrylockArgs*)arg;
    EXPECT_EQ(trylock_args->rc, bthread_rwlock_trywrlock(trylock_args->rw));
    if (0 != trylock_args->rc) {
        return NULL;
    }
    EXPECT_EQ(trylock_args->rc, bthread_rwlock_unlock(trylock_args->rw));
    return NULL;
}

TEST(RWLockTest, trylock) {
    bthread_rwlock_t rw;
    ASSERT_EQ(0, bthread_rwlock_init(&rw, NULL));

    ASSERT_EQ(0, bthread_rwlock_tryrdlock(&rw));
    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));
    ASSERT_EQ(0, bthread_rwlock_rdlock(&rw));
    bthread_t th;
    TrylockArgs args{&rw, 0};
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, do_tryrdlock, &args));
    ASSERT_EQ(0, bthread_join(th, NULL));
    args.rc = EBUSY;
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, do_trywrlock, &args));
    ASSERT_EQ(0, bthread_join(th, NULL));
    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));

    ASSERT_EQ(0, bthread_rwlock_trywrlock(&rw));
    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));
    ASSERT_EQ(0, bthread_rwlock_wrlock(&rw));
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, do_tryrdlock, &args));
    ASSERT_EQ(0, bthread_join(th, NULL));
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, do_trywrlock, &args));
    ASSERT_EQ(0, bthread_join(th, NULL));
    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));

    ASSERT_EQ(0, bthread_rwlock_destroy(&rw));
}

TEST(RWLockTest, cpp_wrapper) {
    bthread::RWLock rw;
    ASSERT_TRUE(rw.try_rdlock());
    rw.unlock();
    rw.rdlock();
    rw.unlock();
    ASSERT_TRUE(rw.try_wrlock());
    rw.unlock();
    rw.wrlock();
    rw.unlock();

    struct timespec t = { -2, 0 };
    ASSERT_TRUE(rw.timed_rdlock(&t));
    rw.unlock();
    ASSERT_TRUE(rw.timed_wrlock(&t));
    rw.unlock();

    {
        bthread::RWLockRdGuard guard(rw);
    }
    {
        bthread::RWLockWrGuard guard(rw);
    }
    {
        std::lock_guard<bthread::RWLock> guard(rw, true);
    }
    {
        std::lock_guard<bthread::RWLock> guard(rw, false);
    }
    {
        std::lock_guard<bthread_rwlock_t> guard(*rw.native_handler(), true);
    }
    {
        std::lock_guard<bthread_rwlock_t> guard(*rw.native_handler(), false);
    }
}

bool g_started = false;
bool g_stopped = false;

void read_op(bthread_rwlock_t* rw, int64_t sleep_us) {
    ASSERT_EQ(0, bthread_rwlock_rdlock(rw));
    if (0 != sleep_us) {
        bthread_usleep(sleep_us);
    }
    ASSERT_EQ(0, bthread_rwlock_unlock(rw));
}

void write_op(bthread_rwlock_t* rw, int64_t sleep_us) {
    ASSERT_EQ(0, bthread_rwlock_wrlock(rw));
    if (0 != sleep_us) {
        bthread_usleep(sleep_us);
    }
    ASSERT_EQ(0, bthread_rwlock_unlock(rw));
}

typedef void (*OP)(bthread_rwlock_t* rw, int64_t sleep_us);

struct MixThreadArg {
    bthread_rwlock_t* rw;
    OP op;
};

void* loop_until_stopped(void* arg) {
    auto args = (MixThreadArg*)arg;
    while (!g_stopped) {
        args->op(args->rw, 20);
    }
    return NULL;
}

TEST(RWLockTest, mix_thread_types) {
    g_stopped = false;
    bthread_rwlock_t rw;
    ASSERT_EQ(0, bthread_rwlock_init(&rw, NULL));

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
            args.push_back({&rw, read_op});
        } else {
            args.push_back({&rw, write_op});
        }
        ASSERT_EQ(0, pthread_create(&pthreads[i], NULL, loop_until_stopped, &args.back()));
    }

    for (int i = 0; i < M; ++i) {
        if (i % 2 == 0) {
            args.push_back({&rw, read_op});
        } else {
            args.push_back({&rw, write_op});
        }
        const bthread_attr_t* attr = i % 2 ? NULL : &BTHREAD_ATTR_PTHREAD;
        ASSERT_EQ(0, bthread_start_urgent(&bthreads[i], attr, loop_until_stopped, &args.back()));
    }
    bthread_usleep(1000L * 1000);
    g_stopped = true;
    for (int i = 0; i < M; ++i) {
        bthread_join(bthreads[i], NULL);
    }
    for (int i = 0; i < N; ++i) {
        pthread_join(pthreads[i], NULL);
    }

    ASSERT_EQ(0, bthread_rwlock_destroy(&rw));
}

// Tests below verify the writer-priority semantics and the cleanup path
// guarded by the design notes in bthread/rwlock.cpp.
struct WriterPriorityArgs {
    bthread_rwlock_t* rw;
    butil::atomic<int>* order;
    int my_order; // sequence number captured inside the critical section
    int hold_us;
};

void* wp_writer_fn(void* arg) {
    auto* a = (WriterPriorityArgs*)arg;
    EXPECT_EQ(0, bthread_rwlock_wrlock(a->rw));
    a->my_order = a->order->fetch_add(1, butil::memory_order_relaxed);
    bthread_usleep(a->hold_us);
    EXPECT_EQ(0, bthread_rwlock_unlock(a->rw));
    return NULL;
}

void* wp_reader_fn(void* arg) {
    auto* a = (WriterPriorityArgs*)arg;
    EXPECT_EQ(0, bthread_rwlock_rdlock(a->rw));
    a->my_order = a->order->fetch_add(1, butil::memory_order_relaxed);
    bthread_usleep(a->hold_us);
    EXPECT_EQ(0, bthread_rwlock_unlock(a->rw));
    return NULL;
}

// Verifies the writer-priority invariant guarded by the order
// "unlock writer_queue_mutex BEFORE fetch_sub(writer_wait_count)" in
// rwlock_unwrlock(): once a writer is queued, any new reader arriving
// later MUST yield to that writer.
TEST(RWLockTest, writer_priority) {
    bthread_setconcurrency(8);
    bthread_rwlock_t rw;
    ASSERT_EQ(0, bthread_rwlock_init(&rw, NULL));

    // (1) Main thread holds the read lock first.
    ASSERT_EQ(0, bthread_rwlock_rdlock(&rw));

    butil::atomic<int> order(0);
    WriterPriorityArgs warg  {&rw, &order, -1, 5000};
    WriterPriorityArgs r2arg {&rw, &order, -1, 0};

    // (2) Start a writer; it should park inside wrlock() because the read
    //     lock is held. Sleep long enough for it to fetch_add into
    //     writer_wait_count and reach the butex_wait on `lock_word'.
    bthread_t wth;
    ASSERT_EQ(0, bthread_start_urgent(&wth, NULL, wp_writer_fn, &warg));
    bthread_usleep(50 * 1000);

    // (3) Now spawn a fresh reader. By writer-priority it MUST observe
    //     writer_wait_count > 0 and park on it (NOT join the active read
    //     lock).
    bthread_t r2th;
    ASSERT_EQ(0, bthread_start_urgent(&r2th, NULL, wp_reader_fn, &r2arg));
    bthread_usleep(50 * 1000);

    // (4) Release the original read lock. The writer should win the race
    //     and complete BEFORE the queued reader.
    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));

    bthread_join(wth, NULL);
    bthread_join(r2th, NULL);

    EXPECT_GE(warg.my_order, 0);
    EXPECT_GE(r2arg.my_order, 0);
    EXPECT_LT(warg.my_order, r2arg.my_order)
        << "Writer-priority violated: writer entered with order="
        << warg.my_order << " but late reader entered with order="
        << r2arg.my_order;

    ASSERT_EQ(0, bthread_rwlock_destroy(&rw));
}

void* wp_timed_wrlock_short(void* arg) {
    auto* rw = (bthread_rwlock_t*)arg;
    timespec ts = butil::milliseconds_from_now(50);
    EXPECT_EQ(ETIMEDOUT, bthread_rwlock_timedwrlock(rw, &ts));
    return NULL;
}

// Verifies the cleanup path of rwlock_wrlock_cleanup(): after multiple
// writers fail with ETIMEDOUT, writer_wait_count must be back to 0 so
// that subsequent readers are not blocked by leftover "ghost shares".
TEST(RWLockTest, wrlock_failure_does_not_leak_writer_count) {
    bthread_setconcurrency(8);
    bthread_rwlock_t rw;
    ASSERT_EQ(0, bthread_rwlock_init(&rw, NULL));

    // Hold the read lock so every wrlock attempt must block on `lock_word'.
    ASSERT_EQ(0, bthread_rwlock_rdlock(&rw));

    const int N = 8;
    bthread_t wth[N];
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(0, bthread_start_urgent(&wth[i], NULL, wp_timed_wrlock_short, &rw));
    }
    // Wait for all timed wrlock attempts to time out and run cleanup.
    for (int i = 0; i < N; ++i) {
        bthread_join(wth[i], NULL);
    }

    // Release the read lock; from this point on no writer is in flight,
    // so a new reader MUST acquire the lock immediately.
    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));

    timespec ts = butil::milliseconds_from_now(500);
    butil::Timer t;
    t.start();
    ASSERT_EQ(0, bthread_rwlock_timedrdlock(&rw, &ts));
    t.stop();
    EXPECT_LT(t.m_elapsed(), 100)
        << "Reader was blocked for " << t.m_elapsed() << "ms; "
        << "writer_wait_count was likely leaked by the cleanup path.";

    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));
    ASSERT_EQ(0, bthread_rwlock_destroy(&rw));
}

struct DataConsistencyArgs {
    bthread_rwlock_t* rw;
    int64_t* shared;       // protected by rw
    int64_t local_inc;     // writer: number of increments this thread did
    int64_t observed_max;  // reader: max value observed
    bool is_writer;
};

void* dc_worker(void* arg) {
    auto* a = (DataConsistencyArgs*)arg;
    while (!g_stopped) {
        if (a->is_writer) {
            EXPECT_EQ(0, bthread_rwlock_wrlock(a->rw));
            ++(*a->shared);
            ++a->local_inc;
            EXPECT_EQ(0, bthread_rwlock_unlock(a->rw));
        } else {
            EXPECT_EQ(0, bthread_rwlock_rdlock(a->rw));
            int64_t v = *a->shared;
            if (v > a->observed_max) {
                a->observed_max = v;
            }
            EXPECT_EQ(0, bthread_rwlock_unlock(a->rw));
        }
    }
    return NULL;
}

// Verifies the release/acquire memory ordering pair on `lock_word'.
// If the CAS in unwrlock()/unrdlock() weren't release-ordered, or the
// CAS in rdlock()/wrlock() weren't acquire-ordered, writes done inside
// the critical section could appear lost or inconsistent to other
// threads, causing the final counter to disagree with total writer ops.
TEST(RWLockTest, data_consistency) {
    bthread_rwlock_t rw;
    ASSERT_EQ(0, bthread_rwlock_init(&rw, NULL));

    g_stopped = false;
    const int W = 4;
    const int R = 8;
    bthread_setconcurrency(W + R + 4);

    int64_t shared = 0;
    std::vector<DataConsistencyArgs> args(W + R);
    std::vector<bthread_t> threads(W + R);
    for (int i = 0; i < W + R; ++i) {
        args[i].rw = &rw;
        args[i].shared = &shared;
        args[i].local_inc = 0;
        args[i].observed_max = -1;
        args[i].is_writer = (i < W);
        ASSERT_EQ(0, bthread_start_urgent(&threads[i], NULL, dc_worker, &args[i]));
    }

    bthread_usleep(500 * 1000);
    g_stopped = true;

    int64_t total_inc = 0;
    for (int i = 0; i < W + R; ++i) {
        bthread_join(threads[i], NULL);
        if (args[i].is_writer) {
            total_inc += args[i].local_inc;
        }
    }

    // No lost updates: every writer's increment is reflected in `shared'.
    EXPECT_EQ(total_inc, shared)
        << "Lost updates: total writer ops=" << total_inc
        << " but shared counter=" << shared;
    // No reader saw a value greater than the final counter.
    for (int i = W; i < W + R; ++i) {
        EXPECT_LE(args[i].observed_max, shared)
            << "Reader " << i << " observed_max=" << args[i].observed_max
            << " > final shared=" << shared;
    }

    ASSERT_EQ(0, bthread_rwlock_destroy(&rw));
}

void* ws_reader_loop(void* arg) {
    auto* rw = (bthread_rwlock_t*)arg;
    while (!g_stopped) {
        EXPECT_EQ(0, bthread_rwlock_rdlock(rw));
        // Hold the read lock briefly to keep the lock continuously busy.
        bthread_usleep(100);
        EXPECT_EQ(0, bthread_rwlock_unlock(rw));
    }
    return NULL;
}

// Verifies that under a continuous read load, a writer can still acquire
// the lock in bounded time. This is the end-to-end guarantee of the
// writer-priority strategy: any reader arriving AFTER the writer entered
// wrlock() must yield, ensuring the writer never starves.
TEST(RWLockTest, no_writer_starvation) {
    bthread_rwlock_t rw;
    ASSERT_EQ(0, bthread_rwlock_init(&rw, NULL));

    g_stopped = false;
    const int R = 16;
    bthread_setconcurrency(R + 4);
    bthread_t rth[R];
    for (int i = 0; i < R; ++i) {
        ASSERT_EQ(0, bthread_start_urgent(&rth[i], NULL, ws_reader_loop, &rw));
    }

    // Let the readers ramp up and saturate the lock.
    bthread_usleep(50 * 1000);

    // A single writer must succeed within a generous budget.
    butil::Timer t;
    t.start();
    ASSERT_EQ(0, bthread_rwlock_wrlock(&rw));
    t.stop();

    EXPECT_LT(t.m_elapsed(), 1000)
        << "Writer starved for " << t.m_elapsed() << "ms under "
        << R << " concurrent readers; writer-priority is broken.";

    ASSERT_EQ(0, bthread_rwlock_unlock(&rw));

    g_stopped = true;
    for (int i = 0; i < R; ++i) {
        bthread_join(rth[i], NULL);
    }
    ASSERT_EQ(0, bthread_rwlock_destroy(&rw));
}

struct BAIDU_CACHELINE_ALIGNMENT PerfArgs {
    bthread_rwlock_t* rw;
    int64_t counter;
    int64_t elapse_ns;
    bool ready;

    PerfArgs() : rw(NULL), counter(0), elapse_ns(0), ready(false) {}
};

template <bool Reader>
void* add_with_mutex(void* void_arg) {
    auto args = (PerfArgs*)void_arg;
    args->ready = true;
    butil::Timer t;
    while (!g_stopped) {
        if (g_started) {
            break;
        }
        bthread_usleep(10);
    }
    t.start();
    while (!g_stopped) {
        if (Reader) {
            bthread_rwlock_rdlock(args->rw);
        } else {
            bthread_rwlock_wrlock(args->rw);
        }
        ++args->counter;
        bthread_rwlock_unlock(args->rw);
    }
    t.stop();
    args->elapse_ns = t.n_elapsed();
    return NULL;
}

int g_prof_name_counter = 0;

template <typename ThreadId, typename ThreadCreateFn, typename ThreadJoinFn>
void PerfTest(uint32_t writer_ratio, ThreadId* /*dummy*/, int thread_num,
              const ThreadCreateFn& create_fn, const ThreadJoinFn& join_fn) {
    ASSERT_LE(writer_ratio, 100U);

    g_started = false;
    g_stopped = false;
    bthread_setconcurrency(thread_num + 4);
    std::vector<ThreadId> threads(thread_num);
    std::vector<PerfArgs> args(thread_num);
    bthread_rwlock_t rw;
    bthread_rwlock_init(&rw, NULL);
    int writer_num = thread_num * writer_ratio / 100;
    int reader_num = thread_num - writer_num;
    for (int i = 0; i < thread_num; ++i) {
        args[i].rw = &rw;
        if (i < writer_num) {
            ASSERT_EQ(0, create_fn(&threads[i], NULL, add_with_mutex<false>, &args[i]));
        } else {
            ASSERT_EQ(0, create_fn(&threads[i], NULL, add_with_mutex<true>, &args[i]));
        }
    }
    while (true) {
        bool all_ready = true;
        for (int i = 0; i < thread_num; ++i) {
            if (!args[i].ready) {
                all_ready = false;
                break;
            }
        }
        if (all_ready) {
            break;
        }
        usleep(1000);
    }
    g_started = true;
    char prof_name[32];
    snprintf(prof_name, sizeof(prof_name), "bthread_rwlock_perf_%d.prof", ++g_prof_name_counter);
    ProfilerStart(prof_name);
    usleep(1000 * 1000);
    ProfilerStop();
    g_stopped = true;

    int64_t read_wait_time = 0;
    int64_t read_count = 0;
    int64_t write_wait_time = 0;
    int64_t write_count = 0;
    for (int i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, join_fn(threads[i], NULL));
        if (i < writer_num) {
            write_wait_time += args[i].elapse_ns;
            write_count += args[i].counter;
        } else {
            read_wait_time += args[i].elapse_ns;
            read_count += args[i].counter;
        }
    }
    LOG(INFO) << "bthread rwlock in "
              << ((void*)create_fn == (void*)pthread_create ? "pthread" : "bthread")
              << " thread_num=" << thread_num
              << " writer_ratio=" << writer_ratio
              << " reader_num=" << reader_num
              << " read_count=" << read_count
              << " read_average_time=" << (read_count == 0 ? 0 : read_wait_time / (double)read_count) << "ns"
              << " writer_num=" << writer_num
              << " write_count=" << write_count
              << " write_average_time=" << (write_count == 0 ? 0 : write_wait_time / (double)write_count) << "ns";
}

TEST(RWLockTest, performance) {
    bthread_setconcurrency(16);
    const int thread_num = 12;
    PerfTest(0, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(0, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
    PerfTest(10, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(20, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
    PerfTest(100, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(100, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
}


void* read_thread(void* arg) {
    const size_t N = 10000;
#ifdef CHECK_RWLOCK
    pthread_rwlock_t* lock = (pthread_rwlock_t*)arg;
#else
    pthread_mutex_t* lock = (pthread_mutex_t*)arg;
#endif
    const long t1 = butil::cpuwide_time_ns();
    for (size_t i = 0; i < N; ++i) {
#ifdef CHECK_RWLOCK
        pthread_rwlock_rdlock(lock);
        pthread_rwlock_unlock(lock);
#else
        pthread_mutex_lock(lock);
        pthread_mutex_unlock(lock);
#endif
    }
    const long t2 = butil::cpuwide_time_ns();
    return new long((t2 - t1)/N);
}

void* write_thread(void*) {
    return NULL;
}

TEST(RWLockTest, pthread_rdlock_performance) {
#ifdef CHECK_RWLOCK
    pthread_rwlock_t lock1;
    ASSERT_EQ(0, pthread_rwlock_init(&lock1, NULL));
#else
    pthread_mutex_t lock1;
    ASSERT_EQ(0, pthread_mutex_init(&lock1, NULL));
#endif
    pthread_t rth[16];
    pthread_t wth;
    for (size_t i = 0; i < ARRAY_SIZE(rth); ++i) {
        ASSERT_EQ(0, pthread_create(&rth[i], NULL, read_thread, &lock1));
    }
    ASSERT_EQ(0, pthread_create(&wth, NULL, write_thread, &lock1));
    
    for (size_t i = 0; i < ARRAY_SIZE(rth); ++i) {
        long* res = NULL;
        pthread_join(rth[i], (void**)&res);
        printf("read thread %lu = %ldns\n", i, *res);
        delete res;
    }
    pthread_join(wth, NULL);
#ifdef CHECK_RWLOCK
    pthread_rwlock_destroy(&lock1);
#else
    pthread_mutex_destroy(&lock1);
#endif
}
} // namespace
