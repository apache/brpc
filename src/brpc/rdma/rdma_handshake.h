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

#ifndef BRPC_RDMA_HANDSHAKE_H
#define BRPC_RDMA_HANDSHAKE_H

#if BRPC_WITH_RDMA

#include <memory>
#include <infiniband/verbs.h>
#include "butil/macros.h"
#include "butil/containers/optional.h"
#include "brpc/rdma/rdma_handshake_constants.h"

namespace butil {
class IOBuf;
}

namespace brpc {
namespace rdma {

class RdmaEndpoint;

// Wire-format-agnostic representation of a peer's hello message.
// Each protocol version (v2 binary, v3 protobuf) translates its own
// wire format into this struct so the state-machine driver in
// RdmaEndpoint::ProcessHandshakeAt{Client,Server} stays free of any
// wire-format details.
struct ParsedHello {
    uint32_t block_size;
    uint16_t sq_size;
    uint16_t rq_size;
    uint16_t lid;
    ibv_gid  gid;
    uint32_t qp_num;
    // ECE (Enhanced Connection Establishment), v3 handshake only.
    // nullopt means the peer did not advertise any ECE (either it disabled
    // ECE, its lib does not support ECE, or it is a v2 peer). When engaged:
    //   - on the server side: the client's queried ECE capabilities;
    //   - on the client side: the server's reduced/negotiated ECE.
    butil::optional<ibv_ece> ece;
};

// Result of reading/parsing a peer's hello (see ReceiveAndParseRemoteHello).
enum class RemoteHelloResult {
    // A full hello was read and negotiation succeeded.
    NEGOTIATED,
    // A full hello was read but negotiation failed.
    FALLBACK,
    // (server only) Not enough data yet.
    NEED_MORE,
    // IO/protocol error (errno set).
    ERROR,
};

namespace v2_wire {

// v2 binary HelloMessage.
struct HelloMessage {
    void Serialize(void* data) const;
    void Deserialize(void* data);

    uint16_t msg_len;
    uint16_t hello_ver;
    uint16_t impl_ver;
    uint32_t block_size;
    uint16_t sq_size;
    uint16_t rq_size;
    uint16_t lid;
    ibv_gid  gid;
    uint32_t qp_num;
};

}  // namespace v2_wire

// Base class of an RDMA handshake, shared by both roles.
class RdmaHandshake {
public:
    RdmaHandshake(RdmaEndpoint* ep, int version) : _ep(ep), _version(version) {}
    virtual ~RdmaHandshake() = default;

    DISALLOW_COPY_AND_ASSIGN(RdmaHandshake);

    // Wire-level protocol version (2 for "RDMA", 3 for "RDM3").
    int ProtocolVersion() const { return _version; }

    // Build and send the local hello (including the protocol magic).
    // Returns 0 on success, -1 on IO error (errno set).
    //
    // For a server in fallback state, implementations MUST still
    // produce a sendable message; each version uses its own wire
    // convention to signal "I am falling back" to the peer:
    //   - v2: zero hello_ver/impl_ver so the peer's HelloNegotiationValid
    //         rejects it;
    //   - v3: qp_num==0 so the peer's ValidRdmaHello rejects it.
    virtual int SendLocalHello() = 0;

    // Read and parse the peer's hello into *remote.
    virtual RemoteHelloResult ReceiveAndParseRemoteHello(ParsedHello* remote) = 0;

protected:
    RdmaEndpoint* _ep;
    int _version;
};

// Server-side handshake base: parses the remote hello non-blockingly out of
// `_source` (an IOBuf filled by InputMessenger), never touching the fd.
class ServerRdmaHandshake : public RdmaHandshake {
public:
    ServerRdmaHandshake(RdmaEndpoint* ep, butil::IOBuf* source, int version)
        : RdmaHandshake(ep, version), _source(source) {}

protected:
    butil::IOBuf* _source;
};

// v2 handshake (legacy "RDMA" magic, 36B binary HelloMessage).
class RdmaHandshakeClientV2 : public RdmaHandshake {
public:
    explicit RdmaHandshakeClientV2(RdmaEndpoint* ep) : RdmaHandshake(ep, 2) {}
    int SendLocalHello() override;
    RemoteHelloResult ReceiveAndParseRemoteHello(ParsedHello* remote) override;
};

class RdmaHandshakeServerV2 : public ServerRdmaHandshake {
public:
    RdmaHandshakeServerV2(RdmaEndpoint* ep, butil::IOBuf* source)
        : ServerRdmaHandshake(ep, source, 2) {}
    int SendLocalHello() override;
    RemoteHelloResult ReceiveAndParseRemoteHello(ParsedHello* remote) override;
};

// v3 handshake (new "RDM3" magic, protobuf RdmaHello).
// [ "RDM3" 4B ][ pb_size 4B (big-endian) ][ RdmaHello protobuf bytes ]
class RdmaHandshakeClientV3 : public RdmaHandshake {
public:
    explicit RdmaHandshakeClientV3(RdmaEndpoint* ep) : RdmaHandshake(ep, 3) {}
    int SendLocalHello() override;
    RemoteHelloResult ReceiveAndParseRemoteHello(ParsedHello* remote) override;
};

class RdmaHandshakeServerV3 : public ServerRdmaHandshake {
public:
    RdmaHandshakeServerV3(RdmaEndpoint* ep, butil::IOBuf* source)
        : ServerRdmaHandshake(ep, source, 3) {}
    int SendLocalHello() override;
    RemoteHelloResult ReceiveAndParseRemoteHello(ParsedHello* remote) override;
};

// Factory methods
//
// Pick the client-side handshake based on
// FLAGS_rdma_client_handshake_version:
//   2 (default) -> RdmaHandshakeClientV2
//   3           -> RdmaHandshakeClientV3
// Other values fall back to V2.
std::unique_ptr<RdmaHandshake> CreateClientHandshake(RdmaEndpoint* ep);

// Pick the server-side handshake based on the 4B magic already read.
// Returns NULL if `magic` is not a recognized RDMA magic
// (the caller should then fallback to TCP).
//   "RDMA" -> RdmaHandshakeServerV2
//   "RDM3" -> RdmaHandshakeServerV3
std::unique_ptr<RdmaHandshake> CreateServerHandshakeByMagic(
    RdmaEndpoint* ep, butil::IOBuf* source, const uint8_t magic[HELLO_MAGIC_LEN]);

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_WITH_RDMA
#endif  // BRPC_RDMA_HANDSHAKE_H
