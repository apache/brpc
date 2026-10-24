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

#include "brpc/handshake/urma_handshake.h"

#include "brpc/adapter_transport.h"

#if BRPC_WITH_URMA

#include <algorithm>
#include <cstring>
#include <limits>

#include <gflags/gflags.h>

#include "butil/atomicops.h"
#include "butil/iobuf.h"          // IOBuf, IOPortal, IOBufAsZeroCopy*
#include "butil/logging.h"
#include "butil/sys_byteorder.h"

#include "brpc/socket.h"
#include "brpc/urma/urma_endpoint.h"
#include "brpc/urma/urma_helper.h"
#include "brpc/urma/urma_handshake.pb.h"
#include "brpc/urma_transport.h"

namespace brpc {
namespace urma {

DEFINE_int32(urma_client_handshake_version, 2,
              "Client handshake version: 2 = binary, 3 = protobuf");

// ============================================================================
// v2 binary HelloMessage.
// On-wire layout (network byte order, tightly packed body of 82 bytes):
//
//   offset  field            size
//      0    msg_len          2B    (full packet length incl. magic)
//      2    hello_ver        2B
//      4    impl_ver         2B
//      6    buffer_size      4B
//     10    recv_buffer_cnt  4B
//     14    jetty_id         4B
//     18    eid             16B    (raw, no swap)
//     34    uasid            4B
//     38    tp_type          1B
//     39    pad              3B
//     42    seg_eid         16B    (raw)
//     58    seg_uasid        4B
//     62    seg_va           8B
//     70    seg_len          8B
//     78    seg_token_id     4B
//   total = 82 bytes body.
//
// Full packet = magic "URMA" (4B) + body (82B) = 86 bytes.
// ============================================================================

namespace v2_wire {

void HelloMessage::Serialize(void* buf) const {
    uint8_t* p = static_cast<uint8_t*>(buf);
    auto write16 = [&p](uint16_t value) {
        value = butil::HostToNet16(value);
        std::memcpy(p, &value, sizeof(value));
        p += sizeof(value);
    };
    auto write32 = [&p](uint32_t value) {
        value = butil::HostToNet32(value);
        std::memcpy(p, &value, sizeof(value));
        p += sizeof(value);
    };
    auto write64 = [&p](uint64_t value) {
        value = butil::HostToNet64(value);
        std::memcpy(p, &value, sizeof(value));
        p += sizeof(value);
    };

    write16(msg_len);
    write16(hello_ver);
    write16(impl_ver);
    write32(buffer_size);
    write32(recv_buffer_cnt);
    write32(jetty_id);
    std::memcpy(p, eid, sizeof(eid));
    p += sizeof(eid);
    write32(uasid);
    *p++ = tp_type;
    std::memset(p, 0, sizeof(pad));
    p += sizeof(pad);
    std::memcpy(p, seg_eid, sizeof(seg_eid));
    p += sizeof(seg_eid);
    write32(seg_uasid);
    write64(seg_va);
    write64(seg_len);
    write32(seg_token_id);
}

void HelloMessage::Deserialize(const void* buf) {
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    auto read16 = [&p]() {
        uint16_t value;
        std::memcpy(&value, p, sizeof(value));
        p += sizeof(value);
        return butil::NetToHost16(value);
    };
    auto read32 = [&p]() {
        uint32_t value;
        std::memcpy(&value, p, sizeof(value));
        p += sizeof(value);
        return butil::NetToHost32(value);
    };
    auto read64 = [&p]() {
        uint64_t value;
        std::memcpy(&value, p, sizeof(value));
        p += sizeof(value);
        return butil::NetToHost64(value);
    };

    msg_len = read16();
    hello_ver = read16();
    impl_ver = read16();
    buffer_size = read32();
    recv_buffer_cnt = read32();
    jetty_id = read32();
    std::memcpy(eid, p, sizeof(eid));
    p += sizeof(eid);
    uasid = read32();
    tp_type = *p++;
    p += sizeof(pad);
    std::memcpy(seg_eid, p, sizeof(seg_eid));
    p += sizeof(seg_eid);
    seg_uasid = read32();
    seg_va = read64();
    seg_len = read64();
    seg_token_id = read32();
}

}  // namespace v2_wire

// ============================================================================
// Shared helpers.
// ============================================================================

namespace {

constexpr uint32_t MIN_BUFFER_SIZE = 1024;
// Three SQ entries are reserved for flow-control messages. The advertised
// receive count must leave at least one data WR after those reservations.
constexpr uint32_t MIN_BUFFER_CNT = 3;
constexpr uint32_t MAX_BUFFER_CNT = 65535;
constexpr uint32_t MAX_V3_PB_SIZE = 4096;

}  // namespace

bool ValidHello(const ParsedHello& h) {
    if (h.buffer_size < MIN_BUFFER_SIZE) {
        return false;
    }
    if (h.recv_buffer_cnt < MIN_BUFFER_CNT || h.recv_buffer_cnt > MAX_BUFFER_CNT) {
        return false;
    }
    if (h.jetty_id == 0) {
        return false;
    }
    if (h.tp_type > static_cast<uint8_t>(URMA_UTP)) {
        return false;
    }
    // The provider consumes the advertised VA/length as a single remote
    // segment. Reject a wrapped end address before it reaches the import
    // API, which could otherwise turn a malformed hello into an invalid
    // segment range.
    if (h.seg_len == 0 || h.seg_va == 0 ||
        h.seg_len > std::numeric_limits<uint64_t>::max() - h.seg_va) {
        return false;
    }
    return true;
}

namespace {

const handshake::FrameSpec& UrmaV2FrameSpec() {
    static const handshake::FrameSpec spec(
        "URMA", 4, v2_wire::HELLO_PACKET_LEN,
        v2_wire::HELLO_MSG_LEN_MAX,
        handshake::FrameSpec::U16_TOTAL_LENGTH);
    return spec;
}

const handshake::FrameSpec& UrmaV3FrameSpec() {
    static const handshake::FrameSpec spec(
        "URM3", 4, 8, 8 + MAX_V3_PB_SIZE,
        handshake::FrameSpec::U32_BODY_LENGTH);
    return spec;
}

const handshake::FrameSpec& UrmaAckFrameSpec() {
    static const handshake::FrameSpec spec(
        nullptr, 0, sizeof(uint32_t), sizeof(uint32_t),
        handshake::FrameSpec::FIXED);
    return spec;
}

void TranslateV2Hello(const v2_wire::HelloMessage& message,
                      ParsedHello* out) {
    out->buffer_size = message.buffer_size;
    out->recv_buffer_cnt = message.recv_buffer_cnt;
    out->jetty_id = message.jetty_id;
    memcpy(out->eid, message.eid, sizeof(out->eid));
    out->uasid = message.uasid;
    out->tp_type = message.tp_type;
    memcpy(out->seg_eid, message.seg_eid, sizeof(out->seg_eid));
    out->seg_uasid = message.seg_uasid;
    out->seg_va = message.seg_va;
    out->seg_len = message.seg_len;
    out->seg_token_id = message.seg_token_id;
}

void TranslateV3Hello(const UrmaHello& message, ParsedHello* out) {
    out->buffer_size = message.buffer_size();
    out->recv_buffer_cnt = message.recv_buffer_cnt();
    out->jetty_id = message.jetty_id();
    memcpy(out->eid, message.eid().data(), sizeof(out->eid));
    out->uasid = message.uasid();
    out->tp_type = static_cast<uint8_t>(message.tp_type());
    memcpy(out->seg_eid, message.seg_eid().data(), sizeof(out->seg_eid));
    out->seg_uasid = message.seg_uasid();
    out->seg_va = message.seg_va();
    out->seg_len = message.seg_len();
    out->seg_token_id = message.seg_token_id();
}

}  // namespace

const handshake::FrameSpec& UrmaHandshakeAdapter::HelloFrameSpec() const {
    return _version == 3 ? UrmaV3FrameSpec() : UrmaV2FrameSpec();
}

const handshake::FrameSpec& UrmaHandshakeAdapter::AckFrameSpec() const {
    return UrmaAckFrameSpec();
}

handshake::StepResult UrmaHandshakeAdapter::BuildHello(
    bool enabled, std::string* payload) {
    if (_version == 3) {
        UrmaHello message;
        if (enabled) {
            CHECK(_ep != nullptr);
            _ep->FillLocalHelloV3(&message);
        } else {
            message.set_buffer_size(0);
            message.set_recv_buffer_cnt(0);
            message.set_jetty_id(0);
            message.set_eid(std::string(16, '\0'));
            message.set_uasid(0);
            message.set_tp_type(0);
            message.set_seg_eid(std::string(16, '\0'));
            message.set_seg_uasid(0);
            message.set_seg_va(0);
            message.set_seg_len(0);
            message.set_seg_token_id(0);
        }
        return message.SerializeToString(payload)
            ? handshake::STEP_OK : handshake::STEP_ERROR;
    }
    v2_wire::HelloMessage message{};
    if (enabled) {
        CHECK(_ep != nullptr);
        _ep->FillLocalHelloV2(&message);
    } else {
        message.msg_len = v2_wire::HELLO_PACKET_LEN;
    }
    uint8_t body[v2_wire::HELLO_BODY_LEN];
    message.Serialize(body);
    payload->assign(reinterpret_cast<const char*>(body + sizeof(uint16_t)),
                    sizeof(body) - sizeof(uint16_t));
    return handshake::STEP_OK;
}

handshake::StepResult UrmaHandshakeAdapter::ParseHello(
    const std::string& payload) {
    ParsedHello remote{};
    if (_version == 3) {
        UrmaHello message;
        if (!message.ParseFromString(payload) ||
            message.eid().size() != sizeof(remote.eid) ||
            message.seg_eid().size() != sizeof(remote.seg_eid)) {
            errno = EPROTO;
            return handshake::STEP_ERROR;
        }
        // ParsedHello stores this as a byte. Reject an out-of-range protobuf
        // value before narrowing it, otherwise e.g. 256 becomes a valid 0.
        if (message.tp_type() > static_cast<uint32_t>(URMA_UTP)) {
            return handshake::STEP_FALLBACK;
        }
        TranslateV3Hello(message, &remote);
    } else {
        if (payload.size() < v2_wire::HELLO_BODY_LEN - sizeof(uint16_t)) {
            errno = EPROTO;
            return handshake::STEP_ERROR;
        }
        uint8_t body[v2_wire::HELLO_BODY_LEN];
        const uint16_t total_be = butil::HostToNet16(
            static_cast<uint16_t>(4 + sizeof(uint16_t) + payload.size()));
        memcpy(body, &total_be, sizeof(total_be));
        memcpy(body + sizeof(total_be), payload.data(),
               sizeof(body) - sizeof(total_be));
        v2_wire::HelloMessage message{};
        message.Deserialize(body);
        if (message.hello_ver != v2_wire::HELLO_V2_VERSION ||
            message.impl_ver != v2_wire::IMPL_V2_VERSION) {
            return handshake::STEP_FALLBACK;
        }
        TranslateV2Hello(message, &remote);
    }
    if (!ValidHello(remote)) {
        return handshake::STEP_FALLBACK;
    }
    _remote = remote;
    return handshake::STEP_OK;
}

std::unique_ptr<UrmaHandshakeAdapter> CreateClientHandshakeAdapter(
    UrmaEndpoint* ep) {
    return std::unique_ptr<UrmaHandshakeAdapter>(
        new UrmaHandshakeAdapter(
            ep, FLAGS_urma_client_handshake_version == 3 ? 3 : 2));
}

std::vector<std::unique_ptr<UrmaHandshakeAdapter> >
CreateServerHandshakeAdapters(UrmaEndpoint* ep) {
    std::vector<std::unique_ptr<UrmaHandshakeAdapter> > adapters;
    adapters.emplace_back(new UrmaHandshakeAdapter(ep, 2));
    adapters.emplace_back(new UrmaHandshakeAdapter(ep, 3));
    return adapters;
}

// File-local (not in the anonymous namespace so it can be friend-declared
// from urma_endpoint.h's UrmaEndpoint). Reads the body following the magic
// and translates it into ParsedHello.
int ReadBodyAndNegotiate(UrmaEndpoint* ep, ParsedHello* out, bool* negotiated) {
    *negotiated = false;
    uint8_t body[v2_wire::HELLO_BODY_LEN];
    if (ep->ReadFromFd(body, v2_wire::HELLO_BODY_LEN) < 0) {
        return -1;
    }
    v2_wire::HelloMessage m;
    m.Deserialize(body);
    if (m.msg_len < v2_wire::HELLO_MSG_LEN_MIN ||
        m.msg_len > v2_wire::HELLO_MSG_LEN_MAX ||
        m.hello_ver != v2_wire::HELLO_V2_VERSION ||
        m.impl_ver != v2_wire::IMPL_V2_VERSION) { return 0; }
    ParsedHello p;
    p.buffer_size = m.buffer_size;
    p.recv_buffer_cnt = m.recv_buffer_cnt;
    p.jetty_id = m.jetty_id;
    std::memcpy(p.eid, m.eid, 16);
    p.uasid = m.uasid;
    p.tp_type = m.tp_type;
    std::memcpy(p.seg_eid, m.seg_eid, 16);
    p.seg_uasid = m.seg_uasid;
    p.seg_va = m.seg_va;
    p.seg_len = m.seg_len;
    p.seg_token_id = m.seg_token_id;
    if (!ValidHello(p)) {
        return 0;
    }
    // Drain trailing bytes if msg_len advertises more than the fixed body.
    if (m.msg_len > v2_wire::HELLO_PACKET_LEN) {
        if (DrainBytes(ep, m.msg_len - v2_wire::HELLO_PACKET_LEN) < 0) {
            return -1;
        }
    }
    *out = p;
    *negotiated = true;
    return 0;
}

int DrainBytes(UrmaEndpoint* ep, size_t n) {
    char buf[4096];
    while (n > 0) {
        size_t want = std::min(n, sizeof(buf));
        if (ep->ReadFromFd(buf, want) < 0) {
            return -1;
        }
        n -= want;
    }
    return 0;
}

// ============================================================================
// v2 client / server.
// ============================================================================

int UrmaHandshakeClientV2::SendLocalHello() {
    v2_wire::HelloMessage m;
    _ep->FillLocalHelloV2(&m);
    uint8_t packet[v2_wire::HELLO_PACKET_LEN];
    std::memcpy(packet, "URMA", 4);
    m.Serialize(packet + 4);
    return _ep->WriteToFd(packet, v2_wire::HELLO_PACKET_LEN);
}

int UrmaHandshakeClientV2::ReceiveAndParseRemoteHello(ParsedHello* out,
                                                      bool* negotiated) {
    *negotiated = false;
    uint8_t magic[v2_wire::MAGIC_STR_LEN];
    if (_ep->ReadFromFd(magic, v2_wire::MAGIC_STR_LEN) < 0) {
        return -1;
    }
    if (std::memcmp(magic, "URMA", 4) != 0) {
        // Peer is not URMA-capable; push the magic back so the TCP input
        // messenger can re-parse it.
        _ep->PushBackToReadBuf(magic, v2_wire::MAGIC_STR_LEN);
        return 0;
    }
    return ReadBodyAndNegotiate(_ep, out, negotiated);
}

int UrmaHandshakeServerV2::ReceiveAndParseRemoteHello(ParsedHello* out,
                                                       bool* negotiated) {
    return ReadBodyAndNegotiate(_ep, out, negotiated);
}

int UrmaHandshakeServerV2::SendLocalHello() {
    v2_wire::HelloMessage m;
    _ep->FillLocalHelloV2(&m);
    auto* tp = static_cast<UrmaTransport*>(_ep->_socket->_transport.get());
    if (tp->_urma_state.load(butil::memory_order_acquire) ==
        UrmaTransport::URMA_OFF) {
        // Tell the client we are not URMA-capable: zero the version fields so
        // the client's version check fails and it falls back to TCP.
        m.hello_ver = 0;
        m.impl_ver = 0;
        m.jetty_id = 0;
        m.buffer_size = 0;
    }
    uint8_t packet[v2_wire::HELLO_PACKET_LEN];
    std::memcpy(packet, "URMA", 4);
    m.Serialize(packet + 4);
    return _ep->WriteToFd(packet, v2_wire::HELLO_PACKET_LEN);
}

// ============================================================================
// v3 protobuf ("URM3"). The protobuf (de)serialization lives on the endpoint
// because it touches UrmaEndpoint private state; the classes here just call
// FillLocalHelloV3 / WriteHelloV3 / ReadAndParseHelloV3.
// ============================================================================

int UrmaHandshakeClientV3::SendLocalHello() {
    UrmaHello msg;
    _ep->FillLocalHelloV3(&msg);
    return _ep->WriteHelloV3(msg);
}

int UrmaHandshakeClientV3::ReceiveAndParseRemoteHello(ParsedHello* out,
                                                      bool* negotiated) {
    *negotiated = false;
    uint8_t magic[v2_wire::MAGIC_STR_LEN];
    if (_ep->ReadFromFd(magic, v2_wire::MAGIC_STR_LEN) < 0) {
        return -1;
    }
    if (std::memcmp(magic, "URM3", 4) != 0) {
        _ep->PushBackToReadBuf(magic, v2_wire::MAGIC_STR_LEN);
        return 0;
    }
    return _ep->ReadAndParseHelloV3(out, negotiated);
}

int UrmaHandshakeServerV3::ReceiveAndParseRemoteHello(ParsedHello* out,
                                                      bool* negotiated) {
    return _ep->ReadAndParseHelloV3(out, negotiated);
}

int UrmaHandshakeServerV3::SendLocalHello() {
    UrmaHello msg;
    _ep->FillLocalHelloV3(&msg);
    // v3 has no zero-out path; the client rejects jetty_id==0 via ValidHello.
    return _ep->WriteHelloV3(msg);
}

// ============================================================================
// Factories.
// ============================================================================

UrmaHandshake* CreateClientHandshake(UrmaEndpoint* ep) {
    switch (FLAGS_urma_client_handshake_version) {
    case 3: return new UrmaHandshakeClientV3(ep);
    case 2:
    default: return new UrmaHandshakeClientV2(ep);
    }
}

UrmaHandshake* CreateServerHandshakeByMagic(UrmaEndpoint* ep,
                                             const uint8_t magic[v2_wire::MAGIC_STR_LEN]) {
    if (std::memcmp(magic, "URMA", 4) == 0) {
        return new UrmaHandshakeServerV2(ep);
    }
    if (std::memcmp(magic, "URM3", 4) == 0) {
        return new UrmaHandshakeServerV3(ep);
    }
    return nullptr;
}

}  // namespace urma

namespace handshake {

class UrmaServerHandshakeTransport : public HandshakeTransport {
public:
    UrmaServerHandshakeTransport(UrmaTransport* transport,
                                 butil::IOBuf* source)
        : _transport(transport), _source(source), _protocol(nullptr) {}

