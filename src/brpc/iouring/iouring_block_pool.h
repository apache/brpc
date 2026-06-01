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

// iouring_block_pool.h
//
// IouringMemPool – global pre-registered memory pool for io_uring I/O.
//
// Background
// ----------
// io_uring supports pre-registering memory regions with the kernel via
// io_uring_register_buffers().  Once registered, any page in those regions is
// permanently pinned, so individual I/O operations skip the per-op
// get_user_pages / put_page overhead.  Registered buffers work for BOTH
// directions:
//   IORING_OP_READ_FIXED  – kernel DMA-writes received data into the buf
//   IORING_OP_WRITE_FIXED – kernel DMA-reads send data from the buf
// The |buf_index| argument selects which registered iovec entry to use.
//
// IouringMemPool  (global, one per process)
// -----------------------------------------
// Replaces butil::iobuf::blockmem_allocate so that EVERY IOBuf block comes
// from a pre-registered slab.  This means:
//
//   Write path: IOBuf blocks can be handed directly to IORING_OP_WRITE_FIXED
//               with no copy and no per-op page pinning.
//
//   Read path:  Allocate() a block, submit IORING_OP_READ_FIXED pointing at
//               it; on CQE wrap it zero-copy in IOBuf with a Deallocate()
//               deleter.  Because every block in the pool already has a
//               registered buf_index, no separate slot-pool bookkeeping is
//               needed – IouringMemPool's reference counting (via IOBuf) is
//               sufficient.
//
// Layout of one region (slab):
//
//   [ iovec_table_entry_0 ][ iovec_table_entry_1 ] ... [ iovec_table_entry_N ]
//   ^                                                                          ^
//   region.base                               region.base + region.size
//
// Each iovec entry covers exactly one IOBuf block (iouring_iobuf_block_size
// bytes).  The kernel buf_index for block at offset k in region r is:
//
//   buf_index = region.buf_index_base + k
//
// GetBufIndex(ptr) does this lookup in O(max_regions) time, same as RDMA's
// GetRegionId().
//
// Dynamic growth
// --------------
// When the pool runs out of blocks a new region is allocated and all rings
// are updated via io_uring_register_buffers_update() (kernel >= 5.13) or a
// full re-registration on older kernels.  Growth is serialised by a mutex;
// the hot allocation path is lock-free (TLS free-list).
//
// Thread safety
// -------------
// IouringMemPool: thread-safe (TLS fast-path + mutex for growth).

#ifndef BRPC_IOURING_BLOCK_POOL_H
#define BRPC_IOURING_BLOCK_POOL_H

#if BRPC_WITH_IOURING

#include <liburing.h>
#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <memory>
#include <vector>
#include "butil/atomicops.h"
#include "butil/macros.h"
#include "butil/synchronization/lock.h"
#include <gflags/gflags_declare.h>

namespace brpc {
namespace iouring {

// ---------------------------------------------------------------------------
// gflags (defined in iouring_block_pool.cpp)
// ---------------------------------------------------------------------------

DECLARE_bool(iouring_register_buffers);   // master switch: enables fixed-buf I/O

// IOBuf block pool (for READ_FIXED / WRITE_FIXED)
DECLARE_int32(iouring_mem_pool_initial_mb);   // initial slab size per region (MB)
DECLARE_int32(iouring_mem_pool_increase_mb);  // increment on growth (MB)
DECLARE_int32(iouring_mem_pool_max_regions);  // max regions (default 8)
DECLARE_int32(iouring_iobuf_block_size);      // IOBuf block size; synced to butil via SetDefaultBlockSize()
DECLARE_int32(iouring_mem_pool_free_buckets); // number of global free-list buckets (default 8)
DECLARE_int32(iouring_mem_pool_tls_cache_num); // max blocks cached per thread in TLS (default 128)

// ---------------------------------------------------------------------------
// IouringReadSlot – describes one READ_FIXED receive buffer.
//
// Allocated from IouringMemPool (already pre-registered with io_uring), so
// buf_index is always valid and no separate slot-pool is needed.
// The endpoint holds one IouringReadSlot while a READ_FIXED SQE is in flight.
// When the CQE arrives the buf is wrapped in IOBuf zero-copy; the IOBuf
// deleter calls IouringMemPool::Deallocate(buf), which is thread-safe.
// ---------------------------------------------------------------------------
struct IouringReadSlot {
    void*  buf;        // pointer to the block (from IouringMemPool)
    int    buf_index;  // pre-registered iovec index for this block
    size_t size;       // block size in bytes
};

// ---------------------------------------------------------------------------
// IouringMemPool
//
// Global memory pool for IOBuf blocks.  Must be initialised once (via Init)
// before any IOBuf is allocated.
//
// Usage:
//   // At startup (before any IOBuf allocation):
//   IouringMemPool::Instance().Init(block_size);
//
//   // Read path – allocate a block for READ_FIXED:
//   void* buf = IouringMemPool::Instance().Allocate(block_size);
//   int   idx = IouringMemPool::Instance().GetBufIndex(ring, buf);
//   // submit IORING_OP_READ_FIXED(buf, idx)
//   // On CQE: IOBuf::append_user_data(buf, res, IouringMemPool::Deallocate)
//
//   // Write path – for each IOBuf block:
//   int buf_index = IouringMemPool::Instance().GetBufIndex(ring, block_ptr);
//   if (buf_index >= 0)
//       // submit IORING_OP_WRITE_FIXED(block_ptr, buf_index)
//   else
//       // block is not in a registered region; log and fail
//       // (no WRITEV fallback in fixed-buffer mode)
// ---------------------------------------------------------------------------

// Callback invoked whenever a new region is allocated, once per registered
// ring.  The implementation should call io_uring_register_buffers_update()
// (or equivalent) to pin the new pages in the ring's registered-buffer table.
// |buf_index_base| is the starting index for this region and is identical
// across all rings (all rings share the same iovec layout).
using RegionRegisterCb = std::function<void(void* base, size_t size,
                                            size_t block_size,
                                            int    buf_index_base)>;

class IouringMemPool {
public:
    static IouringMemPool& Instance();

