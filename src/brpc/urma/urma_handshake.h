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

#ifndef BRPC_URMA_HANDSHAKE_H
#define BRPC_URMA_HANDSHAKE_H

#include <cstddef>
#include <cstdint>

#if BRPC_WITH_URMA

#include "urma_types.h"

namespace brpc {
namespace urma {

class UrmaEndpoint;

// Wire-format-agnostic view of a peer's hello message. Both v2 (binary) and
// v3 (protobuf) translate the bytes on the wire into this struct. The endpoint
// then uses ApplyRemoteHello to size its send/recv windows and to drive
// urma_import_seg / urma_import_jetty.
struct ParsedHello {
    uint32_t buffer_size = 0;      // peer recv buffer size in bytes (per WR)
    uint32_t recv_buffer_cnt = 0;  // peer recv buffer count (RQ depth - 1)
    uint32_t jetty_id = 0;         // peer jetty id
    uint8_t eid[16] = {0};          // peer EID (network order)
    uint32_t uasid = 0;             // peer uasid
    uint8_t tp_type = 0;            // urma_tp_type_t: URMA_RTP / URMA_CTP / URMA_UTP

    // Flattened peer buffer-pool segment.
    uint8_t seg_eid[16] = {0};      // EID owning the peer segment
    uint32_t seg_uasid = 0;         // uasid owning the peer segment
    uint64_t seg_va = 0;            // segment virtual address
    uint64_t seg_len = 0;          // segment length in bytes
    uint32_t seg_token_id = 0;      // segment token id
};

// Validate all values that influence resource import and queue/window sizing.
// Both v2 and v3 handshakes must pass this check before negotiation succeeds.
bool ValidHello(const ParsedHello& hello);

// v2 binary wire layout. The full on-wire packet is:
//   [ "URMA" 4B ][ HelloMessage body 82B ]   => 86 bytes total.
// (msg_len = 86 includes the magic prefix and the body, matching RDMA's
// convention where msg_len covers the whole packet.)
namespace v2_wire {

constexpr size_t MAGIC_STR_LEN = 4;
constexpr size_t HELLO_BODY_LEN = 82;
constexpr size_t HELLO_PACKET_LEN = MAGIC_STR_LEN + HELLO_BODY_LEN;  // 86
constexpr size_t HELLO_MSG_LEN_MIN = HELLO_PACKET_LEN;
constexpr size_t HELLO_MSG_LEN_MAX = 4096;
constexpr uint16_t HELLO_V2_VERSION = 2;
constexpr uint16_t IMPL_V2_VERSION = 1;

// The serializable struct. Aligned so it can be reinterpreted as raw bytes.
struct HelloMessage {
    uint16_t msg_len;       // total packet length (incl. magic)
    uint16_t hello_ver;
    uint16_t impl_ver;
    uint32_t buffer_size;
    uint32_t recv_buffer_cnt;
    uint32_t jetty_id;
    uint8_t eid[16];
    uint32_t uasid;
    uint8_t tp_type;
    uint8_t pad[3];        // keep the struct 4-byte aligned
    uint8_t seg_eid[16];
    uint32_t seg_uasid;
    uint64_t seg_va;
    uint64_t seg_len;
    uint32_t seg_token_id;

    void Serialize(void* buf) const;   // host -> network order, write to buf
    void Deserialize(const void* buf);  // network -> host order, read from buf
};

}  // namespace v2_wire

// Abstract handshake strategy. v2 speaks binary; v3 speaks protobuf ("URM3").
class UrmaHandshake {
public:
    virtual ~UrmaHandshake() = default;
    virtual int ProtocolVersion() const = 0;

    // Send our local hello over the TCP fd held by the endpoint.
    // Returns 0 on success, -1 on failure (errno set).
    virtual int SendLocalHello() = 0;

    // Read the peer's hello and parse it into @out. Sets @negotiated to true
    // if the peer is URMA-capable and the message validates; false (with
    // return 0) to signal a graceful fall-back to TCP.
    // Returns -1 on IO error (errno set).
    virtual int ReceiveAndParseRemoteHello(ParsedHello* out, bool* negotiated) = 0;
};

// v2 binary handshake (magic "URMA").
class UrmaHandshakeClientV2 : public UrmaHandshake {
public:
    explicit UrmaHandshakeClientV2(UrmaEndpoint* ep) : _ep(ep) {}
    int ProtocolVersion() const override { return 2; }
    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* out, bool* negotiated) override;

private:
    UrmaEndpoint* _ep;
};

class UrmaHandshakeServerV2 : public UrmaHandshake {
public:
    explicit UrmaHandshakeServerV2(UrmaEndpoint* ep) : _ep(ep) {}
    int ProtocolVersion() const override { return 2; }
    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* out, bool* negotiated) override;

private:
    UrmaEndpoint* _ep;
};

// v3 protobuf handshake (magic "URM3").
class UrmaHandshakeClientV3 : public UrmaHandshake {
public:
    explicit UrmaHandshakeClientV3(UrmaEndpoint* ep) : _ep(ep) {}
    int ProtocolVersion() const override { return 3; }
    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* out, bool* negotiated) override;

private:
    UrmaEndpoint* _ep;
};

class UrmaHandshakeServerV3 : public UrmaHandshake {
public:
    explicit UrmaHandshakeServerV3(UrmaEndpoint* ep) : _ep(ep) {}
    int ProtocolVersion() const override { return 3; }
    int SendLocalHello() override;
    int ReceiveAndParseRemoteHello(ParsedHello* out, bool* negotiated) override;

private:
    UrmaEndpoint* _ep;
};

// Client-side factory: picks v2/v3 based on --urma_client_handshake_version.
UrmaHandshake* CreateClientHandshake(UrmaEndpoint* ep);

// Server-side factory: dispatches on the first 4 bytes read from the TCP fd.
// Returns nullptr if the magic is not "URMA"/"URM3" (caller falls back to TCP).
// @magic is the first MAGIC_STR_LEN bytes already read from the fd.
UrmaHandshake* CreateServerHandshakeByMagic(UrmaEndpoint* ep,
                                             const uint8_t magic[v2_wire::MAGIC_STR_LEN]);

// Drain @n bytes from the TCP fd (used when msg_len > HELLO_MSG_LEN_MIN).
int DrainBytes(UrmaEndpoint* ep, size_t n);

// Read the body following the magic and translate to ParsedHello. Friend of
// UrmaEndpoint. Returns -1 on IO error; 0 with *negotiated=false on invalid
// (peer not URMA-capable); 0 with *negotiated=true on success.
int ReadBodyAndNegotiate(UrmaEndpoint* ep, ParsedHello* out, bool* negotiated);

}  // namespace urma
}  // namespace brpc

#endif  // BRPC_WITH_URMA

#endif  // BRPC_URMA_HANDSHAKE_H
