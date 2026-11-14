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

#ifndef BRPC_RDMA_RQ_CONNECTION_H
#define BRPC_RDMA_RQ_CONNECTION_H
#if BRPC_WITH_RDMA
#include <functional>
#include <memory>
#include <vector>
#include "butil/atomicops.h"
#include "butil/containers/mpsc_queue.h"
#include "brpc/socket.h"
#include "brpc/rdma/rdma_async_event.h"
#include "brpc/rdma/rdma_connection.h"

namespace brpc {
namespace rdma {
DECLARE_int32(rdma_poller_num);
class RqConnection;
struct RdmaResource : public RdmaAsyncEventContext {
    // Published once after `connection` is initialized. Acquire readers may
    // then copy that immutable weak reference. Invalid for unused resources.
    butil::atomic<SocketId> socket_id{INVALID_SOCKET_ID};
    // Exact connection generation. A revived Socket may reuse `socket_id`,
    // so deferred errors must also check this connection's identity.
    std::weak_ptr<RqConnection> connection;
    std::function<void()> PrepareAsyncEvent(ibv_event_type type) override;
    // Link in the pool of prepared, unused private QPs.
    RdmaResource* next{nullptr};
    // RC QP owning this connection's private RQ and SQ.
    ibv_qp* qp{nullptr};
    // Combined send/receive CQ used only by legacy polling mode.
    ibv_cq* polling_cq{nullptr};
    // Private TX CQ used by event mode.
    ibv_cq* send_cq{nullptr};
    // Private RX CQ used by event mode.
    ibv_cq* recv_cq{nullptr};
    // Completion notification channel shared by the two private CQs.
    ibv_comp_channel* comp_channel{nullptr};
    // Largest message posted inline on `qp'.
    uint32_t max_inline_data{0};

