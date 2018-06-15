// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2018 baidu-rpc authors

#include <errno.h>
#include <bthread/bthread.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <brpc/rdma/block_pool.h>

class BlockPoolTest : public ::testing::Test {
protected:
    BlockPoolTest() { }
    ~BlockPoolTest() { }
};

#ifdef BRPC_RDMA

namespace brpc {
namespace rdma {
DECLARE_int32(rdma_memory_pool_initial_size_mb);
DECLARE_int32(rdma_memory_pool_increase_size_mb);
DECLARE_int32(rdma_memory_pool_max_regions);
extern void DestroyBlockPool();
extern int GetBlockType(void* buf);
extern size_t GetTLSLen(int block_type);
extern size_t GetGlobalLen(int block_type);
extern size_t GetRegionNum();
}
}

using namespace brpc::rdma;

static uint32_t DummyCallback(void*, size_t) {
    return 1;
}

TEST_F(BlockPoolTest, single_thread) {
    FLAGS_rdma_memory_pool_initial_size_mb = 512;
    FLAGS_rdma_memory_pool_increase_size_mb = 512;
    FLAGS_rdma_memory_pool_max_regions = 16;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    void* buf[4096];
    for (int i = 0; i < 4096; ++i) {
        buf[i] = AllocBlock(8192);
        EXPECT_TRUE(buf[i] != NULL);
        EXPECT_EQ(0, GetBlockType(buf[i]));
        EXPECT_EQ(0, GetBlockType(buf[i]));
    }
    for (int i = 0; i < 4096; ++i) {
        DeallocBlock(buf[i]);
        buf[i] = NULL;
    }
    for (int i = 0; i < 4096; ++i) {
        buf[i] = AllocBlock(8193);
        EXPECT_TRUE(buf[i] != NULL);
#ifdef IOBUF_HUGE_BLOCK
        EXPECT_EQ(0, GetBlockType(buf[i]));
#else
        EXPECT_EQ(1, GetBlockType(buf[i]));
#endif
    }
    for (int i = 4095; i >= 4096; --i) {
        DeallocBlock(buf[i]);
        buf[i] = NULL;
    }
    for (int i = 0; i < 4096; ++i) {
        buf[i] = AllocBlock(65535);
        EXPECT_TRUE(buf[i] != NULL);
#ifdef IOBUF_HUGE_BLOCK
        EXPECT_EQ(0, GetBlockType(buf[i]));
#else
        EXPECT_EQ(3, GetBlockType(buf[i]));
#endif
    }
    for (int i = 4095; i >= 4096; --i) {
        DeallocBlock(buf[i]);
        buf[i] = NULL;
    }

    DestroyBlockPool();
}

static int GetBlockSize(int type) {
    switch (type) {
        case 0: return 8192;
        case 1: return 16384;
        case 2: return 32738;
        case 3: return 65536;
        default: return 0;
    }
}

const char one[65536] = { 1 };
const char zero[65536] = { 0 };

static void* AllocAndDealloc(void* arg) {
    uintptr_t i = (uintptr_t)arg;
    int len = GetBlockSize(i % 4);
    int iterations = 10000;
    while (iterations > 0) {
        void* buf = AllocBlock(len);
        memcpy(buf, one, len);
        EXPECT_TRUE(buf != NULL);
#ifdef IOBUF_HUGE_BLOCK
        EXPECT_EQ(0, GetBlockType(buf));
#else
        EXPECT_EQ(i % 4, GetBlockType(buf));
#endif
        EXPECT_EQ(0, memcmp(buf, one, len));
        memcpy(buf, zero, len);
        DeallocBlock(buf);
        --iterations;
    }
    return NULL;
}

