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
#include "brpc/rdma/rdma_connection.h"


namespace brpc {
class Socket;
class RdmaTransport;
namespace rdma {

DECLARE_bool(rdma_use_polling);
DECLARE_int32(rdma_poller_num);
DECLARE_bool(rdma_disable_bthread);

class BAIDU_CACHELINE_ALIGNMENT RdmaEndpoint {
friend class Socket;
friend class ::brpc::RdmaTransport;
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

    void GetLocalConnectionInfo(ParsedHello* local) const;
    // Returns 0 on success, 1 when ECE query is unavailable, -1 on error.
    int QueryLocalEce(ibv_ece* ece) const;
    void SetOutgoingEce(const ibv_ece& ece);
    int ReconnectForHandshake();

    // Initialize polling mode
    static int PollingModeInitialize(bthread_tag_t tag,
                                     std::function<void(void)> callback,
                                     std::function<void(void)> init_fn,
                                     std::function<void(void)> release_fn);

    static void PollingModeRelease(bthread_tag_t tag);

private:
    // Take ownership of fd and install it for the next hello.
    int ReplaceHandshakeFd(int fd);

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

    // Bringup the QP from RESET state to RTS state.
    // Arguments:
    //   remote: parsed remote hello. Provides the remote LID/GID/QP
    //           number for the RTR transition, and (on v3) the peer's
    //           ECE to set during the INIT->RTR transition.
    //   is_server: true on the server side, false on the client side.
    // Returns 0 on success, -1 on failed and errno set.
    int BringUpQp(const ParsedHello& remote, bool is_server);

    Socket* _socket;

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
};

}  // namespace rdma
}  // namespace brpc

#else  // if BRPC_WITH_RDMA

class RdmaEndpoint { };

#endif  // BRPC_WITH_RDMA

#endif // BRPC_RDMA_ENDPOINT_H