    RdmaResource() = default;
    ~RdmaResource();
    DISALLOW_COPY_AND_ASSIGN(RdmaResource);
};

// Private receive queues, completion queues and legacy ACK/window protocol.
class RqConnection : public RdmaConnection,
                     public std::enable_shared_from_this<RqConnection> {
public:
    static std::shared_ptr<RqConnection> Create(Socket* socket);
    ~RqConnection() override;
    void GetLocalInfo(RdmaLocalInfo* info) const override;
    int QueryEce(ibv_ece* ece) override;
    int Activate(const ParsedHello& peer, bool is_server,
                 butil::optional<ibv_ece>* outgoing_ece) override;
    int EnableReceive() override;
    ssize_t Send(butil::IOBuf** from, size_t count) override;
    bool IsWritable() override;
    void Close(int error = 0) override;
    void Debug(std::ostream& out,
               butil::StringPiece connector = "\n") const override;

    static int GlobalInitialize();
    static void GlobalRelease();
    static int PollingModeInitialize(bthread_tag_t tag,
                                     std::function<void()> callback,
                                     std::function<void()> init_fn,
                                     std::function<void()> release_fn);
    static void PollingModeRelease(bthread_tag_t tag);

private:
    friend struct RdmaResource;

    // Owned by the CQ Socket. A queued callback retains its exact connection.
    struct CqContext : public SocketUser {
        explicit CqContext(const std::shared_ptr<RqConnection>& c) : connection(c) {}
        void BeforeRecycle(Socket*) override { delete this; }
        // Released only after every callback holding this CQ Socket has exited.
        std::shared_ptr<RqConnection> connection;
    };
    explicit RqConnection(Socket* socket);
    void FailFromAsyncEvent();
    int Initialize();
    void DeallocateResources();
    // Send Imm data to the remote side
    // Arguments:
    //     imm: imm data in the WR
    // Return:
    //     0:   success
    //     -1:  failed, errno set
    int SendImm(uint32_t imm);

    // Try to send pure ACK to the remote side
    // Arguments:
    //     num: the number of rq entry received
    // Return:
    //     0:   success
    //     -1:  failed, errno set
    int SendAck(int num);

    // Handle CQE
    // If wc is not RDMA RECV event:
    //     return 0 if success, -1 if failed and errno set
    // If wc is RDMA RECV event:
    //     return bytes appended if success, -1 if failed and errno set
    ssize_t HandleCompletion(ibv_wc& wc);

    // Post a given number of WRs to Recv Queue
    // If zerocopy is true, reallocate block.
    // Return 0 if success, -1 if failed and errno set
    int PostRecv(uint32_t num, bool zerocopy);

    // Post a WR pointing to the block to the local Recv Queue
    // Arguments:
    //     block: the addr to receive data (ibv_sge.addr)
    //     block_size: the maximum length can be received (ibv_sge.length)
    // Return:
    //     0:   success
    //     -1:  failed, errno set
    int DoPostRecv(void* block, size_t block_size);

    // Get event from comp channel and ack the events
    int GetAndAckEvents(SocketUniquePtr& s);

    // Request completion notification on a send/recv CQ.
    int ReqNotifyCq(bool send_cq, bool fatal_on_error);

    // Poll CQ and get the work completion
    static void PollCq(Socket* m);

    // Add cq socket id to poller
    void PollerAddCqSid();

    // Remove cq socket id to poller
    void PollerRemoveCqSid();

    // Borrowed from the owning Socket. Callbacks first reacquire _sid.
    Socket* _socket;
    // Versioned identity used before dereferencing the borrowed Socket.
    SocketId _sid;
    // Set before teardown and never cleared. Deferred async errors read it
    // after acquiring the Socket reference to reject this old generation.
    butil::atomic<bool> _closed{false};
    // Private QP/CQs and completion channel, or null after resource retirement.
    RdmaResource* _resource;
    // Received bytes accumulated for one parser dispatch after polling a batch.
    butil::IOBuf _received;
    // Parser delivery was enabled after the TCP handshake parser exited.
    bool _receiving_enabled{false};
    // The number of events requiring ack.
    unsigned int _send_cq_events;
    unsigned int _recv_cq_events;

    // The SocketId which wrap the comp channel of CQ.
    SocketId _cq_sid;
    // Tag used to register private-RQ polling. Close may run under another tag.
    // REMOVE must target the original polling group.
    bthread_tag_t _poller_tag{BTHREAD_TAG_INVALID};

    // Capacity of local Send Queue and local Recv Queue
    uint16_t _sq_size;
    uint16_t _rq_size;

    // Act as sendbuf and recvbuf, but requires no memcpy
    std::vector<butil::IOBuf> _sbuf;
    std::vector<butil::IOBuf> _rbuf;
    // Data address of _rbuf
    std::vector<void*> _rbuf_data;
    // Remote block size for receiving
    uint32_t _remote_recv_block_size;

    // The number of new recv WRs acked to the remote side
    uint16_t _accumulated_ack;
    // The number of WRs sent without solicited flag
    uint16_t _unsolicited;
    // The bytes sent without solicited flag
    uint32_t _unsolicited_bytes;
    // The current index should be used for sending
    uint16_t _sq_current;
    // The number of send WRs not signaled
    uint16_t _sq_unsignaled;
    // The just completed send WR's index
    uint16_t _sq_sent;
    // The just completed recv WR's index
    uint16_t _rq_received;
    // The capacity of local window: min(local SQ, remote RQ)
    uint16_t _local_window_capacity;
    // The capacity of remote window: min(local RQ, remote SQ)
    uint16_t _remote_window_capacity;
    // The number of IMM WRs we can post to the local Send Queue.
    uint16_t _sq_imm_window_size;
    // The number of WRs we can send to remote side.
    butil::atomic<uint16_t> _remote_rq_window_size;
    // The number of WRs we can post to the local Send Queue
    butil::atomic<uint16_t> _sq_window_size;
    // The number of new WRs posted in the local Recv Queue
    butil::atomic<uint16_t> _new_rq_wrs;

    // Cq socket id operation type
    struct CqSidOp {
        enum OpType {
            ADD,
            REMOVE,
        };
        // Versioned CQ Socket to add to or remove from a poller.
        SocketId sid;
        // Requested poller membership update.
        OpType type;
    };
    // Poller instance
    struct BAIDU_CACHELINE_ALIGNMENT Poller {
        // Native-style bthread running the legacy polling loop.
        bthread_t tid{INVALID_BTHREAD};
        // Cross-thread queue of CQ membership changes.
        butil::MPSCQueue<CqSidOp, butil::ObjectPoolAllocator<CqSidOp>> op_queue;
        // Callback used for io_uring/spdk etc
        std::function<void()> callback;
        // Init and Destroy function
        std::function<void()> init_fn;
        // Per-poller teardown callback.
        std::function<void()> release_fn;
    };
    // Poller group
    struct BAIDU_CACHELINE_ALIGNMENT PollerGroup {
        PollerGroup() : pollers(FLAGS_rdma_poller_num), running(false) {}
        // Pollers belonging to this bthread scheduling tag.
        std::vector<Poller> pollers;
        // Stops all poller loops in this tag when cleared.
        butil::atomic<bool> running;
    };
    // Independent polling groups indexed by bthread tag.
    static std::vector<PollerGroup> _poller_groups;
};
}  // namespace rdma
}  // namespace brpc
#endif  // BRPC_WITH_RDMA
#endif  // BRPC_RDMA_RQ_CONNECTION_H
