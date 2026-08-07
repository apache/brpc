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

#ifndef BRPC_IOURING_ENDPOINT_H
#define BRPC_IOURING_ENDPOINT_H

#if BRPC_WITH_IOURING

#include <liburing.h>
#include <pthread.h>
#include <functional>
#include <vector>
#include <unordered_set>
#include "brpc/iouring/iouring_helper.h"   // IouringPollerHandle, kBrpcCqeTag, IouringPollingMode
#include "butil/atomicops.h"
#include "butil/iobuf.h"
#include "butil/macros.h"
#include "butil/containers/mpsc_queue.h"
#include "brpc/socket.h"
#include "brpc/iouring/iouring_block_pool.h"

namespace brpc {
class Socket;
class TcpTransport;
namespace iouring {

// Tag to identify the operation type in user_data of SQE / CQE.
//
// Design note: there are exactly two modes, controlled by
// --iouring_register_buffers.  The two modes are mutually exclusive and
// never mixed within a single endpoint:
//
//   registered   → IOURING_OP_READ_FIXED  + IOURING_OP_WRITE_FIXED
//   unregistered → IOURING_OP_READ        + IOURING_OP_WRITE
//
// No partial / per-block fallback exists.  If fixed-buffer initialisation
// fails the entire io_uring transport is disabled.
enum IouringOpType : uint8_t {
    IOURING_OP_READ        = 0,  // IORING_OP_READ    (--iouring_register_buffers=false)
    IOURING_OP_WRITE       = 1,  // IORING_OP_WRITEV  (--iouring_register_buffers=false)
    IOURING_OP_READ_FIXED  = 2,  // IORING_OP_READ_FIXED  (--iouring_register_buffers=true)
    IOURING_OP_WRITE_FIXED = 3,  // IORING_OP_WRITE_FIXED (--iouring_register_buffers=true)
};

// Per-request context stored as user_data in SQE / CQE.
//
// bounce is non-null only for IOURING_OP_READ (unregistered mode);
// it points to the malloc'd bounce buffer allocated in SubmitRead.
// PollCq wraps it in IOBuf (which takes ownership and calls free()) and
// re-submits the next plain READ.
//
// owned_data holds a reference to the IOBuf backing WRITE / WRITE_FIXED
// SQEs.  The pointers handed to the kernel (iovec bases / registered
// buffers) point directly into these blocks, so the reference MUST stay
// alive from submission until the completion CQE is reaped in PollCq;
// otherwise the blocks could be freed (and reused) while the kernel is
// still reading them asynchronously. Unused for READ / READ_FIXED.
//
// owned_iov holds the iovec ARRAY itself for IOURING_OP_WRITE (plain
// IORING_OP_WRITEV). The sqe only stores a pointer to this array
// (io_uring_prep_writev(sqe, fd, iov.data(), ...)); unlike a synchronous
// writev(2), the kernel does not necessarily copy the iovec array during
// io_uring_submit() -- kernels before 5.5 (i.e. that don't report
// IORING_FEAT_SUBMIT_STABLE) keep referencing the caller-provided iovec
// array until the request COMPLETES, not just until it is submitted. If
// the std::vector backing that array were a stack-local in
// PollerSubmitWrite(), it would be destroyed as soon as that function
// returns -- freeing memory the kernel may still read from, a
// use-after-free. So the vector must be kept alive here, alongside
// owned_data, until the CQE is reaped. Unused for every op except
// IOURING_OP_WRITE.
struct IouringReqContext {
    IouringOpType   op;         // operation type
    int             fd;         // file descriptor
    SocketId        socket_id;  // owning socket id
    void*           bounce{nullptr};  // unregistered-mode bounce buf (may be null)
    butil::IOBuf    owned_data;       // keeps write buffers alive until CQE
    std::vector<struct iovec> owned_iov;  // keeps the iovec[] alive until CQE
                                            // (IOURING_OP_WRITE only)
};

// ---------------------------------------------------------------------------
// IouringEndpoint – per-Socket async I/O endpoint backed by an io_uring ring.
//
// Two I/O modes, selected once at startup by --iouring_register_buffers:
//
// Registered-buffer mode (--iouring_register_buffers=true)
// ----------------------------------------------------------
// Every IOBuf block comes from IouringMemPool (a pre-registered slab).
// No separate per-Poller slot pool is required: because every IouringMemPool
// block already has a permanent buf_index, we can Allocate() a block on
// demand, read into it, and Deallocate() it from the IOBuf zero-copy deleter
// on any thread without any additional bookkeeping.
//
//   AllocateResources()
//     Posts an ADD SidOp to the Poller's op_queue.  The Poller thread
//     allocates a block from IouringMemPool and issues the first SubmitRead
//     there – entirely on the Poller thread.
//
//   SubmitRead()
//     Allocates a fresh IouringMemPool block, stores it in _read_slot,
//     then issues IORING_OP_READ_FIXED into _read_slot.buf / buf_index.
//
//   PollCq()  (READ_FIXED branch)
//     res bytes are already in the slot's pinned memory.
//     IOBuf::append_user_data() wraps them zero-copy; the deleter calls
//     IouringMemPool::Deallocate(buf) (thread-safe) when the last ref drops.
//     A fresh block is allocated immediately and the next READ_FIXED is queued.
//
//   CutFromIOBufList()
//     Every IOBuf block comes from the registered slab; each block gets its
//     own IORING_OP_WRITE_FIXED SQE (no WRITEV fallback, no mixed batches).
//     Submission itself happens on the Poller thread (see PollerSubmitWrite);
//     this method only takes a reference to the data and enqueues a WRITE
//     SidOp (see "Thread safety" below).
//
//   DeallocateResources()
//     Posts a REMOVE SidOp.  If the endpoint holds a live _read_slot (a
//     READ_FIXED was submitted but no CQE arrived yet) the buf is returned to
//     IouringMemPool; otherwise _read_slot is zero and nothing is freed.
//
// Unregistered mode (--iouring_register_buffers=false)
// ------------------------------------------------------
//   SubmitRead()       → IORING_OP_READ into a per-call malloc bounce buffer.
//   CutFromIOBufList() → one IORING_OP_WRITEV per call, submitted on the
//                        Poller thread (see PollerSubmitWrite).
//
// Thread safety
// -------------
// io_uring's SQ is single-producer: io_uring_get_sqe()/io_uring_submit() may
// ONLY be called from the Poller thread that owns the ring (SubmitRead,
// PollerSubmitWrite, SubmitOneSqe all assume this and are called there).
//
// CutFromIOBufList() is the one entry point NOT called on the Poller thread
// -- it runs on whichever bthread is doing Socket::KeepWrite(). It therefore
// never touches the ring directly. Instead it:
//   1. Takes a reference to the caller's data (IOBuf::append(), which shares
//      the underlying blocks and bumps their refcount rather than copying),
//   2. pop_front()s the caller's IOBuf so Socket::IsWriteComplete() sees the
//      bytes as sent,
//   3. Enqueues a SidOp::WRITE carrying that reference through the same MPSC
//      op_queue used by AllocateResources/DeallocateResources.
// The Poller thread dequeues it in PollerDrainOpQueue() and calls
// PollerSubmitWrite(), which does the actual io_uring_get_sqe()/submit() and
// keeps the IOBuf reference alive in IouringReqContext::owned_data until the
// completion CQE is reaped by PollCq() -- this is required because the
// kernel reads directly from that memory asynchronously, well after
// CutFromIOBufList() (and any pop_front()) have returned.
// ---------------------------------------------------------------------------

class BAIDU_CACHELINE_ALIGNMENT IouringEndpoint : public SocketUser {
friend class ::brpc::Socket;
friend class ::brpc::TcpTransport;
friend class IouringPollerHandle;   // needs _poller_groups and Poller
public:
    explicit IouringEndpoint(Socket* s);
    ~IouringEndpoint() override;