    // Must be called once before any IOBuf allocation.
    // |block_size|: size of each IOBuf block (== iouring_iobuf_block_size).
    // After Init(), butil::iobuf::blockmem_allocate is replaced with
    // MemPoolAllocate and blockmem_deallocate with MemPoolDeallocate.
    bool Init(size_t block_size);

    void Destroy();

    // Register a callback invoked for each new region on each ring.
    // Multiple rings can register; all are called in AddRegion().
    void AddRingRegistrar(struct io_uring* ring, RegionRegisterCb cb);
    void RemoveRingRegistrar(struct io_uring* ring);

    // Allocate one block of block_size_ bytes from registered memory.
    // Called via blockmem_allocate hook, and directly from SubmitRead.
    void* Allocate(size_t size);

    // Return a block to the pool.
    // Called via blockmem_deallocate hook, and from the IOBuf zero-copy deleter.
    void Deallocate(void* ptr);

    // Look up the buf_index for a pointer inside a registered region.
    // Returns -1 if |ptr| is not in any registered region.
    //
    // Hot path: uses a lock-free snapshot of region_count_ so that the
    // common case (ptr found in an existing region) avoids any mutex.
    // A concurrent AddRegion publishes the new count only AFTER the Region
    // entry is fully initialised, so reading a stale count simply means we
    // miss the newest region and return -1 (caller must treat this as an
    // error in fixed-buffer mode; no WRITEV fallback exists).
    int GetBufIndex(struct io_uring* ring, const void* ptr) const;

    bool initialized() const { return initialized_; }
    size_t block_size() const { return block_size_; }

private:
    IouringMemPool() = default;
    ~IouringMemPool() { Destroy(); }

    // Grow by allocating and registering a new region.
    // Called under extend_lock_.
    bool AddRegion(size_t region_size_mb);

    struct Region {
        uintptr_t base;
        size_t    size;           // total bytes
        size_t    block_size;     // bytes per block in this region
        // buf_index_base is the same for every ring (io_uring assigns indices
        // in a global namespace per ring, but all rings use the same iovec
        // layout: region 0 gets [0, N0), region 1 gets [N0, N0+N1), …).
        // Storing it once eliminates the per-ring inner loop in GetBufIndex.
        int       buf_index_base; // first buf_index of this region (all rings)
        int       block_count;    // aligned_size / block_size (cached)
    };

    struct FreeNode {
        FreeNode* next;
    };

    // TLS free-list for the hot (lock-free) allocation path.
    static __thread FreeNode* tls_free_;
    static __thread size_t    tls_free_cnt_;

    static void* MemPoolAllocate(size_t size);
    static void  MemPoolDeallocate(void* ptr);

    // Refill TLS from bucket |b|.  Returns number of blocks moved.
    size_t RefillTlsFromBucket(int b);

    // Previous allocator hooks (saved at Init time for fallback).
    void* (*prev_allocate_)(size_t)  = nullptr;
    void  (*prev_deallocate_)(void*) = nullptr;

    bool   initialized_ = false;
    size_t block_size_  = 0;

    // Global free-list split into num_free_buckets_ independent buckets to
    // reduce lock contention when multiple threads flush/refill their TLS
    // caches concurrently.  Configured at Init() time from
    // --iouring_mem_pool_free_buckets.
    struct BAIDU_CACHELINE_ALIGNMENT FreeBucket {
        butil::Mutex lock;
        FreeNode*    head = nullptr;
    };
    int                                   num_free_buckets_ = 0;
    std::unique_ptr<FreeBucket[]>         free_buckets_;

    // Per-thread TLS cache capacity.  Configured at Init() time from
    // --iouring_mem_pool_tls_cache_num.
    size_t                                tls_cache_max_ = 0;

    // Region table (protected by extend_lock_).
    // region_count_ is a lock-free snapshot: written by AddRegion (under
    // extend_lock_) AFTER the new Region is pushed, read by GetBufIndex
    // without any lock for the fast path.
    mutable butil::Mutex      extend_lock_;
    std::vector<Region>       regions_;
    butil::atomic<int>        region_count_{0};

    // Per-ring callbacks (protected by registrar_lock_).
    butil::Mutex              registrar_lock_;
    std::vector<struct io_uring*>  reg_rings_;
    std::vector<RegionRegisterCb>  reg_cbs_;

    DISALLOW_COPY_AND_ASSIGN(IouringMemPool);
};

// ---------------------------------------------------------------------------
// Convenience helpers
// ---------------------------------------------------------------------------

// True when --iouring_register_buffers=true.
bool IsFixedBuffersEnabled();

// Look up buf_index for a ptr in the given ring's registration table.
// Returns -1 if ptr is not in a registered region (caller must treat as error).
inline int GetWriteBufIndex(struct io_uring* ring, const void* ptr) {
    return IouringMemPool::Instance().GetBufIndex(ring, ptr);
}

}  // namespace iouring
}  // namespace brpc

#else  // !BRPC_WITH_IOURING

namespace brpc {
namespace iouring {
inline bool IsFixedBuffersEnabled() { return false; }
}
}

#endif  // BRPC_WITH_IOURING
#endif  // BRPC_IOURING_BLOCK_POOL_H
