// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Jul 13 15:04:18 CST 2014

#include <gtest/gtest.h>
#include "base/time.h"
#include "base/macros.h"
#include "base/string_printf.h"
#include "base/logging.h"
#include "bthread/bthread.h"
#include "bthread/butex.h"
#include "bthread/task_control.h"
#include "bthread/mutex.h"

//#define ENABLE_PROFILE
#ifdef ENABLE_PROFILE
# include <google/profiler.h>
#else
# define ProfilerStart(a)
# define ProfilerStop()
#endif

namespace {
inline unsigned* get_butex(bthread_mutex_t & m) {
    return (unsigned*)bthread::butex_locate(m.butex_memory);
}

long start_time = base::cpuwide_time_ms();
int c = 0;
void* locker(void* arg) {
    bthread_mutex_t* m = (bthread_mutex_t*)arg;
    bthread_mutex_lock(m);
    printf("[%lu] I'm here, %d, %lums\n", pthread_self(), ++c,
           base::cpuwide_time_ms() - start_time);
    bthread_usleep(10000);
    bthread_mutex_unlock(m);
    return NULL;
}

TEST(MutexTest, sanity) {
    bthread_mutex_t m;
    ASSERT_EQ(0, bthread_mutex_init(&m, NULL));
    ASSERT_EQ(0u, *get_butex(m));
    ASSERT_EQ(0, bthread_mutex_lock(&m));
    ASSERT_EQ(1u, *get_butex(m));
    bthread_t th1;
    ASSERT_EQ(0, bthread_start_urgent(&th1, NULL, locker, &m));
    usleep(5000); // wait for locker to run.
    ASSERT_EQ(257u, *get_butex(m)); // contention
    ASSERT_EQ(0, bthread_mutex_unlock(&m));
    ASSERT_EQ(0, bthread_join(th1, NULL));
    ASSERT_EQ(0u, *get_butex(m));
    ASSERT_EQ(0, bthread_mutex_destroy(&m));
}

TEST(MutexTest, used_in_pthread) {
    bthread_mutex_t m;
    ASSERT_EQ(0, bthread_mutex_init(&m, NULL));
    pthread_t th[8];
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, locker, &m));
    }
    for (size_t i = 0; i < ARRAY_SIZE(th); ++i) {
        pthread_join(th[i], NULL);
    }
    ASSERT_EQ(0u, *get_butex(m));
    ASSERT_EQ(0, bthread_mutex_destroy(&m));
}

void* do_locks(void *arg) {
    struct timespec t = { -2, 0 };
    EXPECT_EQ(ETIMEDOUT, bthread_mutex_timedlock((bthread_mutex_t*)arg, &t));
    return NULL;
}

TEST(MutexTest, timedlock) {
    bthread_cond_t c;
    bthread_mutex_t m1;
    bthread_mutex_t m2;
    ASSERT_EQ(0, bthread_cond_init(&c, NULL));
    ASSERT_EQ(0, bthread_mutex_init(&m1, NULL));
    ASSERT_EQ(0, bthread_mutex_init(&m2, NULL));

    struct timespec t = { -2, 0 };

    bthread_mutex_lock (&m1);
    bthread_mutex_lock (&m2);
    bthread_t pth;
    ASSERT_EQ(0, bthread_start_urgent(&pth, NULL, do_locks, &m1));
    ASSERT_EQ(ETIMEDOUT, bthread_cond_timedwait(&c, &m2, &t));
    ASSERT_EQ(0, bthread_join(pth, NULL));
    bthread_mutex_unlock(&m1);
    bthread_mutex_unlock(&m2);
    bthread_mutex_destroy(&m1);
    bthread_mutex_destroy(&m2);
}

