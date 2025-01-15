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


#include <errno.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#if BRPC_WITH_RDMA
#include "bthread/bthread.h"
#include "butil/time.h"
#include "brpc/rdma/block_pool.h"

class BlockPoolTest : public ::testing::Test {
protected:
    BlockPoolTest() { }
    ~BlockPoolTest() { }
};

namespace brpc {
namespace rdma {
DECLARE_int32(rdma_memory_pool_initial_size_mb);
DECLARE_int32(rdma_memory_pool_increase_size_mb);
DECLARE_int32(rdma_memory_pool_max_regions);
DECLARE_int32(rdma_memory_pool_buckets);
extern void DestroyBlockPool();
extern int GetBlockType(void* buf);
extern size_t GetGlobalLen(int block_type);
extern size_t GetRegionNum();
}
}

using namespace brpc::rdma;

static uint32_t DummyCallback(void*, size_t) {
    return 1;
}

TEST_F(BlockPoolTest, single_thread) {
    FLAGS_rdma_memory_pool_initial_size_mb = 1024;
    FLAGS_rdma_memory_pool_increase_size_mb = 1024;
    FLAGS_rdma_memory_pool_max_regions = 16;
    FLAGS_rdma_memory_pool_buckets = 4;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    size_t num = 1024;
    void* buf[num];
    for (size_t i = 0; i < num; ++i) {
        buf[i] = AllocBlock(GetBlockSize(0));
        EXPECT_TRUE(buf[i] != NULL);
        EXPECT_EQ(0, GetBlockType(buf[i]));
    }
    for (size_t i = 0; i < num; ++i) {
        DeallocBlock(buf[i]);
        buf[i] = NULL;
    }
    for (size_t i = 0; i < num; ++i) {
        buf[i] = AllocBlock(GetBlockSize(0) + 1);
        EXPECT_TRUE(buf[i] != NULL);
        EXPECT_EQ(1, GetBlockType(buf[i]));
    }
    for (int i = num - 1; i >= 0; --i) {
        DeallocBlock(buf[i]);
        buf[i] = NULL;
    }
    for (size_t i = 0; i < num; ++i) {
        buf[i] = AllocBlock(GetBlockSize(2));
        EXPECT_TRUE(buf[i] != NULL);
        EXPECT_EQ(2, GetBlockType(buf[i]));
    }
    for (int i = num - 1; i >= 0; --i) {
        DeallocBlock(buf[i]);
        buf[i] = NULL;
    }

    DestroyBlockPool();
}

static void* AllocAndDealloc(void* arg) {
    uintptr_t i = (uintptr_t)arg;
    int len = GetBlockSize(i % 3);
    int iterations = 1000;
    while (iterations > 0) {
        void* buf = AllocBlock(len);
        EXPECT_TRUE(buf != NULL);
        EXPECT_EQ(i % 3, GetBlockType(buf));
        DeallocBlock(buf);
        --iterations;
    }
    return NULL;
}

TEST_F(BlockPoolTest, multiple_thread) {
    FLAGS_rdma_memory_pool_initial_size_mb = 1024;
    FLAGS_rdma_memory_pool_increase_size_mb = 1024;
    FLAGS_rdma_memory_pool_max_regions = 16;
    FLAGS_rdma_memory_pool_buckets = 4;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    uintptr_t thread_num = 32;
    bthread_t tid[thread_num];
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    uint64_t start_time = butil::cpuwide_time_us();
    for (uintptr_t i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, bthread_start_background(&tid[i], &attr, AllocAndDealloc, (void*)i));
    }
    for (uintptr_t i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, bthread_join(tid[i], 0));
    }
    LOG(INFO) << "Total time = " << butil::cpuwide_time_us() - start_time << "us";

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, extend) {
    FLAGS_rdma_memory_pool_initial_size_mb = 64;
    FLAGS_rdma_memory_pool_increase_size_mb = 64;
    FLAGS_rdma_memory_pool_max_regions = 16;
    FLAGS_rdma_memory_pool_buckets = 1;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    EXPECT_EQ(1, GetRegionNum());
    size_t num = 15 * 64 * 1024 * 1024 / GetBlockSize(2);
    void* buf[num];
    for (size_t i = 0; i < num; ++i) {
        buf[i] = AllocBlock(65537);
        EXPECT_TRUE(buf[i] != NULL);
    }
    EXPECT_EQ(16, GetRegionNum());
    for (size_t i = 0; i < num; ++i) {
        DeallocBlock(buf[i]);
    }
    EXPECT_EQ(16, GetRegionNum());

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, memory_not_enough) {
    FLAGS_rdma_memory_pool_initial_size_mb = 64;
    FLAGS_rdma_memory_pool_increase_size_mb = 64;
    FLAGS_rdma_memory_pool_max_regions = 2;
    FLAGS_rdma_memory_pool_buckets = 1;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    EXPECT_EQ(1, GetRegionNum());
    size_t num = 64 * 1024 * 1024 / GetBlockSize(2);
    void* buf[num];
    for (size_t i = 0; i < num; ++i) {
        buf[i] = AllocBlock(65537);
        EXPECT_TRUE(buf[i] != NULL);
    }
    EXPECT_EQ(2, GetRegionNum());
    void* tmp = AllocBlock(65536);
    EXPECT_EQ(ENOMEM, errno);
    EXPECT_EQ(0, GetRegionId(tmp));
    for (size_t i = 0; i < num; ++i) {
        DeallocBlock(buf[i]);
    }
    EXPECT_EQ(2, GetRegionNum());

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, invalid_use) {
    FLAGS_rdma_memory_pool_initial_size_mb = 64;
    FLAGS_rdma_memory_pool_increase_size_mb = 64;
    FLAGS_rdma_memory_pool_max_regions = 2;
    FLAGS_rdma_memory_pool_buckets = 1;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    void* buf = AllocBlock(0);
    EXPECT_EQ(NULL, buf);
    EXPECT_EQ(EINVAL, errno);

    buf = AllocBlock(GetBlockSize(2) + 1);
    EXPECT_EQ(NULL, buf);
    EXPECT_EQ(EINVAL, errno);

    errno = 0;
    DeallocBlock(NULL);
    EXPECT_EQ(EINVAL, errno);

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, dump_info) {
    FLAGS_rdma_memory_pool_initial_size_mb = 64;
    FLAGS_rdma_memory_pool_increase_size_mb = 64;
    FLAGS_rdma_memory_pool_max_regions = 2;
    FLAGS_rdma_memory_pool_buckets = 4;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);
    DumpMemoryPoolInfo(std::cout);
    void* buf = AllocBlock(8192);
    DumpMemoryPoolInfo(std::cout);
    DeallocBlock(buf);
    DumpMemoryPoolInfo(std::cout);
    DestroyBlockPool();
}

#endif  // if BRPC_WITH_RDMA

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}
