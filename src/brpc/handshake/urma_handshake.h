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

#ifndef BRPC_HANDSHAKE_URMA_HANDSHAKE_H
#define BRPC_HANDSHAKE_URMA_HANDSHAKE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "brpc/handshake/handshake_adapter.h"

namespace brpc {
namespace handshake {

HandshakeAdapter* GetUrmaServerHandshakeAdapter();
StepResult RunUrmaFallbackServerHandshake(butil::IOBuf* source, Socket* socket);

}  // namespace handshake
}  // namespace brpc

#if BRPC_WITH_URMA

#include "brpc/urma/urma_endpoint.h"
#include "brpc/transport_handshake.h"

namespace brpc {
namespace urma {

class UrmaEndpoint;

using ParsedHello = UrmaConnectionInfo;

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

// The v2 hello fields are serialized explicitly in network byte order.
// The wire format is independent of the in-memory struct layout.
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
    uint8_t pad[3];         // reserved wire bytes, serialized as zeros
    uint8_t seg_eid[16];
    uint32_t seg_uasid;
    uint64_t seg_va;
    uint64_t seg_len;
    uint32_t seg_token_id;

    void Serialize(void* buf) const;   // host -> network order, write to buf
    void Deserialize(const void* buf);  // network -> host order, read from buf
};

}  // namespace v2_wire

// Wire-format adapter used by the common transport handshake.
// Stores the selected protocol version and parsed peer hello;
// UrmaEndpoint owns the local and imported URMA resources.
class UrmaHandshakeAdapter : public handshake::HandshakeProtocol {
public:
    UrmaHandshakeAdapter(UrmaEndpoint* ep, int version)
        : _ep(ep), _version(version), _remote() {}

    int ProtocolVersion() const override { return _version; }
    bool AckDisabledHello() const override { return false; }
    const handshake::FrameSpec& HelloFrameSpec() const override;
    const handshake::FrameSpec& AckFrameSpec() const override;
    handshake::StepResult BuildHello(bool enabled,
                                     std::string* payload) override;
    handshake::StepResult ParseHello(const std::string& payload) override;
    const ParsedHello& remote() const { return _remote; }

private:
    UrmaEndpoint* _ep;
    int _version;
    ParsedHello _remote;
};

std::unique_ptr<UrmaHandshakeAdapter> CreateClientHandshakeAdapter(
    UrmaEndpoint* ep);
std::vector<std::unique_ptr<UrmaHandshakeAdapter> >
CreateServerHandshakeAdapters(UrmaEndpoint* ep);

}  // namespace urma
}  // namespace brpc

#endif  // BRPC_WITH_URMA

#endif  // BRPC_HANDSHAKE_URMA_HANDSHAKE_H
