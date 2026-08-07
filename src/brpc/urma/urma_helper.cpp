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

#include "brpc/urma/urma_helper.h"

#if BRPC_WITH_URMA

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <new>
#include <utility>
#include <vector>

#include <gflags/gflags.h>

#include "butil/atomicops.h"
#include "butil/containers/flat_map.h"
#include "butil/iobuf.h"
#include "butil/logging.h"
#include "butil/macros.h"
#include "butil/scoped_lock.h"
#include "butil/synchronization/lock.h"

#include "urma_api.h"
#include "urma_types.h"
#include "brpc/urma/urma_bonding.h"
#include "brpc/urma/urma_endpoint.h"

DECLARE_int32(task_group_ntags);

namespace butil {
namespace iobuf {
// declared in iobuf.cpp
extern void* (*blockmem_allocate)(size_t);
extern void  (*blockmem_deallocate)(void*);
}
}

namespace brpc {
namespace urma {

DEFINE_bool(urma_use_polling, false,
            "Use busy polling to poll JFC, instead of event mode");
DEFINE_int32(urma_poller_num, 1,
             "Number of poller bthreads per bthread tag (polling mode only)");
DEFINE_bool(urma_disable_bthread, false,
            "Run the message-processing callback inline (no bthread spawned)");

DEFINE_int32(urma_sq_size, 128,
             "Depth of the local send jetty (JFS). [16, 4096]");
DEFINE_int32(urma_rq_size, 128,
             "Depth of the local recv jetty (JFR). [16, 4096]");
DEFINE_int32(urma_cqe_poll_once, 32,
             "Max completion entries polled per urma_poll_jfc call");
DEFINE_bool(urma_recv_zerocopy, true,
            "Use zero-copy for receives larger than --urma_zerocopy_min_size");
DEFINE_int32(urma_zerocopy_min_size, 512,
             "Receives smaller than this many bytes are copied (not zero-copy)");

DEFINE_string(urma_device, "",
              "The name of the URMA device to use. Empty means the first one.");
DEFINE_int32(urma_max_sge, 0,
             "Max SGEs per WR. 0 means the device maximum.");
DEFINE_int32(urma_bonding_mode, 0,
             "Bonding mode for bonding devices: 0=standalone, "
             "1=active-backup, 2=balance.");
DEFINE_int32(urma_bonding_level, 0,
             "Bonding level for bonding devices: 0=IODIE, 1=port.");
DEFINE_int32(urma_prepared_jetty_cnt, 8,
             "Requested number of pre-allocated Jetty+CQ sets for fast "
             "connect; capped automatically according to RLIMIT_NOFILE");

DEFINE_int32(urma_buffer_size, 8 * 1024,
             "Per-buffer size in the URMA buffer pool (bytes). "
             "Must match IOBuf block size to keep zero-copy working.");
DEFINE_int32(urma_buffer_count, 65536,
             "Number of buffers in the URMA buffer pool.");

DEFINE_bool(urma_poller_yield, false,
            "Yield (bthread_yield) in the busy poll loop to let other "
            "bthreads run");

constexpr size_t kIOBufBlockHeaderLen = 32;

// Set to true to skip real URMA hardware initialization (unit tests). When
// true, GlobalUrmaInitializeOrDie() returns without touching liburma and the
// endpoint builds its state machine without posting real WRs.
bool g_skip_urma_init = false;
butil::atomic<bool> g_urma_available(false);

// ============================================================================
// Global URMA state (single device / single context chosen at init time).
// ============================================================================

static urma_device_t* g_device = nullptr;
static urma_context_t* g_context = nullptr;
static urma_eid_t g_local_eid{};
static bool g_has_local_eid = false;
static urma_device_attr_t g_device_attr{};
static int g_max_sge = 1;
static size_t g_recv_block_size = 8 * 1024;
static bool g_is_bonding_device = false;
// Prefer the device capability table and retain priority 6 as a compatibility
// fallback for CTP providers that do not report a priority.
static uint8_t g_jetty_priority = 6;
// urma_init/urma_uninit manage process-global liburma state. Only uninitialize
// it when this helper performed the successful initialization; URMA_EEXIST
// means another component owns that state.
static bool g_owns_urma_init = false;

// The single registered segment backing the buffer pool. The whole pool is
// one urma_register_seg call, sliced into fixed-size buffers. urma_target_seg_t
// is the per-buffer handle carried by urma_sge_t.tseg on the send/recv path.
static urma_target_seg_t* g_pool_seg = nullptr;
static void* g_pool_base = nullptr;
static size_t g_pool_size = 0;
static size_t g_pool_buffer_size = 0;

// User-registered segments (RegisterMemoryForUrma). Keyed by buffer address.
struct UserSeg {
    urma_target_seg_t* tseg = nullptr;
    void* base = nullptr;
    size_t len = 0;
};
static butil::FlatMap<void*, UserSeg>* g_user_segs = nullptr;
static butil::Mutex* g_user_segs_lock = nullptr;

// Original IOBuf allocator (saved so we can restore it on release).
static void* (*g_mem_alloc_orig)(size_t) = nullptr;
static void (*g_mem_dealloc_orig)(void*) = nullptr;
static size_t g_default_block_size_orig = 0;

namespace {

// Round up to the page size.
size_t PageSize() {
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? static_cast<size_t>(ps) : 4096;
}

size_t AlignUp(size_t v, size_t align) {
    return (v + align - 1) / align * align;
}

bool IsBondingDeviceName(const char* name) {
    return name != nullptr && strncmp(name, "bonding", 7) == 0;
}

union urma_tp_type_en TpTypeCapability(urma_tp_type_t tp_type) {
    union urma_tp_type_en capability{};
    switch (tp_type) {
    case URMA_RTP:
        capability.bs.rtp = 1;
        break;
    case URMA_CTP:
        capability.bs.ctp = 1;
        break;
    case URMA_UTP:
        capability.bs.utp = 1;
        break;
    }
    return capability;
}

bool ConfigureBondingMode(const std::string& device_name) {
    const char* context_device_name =
        g_context != nullptr && g_context->dev != nullptr
            ? g_context->dev->name
            : nullptr;
    g_is_bonding_device =
        IsBondingDeviceName(device_name.c_str()) ||
        IsBondingDeviceName(context_device_name);
    if (!g_is_bonding_device) {
        return true;
    }

#if BRPC_URMA_HAS_BONDING_EXT
    if (FLAGS_urma_bonding_mode < 0 ||
        FLAGS_urma_bonding_mode >= BONDP_BONDING_MODE_MAX ||
        FLAGS_urma_bonding_level < 0 ||
        FLAGS_urma_bonding_level >= BONDP_BONDING_LEVEL_MAX) {
        LOG(ERROR) << "Invalid URMA bonding configuration: mode="
                   << FLAGS_urma_bonding_mode
                   << " level=" << FLAGS_urma_bonding_level;
        errno = EINVAL;
        return false;
    }

    bondp_set_bonding_mode_in_t bond_in{};
    bond_in.bonding_mode =
        static_cast<bondp_bonding_mode_t>(FLAGS_urma_bonding_mode);
    bond_in.bonding_level =
        static_cast<bondp_bonding_level_t>(FLAGS_urma_bonding_level);

    urma_user_ctl_in_t ctl_in{};
    ctl_in.addr = reinterpret_cast<uint64_t>(&bond_in);
    ctl_in.len = static_cast<uint32_t>(sizeof(bond_in));
    ctl_in.opcode = BONDP_USER_CTL_SET_BONDING_MODE;
    urma_user_ctl_out_t ctl_out{};
    const urma_status_t status = urma_user_ctl(g_context, &ctl_in, &ctl_out);
    if (status != URMA_SUCCESS) {
        LOG(ERROR) << "urma_user_ctl(SET_BONDING_MODE) failed: status="
                   << status << " device=" << device_name
                   << " mode=" << FLAGS_urma_bonding_mode
                   << " level=" << FLAGS_urma_bonding_level
                   << ". It must run before segment/JFC/JFR creation";
        errno = status > 0 ? status : EIO;
        return false;
    }
    return true;
#else
    LOG(ERROR) << "URMA bonding device " << device_name
               << " requires provider header urma_ubagg.h";
    errno = ENOTSUP;
    return false;
#endif
}

}  // namespace

// ============================================================================
// Buffer pool: one registered segment, sliced into fixed-size buffers.
// ============================================================================

namespace {

// Shard the free list to reduce contention between allocator threads.
constexpr size_t kShardCount = 64;
struct BufferPool {
    butil::Mutex mutexes[kShardCount];
    std::vector<void*> free_lists[kShardCount];
    std::vector<char> in_use;  // 0/1 per buffer
    butil::atomic<size_t> outstanding{0};

