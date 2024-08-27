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
#include "bthread/rwlock.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/gperftools_profiler.h"

namespace {
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

TEST(RWLockTest, rdlock_performance) {
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
    }
    pthread_join(wth, NULL);
#ifdef CHECK_RWLOCK
    pthread_rwlock_destroy(&lock1);
#else
    pthread_mutex_destroy(&lock1);
#endif
}

TEST(RwlockTest, sanity) {
    bthread_rwlock_t m;
    ASSERT_EQ(0, bthread_rwlock_init(&m, nullptr));
    ASSERT_EQ(0, bthread_rwlock_rdlock(&m));
    ASSERT_EQ(0, bthread_rwlock_unlock(&m));
    ASSERT_EQ(0, bthread_rwlock_wrlock(&m));
    ASSERT_EQ(0, bthread_rwlock_unlock(&m));
    ASSERT_EQ(0, bthread_rwlock_destroy(&m));
}

bool g_started = false;
bool g_stopped = false;

template <typename Rwlock>
struct BAIDU_CACHELINE_ALIGNMENT PerfArgs {
    Rwlock* rwlock;
    int64_t counter;
    int64_t elapse_ns;
    bool ready;
    int32_t op_type;   /*0 for read,1 for write*/

    PerfArgs() : rwlock(nullptr), counter(0), elapse_ns(0), ready(false), op_type(0) {}
};

template <typename Rwlock>
void* add_with_rwlock(void* void_arg) {
    PerfArgs<Rwlock>* args = (PerfArgs<Rwlock>*)void_arg;
    args->ready = true;
    butil::Timer t;
    while (!g_stopped) {
        if (g_started) {
            break;
        }
        bthread_usleep(1000);
    }
    t.start();
    while (!g_stopped) {
        if(args->op_type == 0) {
            // args->rwlock->Rlock();
            bthread_rwlock_rdlock(args->rwlock);
        }
        else {
            // args->rwlock->Wlock();
            bthread_rwlock_wrlock(args->rwlock);
            }
        // args->rwlock->Unlock();
        bthread_rwlock_unlock(args->rwlock);
        ++args->counter;
    }
    t.stop();
    args->elapse_ns = t.n_elapsed();
    return nullptr;
}

int g_prof_name_counter = 0;

template <typename Rwlock, typename ThreadId,
         typename ThreadCreateFn, typename ThreadJoinFn>
         void PerfTest(Rwlock* rwlock,
                 ThreadId* /*dummy*/,
                 int thread_num,
                 const ThreadCreateFn& create_fn,
                 const ThreadJoinFn& join_fn,
                 int op_type=0 /*0 for read,1 for write*/) {
    g_started = false;
    g_stopped = false;
    ThreadId threads[thread_num];
    std::vector<PerfArgs<Rwlock> > args(thread_num);
    for (int i = 0; i < thread_num; ++i) {
        args[i].rwlock = rwlock;
        args[i].op_type = op_type;
        create_fn(&threads[i], nullptr, add_with_rwlock<Rwlock>, &args[i]);
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
    snprintf(prof_name, sizeof(prof_name), "rwlock_perf_%d.prof", ++g_prof_name_counter);
    ProfilerStart(prof_name);
    usleep(500 * 1000);
    ProfilerStop();
    g_stopped = true;
    int64_t wait_time = 0;
    int64_t count = 0;
    for (int i = 0; i < thread_num; ++i) {
        join_fn(threads[i], nullptr);
        wait_time += args[i].elapse_ns;
        count += args[i].counter;
    }
    LOG(INFO) << butil::class_name<Rwlock>() << (op_type==0?" readlock ":" writelock ") << " in "
        << ((void*)create_fn == (void*)pthread_create ? "pthread" : "bthread")
        << " thread_num=" << thread_num
        << " count=" << count
        << " average_time=" << wait_time / (double)count;
}


TEST(RWLockTest, performance) {
    const int thread_num = 12;
    bthread_rwlock_t brw;
    bthread_rwlock_init(&brw, nullptr);
    //rlock
    PerfTest(&brw, (pthread_t*)nullptr, thread_num, pthread_create, pthread_join);
    PerfTest(&brw, (bthread_t*)nullptr, thread_num, bthread_start_background, bthread_join);

    //add test 1 rlock for compare
    PerfTest(&brw, (pthread_t*)nullptr, 1, pthread_create, pthread_join);
    PerfTest(&brw, (bthread_t*)nullptr, 1, bthread_start_background, bthread_join);

    //for wlock
    PerfTest(&brw, (pthread_t*)nullptr, thread_num, pthread_create, pthread_join, 1);
    PerfTest(&brw, (bthread_t*)nullptr, thread_num, bthread_start_background, bthread_join, 1);

    //add test 1 wlock for compare
    PerfTest(&brw, (pthread_t*)nullptr, 1, pthread_create, pthread_join, 1);
    PerfTest(&brw, (bthread_t*)nullptr, 1, bthread_start_background, bthread_join, 1);
}

void* loop_until_stopped(void* arg) {
    bthread_rwlock_t *m = (bthread_rwlock_t*)arg;
    while (!g_stopped) {
        int r = rand() % 100;
        if((r&1)==0)
        {
            bthread::rlock_guard rg(*m);
        }
        else{
            bthread::wlock_guard wg(*m);
        }
        bthread_usleep(20);
    }
    return nullptr;
}

TEST(RwlockTest, mix_thread_types) {
    g_stopped = false;
    const int N = 16;
    const int M = N * 2;
    // bthread::Mutex m;
    bthread_rwlock_t brw;
    bthread_rwlock_init(&brw, nullptr);

    pthread_t pthreads[N];
    bthread_t bthreads[M];
    // reserve enough workers for test. This is a must since we have
    // BTHREAD_ATTR_PTHREAD bthreads which may cause deadlocks (the
    // bhtread_usleep below can't be scheduled and g_stopped is never
    // true, thus loop_until_stopped spins forever)
    bthread_setconcurrency(M);
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(0, pthread_create(&pthreads[i], nullptr, loop_until_stopped, &brw));
    }
    for (int i = 0; i < M; ++i) {
        const bthread_attr_t *attr = i % 2 ? nullptr : &BTHREAD_ATTR_PTHREAD;
        ASSERT_EQ(0, bthread_start_urgent(&bthreads[i], attr, loop_until_stopped, &brw));
    }
    bthread_usleep(1000L * 1000);
    g_stopped = true;
    for (int i = 0; i < M; ++i) {
        bthread_join(bthreads[i], nullptr);
    }
    for (int i = 0; i < N; ++i) {
        pthread_join(pthreads[i], nullptr);
    }
}

} // namespace