    void OnProtocolSelected(HandshakeProtocol* protocol) override {
        _protocol = static_cast<urma::UrmaHandshakeAdapter*>(protocol);
    }
    StepResult PrepareResources() override {
        if (_transport->PrepareUpgradeResources(true) == 0) {
            return STEP_OK;
        }
        _transport->DeactivateUpgrade();
        return STEP_FALLBACK;
    }
    StepResult NegotiateResources() override {
        CHECK(_protocol != nullptr);
        if (_transport->NegotiateUpgradeResources(
                _protocol->remote(), true) == 0) {
            return STEP_OK;
        }
        _transport->DeactivateUpgrade();
        return STEP_FALLBACK;
    }
    void OnEstablished() override { _transport->ActivateUpgrade(); }
    void OnFallback() override { _transport->DeactivateUpgrade(); }
    void OnFailed() override { _transport->DeactivateUpgrade(); }
    StepResult ValidateEstablished() override {
        return _source->empty() ? STEP_OK : STEP_ERROR;
    }

private:
    UrmaTransport* _transport;
    butil::IOBuf* _source;
    urma::UrmaHandshakeAdapter* _protocol;
};

class UrmaServerHandshakeAdapter : public StandardHandshakeAdapter {
protected:
    StepResult RunServerStep(butil::IOBuf* source, Socket* socket) override {
        AdapterTransport* adapter = AdapterTransport::Get(socket);
        if (!adapter->upgrade_capable(SOCKET_MODE_URMA)) {
            return RunUrmaFallbackServerHandshake(source, socket);
        }
        UrmaTransport* transport = static_cast<UrmaTransport*>(
            adapter->high_speed_transport());
        std::vector<std::unique_ptr<urma::UrmaHandshakeAdapter> > owned =
            urma::CreateServerHandshakeAdapters(transport->GetUrmaEp());
        std::vector<HandshakeProtocol*> protocols;
        for (size_t i = 0; i < owned.size(); ++i) {
            protocols.push_back(owned[i].get());
        }
        IOBufHandshakeInput input(source);
        UrmaServerHandshakeTransport participant(transport, source);
        return adapter->handshake_session()->RunServer(
            protocols, &input, &participant, false);
    }

    HandshakeSession* GetSession(Socket* socket) const override {
        return AdapterTransport::Get(socket)->handshake_session();
    }
};

HandshakeAdapter* GetUrmaServerHandshakeAdapter() {
    static UrmaServerHandshakeAdapter adapter;
    return &adapter;
}

}  // namespace handshake
}  // namespace brpc

#endif  // BRPC_WITH_URMA
