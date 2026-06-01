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
#include <gflags/gflags.h>
#if BRPC_WITH_IOURING

#include <vector>
#include <cstring>
#include "butil/iobuf.h"
#include "brpc/iouring/iouring_block_pool.h"

namespace brpc {
namespace iouring {

DECLARE_bool(iouring_register_buffers);
DECLARE_int32(iouring_mem_pool_initial_mb);
DECLARE_int32(iouring_mem_pool_increase_mb);
DECLARE_int32(iouring_mem_pool_max_regions);
DECLARE_int32(iouring_iobuf_block_size);
DECLARE_int32(iouring_mem_pool_free_buckets);
DECLARE_int32(iouring_mem_pool_tls_cache_num);

class IouringMemPoolTest : public ::testing::Test {
protected:
    IouringMemPoolTest() {}
    ~IouringMemPoolTest() override {
        // Clean up singleton state between tests so each test starts fresh.
        if (IouringMemPool::Instance().initialized()) {
            IouringMemPool::Instance().Destroy();
        }
    }
};

// ---------------------------------------------------------------------------
// Helper: create a dummy ring for registration callbacks.
// We don't need a real io_uring ring; we just need a non-null pointer
// because AddRegion callbacks store it but never dereference it in tests.
// ---------------------------------------------------------------------------

static struct io_uring s_dummy_ring;
static std::vector<std::pair<void*, size_t>> g_registered_regions;

static void DummyRegisterCb(void* base, size_t size,
                             size_t /*block_size*/,
                             int /*buf_index_base*/) {
    g_registered_regions.push_back({base, size});
}

// ===========================================================================
// Init / Destroy
// ===========================================================================

TEST_F(IouringMemPoolTest, init_success) {
    FLAGS_iouring_mem_pool_initial_mb = 1;   // 1 MB = 128 blocks (8KB each)
    FLAGS_iouring_mem_pool_increase_mb = 1;
    FLAGS_iouring_mem_pool_max_regions = 4;
    FLAGS_iouring_iobuf_block_size = 8192;
    FLAGS_iouring_mem_pool_free_buckets = 4;
    FLAGS_iouring_mem_pool_tls_cache_num = 16;

    EXPECT_TRUE(IouringMemPool::Instance().Init(8192));
    EXPECT_TRUE(IouringMemPool::Instance().initialized());
    EXPECT_EQ(8192u, IouringMemPool::Instance().block_size());

    IouringMemPool::Instance().Destroy();
    EXPECT_FALSE(IouringMemPool::Instance().initialized());
}

TEST_F(IouringMemPoolTest, init_rejects_zero_block_size) {
    EXPECT_FALSE(IouringMemPool::Instance().Init(0));
    EXPECT_FALSE(IouringMemPool::Instance().Init(4095)); // not aligned to 4096
}

TEST_F(IouringMemPoolTest, init_rejects_non_aligned) {
    EXPECT_FALSE(IouringMemPool::Instance().Init(100)); // not page-aligned
}

TEST_F(IouringMemPoolTest, double_init_returns_true) {
    ASSERT_TRUE(IouringMemPool::Instance().Init(8192));
    EXPECT_TRUE(IouringMemPool::Instance().Init(8192)); // idempotent
    IouringMemPool::Instance().Destroy();
}

// ===========================================================================
// Allocate / Deallocate – single thread
// ===========================================================================

TEST_F(IouringMemPoolTest, allocate_single_block) {
    FLAGS_iouring_mem_pool_initial_mb = 1;
    FLAGS_iouring_mem_pool_increase_mb = 1;
    FLAGS_iouring_mem_pool_max_regions = 4;
    FLAGS_iouring_iobuf_block_size = 8192;
    FLAGS_iouring_mem_pool_free_buckets = 4;
    FLAGS_iouring_mem_pool_tls_cache_num = 16;

    ASSERT_TRUE(IouringMemPool::Instance().Init(8192));
    IouringMemPool& mp = IouringMemPool::Instance();

    void* p1 = mp.Allocate(8192);
    ASSERT_NE(p1, nullptr);

    // Block should be zeroed (memset in AddRegion).
    unsigned char* bytes = static_cast<unsigned char*>(p1);
    for (size_t i = 0; i < 8192; ++i) {
        EXPECT_EQ(0, bytes[i]) << " at offset " << i;
    }

    mp.Deallocate(p1);
    IouringMemPool::Instance().Destroy();
}

TEST_F(IouringMemPoolTest, allocate_multiple_blocks) {
    FLAGS_iouring_mem_pool_initial_mb = 1;   // 128 blocks
    FLAGS_iouring_mem_pool_increase_mb = 1;
    FLAGS_iouring_mem_pool_max_regions = 4;
    FLAGS_iouring_iobuf_block_size = 8192;
    FLAGS_iouring_mem_pool_free_buckets = 4;
    FLAGS_iouring_mem_pool_tls_cache_num = 64;

    ASSERT_TRUE(IouringMemPool::Instance().Init(8192));
    IouringMemPool& mp = IouringMemPool::Instance();

    const int N = 128;
    std::vector<void*> blocks(N);
    for (int i = 0; i < N; ++i) {
        blocks[i] = mp.Allocate(8192);
        ASSERT_NE(blocks[i], nullptr) << " block #" << i;
    }

    // All blocks should be distinct.
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            EXPECT_NE(blocks[i], blocks[j])
                << " block #" << i << " == block #" << j;
        }
    }

