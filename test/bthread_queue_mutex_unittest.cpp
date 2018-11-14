// Copyright (c) 2018 abstraction No Inc
// Author: hairet (hairet@vip.qq.com)
// Date: 2018/11/10 

#include <gtest/gtest.h>
#include "butil/compat.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/string_printf.h"
#include "butil/logging.h"
#include "bthread/bthread.h"
#include "bthread/butex.h"
#include "bthread/task_control.h"
#include "bthread/mutex.h"
#include "butil/gperftools_profiler.h"

namespace {
inline unsigned* get_butex(bthread_queue_mutex_t& m) {
    return m.butex;
}

long start_time = butil::cpuwide_time_ms();
int c = 0;
int register_idx = 0;
void* locker(void* arg) {
    bthread_queue_mutex_t * m = (bthread_queue_mutex_t *)arg;
    int reg_idx = ++register_idx;
    bthread_queue_mutex_lock(m);
    int run_idx = ++c;
    printf("[%" PRIu64 "] I'm here queue locker, run_idx:%d, reg_idx:%d %" PRId64 "ms\n", 
           pthread_numeric_id(), run_idx, reg_idx, butil::cpuwide_time_ms() - start_time);
    bthread_usleep(5000);
    bthread_queue_mutex_unlock(m);
    return NULL;
}

struct try_lock_t {
    int running;
    bthread_queue_mutex_t *m;
};

void* try_locker(void *arg) {
    try_lock_t *t = (try_lock_t*)arg;
    int ret = -1;
    while(t->running) {
        ret = bthread_queue_mutex_trylock(t->m);
        if(0 == ret) {
            //loop until try is onwer
            break;
        }
    }
    printf("[%" PRIu64 "] I'm here trying locker, is_onwer:%d, run_idx:%d, %" PRId64 "ms\n",
        pthread_numeric_id(), (!ret?1:0), ++c, butil::cpuwide_time_ms() - start_time);
    if(0 == ret) {
        bthread_queue_mutex_unlock(t->m);
    }
    return NULL;
}

int unorder_c = 0;
int unorder_register_idx = 0;
void* unorder_locker(void* arg) {
    bthread_mutex_t* m = (bthread_mutex_t*)arg;
    int reg_idx = ++unorder_register_idx;
    bthread_mutex_lock(m);
    int run_idx = ++unorder_c; 
    printf("[%" PRIu64 "] I'm here, unorder locker , run_idx:%d,reg_idx:%d  %" PRId64 "ms\n", 
            pthread_numeric_id(), run_idx, reg_idx, butil::cpuwide_time_ms() - start_time);
    bthread_usleep(5000);
    bthread_mutex_unlock(m);
    return NULL;
}

struct try_unorder_lock_t {
    int running;
    bthread_mutex_t *m;
};

void* try_unoreder_locker(void *arg) {
    try_unorder_lock_t* t = (try_unorder_lock_t *)arg;
    int ret = -1;
    while(t->running) {
        ret = bthread_mutex_trylock(t->m);
        if(0 == ret) {
            //loop until try is onwer
            break;
        }
    }
    printf("[%" PRIu64 "] I'm here trying unoreder locker, is_onwer:%d, run_idx:%d, %" PRId64 "ms\n",
        pthread_numeric_id(), (!ret?1:0), ++unorder_c, butil::cpuwide_time_ms() - start_time);
    if(0 == ret) {
        bthread_mutex_unlock(t->m);
    }
    return NULL;
}

TEST(QueueMutexTest, sanity) {
    bthread_queue_mutex_t m;
    ASSERT_EQ(0, bthread_queue_mutex_init(&m, NULL));
    ASSERT_EQ(0u, *get_butex(m));
    ASSERT_EQ(0, bthread_queue_mutex_lock(&m));
    ASSERT_EQ(1u, *get_butex(m));
    bthread_t th1;
    ASSERT_EQ(0, bthread_start_urgent(&th1, NULL, locker, &m));
    usleep(5000); // wait for locker to run.
    ASSERT_EQ(1u, *get_butex(m)); //if competitor is try lock and do adding wait queue, will be 257u  
    ASSERT_EQ(0, bthread_queue_mutex_unlock(&m));
    ASSERT_EQ(0, bthread_join(th1, NULL));
    ASSERT_EQ(0u, *get_butex(m));
    ASSERT_EQ(0, bthread_queue_mutex_destroy(&m));
}

TEST(QueueMutexTest, used_in_pthread) {
    bthread_queue_mutex_t m;
    ASSERT_EQ(0, bthread_queue_mutex_init(&m, NULL));
    pthread_t th[8];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, locker, &m));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        pthread_join(th[i], NULL);
    }
    ASSERT_EQ(0u, *get_butex(m));
    ASSERT_EQ(0, bthread_queue_mutex_destroy(&m));
}

