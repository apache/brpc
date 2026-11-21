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
#include <cstring>
#include <limits>
#include "butil/arena.h"

TEST(ArenaTest, rejects_unrepresentable_blocks_and_remains_usable) {
    butil::Arena arena;
    char* saved = static_cast<char*>(arena.allocate(16));
    ASSERT_NE(nullptr, saved);
    memcpy(saved, "retained", 9);
    EXPECT_EQ(nullptr, arena.allocate(std::numeric_limits<size_t>::max()));
    if (sizeof(size_t) > sizeof(uint32_t)) {
        EXPECT_EQ(nullptr, arena.allocate(static_cast<size_t>(UINT32_MAX) + 1));
    }
    EXPECT_STREQ("retained", saved);
    ASSERT_NE(nullptr, arena.allocate(16));
    arena.clear();
    ASSERT_NE(nullptr, arena.allocate(16));
}

TEST(ArenaTest, rejects_unrepresentable_initial_block) {
    butil::ArenaOptions options;
    options.initial_block_size = std::numeric_limits<size_t>::max();
    butil::Arena arena(options);
    EXPECT_EQ(nullptr, arena.allocate(1));
}
