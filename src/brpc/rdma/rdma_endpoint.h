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
#include <functional>
#include <infiniband/verbs.h>
#include "butil/atomicops.h"
#include "butil/iobuf.h"
#include "butil/macros.h"
#include "butil/containers/optional.h"
#include "brpc/socket.h"
#include "brpc/rdma/rdma_handshake_server.h"
#include "brpc/rdma/rdma_connection.h"


namespace brpc {
class Socket;
namespace rdma {

DECLARE_bool(rdma_use_polling);
DECLARE_int32(rdma_poller_num);
DECLARE_bool(rdma_disable_bthread);

class RdmaHandshakeClientV2;
class RdmaHandshakeServerV2;
class RdmaHandshakeClientV3;
class RdmaHandshakeServerV3;
struct ParsedHello;
enum class RemoteHelloResult;
class RdmaHello;
class RdmaEndpoint;
class SrqConnection;

namespace v2_wire {
RemoteHelloResult ReadBodyAndNegotiate(RdmaEndpoint* ep, ParsedHello* remote);
int DrainBytes(RdmaEndpoint* ep, size_t n);
}  // namespace v2_wire

namespace v3_wire {
void FillLocalRdmaHello(const RdmaEndpoint* ep, RdmaHello* msg);
int  ReadAndParseV3Hello(RdmaEndpoint* ep, RdmaHello* out);
int  WriteV3Hello(RdmaEndpoint* ep, const RdmaHello& msg);
}  // namespace v3_wire

class RdmaConnect : public AppConnect {
public:
    void StartConnect(const Socket* socket, 
                      void (*done)(int err, void* data), void* data) override;
    // Handshake tasks observe `Socket::Failed()` while waiting. Socket calls
    // this hook only during recycling, after those tasks release their Socket
    // references. Cancellation state must not live on this shared connector.
    void StopConnect(Socket*) override {}
};

class BAIDU_CACHELINE_ALIGNMENT RdmaEndpoint {
friend class RdmaConnect;
friend class Socket;
friend class RdmaHandshakeClientV2;
friend class RdmaHandshakeServerV2;
friend class RdmaHandshakeClientV3;
friend class RdmaHandshakeServerV3;
friend RemoteHelloResult v2_wire::ReadBodyAndNegotiate(RdmaEndpoint*, ParsedHello*);
friend int v2_wire::DrainBytes(RdmaEndpoint*, size_t);
friend void v3_wire::FillLocalRdmaHello(const RdmaEndpoint*, RdmaHello*);
friend int v3_wire::ReadAndParseV3Hello(RdmaEndpoint*, RdmaHello*);
friend int v3_wire::WriteV3Hello(RdmaEndpoint*, const RdmaHello&);
public:
    explicit RdmaEndpoint(Socket* s);
    DISALLOW_COPY_AND_ASSIGN(RdmaEndpoint);

    ~RdmaEndpoint();

    // Global initialization
    // Return 0 if success, -1 if failed and errno set
    static int GlobalInitialize();

    static void GlobalRelease();

    // Reset the endpoint for next use. The caller must have exclusive access
    // to this Socket generation, after in-flight Socket references drain.
    void Reset();

    // Cut data from the given IOBuf list and use RDMA to send
    // Return bytes cut if success, -1 if failed and errno set
    ssize_t CutFromIOBufList(butil::IOBuf** data, size_t ndata);

    // Whether the endpoint can send more data
    bool IsWritable() const;

    // For debug
    void DebugInfo(std::ostream& os,
                   butil::StringPiece connector = "\n") const;

    // Reject callbacks retained by a detached connection generation.
    bool OwnsConnection(const RdmaConnection* connection) const;
    // Caller retains the owning Socket and serializes receive delivery.
    int Deliver(RdmaConnection* source, butil::IOBuf* data, ssize_t bytes,
                InputMessageClosure& last_msg);

    // Callback when there is new epollin event on TCP fd.
    static void OnNewDataFromTcp(Socket* m);

    // Real handshake for RDMA-mode sockets.
    static ParseResult ExecuteServerHandshake(butil::IOBuf* source, Socket* socket);

