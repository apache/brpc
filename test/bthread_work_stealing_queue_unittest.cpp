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

#include <algorithm>                        // std::sort
#include <gtest/gtest.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/scoped_lock.h"
#include "bthread/work_stealing_queue.h"

namespace {
typedef size_t value_type;
bool g_stop = false;
const size_t N = 1024*512;
const size_t CAP = 8;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void* steal_thread(void* arg) {
    std::vector<value_type> *stolen = new std::vector<value_type>;
    stolen->reserve(N);
    bthread::WorkStealingQueue<value_type> *q =
        (bthread::WorkStealingQueue<value_type>*)arg;
    value_type val;
    while (!g_stop) {
        if (q->steal(&val)) {
            stolen->push_back(val);
        } else {
#if defined(ARCH_CPU_ARM_FAMILY)
            asm volatile("yield\n": : :"memory");
#else
            asm volatile("pause\n": : :"memory");
#endif
        }
    }
    return stolen;
}

void* push_thread(void* arg) {
    size_t npushed = 0;
    value_type seed = 0;
    bthread::WorkStealingQueue<value_type> *q =
        (bthread::WorkStealingQueue<value_type>*)arg;
    while (true) {
        pthread_mutex_lock(&mutex);
        const bool pushed = q->push(seed);
        pthread_mutex_unlock(&mutex);
        if (pushed) {
            ++seed;
            if (++npushed == N) {
                g_stop = true;
                break;
            }
        }
    }
    return NULL;
}

void* pop_thread(void* arg) {
    std::vector<value_type> *popped = new std::vector<value_type>;
    popped->reserve(N);
    bthread::WorkStealingQueue<value_type> *q =
        (bthread::WorkStealingQueue<value_type>*)arg;
    while (!g_stop) {
        value_type val;
        pthread_mutex_lock(&mutex);
        const bool res = q->pop(&val);
        pthread_mutex_unlock(&mutex);
        if (res) {
            popped->push_back(val);
        }
    }
    return popped;
}


TEST(WSQTest, sanity) {
    bthread::WorkStealingQueue<value_type> q;
    ASSERT_EQ(0, q.init(CAP));
    pthread_t rth[8];
    pthread_t wth, pop_th;
    for (size_t i = 0; i < ARRAY_SIZE(rth); ++i) {
        ASSERT_EQ(0, pthread_create(&rth[i], NULL, steal_thread, &q));
    }
    ASSERT_EQ(0, pthread_create(&wth, NULL, push_thread, &q));
    ASSERT_EQ(0, pthread_create(&pop_th, NULL, pop_thread, &q));

    std::vector<value_type> values;
    values.reserve(N);
    size_t nstolen = 0, npopped = 0;
    for (size_t i = 0; i < ARRAY_SIZE(rth); ++i) {
        std::vector<value_type>* res = NULL;
        pthread_join(rth[i], (void**)&res);
        for (size_t j = 0; j < res->size(); ++j, ++nstolen) {
            values.push_back((*res)[j]);
        }
    }
    pthread_join(wth, NULL);
    std::vector<value_type>* res = NULL;
    pthread_join(pop_th, (void**)&res);
    for (size_t j = 0; j < res->size(); ++j, ++npopped) {
        values.push_back((*res)[j]);
    }

    value_type val;
    while (q.pop(&val)) {
        values.push_back(val);
    }

    std::sort(values.begin(), values.end());
    values.resize(std::unique(values.begin(), values.end()) - values.begin());
    
    ASSERT_EQ(N, values.size());
    for (size_t i = 0; i < N; ++i) {
        ASSERT_EQ(i, values[i]);
    }
    std::cout << "stolen=" << nstolen
              << " popped=" << npopped
              << " left=" << (N - nstolen - npopped)  << std::endl;
}
} // namespace
