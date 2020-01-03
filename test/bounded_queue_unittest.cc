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

// Date: Sun Nov  3 19:16:50 PST 2019

#include <iostream>
#include "butil/containers/bounded_queue.h"
#include <gtest/gtest.h>

namespace {

TEST(BoundedQueueTest, sanity) {
    const int N = 36;
    char storage[N * sizeof(int)];
    butil::BoundedQueue<int> q(storage, sizeof(storage), butil::NOT_OWN_STORAGE);
    ASSERT_EQ(0ul, q.size());
    ASSERT_TRUE(q.empty());
    ASSERT_TRUE(NULL == q.top());
    ASSERT_TRUE(NULL == q.bottom());
    for (int i = 1; i <= N; ++i) {
        if (i % 2 == 0) {
            ASSERT_TRUE(q.push(i));
        } else {
            int* p = q.push();
            ASSERT_TRUE(p);
            *p = i;
        }
        ASSERT_EQ((size_t)i, q.size());
        ASSERT_EQ(1, *q.top());
        ASSERT_EQ(i, *q.bottom());
    }
    ASSERT_FALSE(q.push(N+1));
    ASSERT_FALSE(q.push_top(N+1));
    ASSERT_EQ((size_t)N, q.size());
    ASSERT_FALSE(q.empty());
    ASSERT_TRUE(q.full());

    for (int i = 1; i <= N; ++i) {
        ASSERT_EQ(i, *q.top());
        ASSERT_EQ(N, *q.bottom());
        if (i % 2 == 0) {
            int tmp = 0;
            ASSERT_TRUE(q.pop(&tmp));
            ASSERT_EQ(tmp, i);
        } else {
            ASSERT_TRUE(q.pop());
        }
        ASSERT_EQ((size_t)(N-i), q.size());
    }
    ASSERT_EQ(0ul, q.size());
    ASSERT_TRUE(q.empty());
    ASSERT_FALSE(q.full());
    ASSERT_FALSE(q.pop());

    for (int i = 1; i <= N; ++i) {
        if (i % 2 == 0) {
            ASSERT_TRUE(q.push_top(i));
        } else {
            int* p = q.push_top();
            ASSERT_TRUE(p);
            *p = i;
        }
        ASSERT_EQ((size_t)i, q.size());
        ASSERT_EQ(i, *q.top());
        ASSERT_EQ(1, *q.bottom());
    }
    ASSERT_FALSE(q.push(N+1));
    ASSERT_FALSE(q.push_top(N+1));
    ASSERT_EQ((size_t)N, q.size());
    ASSERT_FALSE(q.empty());
    ASSERT_TRUE(q.full());

    for (int i = 1; i <= N; ++i) {
        ASSERT_EQ(N, *q.top());
        ASSERT_EQ(i, *q.bottom());
        if (i % 2 == 0) {
            int tmp = 0;
            ASSERT_TRUE(q.pop_bottom(&tmp));
            ASSERT_EQ(tmp, i);
        } else {
            ASSERT_TRUE(q.pop_bottom());
        }
        ASSERT_EQ((size_t)(N-i), q.size());
    }
    ASSERT_EQ(0ul, q.size());
    ASSERT_TRUE(q.empty());
    ASSERT_FALSE(q.full());
    ASSERT_FALSE(q.pop());
}

} // anonymous namespace
