// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <unordered_set>

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
}
} // namespace bthread