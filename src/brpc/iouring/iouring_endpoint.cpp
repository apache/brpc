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
#include <liburing.h>
#include <sys/uio.h>
#include <unordered_set>
#include <string.h>

#include <gflags/gflags.h>
#include "butil/atomicops.h"
#include "butil/fd_utility.h"
#include "butil/logging.h"
#include "butil/macros.h"
#include "butil/object_pool.h"
#include "butil/third_party/murmurhash3/murmurhash3.h"
#include "bthread/bthread.h"
#include "brpc/event_dispatcher.h"
#include "brpc/input_messenger.h"
#include "brpc/socket.h"
#include "brpc/reloadable_flags.h"
#include "brpc/iouring/iouring_block_pool.h"
#include "brpc/iouring/iouring_helper.h"
#include "brpc/iouring/iouring_endpoint.h"

DECLARE_int32(task_group_ntags);

namespace brpc {
namespace iouring {

// ---------------------------------------------------------------------------
// gflags (endpoint-level tunables)
// ---------------------------------------------------------------------------

// Each bthread_tag has exactly one Poller bthread (and one io_uring ring).
// io_uring's SQ is single-producer; bthread work-stealing means multiple
// Pollers per tag could run on different pthreads and race on the SQ.
// Horizontal scaling is achieved by increasing --task_group_ntags instead.

DEFINE_bool(iouring_poller_yield, false,
            "Yield (bthread_yield / sched_yield) after each poll iteration. "
            "Reduces CPU usage at the cost of higher tail latency.");

DEFINE_int32(iouring_max_cqe_poll_once, 32,
             "Maximum CQEs reaped per io_uring_peek_batch_cqe() call.");

static const int32_t MAX_INFLIGHT_WRITES = 64;

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

IouringEndpoint::IouringEndpoint(Socket* s)
    : _socket(s)
    , _inflight_writes(0)
{
    _read_slot = {};
}

IouringEndpoint::~IouringEndpoint() {
    Reset();
}

void IouringEndpoint::Reset() {
    DeallocateResources();
    _inflight_writes.store(0, butil::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Resource management
// ---------------------------------------------------------------------------

int IouringEndpoint::AllocateResources() {
    // Register this socket with the Poller via the MPSC op_queue.
    // The Poller thread will dequeue the ADD message on its next iteration
    // and issue the first SubmitRead there – on the Poller thread, without
    // any locking.
    //
    // Registered-buffer mode: acquire a fixed read slot on the Poller thread
    // when processing the ADD message (see main loop).  Nothing to do here.
    PollerAddSid();
    return 0;
}

void IouringEndpoint::DeallocateResources() {
    // Embed the current read slot (if any) in the REMOVE message so the
    // Poller thread can call IouringMemPool::Deallocate(buf) for any
    // in-flight READ_FIXED buffer that never got a CQE.
    PollerRemoveSid(_read_slot);
    _read_slot = {};
}

// ---------------------------------------------------------------------------
// Ring / Poller access
// ---------------------------------------------------------------------------

IouringEndpoint::Poller* IouringEndpoint::GetPoller() const {
    bthread_tag_t tag = bthread_self_tag();
    if (tag < 0 || tag >= static_cast<int>(_poller_groups.size())) { tag = 0; }
    auto& pollers = _poller_groups[tag].pollers;
    const size_t index = butil::fmix32(_socket->id()) % pollers.size();
    if (!pollers[index].ring_initialized) { return nullptr; }
    return &pollers[index];
}

// ---------------------------------------------------------------------------
// IouringPollerHandle – implementation
//
// ring() and Submit() are implemented here (not in the header) because they
// need access to IouringEndpoint::Poller and IouringEndpoint::_poller_groups,
// which are private and not yet fully defined at the point where
// IouringPollerHandle is declared in iouring_helper.h.
// ---------------------------------------------------------------------------

int IouringPollerHandle::Submit(
        std::function<int(::io_uring*)> prepare_fn) const {
    if (tag_ < 0 || tag_ >= static_cast<int>(
            IouringEndpoint::_poller_groups.size())) {
        errno = ENODEV;
        return -1;
    }
    auto& pollers = IouringEndpoint::_poller_groups[tag_].pollers;
    if (index_ < 0 || index_ >= static_cast<int>(pollers.size())) {
        errno = ENODEV;
        return -1;
    }
    IouringEndpoint::Poller& poller = pollers[index_];
    if (!poller.ring_initialized) {
        errno = ENODEV;
        return -1;
    }
    // Called on the Poller thread (passive path only); no lock needed.
    int n = prepare_fn(&poller.ring);
    if (n < 0) { errno = EBUSY; return -1; }
    if (n == 0) { return 0; }
    int ret = io_uring_submit(&poller.ring);
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

// ---------------------------------------------------------------------------
// SubmitOneSqe
//
// Must be called on the Poller thread; no locking.
// Gets one SQE, calls |prepare_fn(sqe)|, issues io_uring_submit().
// Returns io_uring_submit() result (>= 0) or -1 (errno set).
//   errno=ENOBUFS  → SQ full
//   errno=ENODEV   → ring not yet initialised
// ---------------------------------------------------------------------------

int IouringEndpoint::SubmitOneSqe(
        std::function<void(struct io_uring_sqe*)> prepare_fn) {
    Poller* poller = GetPoller();
    if (!poller) { errno = ENODEV; return -1; }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&poller->ring);
    if (!sqe) { errno = ENOBUFS; return -1; }
    prepare_fn(sqe);
    int ret = io_uring_submit(&poller->ring);
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

// ---------------------------------------------------------------------------
// Ring parameters factory
// ---------------------------------------------------------------------------

struct io_uring_params IouringEndpoint::BuildRingParams() {
    struct io_uring_params p;
    memset(&p, 0, sizeof(p));

    const IouringPollingMode mode = GetPollingMode();

    switch (mode) {
    case IouringPollingMode::SQPOLL:
    case IouringPollingMode::HYBRID:
        p.flags |= IORING_SETUP_SQPOLL;
        if (FLAGS_iouring_sqpoll_idle_ms > 0) {
            p.sq_thread_idle = static_cast<unsigned>(FLAGS_iouring_sqpoll_idle_ms);
        }
        if (FLAGS_iouring_sqpoll_cpu >= 0) {
            p.flags |= IORING_SETUP_SQ_AFF;
            p.sq_thread_cpu = static_cast<unsigned>(FLAGS_iouring_sqpoll_cpu);
        }
        break;
    case IouringPollingMode::IOPOLL:
        p.flags |= IORING_SETUP_IOPOLL;
        break;
    case IouringPollingMode::NONE:
    default:
        break;
    }

    if (FLAGS_iouring_cq_size > 0) {
        p.flags |= IORING_SETUP_CQSIZE;
        p.cq_entries = static_cast<unsigned>(FLAGS_iouring_cq_size);
    }

    return p;
}

// ---------------------------------------------------------------------------
// SubmitRead
//
// Registered mode  (--iouring_register_buffers=true):
//   Allocates one block from IouringMemPool (already pre-registered with the
//   ring), stores it in _read_slot, and submits IORING_OP_READ_FIXED.
//   On completion PollCq wraps the block zero-copy in IOBuf; the IOBuf
//   deleter calls IouringMemPool::Deallocate(buf) (thread-safe).
//
// Unregistered mode (--iouring_register_buffers=false):
//   IORING_OP_READ into a per-call malloc bounce buffer (64 KiB).
//   The buffer is owned by IOBuf after PollCq and free()'d when consumed.
// ---------------------------------------------------------------------------

int IouringEndpoint::SubmitRead(int fd) {
    IouringReqContext* ctx = butil::get_object<IouringReqContext>();
    if (!ctx) { errno = ENOMEM; return -1; }
    ctx->op        = IOURING_OP_READ;  // will be overwritten below
    ctx->fd        = fd;
    ctx->socket_id = _socket->id();
    ctx->bounce    = nullptr;

    if (IsFixedBuffersEnabled()) {
        // Registered path – allocate a fresh block from IouringMemPool.
        IouringMemPool& mp  = IouringMemPool::Instance();
        const size_t blksz  = mp.block_size();
        void* buf = mp.Allocate(blksz);
        if (!buf) {
            butil::return_object(ctx);
            errno = ENOMEM;
            return -1;
        }

        // Look up the pre-registered buf_index for this block.
        Poller* poller = GetPoller();
        if (!poller) {
            mp.Deallocate(buf);
            butil::return_object(ctx);
            errno = ENODEV;
            return -1;
        }
        const int buf_index = mp.GetBufIndex(&poller->ring, buf);
        if (buf_index < 0) {
            // Should never happen: the block was just allocated from a
            // registered region.  Log and fail rather than silently.
            LOG(ERROR) << "io_uring SubmitRead: GetBufIndex returned -1 "
                          "for a freshly allocated block; this is a bug.";
            mp.Deallocate(buf);
            butil::return_object(ctx);
            errno = EINVAL;
            return -1;
        }

        // Record the slot so PollCq (and DeallocateResources) can find it.
        _read_slot.buf       = buf;
        _read_slot.buf_index = buf_index;
        _read_slot.size      = blksz;

        ctx->op = IOURING_OP_READ_FIXED;
        int ret = SubmitOneSqe([&](struct io_uring_sqe* sqe) {
            io_uring_prep_read_fixed(sqe, fd,
                                     buf,
                                     static_cast<unsigned>(blksz),
                                     /*offset=*/0,
                                     buf_index);
            sqe->user_data = reinterpret_cast<uint64_t>(ctx) | kBrpcCqeTag;
        });
        if (ret < 0) {
            // Submission failed; release the block immediately.
            mp.Deallocate(buf);
            _read_slot = {};
            butil::return_object(ctx);
            return -1;
        }
        return 0;
    }

    // Unregistered path – allocate a temporary bounce buffer.
    constexpr size_t kBounceSize = 65536;
    void* bounce = malloc(kBounceSize);
    if (!bounce) { butil::return_object(ctx); errno = ENOMEM; return -1; }
    ctx->op = IOURING_OP_READ;

    int ret = SubmitOneSqe([&](struct io_uring_sqe* sqe) {
        io_uring_prep_read(sqe, fd, bounce, static_cast<unsigned>(kBounceSize),
                           /*offset=*/0);
        sqe->user_data = reinterpret_cast<uint64_t>(ctx) | kBrpcCqeTag;
    });
    if (ret < 0) { free(bounce); butil::return_object(ctx); return -1; }
    ctx->bounce = bounce;  // PollCq takes ownership and wraps it in IOBuf
    return 0;
}


ssize_t IouringEndpoint::CutFromIOBufList(butil::IOBuf** from, size_t ndata) {
    CHECK(from != nullptr);
    CHECK(ndata > 0);

    if (!IsWritable()) { errno = EAGAIN; return -1; }

    const int fd = _socket->fd();

    // ------------------------------------------------------------------
    // WRITE_FIXED path (--iouring_register_buffers=true)
    // ------------------------------------------------------------------
    // Every IOBuf block comes from IouringMemPool (a pre-registered slab).
    // GetBufIndex() must always succeed; if it returns -1 a block somehow
    // escaped the pool – LOG(ERROR) and fail rather than silently degrading.
    //
    // One IORING_OP_WRITE_FIXED SQE is submitted per block so the kernel can
    // DMA directly from pinned pages with no per-op get_user_pages overhead.
    // Called on the Poller thread; no locking needed.
    // ------------------------------------------------------------------
    if (IsFixedBuffersEnabled()) {
        Poller* poller = GetPoller();
        if (!poller) { errno = ENODEV; return -1; }
        IouringMemPool& mp = IouringMemPool::Instance();
        struct io_uring* ring = &poller->ring;

        ssize_t total_bytes = 0;
        int sqes_queued = 0;

        for (size_t i = 0; i < ndata; ++i) {
            if (!from[i] || from[i]->empty()) { continue; }
            butil::IOBuf& buf = *from[i];
            while (!buf.empty()) {
                const void* seg_ptr = buf.fetch1();
                size_t      seg_len = buf.backing_block(0).size();
                if (seg_len == 0) { break; }

                int buf_idx = mp.GetBufIndex(ring, seg_ptr);
                if (buf_idx < 0) {
                    // Every block must be registered when
                    // --iouring_register_buffers=true.  A -1 here means a
                    // block escaped the pool – this is a programming error.
                    LOG(ERROR) << "io_uring: unregistered IOBuf block ptr="
                               << seg_ptr << " fd=" << fd
                               << "; submission aborted.";
                    errno = EINVAL;
                    break;
                }

                struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
                if (!sqe) { errno = ENOBUFS; break; }

                io_uring_prep_write_fixed(sqe, fd,
                                          const_cast<void*>(seg_ptr),
                                          static_cast<unsigned>(seg_len),
                                          0, buf_idx);
                IouringReqContext* ctx = butil::get_object<IouringReqContext>();
                if (!ctx) { errno = ENOMEM; break; }
                ctx->op        = IOURING_OP_WRITE_FIXED;
                ctx->fd        = fd;
                ctx->socket_id = _socket->id();
                ctx->bounce    = nullptr;
                sqe->user_data = reinterpret_cast<uint64_t>(ctx) | kBrpcCqeTag;
                total_bytes += static_cast<ssize_t>(seg_len);
                ++sqes_queued;
                buf.pop_front(seg_len);
            }
        }

        if (sqes_queued > 0) {
            int ret = io_uring_submit(ring);
            if (ret < 0) {
                errno = -ret;
                return total_bytes > 0 ? total_bytes : -1;
            }
            _inflight_writes.fetch_add(sqes_queued, butil::memory_order_relaxed);
        }
        return total_bytes > 0 ? total_bytes : (sqes_queued == 0 ? 0 : -1);
    }

    // ------------------------------------------------------------------
    // Plain WRITEV path (--iouring_register_buffers=false)
    // ------------------------------------------------------------------
    // Collect all iovec entries from the IOBuf list and submit a single
    // IORING_OP_WRITEV.  Cap at IOURING_IOV_MAX to stay within SQ limits.
    // ------------------------------------------------------------------
    static const size_t IOURING_IOV_MAX = 256;
    std::vector<struct iovec> iov;
    ssize_t total_bytes = 0;

    for (size_t i = 0; i < ndata; ++i) {
        if (!from[i] || from[i]->empty()) { continue; }
        butil::IOBuf& buf = *from[i];
        while (!buf.empty() && iov.size() < IOURING_IOV_MAX) {
            const void* seg_ptr = buf.fetch1();
            size_t      seg_len = buf.backing_block(0).size();
            if (seg_len == 0) { break; }
            iov.push_back({const_cast<void*>(seg_ptr), seg_len});
            total_bytes += static_cast<ssize_t>(seg_len);
            buf.pop_front(seg_len);
        }
        if (iov.size() >= IOURING_IOV_MAX) { break; }
    }

    if (iov.empty()) { return 0; }

    IouringReqContext* ctx = butil::get_object<IouringReqContext>();
    if (!ctx) { errno = ENOMEM; return -1; }
    ctx->op        = IOURING_OP_WRITE;
    ctx->fd        = fd;
    ctx->socket_id = _socket->id();
    ctx->bounce    = nullptr;

    int ret = SubmitOneSqe([&](struct io_uring_sqe* sqe) {
        io_uring_prep_writev(sqe, fd, iov.data(),
                             static_cast<unsigned>(iov.size()), 0);
        sqe->user_data = reinterpret_cast<uint64_t>(ctx) | kBrpcCqeTag;
    });
    if (ret < 0) { butil::return_object(ctx); return -1; }
    _inflight_writes.fetch_add(1, butil::memory_order_relaxed);
    return total_bytes;
}

bool IouringEndpoint::IsWritable() const {
    return _inflight_writes.load(butil::memory_order_relaxed) < MAX_INFLIGHT_WRITES;
}

// ---------------------------------------------------------------------------
// PollCq – CQE completion handler
//
// READ_FIXED path (--iouring_register_buffers=true, zero-copy)
// -------------------------------------------------------------
// The kernel has written |res| bytes directly into ep->_read_slot.buf (a
// pre-registered, pinned page).  IOBuf::append_user_data() wraps those bytes
// without copying; the deleter calls IouringMemPool::Deallocate(buf)
// (thread-safe) when the last IOBuf reference is dropped.
// A fresh block is allocated from IouringMemPool for the next READ_FIXED.
//
// READ path (--iouring_register_buffers=false)
// --------------------------------------------
// ctx->bounce points to a per-call malloc'd bounce buffer (64 KiB).
// IOBuf takes ownership (free() destructor) when the data is wrapped.
// ---------------------------------------------------------------------------

void IouringEndpoint::PollCq(Socket* m) {
    IouringEndpoint* ep = static_cast<IouringEndpoint*>(m->user());
    if (!ep) { return; }

    SocketUniquePtr s;
    if (Socket::Address(ep->_socket->id(), &s) < 0) { return; }

    // CQ-side operations need only the ring pointer; no lock required here
    // because io_uring_peek_batch_cqe / io_uring_cqe_seen operate on the CQ
    // which is only touched by the Poller thread.
    Poller* poller = ep->GetPoller();
    if (!poller) { return; }
    struct io_uring* ring = &poller->ring;

    struct io_uring_cqe* cqes[FLAGS_iouring_max_cqe_poll_once];
    InputMessageClosure last_msg;
    int progress = Socket::PROGRESS_INIT;

    while (true) {
        const int cnt = io_uring_peek_batch_cqe(
            ring, cqes, static_cast<unsigned>(FLAGS_iouring_max_cqe_poll_once));

        if (cnt <= 0) {
            if (s->Failed()) { return; }
            if (!m->MoreReadEvents(&progress)) { break; }
            continue;
        }

        ssize_t bytes_read = 0;

        for (int i = 0; i < cnt; ++i) {
            struct io_uring_cqe* cqe = cqes[i];
            const uint64_t udata = cqe->user_data;

            // Only process CQEs that bRPC submitted (bit 63 == 1).
            // All other CQEs are user-submitted; leave them in the ring so the
            // user callback (called right after PollCq returns) can
            // drain and handle them.  Users need no special tagging – bit 63
            // is never set in a canonical user-space pointer or small integer.
            if (!(udata & kBrpcCqeTag)) {
                continue;  // do NOT call io_uring_cqe_seen()
            }

            // Strip the tag bit to recover the original IouringReqContext*.
            // udata == kBrpcCqeTag (NOP wake-up SQE) → ctx will be nullptr.
            IouringReqContext* ctx =
                reinterpret_cast<IouringReqContext*>(
                    static_cast<uintptr_t>(udata & ~kBrpcCqeTag));

            if (!ctx) {
                io_uring_cqe_seen(ring, cqe);
                continue;
            }

            const int res = cqe->res;
            io_uring_cqe_seen(ring, cqe);

            // ---------------------------------------------------------------
            // Error handling
            // ---------------------------------------------------------------
            if (res < 0) {
                if (res == -ECANCELED) {
                    // Op was cancelled.
                    // READ_FIXED: _read_slot still holds the block that was
                    // submitted for this CQE.  DeallocateResources will pass
                    // it in the REMOVE SidOp so the Poller thread can call
                    // IouringMemPool::Deallocate(buf).
                    // IOURING_OP_READ: free the malloc'd bounce buffer.
                    if (ctx->op == IOURING_OP_READ) { free(ctx->bounce); }
                    butil::return_object(ctx);
                    continue;
                }

                const int saved_errno = -res;
                LOG(WARNING) << "io_uring CQE error fd=" << ctx->fd
                             << " socket_id=" << ctx->socket_id
                             << " op=" << (int)ctx->op
                             << ": " << berror(saved_errno);
                SocketUniquePtr cs;
                if (Socket::Address(ctx->socket_id, &cs) == 0) {
                    cs->SetFailed(saved_errno, "io_uring op error: %s",
                                  berror(saved_errno));
                }
                if (ctx->op == IOURING_OP_READ) { free(ctx->bounce); }
                butil::return_object(ctx);
                continue;
            }

            // ---------------------------------------------------------------
            // Dispatch by operation type
            // ---------------------------------------------------------------
            if (ctx->op == IOURING_OP_READ || ctx->op == IOURING_OP_READ_FIXED) {
                if (res == 0) {
                    // EOF
                    SocketUniquePtr cs;
                    if (Socket::Address(ctx->socket_id, &cs) == 0) {
                        cs->SetEOF();
                    }
                    if (ctx->op == IOURING_OP_READ) { free(ctx->bounce); }
                    butil::return_object(ctx);
                    continue;
                }

                if (ctx->op == IOURING_OP_READ_FIXED) {
                    // ---------------------------------------------------------
                    // Registered mode – zero-copy.
                    //
                    // The kernel has written |res| bytes into the block at
                    // ep->_read_slot.buf (a page from IouringMemPool that is
                    // permanently pinned for this ring).
                    //
                    // Rotation:
                    //   1. Steal the current slot from the endpoint.
                    //   2. Submit the next READ_FIXED immediately (SubmitRead
                    //      allocates a new block from IouringMemPool).
                    //   3. Wrap the data zero-copy in IOBuf; the deleter
                    //      calls IouringMemPool::Deallocate(buf) on any thread
                    //      when the last reference drops (thread-safe).
                    //
                    // No separate slot pool is needed: every IouringMemPool
                    // block already has a permanent buf_index, and
                    // Allocate/Deallocate are its own reference count.
                    // ---------------------------------------------------------
                    IouringReadSlot consumed_slot = ep->_read_slot;
                    ep->_read_slot = {};

                    // Submit the next read before handing off the consumed
                    // slot so back-to-back arrivals never stall.
                    ep->SubmitRead(ctx->fd);

                    // Zero-copy: wrap the consumed buffer in IOBuf.
                    // The lambda captures consumed_slot by value; it is
                    // self-contained and can run on any thread.
                    void* const buf = consumed_slot.buf;
                    butil::IOBuf tmp;
                    tmp.append_user_data(
                        buf,
                        static_cast<size_t>(res),
                        [buf](void* /*ptr*/) {
                            IouringMemPool::Instance().Deallocate(buf);
                        });
                    m->_read_buf.append(std::move(tmp));
                } else {
                    // ---------------------------------------------------------
                    // Unregistered mode – bounce buffer.
                    //
                    // The bounce buffer was malloc'd in SubmitRead and stored in
                    // ctx->bounce.  Transfer ownership to IOBuf (free() is
                    // called when IOBuf discards the block).
                    // ---------------------------------------------------------
                    butil::IOBuf tmp;
                    tmp.append_user_data(ctx->bounce,
                                         static_cast<size_t>(res),
                                         free);
                    ctx->bounce = nullptr;  // ownership transferred
                    m->_read_buf.append(std::move(tmp));

                    ep->SubmitRead(ctx->fd);
                }

                bytes_read += res;

            } else {
                // WRITE / WRITE_FIXED completion
                IouringEndpoint* dep = ep;
                if (ctx->socket_id != ep->_socket->id()) {
                    SocketUniquePtr cs;
                    if (Socket::Address(ctx->socket_id, &cs) == 0) {
                        dep = static_cast<IouringEndpoint*>(cs->user());
                    }
                }
                if (dep) {
                    dep->_inflight_writes.fetch_sub(1, butil::memory_order_relaxed);
                    dep->_socket->WakeAsEpollOut();
                }
            }

            butil::return_object(ctx);
        }  // for each cqe

        if (bytes_read > 0) {
            const int64_t received_us   = butil::cpuwide_time_us();
            const int64_t base_realtime = butil::gettimeofday_us() - received_us;
            InputMessenger* messenger = static_cast<InputMessenger*>(s->user());
            if (messenger && messenger->ProcessNewMessage(
                        s.get(), bytes_read, false,
                        received_us, base_realtime, last_msg) < 0) {
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// DebugInfo
// ---------------------------------------------------------------------------

void IouringEndpoint::DebugInfo(std::ostream& os,
                                butil::StringPiece connector) const {
    os << "iouring_polling_mode=" << FLAGS_iouring_polling_mode
       << connector
       << "iouring_inflight_writes="
       << _inflight_writes.load(butil::memory_order_relaxed)
       << connector << "iouring_writable=" << IsWritable()
       << connector << "iouring_register_buffers=" << IsFixedBuffersEnabled();
    if (IsFixedBuffersEnabled() && _read_slot.buf != nullptr) {
        os << " buf_index=" << _read_slot.buf_index
           << " slot_size="  << _read_slot.size;
    }
}

// ---------------------------------------------------------------------------
// GlobalInitialize / GlobalRelease
// ---------------------------------------------------------------------------

int IouringEndpoint::GlobalInitialize() {
    _poller_groups = std::vector<PollerGroup>(FLAGS_task_group_ntags);
    return 0;
}

void IouringEndpoint::GlobalRelease() {
    for (int i = 0; i < FLAGS_task_group_ntags; ++i) {
        PollingModeRelease(i);
    }
}

// ---------------------------------------------------------------------------
// PollerDrainOpQueue
//
// Dequeues all pending SidOps from poller->op_queue and applies them:
//   ADD    – track the socket, allocate a block from IouringMemPool (fixed
//             mode) via SubmitRead, issue the first read.
//   REMOVE – return any in-flight read buffer to IouringMemPool, stop
//             tracking the socket.
//
// Must run exclusively on the Poller thread.
// ---------------------------------------------------------------------------

void IouringEndpoint::PollerDrainOpQueue(
        Poller* poller,
        std::unordered_set<SocketId>& tracked_sids) {
    SidOp op;
    while (poller->op_queue.Dequeue(op)) {
        if (op.type == SidOp::ADD) {
            tracked_sids.emplace(op.sid);
            SocketUniquePtr s_add;
            if (Socket::Address(op.sid, &s_add) == 0) {
                IouringEndpoint* ep =
                    static_cast<IouringEndpoint*>(s_add->user());
                if (ep) {
                    // Issue the first SubmitRead on the Poller thread.
                    // In fixed-buffer mode SubmitRead allocates a block from
                    // IouringMemPool; no separate slot pool is needed.
                    if (ep->SubmitRead(s_add->fd()) < 0) {
                        LOG(WARNING)
                            << "IouringEndpoint: first SubmitRead "
                               "failed for socket "
                            << op.sid << ": " << berror();
                    }
                }
            }
        } else {
            // REMOVE: if a READ_FIXED was in flight (buf != null), the kernel
            // has not yet written into it.  Return the block to IouringMemPool
            // now; no CQE will arrive for a cancelled READ_FIXED (it arrives
            // as -ECANCELED and is freed in PollCq's error path instead).
            // For safety we Deallocate here only when _read_slot is still
            // owned by the endpoint (i.e. SubmitRead set it but PollCq has
            // not yet consumed it).
            if (IsFixedBuffersEnabled() && op.read_slot.buf != nullptr) {
                IouringMemPool::Instance().Deallocate(op.read_slot.buf);
            }
            tracked_sids.erase(op.sid);
        }
    }
}

// ---------------------------------------------------------------------------
// PollingModeInitialize
// ---------------------------------------------------------------------------

std::vector<IouringEndpoint::PollerGroup> IouringEndpoint::_poller_groups;

int IouringEndpoint::PollingModeInitialize(
        bthread_tag_t tag,
        std::function<void(IouringPollerHandle)> callback,
        std::function<void(IouringPollerHandle)> init_fn,
        std::function<void(IouringPollerHandle)> release_fn) {

    if (tag < 0 || tag >= static_cast<int>(_poller_groups.size())) {
        LOG(ERROR) << "io_uring: invalid bthread tag " << tag;
        return -1;
    }

    auto& group   = _poller_groups[tag];
    auto& pollers = group.pollers;
    auto& running = group.running;

    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) { return 0; }

    // -----------------------------------------------------------------------
    // Poller thread arguments
    // -----------------------------------------------------------------------
    struct FnArgs {
        Poller*            poller;
        std::atomic<bool>* running;
        bthread_tag_t      tag;
        int                index;   // poller index within the group
    };

    // -----------------------------------------------------------------------
    // Poller thread body
    // -----------------------------------------------------------------------
    auto fn = [](void* p) -> void* {
        std::unique_ptr<FnArgs> args(static_cast<FnArgs*>(p));
        Poller*            poller  = args->poller;
        std::atomic<bool>* running = args->running;

        // 1. Create the ring.
        struct io_uring_params params = BuildRingParams();
        const unsigned sq_size = static_cast<unsigned>(GetIouringSqSize());

        int ret = io_uring_queue_init_params(sq_size, &poller->ring, &params);
        if (ret < 0) {
            LOG(ERROR) << "io_uring_queue_init_params failed: " << berror(-ret);
            running->store(false, std::memory_order_relaxed);
            return nullptr;
        }
        poller->ring_initialized = true;

        // 2. Initialise the fixed-buffer infrastructure.
        if (IsFixedBuffersEnabled()) {
            // 2a. Register this ring with IouringMemPool.
            // The callback is called for each existing region immediately
            // (to bring the ring up to date) and for each future region
            // (when the pool grows).  It issues register_buffers_update /
            // full re-registration to pin the new pages in this ring.
            IouringMemPool::Instance().AddRingRegistrar(
                &poller->ring,
                [poller](void* base, size_t size, size_t block_size,
                         int buf_index_base) {
                    // Build one iovec per block in this new region.
                    const int n = static_cast<int>(size / block_size);
                    std::vector<struct iovec> iovs(n);
                    for (int i = 0; i < n; ++i) {
                        iovs[i].iov_base =
                            static_cast<char*>(base) + i * block_size;
                        iovs[i].iov_len  = block_size;
                    }
                    // Register the new region's buffers.
                    // io_uring_register_buffers_update (kernel >= 5.13) allows
                    // incremental updates; fall back to a full re-registration
                    // on older kernels or if the function is unavailable.
                    int ret = -ENOSYS;
#ifdef IORING_REGISTER_BUFFERS_UPDATE
                    ret = io_uring_register_buffers_update(
                        &poller->ring,
                        static_cast<unsigned>(buf_index_base),
                        iovs.data(),
                        static_cast<unsigned>(n));
#endif
                    if (ret < 0) {
                        // Full re-registration path.
                        // For the first region just call register_buffers.
                        // For subsequent regions we must rebuild the complete
                        // table; here we only have the new slice so we
                        // attempt a best-effort register and log on failure.
                        if (buf_index_base > 0) {
                            // Unregister previous table before re-registering.
                            io_uring_unregister_buffers(&poller->ring);
                        }
                        int r2 = io_uring_register_buffers(&poller->ring,
                                                           iovs.data(),
                                                           static_cast<unsigned>(n));
                        if (r2 < 0) {
                            LOG(ERROR)
                                << "io_uring_register_buffers failed for new "
                                   "region (buf_index_base=" << buf_index_base
                                << "): " << berror(-r2)
                                << " – any IOBuf block from this region will "
                                   "cause EINVAL on the write path "
                                   "(no WRITEV fallback in fixed-buffer mode).";
                        }
                    }
                });

            LOG(INFO) << "io_uring fixed-buffer mode active: "
                         "READ_FIXED/WRITE_FIXED enabled.";
        }

        if (poller->init_fn) {
            poller->init_fn(IouringPollerHandle(args->tag, args->index));
        }

        // 3. CQE reap strategy.
        const IouringPollingMode mode = GetPollingMode();
        const int hybrid_spins = FLAGS_iouring_hybrid_spin_count;
        struct io_uring_cqe* cqes[FLAGS_iouring_max_cqe_poll_once];  // used by SQPOLL/HYBRID peek only
        std::unordered_set<SocketId> tracked_sids;
        SidOp op;

        // 4. Main loop.
        while (running->load(std::memory_order_relaxed)) {
            // a) Drain op_queue.
            PollerDrainOpQueue(poller, tracked_sids);

            // b) Reap CQEs.
            bool got_cqe = false;  // used by SQPOLL/IOPOLL/HYBRID branches

            if (mode == IouringPollingMode::NONE) {
                // Interrupt-driven mode: block up to 1 ms waiting for a CQE.
                // The 1 ms timeout keeps the Poller loop responsive to new
                // connections arriving in op_queue without burning CPU when
                // there is no I/O traffic.
                struct io_uring_cqe* cqe = nullptr;
                struct __kernel_timespec ts{0, 1000000};  // 1 ms
                int r = io_uring_wait_cqe_timeout(&poller->ring, &cqe, &ts);
                if (r == 0 && cqe) {
                    io_uring_cqe_seen(&poller->ring, cqe);
                }

            } else if (mode == IouringPollingMode::IOPOLL) {
                // IOPOLL (IORING_SETUP_IOPOLL): the kernel never generates
                // interrupts; CQEs are posted only after an explicit
                // io_uring_submit() triggers the poll.  Use peek to drain
                // whatever is already available, then yield to avoid
                // spinning at 100 % when there is no block I/O in flight.
                int cnt = io_uring_peek_batch_cqe(
                    &poller->ring, cqes,
                    static_cast<unsigned>(FLAGS_iouring_max_cqe_poll_once));
                got_cqe = (cnt > 0);
                if (!got_cqe && FLAGS_iouring_poller_yield) {
                    bthread_yield();
                }

            } else if (mode == IouringPollingMode::SQPOLL) {
                // SQPOLL (IORING_SETUP_SQPOLL): the kernel SQ thread submits
                // I/O automatically; just peek for completed CQEs.
                int cnt = io_uring_peek_batch_cqe(
                    &poller->ring, cqes,
                    static_cast<unsigned>(FLAGS_iouring_max_cqe_poll_once));
                got_cqe = (cnt > 0);
                if (!got_cqe && FLAGS_iouring_poller_yield) {
                    bthread_yield();
                }

            } else {
                // HYBRID: busy-spin N times, then fall back to a zero-timeout
                // wait so the Poller thread blocks rather than burning CPU
                // when no CQE arrives.
                for (int spin = 0; spin < hybrid_spins && !got_cqe; ++spin) {
                    int cnt = io_uring_peek_batch_cqe(
                        &poller->ring, cqes,
                        static_cast<unsigned>(FLAGS_iouring_max_cqe_poll_once));
                    got_cqe = (cnt > 0);
                }
                if (!got_cqe) {
                    struct io_uring_cqe* cqe = nullptr;
                    struct __kernel_timespec ts_zero{0, 0};
                    io_uring_wait_cqe_timeout(&poller->ring, &cqe, &ts_zero);
                }
            }

            // c) Dispatch to each socket.
            for (SocketId sid : tracked_sids) {
                SocketUniquePtr s;
                if (Socket::Address(sid, &s) < 0) { continue; }
                IouringEndpoint::PollCq(s.get());
            }

            // d) Optional user callback.  Runs on the Poller thread after
            // every PollCq pass.  The handle gives safe access to ring()
            // for CQ draining and Submit() for SQ submission.
            if (poller->callback) {
                poller->callback(IouringPollerHandle(args->tag, args->index));
            }

            if (FLAGS_iouring_poller_yield &&
                mode != IouringPollingMode::SQPOLL) {
                bthread_yield();
            }
        }  // while running

        // 5. Tear-down.
        if (poller->release_fn) {
            poller->release_fn(IouringPollerHandle(args->tag, args->index));
        }

        if (poller->ring_initialized) {
            // Unregister this ring from the global mem-pool before destroying
            // the ring so no future region growth tries to update a dead ring.
            if (IsFixedBuffersEnabled()) {
                IouringMemPool::Instance().RemoveRingRegistrar(&poller->ring);
            }
            io_uring_queue_exit(&poller->ring);
            poller->ring_initialized = false;
        }

        return nullptr;
    };  // lambda

    // Start the single Poller bthread for this tag.
    for (int i = 0; i < 1; ++i) {
        auto* fargs = new FnArgs{&pollers[i], &running, tag, i};
        bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
        attr.tag = tag;
        bthread_attr_set_name(&attr, "IouringPoller");
        pollers[i].callback   = callback;
        pollers[i].init_fn    = init_fn;
        pollers[i].release_fn = release_fn;

        if (bthread_start_background(&pollers[i].tid, &attr, fn, fargs) != 0) {
            LOG(ERROR) << "Fail to start io_uring poller bthread tag=" << tag
                       << " index=" << i;
            delete fargs;
            running.store(false, std::memory_order_relaxed);
            return -1;
        }
    }
    return 0;
}

void IouringEndpoint::PollingModeRelease(bthread_tag_t tag) {
    if (tag < 0 || tag >= static_cast<int>(_poller_groups.size())) { return; }

    auto& group   = _poller_groups[tag];
    auto& pollers = group.pollers;
    auto& running = group.running;

    running.store(false, std::memory_order_relaxed);

    if (pollers[0].tid != INVALID_BTHREAD) {
        bthread_join(pollers[0].tid, nullptr);
        pollers[0].tid = INVALID_BTHREAD;
    }
}

// ---------------------------------------------------------------------------
// PollerAddSid / PollerRemoveSid
// ---------------------------------------------------------------------------

void IouringEndpoint::PollerAddSid() {
    bthread_tag_t tag = bthread_self_tag();
    if (tag < 0 || tag >= static_cast<int>(_poller_groups.size())) { tag = 0; }
    auto& pollers = _poller_groups[tag].pollers;
    const size_t index = butil::fmix32(_socket->id()) % pollers.size();
    pollers[index].op_queue.Enqueue(SidOp{_socket->id(), SidOp::ADD});
}

void IouringEndpoint::PollerRemoveSid(const IouringReadSlot& slot) {
    bthread_tag_t tag = bthread_self_tag();
    if (tag < 0 || tag >= static_cast<int>(_poller_groups.size())) { tag = 0; }
    auto& pollers = _poller_groups[tag].pollers;
    const size_t index = butil::fmix32(_socket->id()) % pollers.size();
    pollers[index].op_queue.Enqueue(SidOp{_socket->id(), SidOp::REMOVE, slot});
}

}  // namespace iouring
}  // namespace brpc

#endif  // BRPC_WITH_IOURING