    static int  GlobalInitialize();
    static void GlobalRelease();

    void Reset();

    // Submit async read.
    //   registered mode   → IORING_OP_READ_FIXED into _read_slot
    //   unregistered mode → IORING_OP_READ into a malloc bounce buffer
    int SubmitRead(int fd);

    // Cut data from IOBuf list and hand it off to the Poller thread, which
    // performs the actual SQE submission (see PollerDrainOpQueue). This
    // keeps all io_uring_get_sqe()/io_uring_submit() calls confined to the
    // single-producer Poller thread and keeps the backing memory alive
    // (via SidOp::write_data / IouringReqContext::owned_data) until the
    // kernel has actually consumed it.
    ssize_t CutFromIOBufList(butil::IOBuf** data, size_t ndata);

    bool IsWritable() const;

    static void PollCq(Socket* m);

    static int  PollingModeInitialize(
                    bthread_tag_t tag,
                    std::function<void(IouringPollerHandle)> callback,
                    std::function<void(IouringPollerHandle)> init_fn,
                    std::function<void(IouringPollerHandle)> release_fn);
    static void PollingModeRelease(bthread_tag_t tag);


    void DebugInfo(std::ostream& os,
                   butil::StringPiece connector = "\n") const;

private:
    int  AllocateResources();
    void DeallocateResources();

