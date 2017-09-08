// Copyright (c) 2014 Baidu, Inc.
// Author: Ge,Jun (gejun@baidu.com)

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"

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
} // namespace
