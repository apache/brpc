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

#if BRPC_WITH_RDMA
#include "brpc/rdma/srq_receive_pool.h"

#include <algorithm>
#include <errno.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>
#include <map>
#include <new>
#include <vector>
#include <gflags/gflags.h>
#include "bthread/bthread.h"
#include "bthread/mutex.h"
#include "butil/containers/doubly_buffered_data.h"
#include "butil/iobuf_inl.h"
#include "butil/logging.h"
#include "butil/scoped_lock.h"

namespace brpc {
namespace rdma {
extern ibv_mr* (*IbvRegMr)(ibv_pd*, void*, size_t, int);
extern int (*IbvDeregMr)(ibv_mr*);

DEFINE_int32(rdma_srq_pool_grow_blocks, 256,
             "Receive blocks per runtime SRQ pool growth step");
DEFINE_int32(rdma_srq_pool_growth_retry_ms, 100,
             "Delay before retrying failed SRQ pool allocation/registration");
DEFINE_bool(rdma_srq_pool_hugepages, false,
            "Align SRQ receive regions to 2 MiB and request transparent huge pages");

// Each region owns an MR, page-aligned memory and a descriptor array. Growth
// adds regions without relocating existing storage, keeping posted receive
// buffers and application-held zero-copy addresses valid.
struct SrqReceivePool::Region {
    // Registration handle. Destruction retains pages if deregistration fails.
    ibv_mr* mr{nullptr};
    // Page-aligned allocation, moved to global quarantine if deregistration
    // fails.
    void* memory{nullptr};
    // Receive descriptors with stable addresses throughout the region lifetime.
    std::unique_ptr<Buffer[]> buffers;
    // Number of physical blocks in this region.
    uint32_t count{0};
    // Registered extent (alignment padding, if any, is not registered).
    size_t bytes{0};
    // Owner-only allocation order and usage. Returns remain in use until the
    // owner detaches them, even if the final application reference is gone.
    size_t index{0};
    uint32_t in_use{0};
    Buffer* free{nullptr};
    // Whether destruction must first remove the address index entry.
    bool published{false};

    static bool Later(const Region* lhs, const Region* rhs) {
        return lhs->index > rhs->index;
    }

    ~Region();
};

struct SrqReceivePool::Growth {
    // Retained by the task through result publication and eventfd notification.
    SrqReceivePool* pool{nullptr};
    // Reserved against the owner's remaining capacity before task launch.
    uint32_t count{0};
    // Null after failure. Even a failure publishes a result to end _growing.
    std::unique_ptr<Region> region;
};

namespace {
struct PoolRegistry {
    struct LiveRegion {
        // The outstanding block keeps the pool alive until Return releases its
        // reference.
        SrqReceivePool* pool;
        // Descriptor array, indexed by the returned block offset within the
        // region.
        SrqReceivePool::Buffer* buffers;
        // Region extent. Excludes adjacent addresses from pool lookups.
        size_t bytes;
        // Registration key used by the IOBuf send path.
        uint32_t lkey;
    };

    struct RetiredRegion {
        // MR whose failed deregistration requires an explicit retry.
        ibv_mr* mr;
        // Pages retained until the provider confirms successful deregistration.
        void* memory;
        // Bytes still registered with the provider.
        size_t bytes;
    };

