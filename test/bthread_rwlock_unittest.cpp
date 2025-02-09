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
#ifdef BRPC_WITH_GPERFTOOLS
#include <butil/gperftools_profiler.h>
#endif // BRPC_WITH_GPERFTOOLS
#include <bthread/rwlock.h>

namespace {

long start_time = butil::cpuwide_time_ms();
int c = 0;
void* rdlocker(void* arg) {
    auto rw = (bthread_rwlock_t*)arg;
    bthread_rwlock_rdlock(rw);
    LOG(INFO) <<butil::string_printf("[%" PRIu64 "] I'm rdlocker, %d, %" PRId64 "ms\n",
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

#ifdef BRPC_WITH_GPERFTOOLS
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
              << " read_average_time=" << (read_count == 0 ? 0 : read_wait_time / (double)read_count)
              << " writer_num=" << writer_num
              << " write_count=" << write_count
              << " write_average_time=" << (write_count == 0 ? 0 : write_wait_time / (double)write_count);
}

TEST(RWLockTest, performance) {
    const int thread_num = 12;
    PerfTest(0, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(0, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
    PerfTest(10, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(20, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
    PerfTest(100, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(100, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
}
#endif // BRPC_WITH_GPERFTOOLS


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