    void PollerAddSid();
    void PollerRemoveSid(const IouringReadSlot& slot = IouringReadSlot{});

    // The IouringEndpoint attached to a Socket lives inside its TcpTransport
    // (TcpTransport::_iouring_ep), NOT in Socket::_user (which is reserved
    // for the protocol-level object, e.g. InputMessenger). IouringEndpoint
    // is a friend of Socket, so it may reach into the private `_transport'
    // member here to fetch the endpoint that TcpTransport::Init() attached.
    // Returns nullptr if the socket has no TcpTransport / io_uring endpoint.
    static IouringEndpoint* GetAttachedEndpoint(Socket* s);

    // -----------------------------------------------------------------------
    // Per-endpoint state
    // -----------------------------------------------------------------------
    Socket*                    _socket;
    butil::atomic<int32_t>     _inflight_writes;

    // Fixed read slot (registered mode only; always valid after AllocateResources).
    IouringReadSlot            _read_slot;

    DISALLOW_COPY_AND_ASSIGN(IouringEndpoint);

    // -----------------------------------------------------------------------
    // Per-poller state
    // -----------------------------------------------------------------------
    struct SidOp {
        enum OpType {
            ADD,
            REMOVE,
            WRITE,
        };
        SocketId       sid;
        OpType         type;
        // Meaningful for REMOVE in fixed-buffer mode: the slot currently held
        // by the endpoint when DeallocateResources() was called.  The Poller
        // thread calls IouringMemPool::Deallocate(read_slot.buf) if buf != null.
        IouringReadSlot read_slot;
        // Meaningful for WRITE: the data to submit. Holds a reference to the
        // caller's IOBuf blocks (see CutFromIOBufList) so the memory stays
        // alive from the calling thread all the way to SQE submission on
        // the Poller thread.
        butil::IOBuf    write_data;

        SidOp() : sid(0), type(ADD), read_slot() {}
        SidOp(SocketId s, OpType t, IouringReadSlot rs = IouringReadSlot{})
            : sid(s), type(t), read_slot(rs) {}
    };

    struct BAIDU_CACHELINE_ALIGNMENT Poller {
        bthread_t tid{INVALID_BTHREAD};
        butil::MPSCQueue<SidOp, butil::ObjectPoolAllocator<SidOp>> op_queue;

        // Called on the Poller thread with the handle bound to this Poller.
        std::function<void(IouringPollerHandle)> callback;
        std::function<void(IouringPollerHandle)> init_fn;
        std::function<void(IouringPollerHandle)> release_fn;

        struct io_uring ring{};
        bool            ring_initialized{false};

    };

    // Drain all pending SidOps from poller->op_queue.
    // Must be called exclusively on the Poller thread.
    static void PollerDrainOpQueue(Poller* poller,
                                   std::unordered_set<SocketId>& tracked_sids);

    struct BAIDU_CACHELINE_ALIGNMENT PollerGroup {
        // Exactly one Poller per bthread_tag (SQ single-producer constraint).
        PollerGroup() : pollers(1), running(false) {}
        std::vector<Poller> pollers;
        std::atomic<bool>   running;
    };

    static std::vector<PollerGroup> _poller_groups;
    static struct io_uring_params BuildRingParams();

    // Return the Poller that owns this endpoint's ring.
    // Returns nullptr if the ring is not yet initialised.
    // (Declared after Poller so the return type is complete.)
    Poller* GetPoller() const;

    // Single-SQE helper: get one SQE, fill via |prepare_fn|, submit.
    // Must be called on the Poller thread (no locking).
    // Returns io_uring_submit() result (>= 0) or -1 (errno set).
    //   errno=ENOBUFS  → SQ full
    //   errno=ENODEV   → ring not initialised
    int SubmitOneSqe(std::function<void(struct io_uring_sqe*)> prepare_fn);

    // Actually submit WRITE / WRITE_FIXED SQE(s) for |data|, targeting
    // |sid| / |fd|.  Must be called on the Poller thread; invoked from
    // PollerDrainOpQueue() when processing a SidOp::WRITE message.
    // The IouringReqContext(s) created here take ownership of a reference
    // to |data| (via owned_data) so the backing memory survives until the
    // completion CQE is reaped by PollCq().
    // Static (like PollCq) because the target endpoint is looked up from
    // |sid|, mirroring how PollerDrainOpQueue dispatches ADD/REMOVE.
    static void PollerSubmitWrite(SocketId sid, int fd, butil::IOBuf& data);

};

}  // namespace iouring
}  // namespace brpc

#else  // !BRPC_WITH_IOURING

class IouringEndpoint {};

#endif  // BRPC_WITH_IOURING
#endif  // BRPC_IOURING_ENDPOINT_H
