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
struct IouringReqContext {
    IouringOpType   op;         // operation type
    int             fd;         // file descriptor
    SocketId        socket_id;  // owning socket id
    void*           bounce{nullptr};  // unregistered-mode bounce buf (may be null)
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
//
//   DeallocateResources()
//     Posts a REMOVE SidOp.  If the endpoint holds a live _read_slot (a
//     READ_FIXED was submitted but no CQE arrived yet) the buf is returned to
//     IouringMemPool; otherwise _read_slot is zero and nothing is freed.
//
// Unregistered mode (--iouring_register_buffers=false)
// ------------------------------------------------------
//   SubmitRead()       → IORING_OP_READ into a per-call malloc bounce buffer.
//   CutFromIOBufList() → one IORING_OP_WRITEV per call.
//
// Thread safety
// -------------
// All SQ operations (SubmitRead, CutFromIOBufList, SubmitOneSqe) run on the
// Poller thread.  IouringMemPool is thread-safe.
// AllocateResources / DeallocateResources enqueue a SidOp message; the Poller
// thread processes them on its next iteration.
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

    // Cut data from IOBuf list and submit write SQE(s).
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
        };
        SocketId       sid;
        OpType         type;
        // Meaningful for REMOVE in fixed-buffer mode: the slot currently held
        // by the endpoint when DeallocateResources() was called.  The Poller
        // thread calls IouringMemPool::Deallocate(read_slot.buf) if buf != null.
        IouringReadSlot read_slot;

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

};

}  // namespace iouring
}  // namespace brpc

#else  // !BRPC_WITH_IOURING

class IouringEndpoint {};

#endif  // BRPC_WITH_IOURING
#endif  // BRPC_IOURING_ENDPOINT_H