    // Lookups may point inside a region, so upper_bound must find its
    // predecessor. butil::FlatMap supports exact keys only and cannot directly
    // replace this ordered interval index.
    using RegionMap = std::map<uintptr_t, LiveRegion>;
    // Protects only byte accounting and failed deregistrations, not index
    // access or verbs calls.
    bthread::Mutex mutex;
    // Maps region bases to lkeys and descriptors. Readers lock only their own
    // pthread TLS wrapper. Removing an index entry waits for old readers before
    // descriptors or MRs can be reclaimed. Never suspend a bthread inside a
    // read section. Modify serializes writers internally and does not need the
    // accounting mutex above.
    butil::DoublyBufferedData<RegionMap> regions;
    // Quarantined allocations retained after failed deregistration.
    std::vector<RetiredRegion> retired;
    // Live pool count. Zero lets RegionId/Recycle skip the address lookup.
    butil::atomic<size_t> count{0};
    // Registered bytes, including live regions and failed deregistrations.
    size_t bytes{0};
};

PoolRegistry& Registry() {
    // IOBufs may be released during static destruction. The registry must
    // outlive them.
    static auto registry = new PoolRegistry;
    return *registry;
}

PoolRegistry::RegionMap::const_iterator
FindRegion(const void* address, const PoolRegistry::RegionMap& regions) {
    uintptr_t ptr = reinterpret_cast<uintptr_t>(address);
    auto it = regions.upper_bound(ptr);
    if (it == regions.begin()) {
        return regions.end();
    }
    --it;
    return ptr - it->first < it->second.bytes ? it : regions.end();
}
}  // namespace

SrqReceivePool::Region::~Region() {
    auto& registry = Registry();
    if (published) {
        registry.regions.Modify([&](PoolRegistry::RegionMap& regions) {
            return regions.erase(reinterpret_cast<uintptr_t>(memory));
        });
    }
    if (mr != nullptr) {
        int rc = IbvDeregMr(mr);
        BAIDU_SCOPED_LOCK(registry.mutex);
        if (rc == 0) {
            registry.bytes -= bytes;
        } else {
            // A failed deregistration still owns the pages. Keep accounting
            // and allocation ownership until RetryDeregistrations succeeds.
            LOG(ERROR) << "Cannot deregister SRQ receive region, mr=" << mr
                       << ", bytes=" << bytes << ", rc=" << rc << ": " << berror(rc)
                       << ", retaining its pages";
            registry.retired.push_back({mr, memory, bytes});
            memory = nullptr;
        }
    }
    ::free(memory);
}

void* SrqReceivePool::operator new(size_t bytes) {
    void* memory = nullptr;
    if (posix_memalign(&memory, alignof(SrqReceivePool), bytes) != 0) {
        throw std::bad_alloc();
    }
    return memory;
}

void SrqReceivePool::operator delete(void* memory) {
    free(memory);
}

SrqReceivePool* SrqReceivePool::Create(ibv_pd* pd, size_t block_size, uint32_t block_count,
                                       uint32_t grow_blocks, uint32_t initial_blocks) {
    if (block_size <= sizeof(butil::IOBuf::Block) || block_size > UINT32_MAX ||
        block_count == 0 || block_size % alignof(butil::IOBuf::Block) != 0 ||
        block_size > SIZE_MAX / block_count || grow_blocks > block_count ||
        initial_blocks > block_count || FLAGS_rdma_srq_pool_growth_retry_ms <= 0 ||
        FLAGS_rdma_srq_pool_growth_retry_ms > 60000 ||
        IbvRegMr == nullptr || IbvDeregMr == nullptr) {
        errno = EINVAL;
        return nullptr;
    }

    // The constructor and aligned allocator are private to the factory, so
    // std::make_unique cannot construct this reference-counted pool.
    std::unique_ptr<SrqReceivePool> pool(new SrqReceivePool);
    pool->_pd = pd;
    pool->_block_size = block_size;
    pool->_block_count = block_count;
    pool->_grow_blocks = grow_blocks == 0 ? block_count : grow_blocks;
    pool->_growth_retry_us = int64_t(FLAGS_rdma_srq_pool_growth_retry_ms) * 1000;
    pool->_hugepages = FLAGS_rdma_srq_pool_hugepages;
    pool->_growth_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (pool->_growth_fd < 0) {
        int error = errno;
        PLOG(ERROR) << "Fail to create SRQ receive pool eventfd, pool=" << pool.get();
        errno = error;
        return nullptr;
    }

    auto region = pool->PrepareRegion(
        initial_blocks == 0 ? pool->_grow_blocks : initial_blocks);
    if (region == nullptr) {
        return nullptr;
    }

    pool->AddRegion(std::move(region));
    Registry().count.fetch_add(1, butil::memory_order_release);
    pool->_registered = true;
    return pool.release();
}

std::unique_ptr<SrqReceivePool::Region> SrqReceivePool::PrepareRegion(uint32_t count) {
    auto region = std::make_unique<Region>();
    region->count = count;
    region->bytes = _block_size * count;
    constexpr size_t hugepage_size = 2 * 1024 * 1024;
    int err = ENOMEM;
    if (_hugepages && region->bytes >= hugepage_size &&
        region->bytes <= SIZE_MAX - (hugepage_size - 1)) {
        size_t allocation = (region->bytes + hugepage_size - 1) & ~(hugepage_size - 1);
        err = posix_memalign(&region->memory, hugepage_size, allocation);
        if (err == 0) {
            // THP is a hint, not a requirement. Ordinary pages remain valid if
            // madvise or huge-page allocation fails. Both paths use free(),
            // including the delayed deregistration quarantine.
            madvise(region->memory, allocation, MADV_HUGEPAGE);
        }
    }
    if (err != 0) {
        err = posix_memalign(&region->memory, 4096, region->bytes);
    }
    if (err != 0) {
        LOG(ERROR) << "Fail to allocate SRQ receive region, pool=" << this
                   << ", bytes=" << region->bytes << ", rc=" << err << ": " << berror(err);
        errno = err;
        return nullptr;
    }

    region->buffers = std::make_unique<Buffer[]>(region->count);
    // Register receive memory independently of the ordinary IOBuf pool growth
    // policy and its pthread TLS caches.
    int access = IBV_ACCESS_LOCAL_WRITE;
    if (_try_relaxed_order) {
        access |= IBV_ACCESS_RELAXED_ORDERING;
    }
    region->mr = IbvRegMr(_pd, region->memory, region->bytes, access);
    if (region->mr == nullptr && _try_relaxed_order) {
        int error = errno;
        region->mr = IbvRegMr(_pd, region->memory, region->bytes, IBV_ACCESS_LOCAL_WRITE);
        // Only cache a capability-like rejection followed by a successful
        // fallback. Resource exhaustion must not permanently disable RO.
        if (region->mr != nullptr && (error == EINVAL || error == EOPNOTSUPP)) {
            _try_relaxed_order = false;
        }
    }
    if (region->mr == nullptr) {
        int error = errno;
        PLOG(ERROR) << "Fail to register SRQ receive region, pool=" << this
                    << ", bytes=" << region->bytes << ", blocks=" << count;
        errno = error;
        return nullptr;
    }

    auto& registry = Registry();
    {
        BAIDU_SCOPED_LOCK(registry.mutex);
        registry.bytes += region->bytes;
    }
    for (uint32_t i = 0; i < region->count; ++i) {
        auto* buffer = &region->buffers[i];
        buffer->_pool = this;
        buffer->_region = region.get();
        buffer->_memory = static_cast<char*>(region->memory) + i * _block_size;
        buffer->_lkey = region->mr->lkey;
        buffer->_next = region->free;
        region->free = buffer;
    }
    // Modify invokes this callback for both index copies. Keep accounting and
    // other one-time side effects outside the callback.
    registry.regions.Modify([&](PoolRegistry::RegionMap& regions) {
        regions.emplace(reinterpret_cast<uintptr_t>(region->memory),
            PoolRegistry::LiveRegion{this, region->buffers.get(), region->bytes,
                                     region->mr->lkey});
        return 1;
    });
    region->published = true;
    return region;
}

void SrqReceivePool::AddRegion(std::unique_ptr<Region> region) {
    Region* added = region.get();
    added->index = _regions.size();
    _regions.push_back(std::move(region));
    // Reserve during adoption so returning a formerly full region never needs
    // a heap allocation. _regions grows geometrically, not once per block.
    _free_regions.reserve(_regions.capacity());
    _free_regions.push_back(added);
    std::push_heap(_free_regions.begin(), _free_regions.end(), Region::Later);
    _allocated_blocks += added->count;
    _available += added->count;
}

void SrqReceivePool::AdoptGrowth() {
    if (_completed_growth.load(butil::memory_order_acquire) == nullptr) {
        return;
    }

    std::unique_ptr<Growth> growth(
        _completed_growth.exchange(nullptr, butil::memory_order_acquire));
    _growing = false;
    if (growth->region != nullptr) {
        AddRegion(std::move(growth->region));
    }
}

void SrqReceivePool::RequestGrowth(uint32_t reusable) {
    if (_growing || _allocated_blocks == _block_count ||
        uint64_t(_available) + reusable > std::max(1u, _grow_blocks / 4)) {
        return;
    }
    auto growth = std::make_unique<Growth>();
    growth->pool = this;
    growth->count = std::min(_grow_blocks, _block_count - _allocated_blocks);
    _growing = true;
    _references.fetch_add(1, butil::memory_order_relaxed);
    bthread_t tid;
    CHECK_EQ(bthread_start_background(&tid, nullptr, RunGrowth, growth.get()), 0)
        << "Cannot start SRQ receive pool growth";
    growth.release();
}

void* SrqReceivePool::RunGrowth(void* arg) {
    std::unique_ptr<Growth> growth(static_cast<Growth*>(arg));
    SrqReceivePool* pool = growth->pool;
    if (!pool->_stopped.load(butil::memory_order_acquire)) {
        growth->region = pool->PrepareRegion(growth->count);
        if (growth->region == nullptr &&
            !pool->_stopped.load(butil::memory_order_acquire)) {
            // Keep _growing set throughout backoff. CQ progress can continue,
            // but cannot launch a registration attempt on every refill.
            bthread_usleep(pool->_growth_retry_us);
        }
    }
    // Publication ends all task access to `Growth` and `Region`. The owner may
    // adopt and delete the result immediately, or release its own reference
    // while registration is running. The task reference keeps eventfd and the
    // pool alive until the unconditional completion notification is finished.
    pool->_completed_growth.store(growth.release(), butil::memory_order_release);
    pool->NotifyGrowth();
    pool->ReleaseReference();
    return nullptr;
}

SrqReceivePool::~SrqReceivePool() {
    int saved_errno = errno;
    // The last reference excludes running growth tasks. A completed region may
    // still be pending if the owner stopped before consuming its notification.
    AdoptGrowth();
    auto& registry = Registry();
    // Once Modify returns, readers of the removed entries have left their read
    // sections. Only then may MRs and descriptors be reclaimed. Do not hold the
    // accounting mutex while waiting for readers, so other pools can report
    // bytes and complete deregistration.
    registry.regions.Modify([&](PoolRegistry::RegionMap& regions) {
        for (auto& region : _regions) {
            regions.erase(reinterpret_cast<uintptr_t>(region->memory));
        }
        return _regions.size();
    });
    for (auto& region : _regions) {
        region->published = false;
    }
    _regions.clear();
    if (_growth_fd >= 0) {
        close(_growth_fd);
    }
    if (_registered) {
        // Zero live pools must also exclude growth-task cleanup still using
        // verbs entry points, not just owners that have released their handles.
        registry.count.fetch_sub(1, butil::memory_order_release);
    }
    errno = saved_errno;
}

void SrqReceivePool::Release() {
    _stopped.store(true, butil::memory_order_release);
    ReleaseReference();
}

void SrqReceivePool::ReleaseReference() {
    if (_references.fetch_sub(1, butil::memory_order_acq_rel) == 1) {
        delete this;
    }
}

void SrqReceivePool::Buffer::Initialize() {
    CHECK(empty());
    char* memory = static_cast<char*>(_memory);
    auto* block = new (memory) butil::IOBuf::Block(
        memory + sizeof(butil::IOBuf::Block), capacity());
    block->size = block->cap;
    const butil::IOBuf::BlockRef ref = {0, block->cap, block};
    _move_back_ref(ref);
}

void* SrqReceivePool::Buffer::data() const {
    return static_cast<char*>(_memory) + sizeof(butil::IOBuf::Block);
}

uint32_t SrqReceivePool::Buffer::capacity() const {
    return _pool->_block_size - sizeof(butil::IOBuf::Block);
}

uint32_t SrqReceivePool::Buffer::lkey() const {
    return _lkey;
}

butil::IOBuf SrqReceivePool::Buffer::TakeReceived(size_t bytes) {
    butil::IOBuf received;
    CHECK_LE(bytes, size());
    // Move the single BlockRef before shortening it. cutn on a partial block
    // would add a reference only to drop the unused suffix immediately.
    swap(received);
    received.pop_back(received.size() - bytes);
    return received;
}

void SrqReceivePool::Buffer::clear() {
    // IOBuf::clear updates its BlockRefs after invoking the recycle hook. Move
    // the refs to a local IOBuf before releasing them: once Return publishes
    // this descriptor, the receive owner may reuse it immediately, so the old
    // clear must no longer modify the descriptor.
    butil::IOBuf previous;
    swap(previous);
}

SrqReceivePool::Buffer* SrqReceivePool::Acquire() {
    if (_available == 0) {
        Prepare();
    }
    if (_available == 0) {
        errno = _allocated_blocks == _block_count ? ENOMEM : EAGAIN;
        return nullptr;
    }
    RequestGrowth();
    Region* region = _free_regions.front();
    Buffer* buffer = region->free;
    region->free = buffer->_next;
    if (region->free == nullptr) {
        std::pop_heap(_free_regions.begin(), _free_regions.end(), Region::Later);
        _free_regions.pop_back();
    }
    buffer->_next = nullptr;
    ++region->in_use;
    --_available;
    _references.fetch_add(1, butil::memory_order_relaxed);
    buffer->Initialize();
    return buffer;
}

void SrqReceivePool::Prepare(uint32_t reusable) {
    DrainReturned();
    AdoptGrowth();
    RequestGrowth(reusable);
}

void SrqReceivePool::DrainReturned() {
    // Only the owner detaches links. After acquire, returners no longer touch
    // these descriptors, so regrouping by region needs no additional locks.
    Buffer* head = _returned.exchange(nullptr, butil::memory_order_acquire);
    // Reverse the detached stack before prepending to each region, preserving
    // LIFO reuse within that region rather than favoring the oldest return.
    Buffer* reversed = nullptr;
    while (head != nullptr) {
        Buffer* next = head->_next;
        head->_next = reversed;
        reversed = head;
        head = next;
    }
    head = reversed;
    while (head != nullptr) {
        Buffer* buffer = head;
        head = buffer->_next;
        Region* region = buffer->_region;
        if (region->free == nullptr) {
            _free_regions.push_back(region);
            std::push_heap(_free_regions.begin(), _free_regions.end(), Region::Later);
        }
        buffer->_next = region->free;
        region->free = buffer;
        DCHECK_GT(region->in_use, 0u);
        --region->in_use;
        ++_available;
    }
}

void SrqReceivePool::Return(Buffer* buffer) {
    // Initialize the intrusive link before release publication. After CAS
    // succeeds, `DrainReturned()` may immediately adopt and reuse the descriptor.
    // Touch neither `buffer` nor its `Block` again. Drop the outstanding block's
    // pool reference only after publication, allowing shutdown to reclaim it.
    Buffer* head = _returned.load(butil::memory_order_relaxed);
    do {
        buffer->_next = head;
    } while (!_returned.compare_exchange_weak(head, buffer,
                                               butil::memory_order_release,
                                               butil::memory_order_relaxed));
    ReleaseReference();
}

void SrqReceivePool::NotifyGrowth() {
    uint64_t value = 1;
    ssize_t written;
    do {
        written = write(_growth_fd, &value, sizeof(value));
    } while (written < 0 && errno == EINTR);
    // EAGAIN means eventfd already has a pending notification. Always notify
    // growth completion so a sleeping owner adopts it or retries after backoff
    // even without further CQEs. Block returns do not wake the owner.
    PLOG_IF(ERROR, written < 0 && errno != EAGAIN) << "SRQ pool growth notification";
}

uint32_t SrqReceivePool::RegionId(const void* address) {
    auto& registry = Registry();
    if (registry.count.load(butil::memory_order_acquire) == 0) {
        return 0;
    }
    butil::DoublyBufferedData<PoolRegistry::RegionMap>::ScopedPtr regions;
    if (registry.regions.Read(&regions) != 0) {
        return 0;
    }
    auto it = FindRegion(address, *regions);
    return it != regions->end() ? it->second.lkey : 0;
}

bool SrqReceivePool::Recycle(void* block) {
    SrqReceivePool* pool;
    Buffer* buffer;
    {
        auto& registry = Registry();
        if (registry.count.load(butil::memory_order_acquire) == 0) {
            return false;
        }
        butil::DoublyBufferedData<PoolRegistry::RegionMap>::ScopedPtr regions;
        // A failed index read must not fall back to ordinary IOBuf
        // deallocation: block may be an interior address of a registered
        // region.
        CHECK_EQ(0, registry.regions.Read(&regions));
        auto it = FindRegion(block, *regions);
        if (it == regions->end()) {
            return false;
        }
        pool = it->second.pool;
        uintptr_t offset = reinterpret_cast<uintptr_t>(block) - it->first;
        CHECK_EQ(0u, offset % pool->_block_size);
        buffer = &it->second.buffers[offset / pool->_block_size];
        // This block still holds a pool reference. Leave the index read section
        // before Return/Release: dropping the final reference may synchronously
        // destroy the pool and Modify the index, which would otherwise wait for
        // this same reader.
    }
    pool->Return(buffer);
    return true;
}

size_t SrqReceivePool::RegisteredBytes() {
    PoolRegistry& registry = Registry();
    BAIDU_SCOPED_LOCK(registry.mutex);
    return registry.bytes;
}

size_t SrqReceivePool::LivePools() {
    return Registry().count.load(butil::memory_order_acquire);
}

size_t SrqReceivePool::RetryDeregistrations() {
    PoolRegistry& registry = Registry();
    std::vector<PoolRegistry::RetiredRegion> retired;
    {
        BAIDU_SCOPED_LOCK(registry.mutex);
        retired.swap(registry.retired);
    }
    size_t released_bytes = 0;
    // Retain failed regions in place and subtract successfully deregistered
    // bytes once after the loop.
    auto retained_end = std::remove_if(retired.begin(), retired.end(),
        [&](const PoolRegistry::RetiredRegion& region) {
            if (IbvDeregMr(region.mr) != 0) {
                return false;
            }
            free(region.memory);
            released_bytes += region.bytes;
            return true;
        });
    BAIDU_SCOPED_LOCK(registry.mutex);
    registry.bytes -= released_bytes;
    // Append only this batch of failures. Preserve regions quarantined by other
    // pools while the verbs calls ran outside the mutex.
    registry.retired.insert(registry.retired.end(), retired.begin(), retained_end);
    return registry.retired.size();
}

}  // namespace rdma
}  // namespace brpc
#endif  // BRPC_WITH_RDMA
