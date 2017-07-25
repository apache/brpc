// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
// Author: Ge,Jun (gejun@baidu.com)

#include <algorithm>                        // std::sort
#include <gtest/gtest.h>
#include "base/time.h"
#include "base/macros.h"
#include "base/scoped_lock.h"
#include "bthread/work_stealing_queue.h"

namespace {
typedef size_t value_type;
value_type seed = 0;
base::atomic<size_t> npushed(0);
const size_t N = 1000000;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* steal_thread(void* arg) {
    std::vector<value_type> *stolen = new std::vector<value_type>;
    stolen->reserve(N);
    bthread::WorkStealingQueue<value_type> *q =
        (bthread::WorkStealingQueue<value_type>*)arg;
    value_type val;
    while (npushed.load(base::memory_order_relaxed) != N) {
        if (q->steal(&val)) {
            stolen->push_back(val);
        } else {
            asm volatile("pause\n": : :"memory");
        }
    }
    return stolen;
}

void* push_thread(void* arg) {
    seed = 0;
    bthread::WorkStealingQueue<value_type> *q =
        (bthread::WorkStealingQueue<value_type>*)arg;
    for (size_t i = 0; i < N; ++i, ++seed, ++npushed) {
        BAIDU_SCOPED_LOCK(mutex);
        q->push(seed);
    }
    return NULL;
}

void* pop_thread(void* arg) {
    std::vector<value_type> *popped = new std::vector<value_type>;
    popped->reserve(N);
    bthread::WorkStealingQueue<value_type> *q =
        (bthread::WorkStealingQueue<value_type>*)arg;
    while (npushed.load(base::memory_order_relaxed) != N) {
        value_type val;
        pthread_mutex_lock(&mutex);
        bool res = q->pop(&val);
        pthread_mutex_unlock(&mutex);
        if (res) {
            popped->push_back(val);
        }
    }
    return popped;
}


TEST(WSQTest, sanity) {
    bthread::WorkStealingQueue<value_type> q;
    ASSERT_EQ(0, q.init(N));
    pthread_t rth[8];
    pthread_t wth, pop_th;
    for (size_t i = 0; i < ARRAY_SIZE(rth); ++i) {
        ASSERT_EQ(0, pthread_create(&rth[i], NULL, steal_thread, &q));
    }
    ASSERT_EQ(0, pthread_create(&wth, NULL, push_thread, &q));
    ASSERT_EQ(0, pthread_create(&pop_th, NULL, pop_thread, &q));


    std::vector<value_type> stolen;
    stolen.reserve(N);
    size_t nstolen = 0, npopped = 0;
    for (size_t i = 0; i < ARRAY_SIZE(rth); ++i) {
        std::vector<value_type>* res = NULL;
        pthread_join(rth[i], (void**)&res);
        for (size_t j = 0; j < res->size(); ++j, ++nstolen) {
            stolen.push_back((*res)[j]);
        }
    }
    pthread_join(wth, NULL);
    std::vector<value_type>* res = NULL;
    pthread_join(pop_th, (void**)&res);
    for (size_t j = 0; j < res->size(); ++j, ++npopped) {
        stolen.push_back((*res)[j]);
    }

    value_type val;
    while (q.pop(&val)) {
        stolen.push_back(val);
    }

    std::sort(stolen.begin(), stolen.end());
    stolen.resize(std::unique(stolen.begin(), stolen.end()) - stolen.begin());
    
    ASSERT_EQ(N, stolen.size());
    ASSERT_EQ(0UL, stolen[0]);
    ASSERT_EQ(N-1, stolen[N-1]);
    std::cout << "stolen=" << nstolen << " popped=" << npopped << " left=" << (N - nstolen - npopped)  << std::endl;
}
} // namespace
