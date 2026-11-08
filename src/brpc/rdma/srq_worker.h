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

#ifndef BRPC_RDMA_SRQ_WORKER_H
#define BRPC_RDMA_SRQ_WORKER_H
#if BRPC_WITH_RDMA
#include <functional>
#include <memory>
#include <mutex>
#include <gflags/gflags_declare.h>
#include "bthread/mutex.h"
#include "bthread/types.h"
#include "butil/atomicops.h"
#include "butil/containers/mpsc_queue.h"
#include "butil/iobuf.h"
#include "brpc/socket_id.h"
#include "brpc/rdma/rdma_async_event.h"
#include "brpc/rdma/rdma_connection.h"
#include "brpc/rdma/srq_flow_control.h"

namespace brpc {

class Socket;

namespace rdma {

DECLARE_bool(rdma_use_srq);
DECLARE_int32(rdma_srq_window);
DECLARE_int32(rdma_srq_sq_size);

class SrqWorker;
struct ProgressRound;

// Per-connection RC QP, send window and I/O state.
//
// Synchronization between concurrent execution paths:
// - Socket writers own submission and FC send state under _post_mutex.
// - The worker owns CQ processing, receive slots and QPN lookup under
//   _progress_mutex. Normal completion and control reception use atomic
//   publication instead of waiting for _post_mutex.
// - Worker progress that needs send state takes _post_mutex after
//   _progress_mutex. Writers must never acquire these locks in reverse order.
// - The async event reader acknowledges events before publishing LAST_WQE or
//   queueing failures. It never waits for either transport lock.
// - _delivery_events elects one consumer for _incoming and the RPC parser.
//   Delivery runs outside transport locks and releases consumer ownership
//   before invoking the last application callback.
// See TxRecord for the two-way release/acquire handoff of send slots, and
// SrqFlowControl for receive-event application at submission batch boundaries.
//
// The worker QPN table retains this object until QP teardown makes DMA safe.
// Delivery tasks may retain the connection and Socket longer. Endpoint lifetime
// does not determine QP or buffer lifetime. Closing stops new submissions.
// Destroying the QP stops its DMA. Releasing the last exported BlockRef permits
// receive-memory reuse. These are distinct lifetime boundaries.
class SrqConnection : public RdmaConnection,
                      public RdmaAsyncEventContext,
                      public std::enable_shared_from_this<SrqConnection> {
public:
    static std::shared_ptr<SrqConnection>
    Create(Socket* socket, bthread_tag_t tag, uint32_t sq_size);
    ~SrqConnection() override;

    void GetLocalInfo(RdmaLocalInfo* info) const override;
    int QueryEce(ibv_ece* ece) override;
    int Activate(const ParsedHello& peer, bool is_server,
                 butil::optional<ibv_ece>* outgoing_ece) override;
    int EnableReceive() override;
    ssize_t Send(butil::IOBuf** from, size_t count) override;
    bool IsWritable() override;
    void Close(int error = 0) override;
    void Debug(std::ostream& out, butil::StringPiece connector = "\n") const override;
    bool worker_retired() const;
    std::function<void()> PrepareAsyncEvent(ibv_event_type type) override;

private:
friend class SrqWorker;

    // All submit-lock holders use this guard so unlocking can notify worker
    // progress deferred by contention. Ordinary callers block on the mutex.
    // Normal worker progress uses try_to_lock while holding _progress_mutex. A
    // failed attempt registers an unlock notification and retries once before
    // deferring.
    class PostLock {
    public:
        explicit PostLock(const SrqConnection* connection);
        PostLock(const SrqConnection* connection, std::try_to_lock_t);
        ~PostLock();
        bool owns_lock() const { return _locked; }

    private:
        PostLock(const PostLock&) = delete;
        PostLock& operator=(const PostLock&) = delete;
        // The caller retains the connection for the entire guard lifetime.
        const SrqConnection* _connection;
        // Whether this guard owns the mutex and must unlock it on destruction.
        bool _locked;
    };

    // One record per SQ slot. Send WR IDs encode the slot index plus one. Zero
    // is invalid. Before post_send, the submitter release-publishes active for
    // the CQ owner to acquire. Completion cleans the record before publishing
    // _sq_completed, which permits reuse. Payload references must survive until
    // completion or confirmed DMA shutdown.
    struct TxRecord {
        // Payload references. The NIC may still read them for non-inline sends.
        butil::IOBuf payload;
        // The complete record is published and may have been submitted to the
        // NIC. The submitter rolls back only a known unposted suffix. All other
        // records require a CQ completion or confirmed DMA shutdown before
        // cleanup.
        butil::atomic<bool> active{false};
        // Whether this WR is a pure grant using reserved control capacity.
        bool control{false};
        // Releasing the payload may invoke a user deleter. Defer it outside
        // transport locks.
        bool has_user_data{false};
    };

    // Input cursor and post result local to one Send call. Batch arrays are
    // borrowed from the object pool, not kept per connection or on the stack.
    struct SendInput;
    struct SendBatch;
    struct SendPostResult;

    explicit SrqConnection(const std::shared_ptr<SrqWorker>& worker,
                           Socket* socket, bthread_tag_t tag);
    // Fault-injection tests borrow the QP only while holding _post_mutex.
    ibv_qp* qp() const;

    // Worker operations. The caller holds the worker _progress_mutex. Normal
    // CompleteSend reclamation and ReceiveControl publication do not take
    // _post_mutex. EnqueueReceived needs no receive lock. CompleteSend returns
    // whether it reclaimed a record, allowing the worker to batch CQ-credit
    // returns and writer wakeups.
    bool CompleteSend(const ibv_wc& wc, butil::IOBuf& released);
    bool ReceiveControl(uint32_t flags, size_t bytes);
    bool EnqueueReceived(butil::IOBuf received);
    // Returns grant, writability and close-progress results for the worker to
    // accumulate into the current round.
    ProgressRound Progress();
    // Requests close and schedules further progress. Takes _post_mutex
    // internally without calling Socket. Worker callers add the writer to the
    // round. Public Close wakes it after this method returns.
    void RequestClose(int error);

    // Locked methods below require _post_mutex. Teardown checks and destruction
    // also require _progress_mutex. ReadyToDestroyLocked requires a closing
    // connection with a live QP. Even after it returns true, the worker must
    // drain the RX CQ before destroying the QP.
    bool ReadyToDestroyLocked() const;
    // Destroys only the QP, without reclaiming send records. Normal close first
    // reclaims TX and drains RX. Worker failure first stops all DMA, then
    // RetireSendsLocked reclaims the remaining records.
    bool DestroyQpLocked();
    // Reserved but unreclaimed SQ slots, including WRs prepared but not yet
    // posted.
    uint32_t OutstandingLocked() const;
    // Maximum total SQ occupancy allowed for data submission, leaving control
    // slots for pure grants.
    uint32_t DataSqLimit() const;
    // Data WRs that can still be submitted. Zero once total occupancy reaches
    // or exceeds the data limit. Counts both data and control WRs and reads
    // completion progress once to avoid unsigned underflow.
    uint32_t DataSqAvailableLocked() const;
    // Worker progress only, with both transport locks held. It checks worker
    // failure after this call, so an ambiguous post needs no extra wakeup.
    int SendPureGrantLocked();
    // Reserve batch capacity and prepare WRs against an FC snapshot. Returns
    // an error code. Zero reserved slots means resource exhaustion. Payloads
    // removed from failed preparation are retained in unposted until unlock.
    int PrepareSendBatchLocked(SendInput& input, SendBatch& batch,
                               butil::IOBuf& unposted);
    // Publish records before verbs. Completions may reclaim them before this
    // returns. The result identifies the accepted or indeterminate prefix.
    SendPostResult PostSendBatchLocked(SendBatch& batch);
    // Account for the prefix, roll back only the known unposted suffix, then
    // apply received FC events. Returns submitted bytes using batch metadata,
    // without accessing records that the completion owner may have reclaimed.
    ssize_t FinishSendBatchLocked(SendBatch& batch, const SendPostResult& result,
                                 butil::IOBuf& unposted);
    // CQ owner only: transfers user payload references for release outside
    // transport locks.
    void ReleasePayload(TxRecord& record, butil::IOBuf& released);
    // Reclaims remaining send records only after all worker DMA resources have
    // been successfully destroyed.
    void RetireSendsLocked(butil::IOBuf& released);
    // Requires either _post_mutex or _progress_mutex. QP destruction holds
    // both. Atomic coalescing lets the receive owner schedule progress without
    // waiting for _post_mutex.
    void ScheduleProgressLocked(bool wake);
    // Requeues existing progress responsibility directly, leaving
    // _progress_scheduled set so concurrent requests do not enqueue duplicates.
    void EnqueueProgressLocked(bool wake);
    void WakeWriter();

    // Called after enqueueing and outside _progress_mutex so user callbacks
    // cannot run under transport locks. Only the caller changing the event
    // count from zero to one may start a consumer. Returns whether a task was
    // started.
    bool StartDelivery(bool urgent = false);
    // The caller must own consumption. A null socket discards input, including
    // cleanup after task-start failure. data holds the optional first block
    // dequeued by the starter. Further input comes from the queue. Release
    // consumer ownership after dequeueing/parsing and before running the last
    // message callback.
    void Consume(Socket* socket, butil::IOBuf data = butil::IOBuf());
    static void* RunDelivery(void* arg);

    // Keeps the shared SRQ/CQ owner alive for this connection lifetime.
    std::shared_ptr<SrqWorker> _worker;
    // Versioned Socket ID used to reacquire a reference on asynchronous paths.
    SocketId _sid;
    // bthread scheduling tag inherited by receive-delivery tasks.
    bthread_tag_t _tag;

    // Serializes data/control submission, FC send state and close transitions
    // for both bthreads and pthreads. Normal TX completion transfers slots
    // atomically, and control reception publishes atomic events without this
    // lock. Send takes only this connection lock, allowing different QPs to
    // submit concurrently. Worker-wide atomic reservations bound shared TX CQ
    // use.
    //
    // Lock order is _progress_mutex then _post_mutex. Send paths must never
    // acquire _progress_mutex while holding this lock. Use PostLock for every
    // acquisition so any holder can notify worker progress deferred by lock
    // contention.
    mutable bthread::Mutex _post_mutex;
    // Pending unlock notification registered by a failed worker try_lock. An
    // unlocker atomically takes it and calls Kick. A successful worker
    // acquisition clears it. This is independent of _progress_scheduled: queued
    // work may be waiting for resources rather than this mutex. A wakeup
    // reserves neither the lock nor SQ/CQ credits.
    mutable butil::atomic<bool> _post_retry{false};
    // RC QP owned by this connection. Cleared only after successful verbs
    // destruction.
    ibv_qp* _qp{nullptr};
    // QP number used for shared-CQ and asynchronous-failure dispatch.
    uint32_t _qpn{0};
    // Usable SQ depth and number of entries in _records.
    uint32_t _sq_size{0};
    // Maximum SGEs in one send WR.
    uint32_t _max_sge{0};
    // Inline byte limit bounded by configuration and the created QP capability.
    uint32_t _inline_size{0};
    // Peer receive-block payload capacity, bounding the size of one SEND.
    uint32_t _remote_block_size{0};

    // Send state is protected by _post_mutex. The receiver writes only the
    // atomic control-event inbox.
    SrqFlowControl _fc;
    // Send-record ring allocated once when the QP is created.
    std::unique_ptr<TxRecord[]> _records;
    // Next logical slot sequence, modified by the submitter under _post_mutex.
    // An unposted suffix may roll it back. Modulo selects a record, and the
    // difference from _sq_completed is reserved SQ occupancy.
    uint64_t _sq_head{0};
    // Advanced by the CQ owner in RC QP send-completion order.
    // Release-published only after record cleanup. The submitter must acquire
    // before reusing the slot. No free list is shared between these two paths.
    butil::atomic<uint64_t> _sq_completed{0};
    // Outstanding pure grants, bounded by the control reserve. Modified only by
    // the owner holding _progress_mutex.
    uint32_t _control_outstanding{0};
    // Last successful activation stage, protected by _post_mutex and retained
    // on close. Sending requires RTS and no close request. A QP that reached
    // RTR must wait for LAST_WQE during normal close.
    QpActivationStage _activation_stage{QpActivationStage::NONE};
    // An existing task owns progress for this connection, coalescing hard
    // requests, shared-CQ waits and close requests. Responsibility may reside
    // in the command queue, a running callback or the round retirement list.
    // Submitters set it under _post_mutex and receivers under _progress_mutex.
    // Progress clears it while holding both. Requeueing keeps it set. The
    // generic worker scheduler does not manage this connection state.
    butil::atomic<bool> _progress_scheduled{false};
    // The writer is waiting for shared TX CQ capacity. The owner wakes it when
    // capacity is observed.
    bool _waiting_cq{false};
    // Close request published to send, completion and delivery paths before
    // resources are reclaimed.
    butil::atomic<bool> _closing{false};
    // The async event thread has acknowledged LAST_WQE. The owner must observe
    // this before collecting a QP for normal retirement.
    butil::atomic<bool> _last_wqe{false};
    // Whether a Socket error was reported, protected by _post_mutex. A normal
    // close still allows a later first error report.
    bool _failure_reported{false};

    // Each node takes ownership of one received IOBuf without copying payload
    // or adding Block references. Only the consumer owner may Dequeue. Close
    // must not become a second consumer.
    butil::MPSCQueue<butil::IOBuf, butil::ObjectPoolAllocator<butil::IOBuf>> _incoming;
    // Whether received bytes may enter the RPC parser. Until enabled, input
    // stays in _incoming. After TCP handshake parsing finishes, EnableReceive
    // release-publishes true and notifies the consumer. The delivery task
    // acquires it before using the parser. After close, _closing prevents
    // parsing while the consumer may still dequeue and discard input.
    butil::atomic<bool> _delivery_enabled{false};
    // Coalesced delivery notifications: zero means no consumer owner. Nonzero
    // means a task is responsible for checking the queue. After draining, the
    // consumer CASes its observed count to zero. Intervening notifications
    // force another pass. Ownership serializes dequeueing and parsing, but is
    // released before the last application callback. Counts notifications, not
    // queued buffers. One CQ round may combine many receive completions.
    butil::atomic<uint64_t> _delivery_events{0};
};

bool ValidateSrqOptions();
void StopAllSrqWorkers();
size_t SrqLiveConnections();
size_t SrqWorkerCount();

}  // namespace rdma
}  // namespace brpc
#endif
#endif  // BRPC_RDMA_SRQ_WORKER_H
