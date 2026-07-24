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

#if BRPC_WITH_IOURING

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#include <gflags/gflags.h>
#include <liburing.h>

#include "butil/errno.h"          // berror()
#include "butil/fast_rand.h"       // butil::fast_rand()
#include "butil/logging.h"
#include "butil/scoped_lock.h"      // BAIDU_SCOPED_LOCK
#include "butil/iobuf.h"          // butil::iobuf::blockmem_allocate
#include "bvar/bvar.h"             // bvar::Adder, bvar::PerSecond, bvar::PassiveStatus
#include "brpc/iouring/iouring_block_pool.h"

// iobuf internal hooks – declared in iobuf.cpp / iobuf_inl.h
namespace butil {
namespace iobuf {
extern void* (*blockmem_allocate)(size_t);
extern void  (*blockmem_deallocate)(void*);
}
}

namespace brpc {
namespace iouring {

// ---------------------------------------------------------------------------
// bvar metrics for IouringMemPool allocation paths
// ---------------------------------------------------------------------------
//
// iouring_mem_pool_tls_alloc_count    – cumulative TLS-hit allocations
// iouring_mem_pool_global_alloc_count – cumulative global-bucket-hit allocations
//                                       (TLS miss, global bucket had blocks)
// iouring_mem_pool_pool_grow_count    – cumulative pool-grow events
//                                       (TLS miss + all buckets empty → AddRegion)
// iouring_mem_pool_tls_alloc_second   – TLS-hit allocations per second
// iouring_mem_pool_global_alloc_second– global-bucket allocations per second
// iouring_mem_pool_tls_hit_rate       – ratio tls_alloc / (tls_alloc + global_alloc)
//                                       over the lifetime of the process.
//
// During normal operation tls_hit_rate should be close to 1.0 and
// global_alloc_second should be near 0.  A sustained non-zero
// global_alloc_second or a hit-rate below ~0.99 indicates the TLS cache
// is too small (--iouring_mem_pool_tls_cache_max) or the pool needs more
// initial memory (--iouring_mem_pool_initial_mb).

static bvar::Adder<int64_t> g_tls_alloc_count;
static bvar::Adder<int64_t> g_global_alloc_count;
static bvar::Adder<int64_t> g_pool_grow_count;

static bvar::PerSecond<bvar::Adder<int64_t>> g_tls_alloc_per_second(
    "iouring_mem_pool_tls_alloc_second",    &g_tls_alloc_count);
static bvar::PerSecond<bvar::Adder<int64_t>> g_global_alloc_per_second(
    "iouring_mem_pool_global_alloc_second", &g_global_alloc_count);

// Named Adder bvars so the raw counters are also accessible by name.
static bvar::Window<bvar::Adder<int64_t>> g_tls_alloc_window(
    "iouring_mem_pool_tls_alloc_count",    &g_tls_alloc_count,    0);
static bvar::Window<bvar::Adder<int64_t>> g_global_alloc_window(
    "iouring_mem_pool_global_alloc_count", &g_global_alloc_count, 0);
static bvar::Window<bvar::Adder<int64_t>> g_pool_grow_window(
    "iouring_mem_pool_pool_grow_count",    &g_pool_grow_count,    0);

// PassiveStatus that computes the lifetime TLS hit-rate on demand.
static double GetTlsHitRate(void*) {
    const int64_t tls    = g_tls_alloc_count.get_value();
    const int64_t global = g_global_alloc_count.get_value();
    const int64_t total  = tls + global;
    return total > 0 ? static_cast<double>(tls) / static_cast<double>(total)
                     : 1.0;
}
static bvar::PassiveStatus<double> g_tls_hit_rate(
    "iouring_mem_pool_tls_hit_rate", GetTlsHitRate, nullptr);

// ---------------------------------------------------------------------------
// gflags
// ---------------------------------------------------------------------------

DEFINE_bool(iouring_register_buffers, false,
            "Enable io_uring pre-registered buffer I/O (READ_FIXED / "
            "WRITE_FIXED).  When true, all IOBuf blocks are allocated from a "
            "registered slab so that writes can use IORING_OP_WRITE_FIXED "
            "with no per-op page pinning, and reads use IORING_OP_READ_FIXED. "
            "Requires kernel >= 5.1.");

DEFINE_int32(iouring_mem_pool_initial_mb, 256,
             "Initial size of the io_uring fixed-buffer memory pool (MB). "
             "Effective only with --iouring_register_buffers=true.");

DEFINE_int32(iouring_mem_pool_increase_mb, 256,
             "Growth increment when the pool is exhausted (MB). "
             "Effective only with --iouring_register_buffers=true.");

DEFINE_int32(iouring_mem_pool_max_regions, 8,
             "Maximum number of memory regions. "
             "Each region causes one io_uring_register_buffers_update() per "
             "ring on growth.");

DEFINE_int32(iouring_iobuf_block_size, 8192,
             "Size of each IOBuf block when --iouring_register_buffers=true. "
             "butil::SetDefaultBlockSize() is called with this value at "
             "startup so IOBuf and the registered slab are always in sync.");

DEFINE_int32(iouring_mem_pool_free_buckets, 8,
             "Number of independent global free-list buckets in the io_uring "
             "fixed-buffer memory pool.  More buckets reduce mutex contention "
             "when many threads flush/refill their TLS caches concurrently. "
             "Must be in [1, 64].  Takes effect only at pool initialisation "
             "(--iouring_register_buffers=true).");

DEFINE_int32(iouring_mem_pool_tls_cache_num, 128,
             "Maximum number of blocks cached per thread in the TLS free-list "
             "of the io_uring fixed-buffer memory pool.  Larger values reduce "
             "trips to the global bucket at the cost of per-thread memory "
             "overhead (each block is iouring_iobuf_block_size bytes). "
             "Must be in [1, 4096].  Takes effect only at pool initialisation "
             "(--iouring_register_buffers=true).");

// ---------------------------------------------------------------------------
bool IsFixedBuffersEnabled() { return FLAGS_iouring_register_buffers; }

// ---------------------------------------------------------------------------
// IouringMemPool – implementation
// ---------------------------------------------------------------------------

__thread IouringMemPool::FreeNode* IouringMemPool::tls_free_     = nullptr;
__thread size_t                    IouringMemPool::tls_free_cnt_ = 0;

static const size_t kBytesPerMB  = 1UL << 20;

// Validated bounds for the two new gflags.
static const int kMinFreeBuckets   = 1;
static const int kMaxFreeBuckets   = 64;
static const int kMinTlsCacheNum   = 1;
static const int kMaxTlsCacheNum   = 4096;

IouringMemPool& IouringMemPool::Instance() {
    static IouringMemPool inst;
    return inst;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
bool IouringMemPool::Init(size_t block_size) {
    if (initialized_) {
        LOG(WARNING) << "IouringMemPool already initialized";
        return true;
    }
    if (block_size == 0 || block_size % 4096 != 0) {
        LOG(ERROR) << "IouringMemPool::Init: block_size must be a nonzero "
                      "multiple of 4096, got " << block_size;
        return false;
    }

    // Validate and apply free_buckets gflag.
    int nbuckets = FLAGS_iouring_mem_pool_free_buckets;
    if (nbuckets < kMinFreeBuckets || nbuckets > kMaxFreeBuckets) {
        LOG(WARNING) << "iouring_mem_pool_free_buckets (" << nbuckets
                     << ") out of [" << kMinFreeBuckets << ","
                     << kMaxFreeBuckets << "], clamped to 8";
        nbuckets = 8;
    }
    num_free_buckets_ = nbuckets;
    free_buckets_.reset(new (std::nothrow) FreeBucket[num_free_buckets_]);
    if (!free_buckets_) {
        LOG(ERROR) << "IouringMemPool::Init: failed to allocate bucket array";
        return false;
    }

    // Validate and apply tls_cache_num gflag.
    int tls_cache = FLAGS_iouring_mem_pool_tls_cache_num;
    if (tls_cache < kMinTlsCacheNum || tls_cache > kMaxTlsCacheNum) {
        LOG(WARNING) << "iouring_mem_pool_tls_cache_num (" << tls_cache
                     << ") out of [" << kMinTlsCacheNum << ","
                     << kMaxTlsCacheNum << "], clamped to 128";
        tls_cache = 128;
    }
    tls_cache_max_ = static_cast<size_t>(tls_cache);

    block_size_ = block_size;

    // Hook IOBuf's block allocator.
    prev_allocate_   = butil::iobuf::blockmem_allocate;
    prev_deallocate_ = butil::iobuf::blockmem_deallocate;
    butil::iobuf::blockmem_allocate   = MemPoolAllocate;
    butil::iobuf::blockmem_deallocate = MemPoolDeallocate;

    // Allocate the initial region.
    if (!AddRegion(static_cast<size_t>(FLAGS_iouring_mem_pool_initial_mb))) {
        // Unhook on failure.
        butil::iobuf::blockmem_allocate   = prev_allocate_;
        butil::iobuf::blockmem_deallocate = prev_deallocate_;
        return false;
    }

    initialized_ = true;
    LOG(INFO) << "IouringMemPool ready: block_size=" << block_size_
              << " initial_mb=" << FLAGS_iouring_mem_pool_initial_mb
              << " free_buckets=" << num_free_buckets_
              << " tls_cache_num=" << tls_cache_max_;
    return true;
}

// ---------------------------------------------------------------------------
// Destroy
// ---------------------------------------------------------------------------
void IouringMemPool::Destroy() {
    if (!initialized_) { return; }

    butil::iobuf::blockmem_allocate   = prev_allocate_;
    butil::iobuf::blockmem_deallocate = prev_deallocate_;

    {
        BAIDU_SCOPED_LOCK(extend_lock_);
        for (auto& r : regions_) {
            free(reinterpret_cast<void*>(r.base));
        }
        regions_.clear();
    }
    for (int i = 0; i < num_free_buckets_; ++i) {
        BAIDU_SCOPED_LOCK(free_buckets_[i].lock);
        free_buckets_[i].head = nullptr;
    }
    free_buckets_.reset();
    num_free_buckets_ = 0;
    tls_cache_max_    = 0;
    initialized_ = false;
}

// ---------------------------------------------------------------------------
// AddRingRegistrar / RemoveRingRegistrar
// ---------------------------------------------------------------------------
void IouringMemPool::AddRingRegistrar(struct io_uring* ring, RegionRegisterCb cb) {
    BAIDU_SCOPED_LOCK(registrar_lock_);
    // Register all existing regions with the new ring using the already-computed
    // buf_index_base (identical for every ring).
    {
        BAIDU_SCOPED_LOCK(extend_lock_);
        for (const auto& r : regions_) {
            cb(reinterpret_cast<void*>(r.base), r.size, r.block_size,
               r.buf_index_base);
        }
    }
    reg_rings_.push_back(ring);
    reg_cbs_.push_back(std::move(cb));
}

void IouringMemPool::RemoveRingRegistrar(struct io_uring* ring) {
    BAIDU_SCOPED_LOCK(registrar_lock_);
    for (size_t i = 0; i < reg_rings_.size(); ++i) {
        if (reg_rings_[i] == ring) {
            reg_rings_.erase(reg_rings_.begin() + i);
            reg_cbs_.erase(reg_cbs_.begin()   + i);
            break;
        }
    }
    // No per-region ring state to clean up: buf_index_base is ring-agnostic.
}

// ---------------------------------------------------------------------------
// AddRegion
// Must be called under extend_lock_ (but NOT under free_lock_ or
// registrar_lock_, which are acquired internally in the correct order).
//
// Lock ordering enforced throughout IouringMemPool:
//   extend_lock_  (coarsest – serialises region growth)
//     registrar_lock_  (ring registration callbacks)
//       free_lock_  (free-list access – fine-grained, brief)
// ---------------------------------------------------------------------------
bool IouringMemPool::AddRegion(size_t region_size_mb) {
    // Must be called under extend_lock_ (caller's responsibility).
    if (static_cast<int>(regions_.size()) >= FLAGS_iouring_mem_pool_max_regions) {
        LOG_EVERY_SECOND(ERROR)
            << "IouringMemPool: max regions (" << FLAGS_iouring_mem_pool_max_regions
            << ") reached.  Increase --iouring_mem_pool_max_regions.";
        return false;
    }

    const size_t region_size = region_size_mb * kBytesPerMB;
    // Round down to a multiple of block_size_.
    const size_t aligned_size = (region_size / block_size_) * block_size_;
    if (aligned_size == 0) {
        LOG(ERROR) << "IouringMemPool: region_size_mb too small";
        return false;
    }

    void* mem = nullptr;
    if (posix_memalign(&mem, 4096, aligned_size) != 0) {
        PLOG(ERROR) << "IouringMemPool: posix_memalign failed";
        return false;
    }
    memset(mem, 0, aligned_size);

    // Compute buf_index_base: sum of blocks in all existing regions.
    // Safe to read regions_ here because we hold extend_lock_.
    // The same value is used for every ring (all rings share the same
    // iovec layout), so we only need to store it once per Region.
    int buf_index_base = 0;
    for (const auto& r : regions_) {
        buf_index_base += r.block_count;
    }

    const int blocks = static_cast<int>(aligned_size / block_size_);

    Region region;
    region.base            = reinterpret_cast<uintptr_t>(mem);
    region.size            = aligned_size;
    region.block_size      = block_size_;
    region.buf_index_base  = buf_index_base;
    region.block_count     = blocks;

    // Notify all registered rings (registrar_lock_ < extend_lock_ in the
    // global ordering, but here extend_lock_ is already held, so we must NOT
    // acquire extend_lock_ inside registrar_lock_ elsewhere).
    {
        BAIDU_SCOPED_LOCK(registrar_lock_);
        for (size_t i = 0; i < reg_rings_.size(); ++i) {
            reg_cbs_[i](mem, aligned_size, block_size_, buf_index_base);
        }
    }

    regions_.push_back(std::move(region));
    // Publish the new region count atomically so GetBufIndex lock-free readers
    // can discover the new region without holding extend_lock_.
    region_count_.store(static_cast<int>(regions_.size()),
                        butil::memory_order_release);

    // Populate the global free-list buckets with all blocks in the new region.
    // Distribute blocks evenly across buckets (block i -> bucket i % num_free_buckets_).
    // Acquire all bucket locks at once to avoid repeated lock/unlock per block.
    for (int b = 0; b < num_free_buckets_; ++b) {
        free_buckets_[b].lock.lock();
    }
    for (int i = blocks - 1; i >= 0; --i) {
        auto* node = reinterpret_cast<FreeNode*>(
            reinterpret_cast<char*>(mem) + i * block_size_);
        const int b = i % num_free_buckets_;
        node->next              = free_buckets_[b].head;
        free_buckets_[b].head  = node;
    }
    for (int b = 0; b < num_free_buckets_; ++b) {
        free_buckets_[b].lock.unlock();
    }

    LOG(INFO) << "IouringMemPool: added region base=" << mem
              << " size_mb=" << region_size_mb
              << " blocks=" << blocks
              << " buf_index_base=" << buf_index_base;
    return true;
}

// ---------------------------------------------------------------------------
// Allocate / Deallocate
// ---------------------------------------------------------------------------

// Helper: pick a random bucket index.
static inline int RandomBucket(int num_buckets) {
    return static_cast<int>(butil::fast_rand() % static_cast<uint64_t>(num_buckets));
}

// Helper: hash a pointer to a bucket index.
// Using address bits 3..6 (skip the low 3 tag bits of most allocators)
// distributes consecutive blocks across buckets.
static inline int PtrBucket(const void* ptr, int num_buckets) {
    return static_cast<int>(
        (reinterpret_cast<uintptr_t>(ptr) >> 3) %
        static_cast<uintptr_t>(num_buckets));
}

// Refill TLS from a specific bucket.  Returns number of blocks moved.
// Caller must NOT hold bucket lock.
size_t IouringMemPool::RefillTlsFromBucket(int b) {
    BAIDU_SCOPED_LOCK(free_buckets_[b].lock);
    if (!free_buckets_[b].head) { return 0; }
    size_t moved = 0;
    const size_t target = tls_cache_max_ / 2;
    while (free_buckets_[b].head && moved < target) {
        FreeNode* node        = free_buckets_[b].head;
        free_buckets_[b].head = node->next;
        node->next            = tls_free_;
        tls_free_             = node;
        ++tls_free_cnt_;
        ++moved;
    }
    return moved;
}

// TLS fast-path: no lock needed.
void* IouringMemPool::Allocate(size_t /*size*/) {
    // TLS fast-path: no contention.
    if (tls_free_) {
        FreeNode* node = tls_free_;
        tls_free_ = node->next;
        --tls_free_cnt_;
        g_tls_alloc_count << 1;
        return node;
    }

    // TLS cache is empty.  Try to refill from a random global bucket.
    // If that bucket is also empty, try all remaining buckets before growing.
    // -----------------------------------------------------------------------
    // Lock ordering: extend_lock_ (coarsest) → bucket lock (finest).
    // We must NEVER hold a bucket lock while acquiring extend_lock_.
    // -----------------------------------------------------------------------

    // Step 1: try a random bucket first (avoids always hammering bucket 0).
    const int start = RandomBucket(num_free_buckets_);
    for (int i = 0; i < num_free_buckets_; ++i) {
        const int b = (start + i) % num_free_buckets_;
        if (RefillTlsFromBucket(b) > 0) {
            g_global_alloc_count << 1;
            goto done;
        }
    }

    // Step 2: all buckets empty — try to grow the pool.
    {
        BAIDU_SCOPED_LOCK(extend_lock_);  // serialize growth
        // Re-check under extend_lock_: another thread may have grown already.
        bool need_grow = true;
        for (int b = 0; b < num_free_buckets_ && need_grow; ++b) {
            BAIDU_SCOPED_LOCK(free_buckets_[b].lock);
            if (free_buckets_[b].head != nullptr) { need_grow = false; }
        }
        if (need_grow) {
            g_pool_grow_count << 1;
            if (!AddRegion(
                    static_cast<size_t>(FLAGS_iouring_mem_pool_increase_mb))) {
                LOG_EVERY_SECOND(ERROR)
                    << "IouringMemPool: out of memory, cannot grow.";
                return nullptr;
            }
        }
    }  // extend_lock_ released here

    // Step 3: steal blocks from the now-populated buckets.
    {
        bool refilled = false;
        for (int i = 0; i < num_free_buckets_; ++i) {
            const int b = (start + i) % num_free_buckets_;
            if (RefillTlsFromBucket(b) > 0) {
                refilled = true;
                break;
            }
        }
        if (refilled) {
            g_global_alloc_count << 1;
        }
    }

done:
    if (!tls_free_) { return nullptr; }
    FreeNode* node = tls_free_;
    tls_free_ = node->next;
    --tls_free_cnt_;
    return node;
}

void IouringMemPool::Deallocate(void* ptr) {
    if (!ptr) { return; }

    // TLS fast-path: cache locally.
    if (tls_free_cnt_ < tls_cache_max_) {
        auto* node = reinterpret_cast<FreeNode*>(ptr);
        node->next = tls_free_;
        tls_free_  = node;
        ++tls_free_cnt_;
        return;
    }

    // TLS is full: flush half to the global bucket determined by the
    // address of each block being flushed, so they consistently return to
    // the same bucket (improves locality in future allocations).
    const size_t flush = tls_cache_max_ / 2;
    for (size_t i = 0; i < flush && tls_free_; ++i) {
        FreeNode* node = tls_free_;
        tls_free_ = node->next;
        --tls_free_cnt_;
        const int b = PtrBucket(node, num_free_buckets_);
        BAIDU_SCOPED_LOCK(free_buckets_[b].lock);
        node->next            = free_buckets_[b].head;
        free_buckets_[b].head = node;
    }
    // Then put the current block into TLS.
    auto* node = reinterpret_cast<FreeNode*>(ptr);
    node->next = tls_free_;
    tls_free_  = node;
    ++tls_free_cnt_;
}

// Static hooks for butil::iobuf.
void* IouringMemPool::MemPoolAllocate(size_t size) {
    return Instance().Allocate(size);
}
void IouringMemPool::MemPoolDeallocate(void* ptr) {
    Instance().Deallocate(ptr);
}

// ---------------------------------------------------------------------------
// GetBufIndex
//
// Hot path: called once per IOBuf block in CutFromIOBufList.
//
// Lock-free fast path: read region_count_ (acquire) to get a stable count,
// then walk regions_[0..count-1] without a lock.  This is safe because:
//   - regions_ only ever grows (entries are never removed or modified once
//     published).
//   - AddRegion stores to region_count_ with memory_order_release AFTER
//     pushing the new entry, so if we observe count == N we can safely
//     read regions_[0..N-1] without tearing.
//   - If AddRegion is running concurrently and we read a stale count we
//     simply miss the new region and return -1.  In fixed-buffer mode the
//     caller treats -1 as a hard error (no WRITEV fallback exists).
//
// Slow path (ring lookup): once the region is found we still need to find
// the ring's buf_index_base, which lives in per-region parallel vectors.
// These are also append-only for a given region once published, so they
// are safe to read without locks after the region is visible.
// ---------------------------------------------------------------------------
int IouringMemPool::GetBufIndex(struct io_uring* ring, const void* ptr) const {
    if (!ptr) { return -1; }
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

    // Load the count with acquire semantics so we see all stores from
    // AddRegion that preceded the region_count_ store.
    const int count = region_count_.load(butil::memory_order_acquire);

    for (int ri = 0; ri < count; ++ri) {
        const Region& r = regions_[ri];
        if (addr < r.base || addr >= r.base + r.size) { continue; }

        // Found the containing region.  buf_index_base is identical for every
        // ring, so no per-ring lookup is needed — O(1) direct computation.
        const int block_offset =
            static_cast<int>((addr - r.base) / r.block_size);
        (void)ring;  // ring param kept for API compatibility / future use
        return r.buf_index_base + block_offset;
    }
    return -1;
}

}  // namespace iouring
}  // namespace brpc

#endif  // BRPC_WITH_IOURING
