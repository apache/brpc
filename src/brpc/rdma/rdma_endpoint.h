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
#include <infiniband/verbs.h>
#include "butil/atomicops.h"
#include "butil/iobuf.h"
#include "butil/macros.h"
#include "brpc/socket.h"


namespace brpc {
class Socket;
namespace rdma {

class RdmaConnect : public AppConnect {
public:
    void StartConnect(const Socket* socket, 
            void (*done)(int err, void* data), void* data) override;
    void StopConnect(Socket*) override;
    struct RunGuard {
        RunGuard(RdmaConnect* rc) { this_rc = rc; }
        ~RunGuard() { if (this_rc) this_rc->Run(); }
        RdmaConnect* this_rc;
    };

private:
    void Run();
    void (*_done)(int, void*);
    void* _data;
};

struct RdmaResource {
    ibv_qp* qp;
    ibv_cq* cq;
    ibv_comp_channel* comp_channel;
    RdmaResource* next;
    RdmaResource();
    ~RdmaResource();
    DISALLOW_COPY_AND_ASSIGN(RdmaResource);
};

class BAIDU_CACHELINE_ALIGNMENT RdmaEndpoint : public SocketUser {
friend class RdmaConnect;
friend class brpc::Socket;
public:
    RdmaEndpoint(Socket* s);
    ~RdmaEndpoint();

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

    // For debug
    void DebugInfo(std::ostream& os) const;

    // Callback when there is new epollin event on TCP fd
    static void OnNewDataFromTcp(Socket* m);

private:
    enum State {
        UNINIT = 0x0,
        C_ALLOC_QPCQ = 0x1,
        C_HELLO_SEND = 0x2,
        C_HELLO_WAIT = 0x3,
        C_BRINGUP_QP = 0x4,
        C_ACK_SEND = 0x5,
        S_HELLO_WAIT = 0x11,
        S_ALLOC_QPCQ = 0x12,
        S_BRINGUP_QP = 0x13,
        S_HELLO_SEND = 0x14,
        S_ACK_WAIT = 0x15,
        ESTABLISHED = 0x100,
        FALLBACK_TCP = 0x200,
        FAILED = 0x300
    };

    // Process handshake at the client
    static void* ProcessHandshakeAtClient(void* arg);

    // Process handshake at the server
    static void* ProcessHandshakeAtServer(void* arg);

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

    // Read at most len bytes from fd in _socket to data
    // wait for _read_butex if encounter EAGAIN
    // return -1 if encounter other errno (including EOF)
    int ReadFromFd(void* data, size_t len);


    // Write at most len bytes from data to fd in _socket
    // wait for _epollout_butex if encounter EAGAIN
    // return -1 if encounter other errno
    int WriteToFd(void* data, size_t len);

    // Bringup the QP from RESET state to RTS state
    // Arguments:
    //     lid: remote LID
    //     gid: remote GID
    //     qp_num: remote QP number
    // Return:
    //     0:   success
    //     -1:  failed, errno set
    int BringUpQp(uint16_t lid, ibv_gid gid, uint32_t qp_num);

    // Get event from comp channel and ack the events
    int GetAndAckEvents();

    // Poll CQ and get the work completion
    static void PollCq(Socket* m);

    // Get the description of current handshake state
    std::string GetStateStr() const;

    // Try to read data on TCP fd in _socket
    inline void TryReadOnTcp();

    // Not owner
    Socket* _socket;

    // State of Handshake
    State _state;

    // rdma resource
    RdmaResource* _resource;

    // the number of events requiring ack
    int _cq_events;

    // the SocketId which wrap the comp channel of CQ
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
    uint16_t _remote_recv_block_size;

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
    // The number of WRs we can post to the local Send Queue
    butil::atomic<uint16_t> _window_size;
    // The number of new WRs posted in the local Recv Queue
    butil::atomic<uint16_t> _new_rq_wrs;

    // butex for inform read events on TCP fd during handshake
    butil::atomic<int> *_read_butex;

    DISALLOW_COPY_AND_ASSIGN(RdmaEndpoint);
};

}  // namespace rdma
}  // namespace brpc

#else  // if BRPC_WITH_RDMA

class RdmaEndpoint { };

#endif  // ifdef USE_RD<A

#endif // BRPC_RDMA_ENDPOINT_H
