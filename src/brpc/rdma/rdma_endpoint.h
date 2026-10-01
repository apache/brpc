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

#ifndef BRPC_RDMA_ENDPOINT_H
#define BRPC_RDMA_ENDPOINT_H

#if BRPC_WITH_RDMA
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <infiniband/verbs.h>
#include "butil/atomicops.h"
#include "butil/iobuf.h"
#include "butil/macros.h"
#include "butil/containers/mpsc_queue.h"
#include "butil/containers/optional.h"
#include "brpc/socket.h"


namespace brpc {
class Socket;
class RdmaTransport;
namespace rdma {

DECLARE_bool(rdma_use_polling);
DECLARE_int32(rdma_poller_num);
DECLARE_bool(rdma_disable_bthread);

// Wire-independent RDMA connection parameters consumed by resource setup.
// Transport adapters translate their protocol-specific payload into this DTO.
struct RdmaConnectionInfo {
    uint32_t block_size;
    uint16_t sq_size;
    uint16_t rq_size;
    uint16_t lid;
    ibv_gid gid;
    uint32_t qp_num;
    butil::optional<ibv_ece> ece;
};

struct RdmaResource {
    RdmaResource* next{nullptr};
    ibv_qp* qp{nullptr};
    // For polling mode.
    ibv_cq* polling_cq{nullptr};
    // For event mode.
    ibv_cq* send_cq{nullptr};
    ibv_cq* recv_cq{nullptr};
    ibv_comp_channel* comp_channel{nullptr};
    RdmaResource() = default;
    ~RdmaResource();
    DISALLOW_COPY_AND_ASSIGN(RdmaResource);
};

class BAIDU_CACHELINE_ALIGNMENT RdmaEndpoint : public SocketUser {
friend class Socket;
friend class ::brpc::RdmaTransport;
public:
    explicit RdmaEndpoint(Socket* s);
    ~RdmaEndpoint() override;

    // Global initialization
    // Return 0 if success, -1 if failed and errno set
    static int GlobalInitialize();

    static void GlobalRelease();

    // Reset the endpoint (for next use)
    void Reset();

    // Cut data from the given IOBuf list and use RDMA to send
    // Return bytes cut if success, -1 if failed and errno set
    ssize_t CutFromIOBufList(butil::IOBuf** data, size_t ndata);

    // Whether the endpoint can send more data
    bool IsWritable() const;

    // Resource information consumed by the transport-level RDMA adapter.
    void GetLocalConnectionInfo(RdmaConnectionInfo* local) const;
    // Returns 0 on success, 1 when ECE query is unavailable, -1 on error.
    int QueryLocalEce(ibv_ece* ece) const;
    void SetOutgoingEce(const ibv_ece& ece);

    // For debug
    void DebugInfo(std::ostream& os,
                   butil::StringPiece connector = "\n") const;

    // Initialize polling mode
    static int PollingModeInitialize(bthread_tag_t tag,
                                     std::function<void(void)> callback,
                                     std::function<void(void)> init_fn,
                                     std::function<void(void)> release_fn);

    static void PollingModeRelease(bthread_tag_t tag);

private:
    // Allocate resources. On failure the endpoint is left with no RDMA
    // resource attached, so the caller can safely continue without RDMA.
    // Return 0 if success, -1 if failed and errno set
    int AllocateResources();

    // The real implementation of AllocateResources(), which may return
    // in the middle with resources partially allocated.
    // Return 0 if success, -1 if failed and errno set
    int DoAllocateResources();
    // Start consuming CQ events only after handshake parsing has finished.
    int StartCqEvents();

    // Release resources
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

    // Copy negotiated remote parameters into the endpoint and compute the
    // SQ/RQ window capacities.
    void ApplyRemoteInfo(const RdmaConnectionInfo& remote);

    // Bringup the QP from RESET state to RTS state.
    // Arguments:
    //   remote: negotiated peer parameters. Provides the remote LID/GID/QP
    //           number for the RTR transition, and (on v3) the peer's
    //           ECE to set during the INIT->RTR transition.
    //   is_server: true on the server side, false on the client side.
    // Returns 0 on success, -1 on failed and errno set.
    int BringUpQp(const RdmaConnectionInfo& remote, bool is_server);

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

    // Not owner
    Socket* _socket;
    // Input state dedicated to the stream carried by the RDMA QP.
    InputMessengerProcessor _input_processor;

    // ECE payload prepared by resource setup and consumed by the RDMA adapter.
    butil::optional<ibv_ece> _outgoing_ece;

    // rdma resource
    RdmaResource* _resource;

    // The number of events requiring ack.
    unsigned int _send_cq_events;
    unsigned int _recv_cq_events;

    // The SocketId which wrap the comp channel of CQ.
    SocketId _cq_sid;

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

    DISALLOW_COPY_AND_ASSIGN(RdmaEndpoint);

    // Cq socket id operation type
    struct CqSidOp {
        enum OpType {
            ADD,
            REMOVE,
        };
        SocketId sid;
        OpType type;
    };
    // Poller instance
    struct BAIDU_CACHELINE_ALIGNMENT Poller {
        bthread_t tid{INVALID_BTHREAD};
        butil::MPSCQueue<CqSidOp, butil::ObjectPoolAllocator<CqSidOp>> op_queue;
        // Callback used for io_uring/spdk etc
        std::function<void()> callback;
        // Init and Destroy function
        std::function<void()> init_fn;
        std::function<void()> release_fn;
    };
    // Poller group
    struct BAIDU_CACHELINE_ALIGNMENT PollerGroup {
        PollerGroup() : pollers(FLAGS_rdma_poller_num), running(false) {}
        std::vector<Poller> pollers;
        std::atomic<bool> running;
    };
    static std::vector<PollerGroup> _poller_groups;
};

}  // namespace rdma
}  // namespace brpc

#else  // if BRPC_WITH_RDMA

class RdmaEndpoint { };

#endif  // ifdef USE_RD<A

#endif // BRPC_RDMA_ENDPOINT_H