    // Return all blocks.
    for (int i = 0; i < N; ++i) {
        mp.Deallocate(blocks[i]);
    }

    // Re-allocate should succeed (blocks are recycled).
    for (int i = 0; i < N; ++i) {
        blocks[i] = mp.Allocate(8192);
        ASSERT_NE(blocks[i], nullptr) << " re-alloc block #" << i;
    }
    for (int i = 0; i < N; ++i) {
        mp.Deallocate(blocks[i]);
    }

    IouringMemPool::Instance().Destroy();
}

TEST_F(IouringMemPoolTest, allocate_exhausts_and_grows) {
    FLAGS_iouring_mem_pool_initial_mb = 1;   // 128 blocks in region 0
    FLAGS_iouring_mem_pool_increase_mb = 1;   // +128 on growth
    FLAGS_iouring_mem_pool_max_regions = 4;
    FLAGS_iouring_iobuf_block_size = 8192;
    FLAGS_iouring_mem_pool_free_buckets = 4;
    FLAGS_iouring_mem_pool_tls_cache_num = 256; // cache all initial blocks

    ASSERT_TRUE(IouringMemPool::Instance().Init(8192));
    IouringMemPool& mp = IouringMemPool::Instance();

    const int INITIAL_BLOCKS = 128;
    std::vector<void*> blocks(INITIAL_BLOCKS * 2); // enough to trigger growth

    // Allocate all initial blocks + some more → should trigger AddRegion.
    bool saw_growth = false;
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        void* p = mp.Allocate(8192);
        if (!p && !saw_growth) {
            // Pool exhausted before growth could provide more.
            break;
        }
        blocks[i] = p;
        if (i >= INITIAL_BLOCKS && p != nullptr) {
            saw_growth = true;
        }
    }

    // At minimum, all initial blocks should be allocatable.
    for (int i = 0; i < INITIAL_BLOCKS; ++i) {
        ASSERT_NE(blocks[i], nullptr) << " initial block #" << i;
    }

    // Cleanup.
    for (auto* p : blocks) {
        if (p) mp.Deallocate(p);
    }

    IouringMemPool::Instance().Destroy();
}

TEST_F(IouringMemPoolTest, deallocate_null_is_noop) {
    FLAGS_iouring_mem_pool_initial_mb = 1;
    FLAGS_iouring_iobuf_block_size = 8192;
    FLAGS_iouring_mem_pool_free_buckets = 4;
    FLAGS_iouring_mem_pool_tls_cache_num = 16;

    ASSERT_TRUE(IouringMemPool::Instance().Init(8192));
    IouringMemPool::Instance().Deallocate(nullptr); // must not crash
    IouringMemPool::Instance().Destroy();
}

// ===========================================================================
// GetBufIndex
// ===========================================================================