TEST(MutexTest, cpp_wrapper) {
    bthread::Mutex mutex;
    ASSERT_TRUE(mutex.try_lock());
    mutex.unlock();
    mutex.lock();
    mutex.unlock();
    {
        BAIDU_SCOPED_LOCK(mutex);
    }
    {
        std::unique_lock<bthread::Mutex> lck1;
        std::unique_lock<bthread::Mutex> lck2(mutex);
        lck1.swap(lck2);
        lck1.unlock();
        lck1.lock();
    }
    ASSERT_TRUE(mutex.try_lock());
    mutex.unlock();
    {
        BAIDU_SCOPED_LOCK(*mutex.native_handler());
    }
    {
        std::unique_lock<bthread_mutex_t> lck1;
        std::unique_lock<bthread_mutex_t> lck2(*mutex.native_handler());
        lck1.swap(lck2);
        lck1.unlock();
        lck1.lock();
    }
    ASSERT_TRUE(mutex.try_lock());
    mutex.unlock();
}

bool g_stopped = false;
void* loop_until_stopped(void* arg) {
    bthread::Mutex *m = (bthread::Mutex*)arg;
    while (!g_stopped) {
        BAIDU_SCOPED_LOCK(*m);
        usleep(20);
    }
    return NULL;
}

TEST(MutexTest, mix_thread_types) {
    g_stopped = false;
    const int N = 16;
    bthread::Mutex m;
    pthread_t pthreads[N];
    bthread_t bthreads[N + N];
    bthread_setconcurrency(N * 2); // reserve enough workers for test
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(0, pthread_create(&pthreads[i], NULL, loop_until_stopped, &m));
    }
    for (int i = 0; i < N + N; ++i) {
        const bthread_attr_t *attr = i % 2 ? NULL : &BTHREAD_ATTR_PTHREAD;
        ASSERT_EQ(0, bthread_start_urgent(&bthreads[i], attr, loop_until_stopped, &m));
    }
    bthread_usleep(5000L * 1000);
    g_stopped = true;
    for (int i = 0; i < N + N; ++i) {
        bthread_join(bthreads[i], NULL);
    }
    for (int i = 0; i < N; ++i) {
        pthread_join(pthreads[i], NULL);
    }
}

int64_t counter = 0;

template <typename Mutex>
void* add_with_mutex(void* arg) {
    Mutex* m = (Mutex*)arg;
    base::Timer t;
    t.start();
    while (!g_stopped) {
        BAIDU_SCOPED_LOCK(*m);
        ++counter;
    }
    t.stop();
    return (void*)t.n_elapsed();
}

#define TEST_IN_BTHREAD
#ifdef TEST_IN_BTHREAD 
#define pthread_t bthread_t
#define pthread_join bthread_join
#define pthread_create bthread_start_urgent
#endif

TEST(MutexTest, performance) {
    g_stopped = false;
    base::Timer t;
    pthread_t threads[12];
    pthread_mutex_t m;
    pthread_mutex_init(&m, NULL);
    counter = 0;
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_create(&threads[i], NULL, add_with_mutex<pthread_mutex_t>, &m);
    }
    usleep(1000 * 1000);
    g_stopped = true;
    int64_t total_wait_time = 0;
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        void *ret = NULL;
        pthread_join(threads[i], &ret);
        total_wait_time += (int64_t)ret;
    }
    LOG(INFO) << "With pthread_mutex in " << ARRAY_SIZE(threads) << " threads"
              << " counter is " << counter
              << " and the average wait time is " << total_wait_time / counter;
    pthread_mutex_destroy(&m);
    counter = 0;
    g_stopped = false;
    bthread::Mutex mutex;
    ProfilerStart("mutex.prof");
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_create(&threads[i], NULL, add_with_mutex<bthread::Mutex>, &mutex);
    }
    usleep(1000 * 1000);
    g_stopped = true;
    total_wait_time = 0;
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        void *ret = NULL;
        pthread_join(threads[i], &ret);
        total_wait_time += (int64_t)ret;
    }
    ProfilerStop();
    LOG(INFO) << "With bthread_mutex in " << ARRAY_SIZE(threads) << " threads"
              << " counter is " << counter 
              << " and the average wait time is " << total_wait_time / counter;
}
} // namespace
