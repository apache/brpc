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

#ifndef BRPC_RDMA_SRQ_RECEIVE_POOL_H
#define BRPC_RDMA_SRQ_RECEIVE_POOL_H

#if BRPC_WITH_RDMA
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <infiniband/verbs.h>
#include "butil/atomicops.h"
#include "butil/compiler_specific.h"
#include "butil/iobuf.h"

namespace brpc {
namespace rdma {

// Bounded receive pool with regions registered on demand. The worker owns one
// reference and each outstanding physical block owns another, regardless of how
// many IOBuf slices share it. An in-flight growth task also retains the pool.
// The last BlockRef returns its descriptor without allocating a wrapper or
// queue node. Only the receive owner allocates blocks or adopts prepared regions.
// Blocks may be released on any thread.
class SrqReceivePool {
    struct Region;
    struct Growth;

public:
    class Buffer : private butil::IOBuf {
    public:
        Buffer() = default;
        void* data() const;
        uint32_t capacity() const;
        uint32_t lkey() const;
        void clear();
        // Returns an IOBuf owning the received prefix and discards the unused
        // tail. Receive DMA must be complete before calling this method or
        // clear.
        butil::IOBuf TakeReceived(size_t bytes);

    private:
    friend class SrqReceivePool;
        void Initialize();
        // Owning pool. Each outstanding physical block retains one pool
        // reference.
        SrqReceivePool* _pool{nullptr};
        // Stable region metadata. Only the owner updates its free list and
        // in-use count, after detaching this descriptor from the return stack.
        Region* _region{nullptr};
        // Block allocation, including the IOBuf::Block header constructed in
        // place.
        void* _memory{nullptr};
        // Intrusive link for the owner free list or the cross-thread return
        // stack.
        Buffer* _next{nullptr};
        // Region lkey. Growth changes neither existing block addresses nor
        // lkeys.
        uint32_t _lkey{0};
    };

    // block_count caps total capacity. Initialization registers initial_blocks
    // synchronously. Later growth prepares grow_blocks on a background bthread.
    // A zero initial size defaults to grow_blocks. A zero growth size defaults
    // to block_count, preserving fixed-arena callers.
    static SrqReceivePool* Create(ibv_pd* pd, size_t block_size,
                                 uint32_t block_count, uint32_t grow_blocks = 0,
                                 uint32_t initial_blocks = 0);

    DISALLOW_COPY_AND_ASSIGN(SrqReceivePool);

    // Never registers memory synchronously. EAGAIN means no blocks are ready
    // and growth is pending or waiting for a retry. ENOMEM means no blocks are
    // ready and the capacity limit is reached. Returned blocks are adopted on
    // the next owner pass without waking it. Failed allocation or registration
    // notifies after backoff.
    Buffer* Acquire();
    // Owner only: absorb returns and completed growth, then prefetch a region
    // below the low watermark.
    // `reusable` counts unexported leases the worker can repost without `Acquire()`.
    void Prepare(uint32_t reusable = 0);
    // Owner only: move cross-thread returns into the local free lists.
    void DrainReturned();
    // Owner only: stop requesting growth and drop the worker reference after
    // all DMA has stopped. A running growth task and exported IOBufs keep the
    // pool, eventfd, descriptors and MRs alive through their final accesses.
    void Release();

    // O(1), owner only: returns count after DrainReturned, prepared regions after
    // Prepare. Diagnostics temporarily exclude either kind of pending result.
    uint32_t available() const { return _available; }
    uint32_t block_count() const { return _block_count; }
    size_t block_size() const { return _block_size; }
    int growth_fd() const { return _growth_fd; }

    // Consulted when the ordinary IOBuf pool does not recognize an address. The
    // caller must retain a live Block reference or be releasing its final
    // reference.
    static uint32_t RegionId(const void* address);
    static bool Recycle(void* block);
    static size_t RegisteredBytes();
    static size_t LivePools();
    // Retry failed MR deregistrations, retaining pages until success. Returns
    // the number of regions still awaiting deregistration.
    static size_t RetryDeregistrations();

private:
friend struct std::default_delete<SrqReceivePool>;

    SrqReceivePool() = default;
    ~SrqReceivePool();
    // C++14 ordinary new need not honor over-aligned members. Keep allocation
    // and deletion paired even in builds without aligned-new support.
    static void* operator new(size_t bytes);
    static void operator delete(void* memory);
    std::unique_ptr<Region> PrepareRegion(uint32_t count);
    void AddRegion(std::unique_ptr<Region> region);
    void AdoptGrowth();
    void RequestGrowth(uint32_t reusable = 0);
    static void* RunGrowth(void* arg);
    void NotifyGrowth();
    void Return(Buffer* buffer);
    void ReleaseReference();

    // Shared cache line: one owner reference plus one per outstanding block
    // and one per growth task. No descriptor may be touched after publication.
    BAIDU_CACHELINE_ALIGNMENT butil::atomic<uint64_t> _references{1};
    // Atomic return stack. Any thread releasing a block can publish a
    // descriptor.
    butil::atomic<Buffer*> _returned{nullptr};
    // Fully prepared growth result. A release/acquire handoff transfers region
    // ownership. A null region in the result completes a failed growth attempt.
    butil::atomic<Growth*> _completed_growth{nullptr};
    // Checked before registration. A task racing owner release may still
    // register its region. Its reference retains the pool through cleanup.
    butil::atomic<bool> _stopped{false};

    // Owner cache line: confirmed free blocks, excluding the return stack.
    BAIDU_CACHELINE_ALIGNMENT uint32_t _available{0};
    // Min-heap of nonempty regions, ordered by creation index. Within a region
    // blocks remain LIFO. No allocation or region scan is needed per block.
    std::vector<Region*> _free_regions;
    // Registered regions and stable descriptors, reclaimed together after the
    // final pool reference is released.
    std::vector<std::unique_ptr<Region>> _regions;
    // Protection domain shared by all regions. Must outlive the pool.
    ibv_pd* _pd{nullptr};
    // Physical block size, including the IOBuf::Block header.
    size_t _block_size{0};
    // Maximum number of physical blocks the pool may allocate.
    uint32_t _block_count{0};
    // Target blocks per growth step, capped by the remaining capacity.
    uint32_t _grow_blocks{0};
    // Physical blocks allocated so far. Only the owner modifies this value.
    uint32_t _allocated_blocks{0};
    // Includes allocation/registration backoff and a result not yet adopted.
    // This prevents overlapping allocations or exceeding the capacity limit.
    bool _growing{false};
    // Immutable allocation/registration retry interval. A running task waits
    // before notifying the owner on failure.
    int64_t _growth_retry_us{0};
    // Optional THP hint. Failure to obtain huge pages keeps ordinary pages.
    bool _hugepages{false};
    // Registration policy for this pool's PD. Initialization and then the sole
    // growth task access it. Completion publication orders consecutive tasks.
    bool _try_relaxed_order{true};
    // Nonblocking eventfd for growth completion, including failure after backoff.
    // Block returns only publish to `_returned` and never notify the owner.
    int _growth_fd{-1};
    // Whether this pool has been published and counted as live.
    bool _registered{false};
};

}  // namespace rdma
}  // namespace brpc
#endif  // BRPC_WITH_RDMA
#endif  // BRPC_RDMA_SRQ_RECEIVE_POOL_H