    size_t buffer_count() const {
        return in_use.size();
    }
};
BufferPool* g_pool = nullptr;

size_t ShardFor(void* buf) {
    auto* base = static_cast<char*>(buf);
    auto offset = static_cast<size_t>(base - static_cast<char*>(g_pool_base));
    auto idx = offset / g_pool_buffer_size;
    return idx % kShardCount;
}

size_t PreferredShard() {
    // Hash the current thread id across shards. pthread_self() returns an
    // opaque pthread_t; cast through uintptr_t to get a hashable value.
    auto tid = static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(pthread_self()));
    return tid % kShardCount;
}

void* PoolAllocate(size_t size) {
    if (BAIDU_UNLIKELY(g_skip_urma_init)) {
        return g_mem_alloc_orig ? g_mem_alloc_orig(size) : malloc(size);
    }
    // Only serve the configured buffer size; callers always ask for that.
    if (size > g_pool_buffer_size) {
        // Larger than a single buffer -- fall back to the system allocator.
        return g_mem_alloc_orig ? g_mem_alloc_orig(size) : malloc(size);
    }
    auto start = PreferredShard();
    for (size_t i = 0; i < kShardCount; ++i) {
        auto shard = (start + i) % kShardCount;
        BAIDU_SCOPED_LOCK(g_pool->mutexes[shard]);
        auto& fl = g_pool->free_lists[shard];
        if (fl.empty()) {
            continue;
        }
        void* buf = fl.back();
        fl.pop_back();
        auto idx = (static_cast<char*>(buf) -
                    static_cast<char*>(g_pool_base)) / g_pool_buffer_size;
        if (idx < g_pool->in_use.size()) {
            g_pool->in_use[idx] = 1;
        }
        g_pool->outstanding.fetch_add(1, butil::memory_order_relaxed);
        return buf;
    }
    LOG_EVERY_SECOND(WARNING)
        << "URMA buffer pool exhausted; falling back to malloc";
    return g_mem_alloc_orig ? g_mem_alloc_orig(size) : malloc(size);
}

void PoolDeallocate(void* buf) {
    if (BAIDU_UNLIKELY(g_skip_urma_init)) {
        if (g_mem_dealloc_orig) {
            g_mem_dealloc_orig(buf);
        } else {
            free(buf);
        }
        return;
    }
    auto* base = static_cast<char*>(g_pool_base);
    auto* p = static_cast<char*>(buf);
    if (!base || p < base || p >= base + g_pool_size ||
        static_cast<size_t>(p - base) % g_pool_buffer_size != 0) {
        // Not a pool buffer -- hand back to the original allocator.
        if (g_mem_dealloc_orig) {
            g_mem_dealloc_orig(buf);
        } else {
            free(buf);
        }
        return;
    }
    auto shard = ShardFor(buf);
    BAIDU_SCOPED_LOCK(g_pool->mutexes[shard]);
    auto idx = static_cast<size_t>(p - base) / g_pool_buffer_size;
    if (idx < g_pool->in_use.size()) {
        if (!g_pool->in_use[idx]) {
            LOG(WARNING) << "double-free of URMA pool buffer " << buf;
            return;
        }
        g_pool->in_use[idx] = 0;
    }
    g_pool->free_lists[shard].push_back(buf);
    if (g_pool->outstanding.load(butil::memory_order_relaxed) > 0) {
        g_pool->outstanding.fetch_sub(1, butil::memory_order_relaxed);
    }
}

// Register the pool: mmap one large region and urma_register_seg it.
bool InitPool() {
    if (g_pool_buffer_size == 0 || g_pool == nullptr) {
        return false;
    }
    size_t count = g_pool->buffer_count();
    if (count == 0) {
        return false;
    }
    size_t raw = g_pool_buffer_size * count;
    size_t page = PageSize();
    g_pool_size = AlignUp(raw, page);

    g_pool_base = mmap(nullptr, g_pool_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_pool_base == MAP_FAILED) {
        PLOG(WARNING) << "Fail to mmap URMA buffer pool";
        g_pool_base = nullptr;
        return false;
    }

    urma_reg_seg_flag_t flag{};
    flag.bs.token_policy = URMA_TOKEN_NONE;
    flag.bs.cacheable = URMA_NON_CACHEABLE;
    flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE | URMA_ACCESS_ATOMIC;

    urma_seg_cfg_t cfg{};
    cfg.va = reinterpret_cast<uint64_t>(g_pool_base);
    cfg.len = g_pool_size;
    cfg.token_id = nullptr;
    cfg.token_value = {};
    cfg.flag = flag;
    cfg.user_ctx = reinterpret_cast<uint64_t>(g_pool_base);
    cfg.iova = 0;

    errno = 0;
    g_pool_seg = urma_register_seg(g_context, &cfg);
    if (!g_pool_seg) {
        PLOG(WARNING) << "Fail to urma_register_seg";
        munmap(g_pool_base, g_pool_size);
        g_pool_base = nullptr;
        return false;
    }
    g_pool->in_use.assign(count, 0);
    for (size_t i = 0; i < count; ++i) {
        auto shard = i % kShardCount;
        g_pool->free_lists[shard].push_back(
            static_cast<char*>(g_pool_base) + i * g_pool_buffer_size);
    }
    return true;
}

}  // namespace

// Exposed to urma_endpoint.cpp: return the per-buffer target_seg pointer.
// All pool buffers share the same segment, so we return g_pool_seg for any
// pool address; the per-WR length selects the slice.
urma_target_seg_t* GetPoolSegFor(void* buf) {
    if (g_skip_urma_init || !g_pool_seg || !g_pool_base) {
        return nullptr;
    }
    if (buf == nullptr) {
        return g_pool_seg;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_pool_base);
    const uintptr_t p = reinterpret_cast<uintptr_t>(buf);
    if (p >= base && p - base < g_pool_size) {
        return g_pool_seg;
    }
    return nullptr;
}

static void GlobalRelease() {
    g_urma_available.store(false, butil::memory_order_release);
    if (g_mem_alloc_orig) {
        butil::iobuf::blockmem_allocate = g_mem_alloc_orig;
        g_mem_alloc_orig = nullptr;
    }
    if (g_mem_dealloc_orig) {
        butil::iobuf::blockmem_deallocate = g_mem_dealloc_orig;
        g_mem_dealloc_orig = nullptr;
    }
    if (g_default_block_size_orig != 0) {
        butil::SetDefaultBlockSize(g_default_block_size_orig);
        g_default_block_size_orig = 0;
    }
    UrmaEndpoint::GlobalRelease();
    if (g_pool_seg) {
        urma_unregister_seg(g_pool_seg);
        g_pool_seg = nullptr;
    }
    if (g_pool_base) {
        munmap(g_pool_base, g_pool_size);
        g_pool_base = nullptr;
        g_pool_size = 0;
    }
    delete g_pool;
    g_pool = nullptr;
    delete g_user_segs;
    g_user_segs = nullptr;
    delete g_user_segs_lock;
    g_user_segs_lock = nullptr;
    if (g_context) {
        urma_delete_context(g_context);
        g_context = nullptr;
    }
    g_local_eid = urma_eid_t{};
    g_has_local_eid = false;
    g_is_bonding_device = false;
    g_jetty_priority = 6;
    if (g_owns_urma_init) {
        const urma_status_t status = urma_uninit();
        if (status != URMA_SUCCESS) {
            LOG(WARNING) << "Fail to urma_uninit: " << status;
        }
        g_owns_urma_init = false;
    }
    g_device = nullptr;
}

// ============================================================================
// Global initialization.
// ============================================================================

static bool GlobalUrmaInitializeImpl() {
    if (BAIDU_UNLIKELY(g_skip_urma_init)) {
        g_urma_available.store(true, butil::memory_order_release);
        return true;
    }
    if (FLAGS_urma_sq_size < 16 || FLAGS_urma_sq_size > 4096 ||
        FLAGS_urma_rq_size < 16 || FLAGS_urma_rq_size > 4096 ||
        FLAGS_urma_buffer_size < 1024 || FLAGS_urma_buffer_count <= 0 ||
        FLAGS_urma_poller_num <= 0) {
        LOG(ERROR) << "Invalid URMA queue, buffer, or poller configuration";
        errno = EINVAL;
        return false;
    }

    urma_init_attr_t init_attr{};
    const urma_status_t status = urma_init(&init_attr);
    if (status != URMA_SUCCESS && status != URMA_EEXIST) {
        if (status == URMA_FAIL) {
            LOG(ERROR) << "Fail to urma_init: " << status
                       << " (URMA_FAIL). liburma returns URMA_FAIL when it "
                          "cannot load a provider, or when URMA was already "
                          "initialized by another component. Verify readable "
                          "provider libraries under /usr/lib64/urma, loaded "
                          "URMA kernel drivers, and that urma_init is called "
                          "only once per process";
        } else {
            LOG(ERROR) << "Fail to urma_init: " << status;
        }
        return false;
    }
    g_owns_urma_init = (status == URMA_SUCCESS);

    int num_devices = 0;
    urma_device_t** devices = urma_get_device_list(&num_devices);
    if (!devices || num_devices <= 0) {
        LOG(ERROR) << "No URMA device found";
        urma_free_device_list(devices);
        return false;
    }
    urma_device_t* found = nullptr;
    for (int i = 0; i < num_devices; ++i) {
        if (FLAGS_urma_device.empty() ||
            std::string(devices[i]->name) == FLAGS_urma_device) {
            found = devices[i];
            break;
        }
    }
    if (!found) {
        LOG(ERROR) << "URMA device not found: " << FLAGS_urma_device;
        urma_free_device_list(devices);
        return false;
    }
    g_device = found;

    const std::string device_name = found->name;
    if (urma_query_device(found, &g_device_attr) != URMA_SUCCESS) {
        LOG(ERROR) << "Fail to urma_query_device";
        urma_free_device_list(devices);
        g_device = nullptr;
        return false;
    }
    const int ctp_priority =
        FindUrmaPriorityForTpType(g_device_attr, URMA_CTP);
    if (ctp_priority >= 0) {
        g_jetty_priority = static_cast<uint8_t>(ctp_priority);
    } else {
        LOG(WARNING) << "URMA device does not report a CTP priority; "
                        "falling back to compatibility priority "
                     << static_cast<unsigned>(g_jetty_priority);
    }
    uint32_t eid_cnt = 0;
    urma_eid_info_t* eids = urma_get_eid_list(found, &eid_cnt);
    if (!eids || eid_cnt == 0) {
        LOG(ERROR) << "Fail to urma_get_eid_list";
        urma_free_eid_list(eids);
        urma_free_device_list(devices);
        g_device = nullptr;
        return false;
    }
    // Retain the exact EID used to create the context and advertise it in the
    // handshake. This matters for a bonding virtual device whose jetty can
    // carry a provider-selected physical EID.
    g_local_eid = eids[0].eid;
    g_has_local_eid = true;
    g_context = urma_create_context(found, eids[0].eid_index);
    urma_free_eid_list(eids);
    urma_free_device_list(devices);
    g_device = nullptr;
    if (!g_context) {
        LOG(ERROR) << "Fail to urma_create_context";
        return false;
    }
    // The bonding provider only accepts SET_BONDING_MODE while the context
    // has no dependent resource. This must precede register_seg/JFC/JFR.
    if (!ConfigureBondingMode(device_name)) {
        return false;
    }
    uint32_t device_max_sge = g_device_attr.dev_cap.max_jfs_sge;
    if (device_max_sge == 0) {
        device_max_sge = 1;
    }
    // urma_jfs_cfg_t::max_sge is uint8_t.
    if (device_max_sge > 255) {
        device_max_sge = 255;
    }
    g_max_sge = static_cast<int>(device_max_sge);
    if (FLAGS_urma_max_sge > 0) {
        if (FLAGS_urma_max_sge > g_max_sge) {
            LOG(WARNING) << "Cap urma_max_sge from " << FLAGS_urma_max_sge
                         << " to device/config limit " << g_max_sge;
        } else {
            g_max_sge = FLAGS_urma_max_sge;
        }
    }
    g_recv_block_size =
        static_cast<size_t>(FLAGS_urma_buffer_size) -
        kIOBufBlockHeaderLen;

    // User-segment table.
    g_user_segs_lock = new (std::nothrow) butil::Mutex;
    g_user_segs = new (std::nothrow) butil::FlatMap<void*, UserSeg>();
    if (!g_user_segs_lock || !g_user_segs ||
        g_user_segs->init(65536) < 0) {
        LOG(ERROR) << "Fail to init g_user_segs";
        return false;
    }

    // Buffer pool.
    g_pool = new BufferPool();
    g_pool_buffer_size = static_cast<size_t>(FLAGS_urma_buffer_size);
    // Resize the pool's per-shard vectors to hold the configured count.
    size_t count = static_cast<size_t>(FLAGS_urma_buffer_count);
    g_pool->in_use.assign(count, 0);
    for (size_t s = 0; s < kShardCount; ++s) {
        g_pool->free_lists[s].reserve(count / kShardCount + 1);
    }
    if (!InitPool()) {
        LOG(ERROR) << "Fail to init URMA buffer pool";
        return false;
    }

    // Hijack IOBuf allocation so every IOBuf block is backed by a registered
    // segment. This makes the send path trivial: any IOBuf can be posted
    // directly as an urma_sge_t pointing at g_pool_seg.
    g_mem_alloc_orig = butil::iobuf::blockmem_allocate;
    g_mem_dealloc_orig = butil::iobuf::blockmem_deallocate;
    g_default_block_size_orig = butil::GetDefaultBlockSize();
    butil::iobuf::blockmem_allocate = PoolAllocate;
    butil::iobuf::blockmem_deallocate = PoolDeallocate;
    butil::SetDefaultBlockSize(g_pool_buffer_size);

    if (UrmaEndpoint::GlobalInitialize() != 0) {
        LOG(ERROR) << "Fail to initialize URMA endpoint resources";
        return false;
    }

    g_urma_available.store(true, butil::memory_order_release);
    // Do not register GlobalRelease with atexit. IOBuf keeps blocks in
    // thread-local chains whose destructors may run after atexit handlers.
    // Unmapping the registered pool here would leave those TLS chains
    // pointing into unmapped memory. The process reclaims global URMA
    // resources on exit; GlobalRelease remains available for init rollback.
    LOG(INFO) << "URMA initialized: device=" << device_name
              << " bonding=" << g_is_bonding_device
              << " max_sge=" << g_max_sge
              << " buffer_size=" << g_pool_buffer_size
              << " buffer_count=" << g_pool->buffer_count();
    return true;
}

static butil::atomic<int> g_init_once{0};
static butil::Mutex g_init_mutex;

void GlobalUrmaInitializeOrDie() {
    int expected = 0;
    if (g_init_once.load(butil::memory_order_acquire) == 2) {
        return;
    }
    if (g_init_once.compare_exchange_strong(expected, 1,
                                             butil::memory_order_acq_rel)) {
        BAIDU_SCOPED_LOCK(g_init_mutex);
        if (!GlobalUrmaInitializeImpl()) {
            LOG(WARNING) << "URMA initialization failed; falling back to TCP";
            GlobalRelease();
        }
        g_init_once.store(2, butil::memory_order_release);
    } else {
        // Wait for the other thread to finish init.
        while (g_init_once.load(butil::memory_order_acquire) != 2) {
            // spin briefly
        }
    }
}

bool IsUrmaAvailable() {
    return g_urma_available.load(butil::memory_order_acquire);
}

void GlobalDisableUrma() {
    g_urma_available.store(false, butil::memory_order_release);
}

bool SupportedByUrma(const std::string& protocol) {
    return protocol == "baidu_std";
}

urma_context_t* GetUrmaContext() { return g_context; }
const urma_eid_t* GetUrmaLocalEid() {
    return g_has_local_eid ? &g_local_eid : nullptr;
}
bool IsUrmaBondingDevice() { return g_is_bonding_device; }
int FindUrmaPriorityForTpType(const urma_device_attr_t& attr,
                              urma_tp_type_t tp_type) {
    const union urma_tp_type_en expected = TpTypeCapability(tp_type);
    for (int priority = 0; priority <= URMA_MAX_PRIORITY; ++priority) {
        if (attr.dev_cap.priority_info[priority].tp_type.value ==
            expected.value) {
            return priority;
        }
    }
    return -1;
}
uint8_t GetUrmaJettyPriority() { return g_jetty_priority; }
int GetUrmaMaxSge() { return g_max_sge; }
size_t GetUrmaRecvBlockSize() { return g_recv_block_size; }

// ============================================================================
// Polling mode (per bthread tag).
// ============================================================================

bool InitPollingModeWithTag(bthread_tag_t tag,
                            std::function<void()> callback,
                            std::function<void()> init_fn,
                            std::function<void()> release_fn) {
    if (BAIDU_UNLIKELY(g_skip_urma_init)) {
        return true;
    }
    return UrmaEndpoint::PollingModeInitialize(
               tag, std::move(callback), std::move(init_fn),
               std::move(release_fn)) == 0;
}

void ReleasePollingModeWithTag(bthread_tag_t tag) {
    UrmaEndpoint::PollingModeRelease(tag);
}

// ============================================================================
// User memory registration.
// ============================================================================

uint64_t RegisterMemoryForUrma(void* buf, size_t len) {
    if (BAIDU_UNLIKELY(g_skip_urma_init) || !g_context) {
        return 0;
    }
    urma_reg_seg_flag_t flag{};
    flag.bs.token_policy = URMA_TOKEN_NONE;
    flag.bs.cacheable = URMA_NON_CACHEABLE;
    flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE | URMA_ACCESS_ATOMIC;

    urma_seg_cfg_t cfg{};
    cfg.va = reinterpret_cast<uint64_t>(buf);
    cfg.len = len;
    cfg.token_id = nullptr;
    cfg.token_value = {};
    cfg.flag = flag;
    cfg.user_ctx = reinterpret_cast<uint64_t>(buf);
    cfg.iova = 0;

    errno = 0;
    urma_target_seg_t* tseg = urma_register_seg(g_context, &cfg);
    if (!tseg) {
        PLOG(WARNING) << "Fail to urma_register_seg for user memory";
        return 0;
    }
    BAIDU_SCOPED_LOCK(*g_user_segs_lock);
    UserSeg us;
    us.tseg = tseg;
    us.base = buf;
    us.len = len;
    if (!g_user_segs->insert(buf, us)) {
        LOG(WARNING) << "Fail to insert user seg (duplicate?)";
        urma_unregister_seg(tseg);
        return 0;
    }
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(tseg));
}

void DeregisterMemoryForUrma(void* buf) {
    if (BAIDU_UNLIKELY(g_skip_urma_init) || !g_user_segs) {
        return;
    }
    BAIDU_SCOPED_LOCK(*g_user_segs_lock);
    UserSeg* us = g_user_segs->seek(buf);
    if (!us) {
        return;
    }
    urma_unregister_seg(us->tseg);
    g_user_segs->erase(buf);
}

}  // namespace urma
}  // namespace brpc

#else  // BRPC_WITH_URMA

#include <cstdlib>

#include "butil/logging.h"

namespace brpc {
namespace urma {

void GlobalUrmaInitializeOrDie() {
    LOG(FATAL) << "URMA is not compiled in. Rebuild with -DWITH_URMA=ON.";
    exit(1);
}

}  // namespace urma
}  // namespace brpc

#endif  // BRPC_WITH_URMA
