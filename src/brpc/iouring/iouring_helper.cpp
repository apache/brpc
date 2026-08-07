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

#include <pthread.h>
#include <stdlib.h>

#include <gflags/gflags.h>
#include <liburing.h>

#include "butil/atomicops.h"
#include "butil/iobuf.h"            // butil::SetDefaultBlockSize
#include "butil/logging.h"
#include "brpc/iouring/iouring_block_pool.h"
#include "brpc/iouring/iouring_helper.h"
#include "brpc/iouring/iouring_endpoint.h"

DECLARE_int32(task_group_ntags);

namespace brpc {
namespace iouring {

// ---------------------------------------------------------------------------
// gflags
// ---------------------------------------------------------------------------
DEFINE_int32(iouring_sq_size, 256,
             "Submission-queue entries per io_uring ring.");
DEFINE_int32(iouring_cq_size, 0,
             "Completion-queue entries per ring (0 = 2 * sq_size).");
DEFINE_string(iouring_polling_mode, "none",
              "io_uring polling mode: none | sqpoll | iopoll | hybrid.");
DEFINE_int32(iouring_sqpoll_idle_ms, 2000,
             "SQPOLL kernel thread idle timeout (ms).");
DEFINE_int32(iouring_sqpoll_cpu, -1,
             "CPU for the SQPOLL kernel thread (-1 = no pin).");
DEFINE_int32(iouring_hybrid_spin_count, 1000,
             "Hybrid mode: busy-spin iterations before blocking.");

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
static butil::atomic<bool> g_available(false);
static IouringPollingMode  g_polling_mode = IouringPollingMode::NONE;

// ---------------------------------------------------------------------------
// Polling mode helpers
// ---------------------------------------------------------------------------
static IouringPollingMode ParsePollingMode(const std::string& s) {
    if (s == "sqpoll") return IouringPollingMode::SQPOLL;
    if (s == "iopoll") return IouringPollingMode::IOPOLL;
    if (s == "hybrid") return IouringPollingMode::HYBRID;
    if (s != "none") {
        LOG(WARNING) << "Unknown --iouring_polling_mode=\"" << s
                     << "\", using \"none\".";
    }
    return IouringPollingMode::NONE;
}

IouringPollingMode GetPollingMode() { return g_polling_mode; }

// ---------------------------------------------------------------------------
// Kernel capability probe
// ---------------------------------------------------------------------------
static bool ProbeOpcodes() {
    struct io_uring_probe* probe = io_uring_get_probe();
    if (!probe) {
        LOG(ERROR) << "io_uring: kernel probe failed (>= 5.1 required).";
        return false;
    }

    bool ok = true;
    auto need = [&](uint8_t op, const char* name) {
        if (!io_uring_opcode_supported(probe, op)) {
            LOG(ERROR) << "io_uring: " << name << " not supported.";
            ok = false;
        }
    };

    need(IORING_OP_READV,  "IORING_OP_READV");
    need(IORING_OP_WRITEV, "IORING_OP_WRITEV");
    need(IORING_OP_READ,   "IORING_OP_READ");

    if (IsFixedBuffersEnabled()) {
        need(IORING_OP_READ_FIXED,  "IORING_OP_READ_FIXED (disable --iouring_register_buffers)");
        need(IORING_OP_WRITE_FIXED, "IORING_OP_WRITE_FIXED (disable --iouring_register_buffers)");
    }

    if (g_polling_mode == IouringPollingMode::SQPOLL ||
        g_polling_mode == IouringPollingMode::HYBRID) {
        // Warn if kernel < 5.11 (SQPOLL_NONFIXED unavailable).
        struct io_uring ring_tmp{};
        struct io_uring_params p{};
        p.flags = IORING_SETUP_SQPOLL;
        if (io_uring_queue_init_params(1, &ring_tmp, &p) == 0) {
            if (!(p.features & IORING_FEAT_SQPOLL_NONFIXED)) {
                LOG(WARNING) << "SQPOLL: kernel < 5.11 – all I/O buffers must "
                                "be pre-registered. Consider enabling "
                                "--iouring_register_buffers.";
            }
            io_uring_queue_exit(&ring_tmp);
        }
    }

    if (g_polling_mode == IouringPollingMode::IOPOLL) {
        LOG(WARNING) << "IOPOLL is only effective for O_DIRECT block I/O.";
    }

    io_uring_free_probe(probe);
    return ok;
}

// ---------------------------------------------------------------------------
// One-time global initialization
// ---------------------------------------------------------------------------
static void InitImpl() {
    g_polling_mode = ParsePollingMode(FLAGS_iouring_polling_mode);

    if (!ProbeOpcodes()) { exit(1); }

    // Initialise the global IOBuf memory pool BEFORE any IOBuf is allocated.
    // This replaces butil::iobuf::blockmem_allocate with our registered-slab
    // allocator so that ALL subsequent IOBuf blocks reside in memory that is
    // (or will be) registered with every io_uring ring.
    if (IsFixedBuffersEnabled()) {
        const size_t block_sz =
            static_cast<size_t>(FLAGS_iouring_iobuf_block_size);
        // Align IOBuf's default block size with the pool's block size so that
        // every IOBuf block fits exactly one registered slot.  Must be done
        // before IouringMemPool::Init() hooks blockmem_allocate (same pattern
        // as rdma: butil::SetDefaultBlockSize(GetRdmaBlockSize())).
        butil::SetDefaultBlockSize(block_sz);
        if (!IouringMemPool::Instance().Init(block_sz)) {
            LOG(FATAL) << "IouringMemPool::Init failed – "
                          "try reducing --iouring_mem_pool_initial_mb or "
                          "disabling --iouring_register_buffers.";
            exit(1);
        }
    }

    if (IouringEndpoint::GlobalInitialize() < 0) {
        LOG(FATAL) << "IouringEndpoint::GlobalInitialize failed";
        exit(1);
    }

    atexit([]() {
        g_available.store(false, butil::memory_order_release);
        IouringEndpoint::GlobalRelease();
        if (IsFixedBuffersEnabled()) {
            IouringMemPool::Instance().Destroy();
        }
    });

    g_available.store(true, butil::memory_order_relaxed);

    LOG(INFO) << "io_uring ready."
              << " sq_size="         << FLAGS_iouring_sq_size
              << " polling_mode="    << FLAGS_iouring_polling_mode
              << " fixed_buffers="   << (IsFixedBuffersEnabled() ? "on" : "off");
}

static pthread_once_t g_once = PTHREAD_ONCE_INIT;

void GlobalIouringInitializeOrDie() {
    if (pthread_once(&g_once, InitImpl) != 0) {
        LOG(FATAL) << "pthread_once failed for GlobalIouringInitializeOrDie";
        exit(1);
    }
}

void GlobalIouringRelease() {
    if (g_available.exchange(false, butil::memory_order_acq_rel)) {
        IouringEndpoint::GlobalRelease();
    }
}

// ---------------------------------------------------------------------------
// Runtime queries
// ---------------------------------------------------------------------------
bool IsIouringAvailable() {
    return g_available.load(butil::memory_order_acquire);
}

void GlobalDisableIouring() {
    if (g_available.exchange(false, butil::memory_order_acq_rel)) {
        LOG(ERROR) << "io_uring disabled due to an unrecoverable error.";
    }
}

int GetIouringSqSize() { return FLAGS_iouring_sq_size; }

int GetIouringCqSize() {
    return FLAGS_iouring_cq_size > 0 ? FLAGS_iouring_cq_size
                                     : FLAGS_iouring_sq_size * 2;
}

// ---------------------------------------------------------------------------
// Poller group lifecycle (delegates to IouringEndpoint)
// ---------------------------------------------------------------------------
bool InitPollingModeWithTag(
        bthread_tag_t tag,
        std::function<void(IouringPollerHandle)> callback,
        std::function<void(IouringPollerHandle)> init_fn,
        std::function<void(IouringPollerHandle)> release_fn) {
    return IouringEndpoint::PollingModeInitialize(
               tag,
               std::move(callback),
               std::move(init_fn),
               std::move(release_fn)) == 0;
}

void ReleasePollingModeWithTag(bthread_tag_t tag) {
    IouringEndpoint::PollingModeRelease(tag);
}

}  // namespace iouring
}  // namespace brpc

#else  // !BRPC_WITH_IOURING

#include <stdlib.h>
#include "butil/logging.h"

namespace brpc {
namespace iouring {
void GlobalIouringInitializeOrDie() {
    LOG(ERROR) << "Build with -DWITH_IOURING=ON to use io_uring.";
    exit(1);
}
}
}

#endif  // BRPC_WITH_IOURING
