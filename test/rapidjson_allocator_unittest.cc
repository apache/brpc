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
#include <cstdlib>
#include <cstring>
#include "butil/third_party/rapidjson/allocators.h"

namespace {
struct FailingAllocator {
    bool fail = true;
    void* Malloc(size_t size) { return fail ? nullptr : std::malloc(size); }
    static void Free(void* ptr) { std::free(ptr); }
};

TEST(RapidJsonAllocatorTest, allocation_failure_preserves_existing_chunks) {
    FailingAllocator allocator;
    BUTIL_RAPIDJSON_NAMESPACE::MemoryPoolAllocator<FailingAllocator> pool(
        32, &allocator);
    EXPECT_EQ(nullptr, pool.Malloc(8));
    EXPECT_EQ(0u, pool.Size());
    allocator.fail = false;
    char* saved = static_cast<char*>(pool.Malloc(8));
    ASSERT_NE(nullptr, saved);
    memcpy(saved, "stored", 7);
    const size_t size = pool.Size();
    const size_t capacity = pool.Capacity();
    allocator.fail = true;
    EXPECT_EQ(nullptr, pool.Malloc(64));
    EXPECT_EQ(nullptr, pool.Realloc(saved, 8, 64));
    EXPECT_EQ(size, pool.Size());
    EXPECT_EQ(capacity, pool.Capacity());
    EXPECT_STREQ("stored", saved);
    allocator.fail = false;
    char* resized = static_cast<char*>(pool.Realloc(saved, 8, 64));
    ASSERT_NE(nullptr, resized);
    EXPECT_STREQ("stored", resized);
}
}  // namespace