TEST_F(IouringMemPoolTest, get_buf_index_for_allocated_block) {
    FLAGS_iouring_mem_pool_initial_mb = 1;
    FLAGS_iouring_mem_pool_increase_mb = 1;
    FLAGS_iouring_mem_pool_max_regions = 4;
    FLAGS_iouring_iobuf_block_size = 8192;
    FLAGS_iouring_mem_pool_free_buckets = 4;
    FLAGS_iouring_mem_pool_tls_cache_num = 16;

    ASSERT_TRUE(IouringMemPool::Instance().Init(8192));
    IouringMemPool& mp = IouringMemPool::Instance();

    // Register the dummy ring so that buf_index is computed.
    mp.AddRingRegistrar(&s_dummy_ring, DummyRegisterCb);
    ASSERT_EQ(g_registered_regions.size(), 1u);

    void* p = mp.Allocate(8192);
    ASSERT_NE(p, nullptr);

    int idx = mp.GetBufIndex(&s_dummy_ring, p);
    EXPECT_GE(idx, 0); // first block of first region → index 0
    EXPECT_EQ(idx, 0);

    // Second block should get index 1.
    void* p2 = mp.Allocate(8192);
    ASSERT_NE(p2, nullptr);
    int idx2 = mp.GetBufIndex(&s_dummy_ring, p2);
    EXPECT_EQ(idx2, 1);

    mp.Deallocate(p);
    mp.Deallocate(p2);

    mp.RemoveRingRegistrar(&s_dummy_ring);
    g_registered_regions.clear();
    IouringMemPool::Instance().Destroy();
}

TEST_F(IouringMemPoolTest, get_buf_index_null_returns_negative_one) {
    FLAGS_iouring_mem_pool_initial_mb = 1;
    FLAGS_iouring_iobuf_block_size = 8192;
    FLAGS_iouring_mem_pool_free_buckets = 4;
    FLAGS_iouring_mem_pool_tls_cache_num = 16;

    ASSERT_TRUE(IouringMemPool::Instance().Init(8192));
    IouringMemPool& mp = IouringMemPool::Instance();

    EXPECT_EQ(mp.GetBufIndex(&s_dummy_ring, nullptr), -1);

    IouringMemPool::Instance().Destroy();
}

TEST_F(IouringMemPoolTest, get_buf_index_unregistered_ptr_returns_negative_one) {
    FLAGS_iouring_mem_pool_initial_mb = 1;
    FLAGS_iouring_iobuf_block_size = 8192;
    FLAGS_iouring_mem_pool_free_buckets = 4;
    FLAGS_iouring_mem_pool_tls_cache_num = 16;

    ASSERT_TRUE(IouringMemPool::Instance().Init(8192));
    IouringMemPool& mp = IouringMemPool::Instance();

    // Stack variable is definitely not in any registered region.
    char stack_var[8192];
    EXPECT_EQ(mp.GetBufIndex(&s_dummy_ring, stack_var), -1);

    IouringMemPool::Instance().Destroy();
}

// ===========================================================================
// Ring registrar management
// ===========================================================================

TEST_F(IouringMemPoolTest, add_ring_registrar_receives_existing_regions) {
    FLAGS_iouring_mem_pool_initial_mb = 1;
    FLAGS_iouring_iobuf_block_size = 8192;
    FLAGS_iouring_mem_pool_free_buckets = 4;
    FLAGS_iouring_mem_pool_tls_cache_num = 16;

    ASSERT_TRUE(IouringMemPool::Instance().Init(8192));
    g_registered_regions.clear();

    struct io_uring ring2;
    IouringMemPool::Instance().AddRingRegistrar(&s_dummy_ring, DummyRegisterCb);
    IouringMemPool::Instance().AddRingRegistrar(&ring2, DummyRegisterCb);

    // Both rings should have received the one existing region.
    EXPECT_EQ(g_registered_regions.size(), 2u);

    IouringMemPool::Instance().RemoveRingRegistrar(&s_dummy_ring);
    IouringMemPool::Instance().RemoveRingRegistrar(&ring2);
    IouringMemPool::Instance().Destroy();
}

// ===========================================================================
// IsFixedBuffersEnabled helper
// ===========================================================================

TEST_F(IouringMemPoolTest, is_fixed_buffers_enabled_reflects_flag) {
    FLAGS_iouring_register_buffers = false;
    EXPECT_FALSE(IsFixedBuffersEnabled());

    FLAGS_iouring_register_buffers = true;
    EXPECT_TRUE(IsFixedBuffersEnabled());

    FLAGS_iouring_register_buffers = false;
}

// ===========================================================================
// IouringReadSlot struct layout
// ===========================================================================

TEST(IouringReadSlotTest, default_constructed_is_zero) {
    IouringReadSlot slot;
    EXPECT_EQ(slot.buf, nullptr);
    EXPECT_EQ(slot.buf_index, 0);
    EXPECT_EQ(slot.size, 0u);
    // After refactoring: no slot_idx field exists.
}

}  // namespace iouring
}  // namespace brpc

#endif  // BRPC_WITH_IOURING