    // Initialize polling mode
    static int PollingModeInitialize(bthread_tag_t tag,
                                     std::function<void(void)> callback,
                                     std::function<void(void)> init_fn,
                                     std::function<void(void)> release_fn);

    static void PollingModeRelease(bthread_tag_t tag);

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

    static void OnNewDataFromTcpAtClient(Socket* m);
    static void OnNewDataFromTcpAtServer(Socket* m);

    bool HandleTcpEventAfterEstablished();

    // Allocate resources. On failure the endpoint is left with no RDMA
    // resource attached, so that the handshake can safely fall back to TCP.
    // Return 0 if success, -1 if failed and errno set
    int AllocateResources();

    // Release resources
    void DeallocateResources();

    // Enable RDMA delivery only after TCP handshake parsing has returned.
    // The two streams share the Socket's parsing context and preferred index.
    int StartCqEvents();

    // Select the data path before allocation. Only handshake v3 supports SRQ.
    void SelectQueueMode(RdmaQueueMode mode);
    void GetLocalInfo(RdmaLocalInfo* info) const;

    // Read at most len bytes from fd in _socket to data
    // wait for _read_butex if encounter EAGAIN
    // return -1 if encounter other errno (including EOF)
    int ReadFromFd(void* data, size_t len);
    int ReadFromFd(butil::IOPortal* data, size_t len);


    // Write at most len bytes from data to fd in _socket
    // wait for _epollout_butex if encounter EAGAIN
    // return -1 if encounter other errno
    int WriteToFd(void* data, size_t len);

    // Write data to fd in _socket.
    // wait for _epollout_butex if encounter EAGAIN.
    // return -1 if encounter other errno.
    int WriteToFd(butil::IOBuf* data);

    // Bringup the QP from RESET state to RTS state.
    // Arguments:
    //   remote: parsed remote hello. Provides the remote LID/GID/QP
    //           number for the RTR transition, and (on v3) the peer's
    //           ECE to set during the INIT->RTR transition.
    //   is_server: true on the server side, false on the client side.
    // Returns 0 on success, -1 on failed and errno set.
    int BringUpQp(const ParsedHello& remote, bool is_server);

    // Get the description of current handshake state
    std::string GetStateStr() const;

    // Not owner
    Socket* _socket;

    // State of Handshake. FALLBACK_TCP publishes RdmaTransport::_rdma_state
    // with release ordering and is consumed by OnNewDataFromTcpAtClient with acquire
    // ordering. Other state accesses do not publish data and use relaxed
    // ordering.
    butil::atomic<State> _state;

    // Wire-level handshake protocol version (set by dispatch in
    // ProcessHandshakeAtClient/Server). Aligned with the protocol code:
    //   0 = unnegotiated
    //   2 = v2 "RDMA"
    //   3 = v3 "RDM3"
    int _handshake_version;

    // ECE payload to advertise in the next local hello:
    //   Client: the locally queried ECE capabilities (filled
    //           before C_HELLO_SEND).
    //   Server: the reduced/negotiated ECE queried after the
    //           QP reached RTS (filled in BringUpQp).
    butil::optional<ibv_ece> _outgoing_ece;

    // MTU payload to advertise in the next local hello:
    //   Client: the locally queried active MTU (filled
    //           before C_HELLO_SEND).
    //   Server: the minimum of the local and peer active MTUs
    //           (filled in BringUpQp).
    butil::optional<uint32_t> _outgoing_mtu{};

    // Selected backend. Completion tasks may retain it after Reset detaches it.
    std::shared_ptr<RdmaConnection> _connection;
    // Handshake selection, also used when hardware allocation is skipped by UT.
    RdmaQueueMode _queue_mode{RdmaQueueMode::PRIVATE_RQ};
    // Configured hello parameters used before allocation or in hardware-free UT.
    RdmaLocalInfo _local_info;
    // Shared RPC parser for either RDMA receive backend.
    InputMessengerProcessor _input_processor;

    // butex for inform read events on TCP fd during handshake
    butil::atomic<int> *_read_butex;
};

}  // namespace rdma
}  // namespace brpc

#else  // if BRPC_WITH_RDMA

class RdmaEndpoint { };

#endif  // ifdef USE_RD<A

#endif // BRPC_RDMA_ENDPOINT_H
