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

#include "butil/arena.h"
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>

namespace butil {

class ArenaTest : public testing::Test {};

TEST_F(ArenaTest, allocate_basic) {
  Arena arena;
  void *ptr1 = arena.allocate(10);
  EXPECT_TRUE(ptr1 != nullptr);
  void *ptr2 = arena.allocate(20);
  EXPECT_TRUE(ptr2 != NULL);
  EXPECT_NE(ptr1, ptr2);
}

TEST_F(ArenaTest, allocate_aligned) {
  Arena arena;
  // Align of void* is usually 8 on 64-bit, 4 on 32-bit.
  const size_t alignment = sizeof(void *);

  // Allocate 1 byte to make next allocation unaligned if it was tightly packed
  void *ptr1 = arena.allocate(1);
  EXPECT_TRUE(ptr1 != nullptr);

  // This should return an aligned pointer
  void *ptr2 = arena.allocate_aligned(sizeof(void *));
  EXPECT_TRUE(ptr2 != NULL);
  EXPECT_EQ(0u, (uintptr_t)ptr2 % alignment);

  // Allocate another odd number
  arena.allocate(3);

  // Should still be aligned
  void *ptr3 = arena.allocate_aligned(16);
  EXPECT_TRUE(ptr3 != NULL);
  EXPECT_EQ(0u, (uintptr_t)ptr3 % alignment);
}

TEST_F(ArenaTest, heavy_allocation) {
  Arena arena;
  for (int i = 0; i < 1000; ++i) {
    void *ptr = arena.allocate_aligned(rand() % 100 + 1);
    EXPECT_EQ(0u, (uintptr_t)ptr % sizeof(void *));
  }
}

} // namespace butil
