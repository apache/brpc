// Copyright (c) 2014 baidu-rpc authors.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Li Zhaogeng (lizhaogeng01@baidu.com)

#ifndef BRPC_RDMA_ENDPOINT_H
#define BRPC_RDMA_ENDPOINT_H

#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "butil/atomicops.h"         // butil::atomic
#include "butil/iobuf.h"             // butil::IOBuf
#include "butil/macros.h"
#include "bthread/bthread.h"
#include "brpc/socket.h"
#include "brpc/rdma/rdma_communication_manager.h"
#include "brpc/rdma/rdma_completion_queue.h"
#include "brpc/rdma/spsc_queue.h"

namespace brpc {
namespace rdma {

// The magic string to identify RDMA use at the beginning of handshake
const char* const MAGIC_STR = "RDMA";
const size_t MAGIC_LENGTH = 4;
// The length of random string used in handshake
const size_t RANDOM_LENGTH = 4;
// The length of hello message from client (magic+random)
const size_t HELLO_LENGTH = MAGIC_LENGTH + RANDOM_LENGTH;

struct RdmaWrId {
    uint64_t sid;
    uint32_t version;
};

struct SPSCQueue {
private:
    void *_head;
    void *_tail;
};

class BAIDU_CACHELINE_ALIGNMENT RdmaEndpoint {
friend class RdmaCompletionQueue;
public:
    RdmaEndpoint(Socket* s);
    ~RdmaEndpoint();

    // Global initialization
    // Return 0 if success, -1 if failed and errno set
    static int GlobalInitialize();

    // Initialize RdmaEndpoint from accept
    // Return 0 if success, -1 if failed and errno set
    static int InitializeFromAccept(
            RdmaCommunicationManager* rcm, char* data, size_t len);

    // Reset the endpoint (for next use)
    void Reset();

    // Start RDMA handshake
    int StartHandshake();

    // Complete RDMA handshake
    int CompleteHandshake();

    // Process handshake
    ssize_t Handshake();

    // Cut data from the given IOBuf list and use RDMA to send
    // Return bytes cut if success, -1 if failed and errno set
    ssize_t CutFromIOBufList(butil::IOBuf** data, size_t ndata);

    // Whether the endpoint can send more data
    bool IsWritable() const;

    // For debug
    void DebugInfo(std::ostream& os) const;

private:
    enum Status {
        UNINITIALIZED,
        HELLO_C,            // only valid at client
        HELLO_S,            // only valid at server
        ADDR_RESOLVING,     // only valid at client
        ROUTE_RESOLVING,    // only valid at client
        CONNECTING,         // only valid at client
        ACCEPTING,          // only valid at server
        ESTABLISHED
    };

    // Process handshake at the client
    // event is the corresponding rdmacm event
    // Return 0 if success, -1 if failed and errno set
    int HandshakeAtClient(RdmaCMEvent event = RDMACM_EVENT_NONE);

    // Process handshake at the server
    // event is the corresponding rdmacm event
    // Return 0 if success, -1 if failed and errno set
    int HandshakeAtServer(RdmaCMEvent event = RDMACM_EVENT_NONE);

    // Allocate resources
    // Return 0 if success, -1 if failed and errno set
    int AllocateResources();

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

    // Handle CQE wrapped in rc
    // If rc is not RDMA RECV event:
    //     return 0 if success, -1 if failed and errno set
    // If rc is RDMA RECV event:
    //     return bytes appended if success, -1 if failed and errno set
    ssize_t HandleCompletion(RdmaCompletion& rc);

    // Post a given number of WRs to Recv Queue
    // If zerocopy is true, malloc new buffer for WR
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

    // Get wr_id of this RdmaEndpoint
    RdmaWrId* GetWrId();

    // Not owner
    Socket* _socket;

    // Destroyed when this RdmaEndpoint is destroyed
    RdmaCommunicationManager* _rcm;

    // Destroyed when this RdmaEndpoint is destroyed in non-shared case
    // Only call Release when this RdmaEndpoint is destroyed in shared case
    RdmaCompletionQueue* _rcq;

    // QP, not owner
    void* _qp;

    // Status of the connection
    Status _status;

    // The version of this RdmaEndpoint
    uint32_t _version;

    // Capacity of local Send Queue and local Recv Queue
    uint32_t _sq_size;
    uint32_t _rq_size;

    // Act as sendbuf and recvbuf, but requires no copy
    std::vector<butil::IOBuf> _sbuf;
    std::vector<butil::IOBuf> _rbuf;

    // Data address of _rbuf
    std::vector<void*> _rbuf_data;

    // Receive buffer during handshake
    butil::IOPortal _handshake_buf;

    // Remote block size for receiving
    uint32_t _remote_recv_block_size;

    // The number of new recv WRs acked to the remote side
    uint32_t _accumulated_ack;
    // The number of WRs sent without solicited flag
    uint32_t _unsolicited;
    // The bytes sent without solicited flag
    uint32_t _unsolicited_bytes;
    // The current index should be used for sending
    uint32_t _sq_current;
    // The number of send WRs not signaled
    uint32_t _sq_unsignaled;
    // The just completed send WR's index
    uint32_t _sq_sent;
    // The just completed recv WR's index
    uint32_t _rq_received;

    // The capacity of local window: min(local SQ, remote RQ)
    uint32_t _local_window_capacity;
    // The capacity of remote window: min(local RQ, remote SQ)
    uint32_t _remote_window_capacity;
    // The number of WRs we can post to the local Send Queue
    butil::atomic<uint32_t> _window_size;
    // The number of new WRs posted in the local Recv Queue
    butil::atomic<uint32_t> _new_rq_wrs;

    // Remote side SocketId
    uint64_t _remote_sid;

    // Only used when shared CQ is enabled
    SpscQueue<RdmaCompletion*> _completion_queue;
    // Number of completions received
    butil::atomic<int> _ncompletions;

    // pipe fd used for waking handshake
    int _pipefd[2];

    // Random number used in RDMA connection (as cookies for security)
    // See rdma_endpoint.cpp to get more details.
    char _rand_str[RANDOM_LENGTH];

    DISALLOW_COPY_AND_ASSIGN(RdmaEndpoint);
};

}  // namespace rdma
}  // namespace brpc

#endif // BRPC_RDMA_ENDPOINT_H