TEST_F(BlockPoolTest, multiple_thread) {
    FLAGS_rdma_memory_pool_initial_size_mb = 512;
    FLAGS_rdma_memory_pool_increase_size_mb = 512;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    uintptr_t thread_num = 32;
    bthread_t tid[thread_num];
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    for (uintptr_t i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, bthread_start_background(&tid[i], &attr, AllocAndDealloc, (void*)i));
    }
    for (uintptr_t i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, bthread_join(tid[i], 0));
    }

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, prefetch) {
    FLAGS_rdma_memory_pool_initial_size_mb = 512;
    FLAGS_rdma_memory_pool_increase_size_mb = 512;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    EXPECT_EQ(0, GetTLSLen(0));
    void* buf1 = AllocBlock(8192);
#ifdef IOBUF_HUGE_BLOCK
    EXPECT_EQ(256 * 1024, GetTLSLen(0));
#else
    EXPECT_EQ(16 * 8192, GetTLSLen(0));
#endif
    void* buf2 = AllocBlock(8192);
#ifdef IOBUF_HUGE_BLOCK
    EXPECT_EQ(0, GetTLSLen(0));
#else
    EXPECT_EQ(15 * 8192, GetTLSLen(0));
#endif
    DeallocBlock(buf1);
#ifdef IOBUF_HUGE_BLOCK
    EXPECT_EQ(256 * 1024, GetTLSLen(0));
#else
    EXPECT_EQ(16 * 8192, GetTLSLen(0));
#endif
    DeallocBlock(buf2);
#ifdef IOBUF_HUGE_BLOCK
    EXPECT_EQ(512 * 1024, GetTLSLen(0));
#else
    EXPECT_EQ(17 * 8192, GetTLSLen(0));
#endif

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, recycle) {
    FLAGS_rdma_memory_pool_initial_size_mb = 512;
    FLAGS_rdma_memory_pool_increase_size_mb = 512;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    void* buf[4096];
    for (int i = 0; i < 4096; ++i) {
        buf[i] = AllocBlock(8192);
        EXPECT_TRUE(buf[i] != NULL);
        EXPECT_EQ(0, GetBlockType(buf[i]));
    }
    for (int i = 0; i < 4096; ++i) {
        DeallocBlock(buf[i]);
        buf[i] = NULL;
        EXPECT_TRUE(GetTLSLen(0) <= 256 * 8192);
    }

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, recycle_all) {
    FLAGS_rdma_memory_pool_initial_size_mb = 512;
    FLAGS_rdma_memory_pool_increase_size_mb = 512;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    EXPECT_EQ(512 * 1048576, GetGlobalLen(0));
    pthread_t tid;
    ASSERT_EQ(0, pthread_create(&tid, NULL, AllocAndDealloc, NULL));
    ASSERT_EQ(0, pthread_join(tid, 0));
    EXPECT_EQ(512 * 1048576, GetGlobalLen(0));

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, extend) {
    FLAGS_rdma_memory_pool_initial_size_mb = 64;
    FLAGS_rdma_memory_pool_increase_size_mb = 64;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    EXPECT_EQ(1, GetRegionNum());
    void* buf[4096];
    for (int i = 0; i < 4096; ++i) {
        buf[i] = AllocBlock(65534);
        EXPECT_TRUE(buf[i] != NULL);
    }
#ifdef IOBUF_HUGE_BLOCK
    EXPECT_EQ(FLAGS_rdma_memory_pool_max_regions, GetRegionNum());
#else
    EXPECT_EQ(5, GetRegionNum());
#endif
    for (int i = 0; i < 4096; ++i) {
        DeallocBlock(buf[i]);
    }
#ifdef IOBUF_HUGE_BLOCK
    EXPECT_EQ(FLAGS_rdma_memory_pool_max_regions, GetRegionNum());
#else
    EXPECT_EQ(5, GetRegionNum());
#endif

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, memory_not_enough) {
    FLAGS_rdma_memory_pool_initial_size_mb = 64;
    FLAGS_rdma_memory_pool_increase_size_mb = 64;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    EXPECT_EQ(1, GetRegionNum());
    void* buf[15360];
#ifdef IOBUF_HUGE_BLOCK
    for (int i = 0; i < 4096; ++i) {
#else
    for (int i = 0; i < 15360; ++i) {
#endif
        buf[i] = AllocBlock(65534);
        EXPECT_TRUE(buf[i] != NULL);
    }
    EXPECT_EQ(16, GetRegionNum());
    void* tmp = AllocBlock(65536);
    EXPECT_EQ(ENOMEM, errno);
    EXPECT_EQ(0, GetRegionId(tmp));
#ifdef IOBUF_HUGE_BLOCK
    for (int i = 0; i < 4096; ++i) {
#else
    for (int i = 0; i < 15360; ++i) {
#endif
        DeallocBlock(buf[i]);
    }
    EXPECT_EQ(16, GetRegionNum());

    DestroyBlockPool();
}

TEST_F(BlockPoolTest, invalid_use) {
    FLAGS_rdma_memory_pool_initial_size_mb = 64;
    FLAGS_rdma_memory_pool_increase_size_mb = 64;
    EXPECT_TRUE(InitBlockPool(DummyCallback) != NULL);

    void* buf = AllocBlock(0);
    EXPECT_EQ(NULL, buf);
    EXPECT_EQ(EINVAL, errno);

#ifdef IOBUF_HUGE_BLOCK
    buf = AllocBlock(2097153);
#else
    buf = AllocBlock(65537);
#endif
    EXPECT_EQ(NULL, buf);
    EXPECT_EQ(EINVAL, errno);

    errno = 0;
    DeallocBlock(NULL);
    EXPECT_EQ(EINVAL, errno);

    DestroyBlockPool();
}

#else

TEST_F(BlockPoolTest, dummy_test) {
}

#endif

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    google::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