TEST(QueueMutexTest, queue_order_test_bthread_pthread) {
    bthread_queue_mutex_t m;
    ASSERT_EQ(0, bthread_queue_mutex_init(&m, NULL));
    ASSERT_EQ(0, bthread_queue_mutex_lock(&m));
    pthread_t th[8];   
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, locker, &m));
        usleep(2000);  //sleep make pthread create and lock in wait queue by order
    }
    bthread_t bth[8];
    for (size_t i = 0; i < ARRAY_SIZE(bth); ++i) {
        ASSERT_EQ(0, bthread_start_urgent(&bth[i], NULL, locker, &m));
        usleep(2000);  //sleep make bthread create and lock in wait queue by order
    }
    usleep(5000); // wait for all 8 pthread and 8 bthread locker to run.
    pthread_t try_th;
    try_lock_t t;
    t.running = 1;
    t.m = &m;
    ASSERT_EQ(0, pthread_create(&try_th, NULL, try_locker, &t));

    printf("queue_order_test_bthread_pthread release mutex for order test\n");
    ASSERT_EQ(0, bthread_queue_mutex_unlock(&m));
    //here we expect locker fun in bthread\pthread run with same reg_idx and run_idx

    usleep(500000);           //wait for some queue locker done
    t.running = 0;          //no competitor now
    ASSERT_EQ(0, pthread_join(try_th, NULL) );
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], NULL) );
    }
    for (size_t i = 0; i < ARRAY_SIZE(bth); ++i) {
        ASSERT_EQ(0, bthread_join(bth[i], NULL));
    }
    ASSERT_EQ(0, bthread_queue_mutex_destroy(&m));
}

TEST(QueueMutexTest, queue_unorder_test_bthread_pthread) {
    bthread_mutex_t m;
    ASSERT_EQ(0, bthread_mutex_init(&m, NULL));
    ASSERT_EQ(0, bthread_mutex_lock(&m));
    pthread_t th[8];   
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, unorder_locker, &m));
        usleep(2000);  //sleep make pthread create and lock in wait queue by order
    }
    bthread_t bth[8];
    for (size_t i = 0; i < ARRAY_SIZE(bth); ++i) {
        ASSERT_EQ(0, bthread_start_urgent(&bth[i], NULL, unorder_locker, &m));
        usleep(2000);  //sleep make bthread create and lock in wait queue by order
    }
    usleep(5000); // wait for all 8 pthread and 8 bthread locker to run.
    pthread_t try_th;
    try_unorder_lock_t t;
    t.running = 1;
    t.m = &m;
    ASSERT_EQ(0, pthread_create(&try_th, NULL, try_unoreder_locker, &t));

    printf("queue_unorder_test_bthread_pthread release mutex for unorder test\n");
    ASSERT_EQ(0, bthread_mutex_unlock(&m));
    //here we expect locker fun in bthread\pthread run with diff reg_idx and run_idx,most likely happen reg_idx 1 with a big run_idx

    usleep(500000);           //wait for some queue locker done
    t.running = 0;          //no competitor now
    ASSERT_EQ(0, pthread_join(try_th, NULL) );
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], NULL) );
    }
    for (size_t i = 0; i < ARRAY_SIZE(bth); ++i) {
        ASSERT_EQ(0, bthread_join(bth[i], NULL));
    }
    ASSERT_EQ(0, bthread_mutex_destroy(&m));
}

bool g_started = false;
bool g_stopped = false;

struct BAIDU_CACHELINE_ALIGNMENT PerfArgs {
    bthread_queue_mutex_t* mutex;
    int64_t counter;
    int64_t elapse_ns;
    bool ready;

    PerfArgs() : mutex(NULL), counter(0), elapse_ns(0), ready(false) {}
};

void* add_with_mutex(void* void_arg) {
    PerfArgs* args = (PerfArgs*)void_arg;
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
        bthread_queue_mutex_lock(args->mutex);
        bthread_queue_mutex_unlock(args->mutex);
        ++args->counter;
    }
    t.stop();
    args->elapse_ns = t.n_elapsed();
    return NULL;
}

int g_prof_name_counter = 0;

template <typename ThreadId,
         typename ThreadCreateFn, typename ThreadJoinFn>
void PerfTest(bthread_queue_mutex_t* mutex,
        ThreadId* /*dummy*/,
        int thread_num,
        const ThreadCreateFn& create_fn,
        const ThreadJoinFn& join_fn) {
    g_started = false;
    g_stopped = false;
    ThreadId threads[thread_num];
    std::vector<PerfArgs> args(thread_num);
    for (int i = 0; i < thread_num; ++i) {
        args[i].mutex = mutex;
        create_fn(&threads[i], NULL, add_with_mutex, &args[i]);
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
    snprintf(prof_name, sizeof(prof_name), "queue_mutex_perf_%d.prof", ++g_prof_name_counter);
    ProfilerStart(prof_name);
    usleep(500 * 1000);
    ProfilerStop();
    g_stopped = true;
    int64_t wait_time = 0;
    int64_t count = 0;
    for (int i = 0; i < thread_num; ++i) {
        join_fn(threads[i], NULL);
        wait_time += args[i].elapse_ns;
        count += args[i].counter;
    }
    LOG(INFO) << "queue_bthread_mutex in "
        << ((void*)create_fn == (void*)pthread_create ? "pthread" : "bthread")
        << " thread_num=" << thread_num
        << " count=" << count
        << " average_time=" << wait_time / (double)count;
}


TEST(QueueMutexTest, performance) {
    int thread_num = 12;
    bthread_queue_mutex_t bth_queue_mutex;
    ASSERT_EQ(0, bthread_queue_mutex_init(&bth_queue_mutex, NULL));
    PerfTest(&bth_queue_mutex, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(&bth_queue_mutex, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);

    thread_num = 5;
    PerfTest(&bth_queue_mutex, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(&bth_queue_mutex, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);
    
    thread_num = 1;
    PerfTest(&bth_queue_mutex, (pthread_t*)NULL, thread_num, pthread_create, pthread_join);
    PerfTest(&bth_queue_mutex, (bthread_t*)NULL, thread_num, bthread_start_background, bthread_join);

    ASSERT_EQ(0, bthread_queue_mutex_destroy(&bth_queue_mutex));
}

} // namespace
