// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdlib>
#include <unordered_set>
#include <vector>

#include "bthread/list_of_abafree_id.h"

namespace bthread {
// define TestId
struct TestIdTraits {
    static const size_t BLOCK_SIZE = 16;
    static const size_t MAX_ENTRIES = 1000;
    static const size_t INIT_GC_SIZE = 100;
    static const int ID_INIT = 0xFF;
    static std::unordered_set<int> id_set;
    static bool exists(int id) { return id_set.count(id) > 0; }
};

std::unordered_set<int> TestIdTraits::id_set;

TEST(AbaListTest, TestGc) {
    ListOfABAFreeId<int, TestIdTraits> aba_list;

    size_t wait_seq = 0;
    for (size_t i = 0; i < 1000000; i++) {
        size_t cnts[1];
        aba_list.get_sizes(cnts, 1);
        if (wait_seq > cnts[0] + 1) {
            wait_seq = 0;
            TestIdTraits::id_set.clear();
        }
        EXPECT_EQ(aba_list.add(i), 0);
        if (TestIdTraits::id_set.size() < 4) {
            TestIdTraits::id_set.insert(i);
        } else {
            wait_seq++;
        }
    }
    size_t cnts[1];
    aba_list.get_sizes(cnts, 1);
    EXPECT_EQ(cnts[0], (size_t)96);
    TestIdTraits::id_set.clear();
}

void VerifyGcPreservesAddedIds(bool churn) {
    TestIdTraits::id_set.clear();
    ListOfABAFreeId<int, TestIdTraits> aba_list;
    std::vector<int> accepted;
    unsigned int seed = 12345;
    size_t max_cap = 0;
    for (int id = 1000; id < 2000; ++id) {
        size_t before_cap = 0;
        aba_list.get_sizes(&before_cap, 1);
        if (before_cap > max_cap) {
            max_cap = before_cap;
        }
        TestIdTraits::id_set.insert(id);
        int rc = aba_list.add(id);
        if (before_cap > TestIdTraits::MAX_ENTRIES && rc == EAGAIN) {
            TestIdTraits::id_set.erase(id);
            continue;
        }
        ASSERT_EQ(rc, 0) << "unexpected add() failure at id=" << id
                         << ", before_cap=" << before_cap;
        accepted.push_back(id);
        if (churn && (rand_r(&seed) % 4 == 0) && !accepted.empty()) {
            TestIdTraits::id_set.erase(accepted[rand_r(&seed) % accepted.size()]);
        }
        std::unordered_set<int> seen;
        aba_list.apply([&](int x) { seen.insert(x); });
        for (int a : accepted) {
            if (TestIdTraits::id_set.count(a) > 0) {
                ASSERT_EQ(seen.count(a), 1u) << "lost live id=" << a << " after add(" << id << ")";
            }
        }
    }
    const size_t init_gc_size = TestIdTraits::INIT_GC_SIZE;
    EXPECT_GT(max_cap, init_gc_size);
    TestIdTraits::id_set.clear();
}

TEST(AbaListTest, TestGcPreservesAddedIds) {
    VerifyGcPreservesAddedIds(false);
    VerifyGcPreservesAddedIds(true);
}
} // namespace bthread