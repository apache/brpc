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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <infiniband/verbs.h>
#include "butil/macros.h"

namespace brpc {
namespace rdma {

class RdmaEndpoint;

// Length of the RDMA handshake magic string (e.g. "RDMA", "RDM3").
static const size_t MAGIC_STR_LEN = 4;

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
};

namespace v2_wire {

// Wire constants for the v2 hello.
//
// HELLO_MSG_LEN_MIN: total length of the base v2 hello (4B magic +
// 36B HelloMessage). Anything shorter than this is malformed.
// HELLO_MSG_LEN_MAX: upper bound for the entire v2 hello message
// length declared by HelloMessage::msg_len. Anything beyond this is
// treated as a protocol error and the connection is closed without
// attempting to drain.
static constexpr size_t HELLO_MSG_LEN_MIN = 40;
static constexpr size_t HELLO_MSG_LEN_MAX = 4096;

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

// Abstract base class of an RDMA handshake.
//
// Acts as the protocol-version dispatch point for the state machine
// driven by RdmaEndpoint::ProcessHandshakeAt{Client,Server}.
class RdmaHandshake {
public:
    explicit RdmaHandshake(RdmaEndpoint* ep) : _ep(ep) {}
    virtual ~RdmaHandshake() = default;

    DISALLOW_COPY_AND_ASSIGN(RdmaHandshake);

    // Wire-level protocol version (2 for "RDMA", 3 for "RDM3").
    virtual int ProtocolVersion() const = 0;

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

    // Read the peer's hello, validate it, and translate into ParsedHello.
    //
    // Role-specific semantics:
    //   - Client subclasses: read & verify the 4B magic first, then the
    //     body. (The endpoint did NOT pre-read the magic on the client
    //     side.)
    //   - Server subclasses: read ONLY the body. The 4B magic was
    //     already consumed by ProcessHandshakeAtServer and was used to
    //     pick `this` from CreateServerHandshakeByMagic; re-reading
    //     would deadlock.
    //
    // Outputs:
    //   *negotiated -- true if the remote hello is structurally valid
    //                  AND passes per-protocol negotiation checks;
    //                  false means the peer asked for fallback or sent
    //                  something we can't honor.
    // Returns:
    //    0 -- IO/parsing layer OK; check *negotiated and *remote.
    //   -1 -- IO error or unrecoverable protocol error (errno set).
    virtual int ReceiveAndParseRemoteHello(ParsedHello* remote, bool* negotiated) = 0;

protected:
    RdmaEndpoint* _ep;
};

// v2 handshake (legacy "RDMA" magic, 36B binary HelloMessage).
class RdmaHandshakeClientV2 : public RdmaHandshake {
public:
    using RdmaHandshake::RdmaHandshake;
    int ProtocolVersion() const override { return 2; }

    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* remote, bool* negotiated) override;
};

class RdmaHandshakeServerV2 : public RdmaHandshake {
public:
    using RdmaHandshake::RdmaHandshake;
    int ProtocolVersion() const override { return 2; }

    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* remote, bool* negotiated) override;
};

// v3 handshake (new "RDM3" magic, protobuf RdmaHello).
// [ "RDM3" 4B ][ pb_size 4B (big-endian) ][ RdmaHello protobuf bytes ]
class RdmaHandshakeClientV3 : public RdmaHandshake {
public:
    using RdmaHandshake::RdmaHandshake;
    int ProtocolVersion() const override { return 3; }

    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* remote, bool* negotiated) override;
};

class RdmaHandshakeServerV3 : public RdmaHandshake {
public:
    using RdmaHandshake::RdmaHandshake;
    int ProtocolVersion() const override { return 3; }

    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* remote, bool* negotiated) override;
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
    RdmaEndpoint* ep, const uint8_t magic[MAGIC_STR_LEN]);

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_WITH_RDMA
#endif  // BRPC_RDMA_HANDSHAKE_H
