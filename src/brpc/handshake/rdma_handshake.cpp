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

#include "brpc/handshake/rdma_handshake.h"

#include <errno.h>
#include <limits>
#include <string>

#include "butil/logging.h"
#include "butil/raw_pack.h"
#include "butil/sys_byteorder.h"
#include "brpc/adapter_transport.h"
#include "brpc/handshake/rdma_handshake_constants.h"
#include "brpc/rdma_handshake.pb.h"
#include "brpc/socket.h"

#if BRPC_WITH_RDMA

#include <cstring>

#include <gflags/gflags.h>

#include "brpc/rdma_transport.h"

namespace brpc {
namespace rdma {

DEFINE_int32(rdma_client_handshake_version, 2,
             "RDMA handshake protocol version used by client. "
             "2 = legacy 'RDMA' magic (default, compatible with all servers); "
             "3 = new 'RDM3' protobuf-based handshake "
             "(MUST only be enabled after target servers support v3).");
DECLARE_bool(rdma_trace_verbose);

extern const uint16_t MIN_QP_SIZE;
extern const uint16_t MIN_BLOCK_SIZE;
extern bool g_skip_rdma_init;

DEFINE_bool(rdma_ece, false,
            "Enable end-to-end ECE negotiation in the RDMA v3 handshake");

void RdmaHandshakeAdapter::FillLocalHello(ParsedHello* local) const {
    _ep->GetLocalConnectionInfo(local);
}

void RdmaHandshakeAdapter::PrepareClientEce() {
    if (!FLAGS_rdma_ece) {
        return;
    }
    ibv_ece ece;
    const int rc = _ep->QueryLocalEce(&ece);
    if (rc == 0) {
        _ep->SetOutgoingEce(ece);
    } else if (rc < 0) {
        LOG_IF(WARNING, FLAGS_rdma_trace_verbose)
            << "Fail to IbvQueryEce on client, ECE not advertised";
    }
}

const handshake::FrameSpec& RdmaHandshakeAdapter::AckFrameSpec() const {
    return RdmaAckFrameSpec();
}

handshake::StepResult RdmaHandshakeAdapter::BuildHello(
    bool enabled, std::string* payload) {
    return BuildLocalHello(enabled, payload);
}

handshake::StepResult RdmaHandshakeAdapter::ParseHello(
    const std::string& payload) {
    return ParseRemoteHello(payload, &_remote);
}

namespace v2_wire {

void HelloMessage::Serialize(void* data) const {
    butil::RawPacker(data)
        .pack16(msg_len)
        .pack16(hello_ver)
        .pack16(impl_ver)
        .pack32(block_size)
        .pack16(sq_size)
        .pack16(rq_size)
        .pack16(lid)
        .pack_bytes(gid.raw, sizeof(gid.raw))
        .pack32(qp_num);
}

void HelloMessage::Deserialize(const void* data) {
    butil::RawUnpacker(data)
        .unpack16(msg_len)
        .unpack16(hello_ver)
        .unpack16(impl_ver)
        .unpack32(block_size)
        .unpack16(sq_size)
        .unpack16(rq_size)
        .unpack16(lid)
        .unpack_bytes(gid.raw, sizeof(gid.raw))
        .unpack32(qp_num);
}

static bool ValidHelloMessage(const HelloMessage& msg) {
    return msg.hello_ver == HELLO_V2_VERSION &&
           msg.impl_ver == IMPL_V2_VERSION &&
           msg.block_size >= MIN_BLOCK_SIZE &&
           msg.sq_size >= MIN_QP_SIZE &&
           msg.rq_size >= MIN_QP_SIZE;
}

static void TranslateHello(const HelloMessage& msg, ParsedHello* out) {
    out->block_size = msg.block_size;
    out->sq_size = msg.sq_size;
    out->rq_size = msg.rq_size;
    out->lid = msg.lid;
    out->gid = msg.gid;
    out->qp_num = msg.qp_num;
}

static void FillMessage(const ParsedHello& local, HelloMessage* msg) {
    msg->msg_len = HELLO_V2_MSG_LEN_MIN;
    msg->hello_ver = HELLO_V2_VERSION;
    msg->impl_ver = IMPL_V2_VERSION;
    msg->block_size = local.block_size;
    msg->sq_size = local.sq_size;
    msg->rq_size = local.rq_size;
    msg->lid = local.lid;
    msg->gid = local.gid;
    msg->qp_num = local.qp_num;
}

static handshake::StepResult SerializePayload(
    const HelloMessage& msg, std::string* payload) {
    uint8_t body[HELLO_V2_MSG_LEN_MIN - HELLO_MAGIC_LEN];
    msg.Serialize(body);
    // FrameCodec owns msg_len, so the protocol payload starts after it.
    payload->assign(reinterpret_cast<const char*>(body + sizeof(uint16_t)),
                    sizeof(body) - sizeof(uint16_t));
    return handshake::STEP_OK;
}

static handshake::StepResult ParsePayload(
    const std::string& payload, ParsedHello* remote) {
    const size_t base_payload_len =
        HELLO_V2_MSG_LEN_MIN - HELLO_MAGIC_LEN - sizeof(uint16_t);
    if (payload.size() < base_payload_len) {
        errno = EPROTO;
        return handshake::STEP_ERROR;
    }
    uint8_t body[HELLO_V2_MSG_LEN_MIN - HELLO_MAGIC_LEN];
    const uint16_t total_be = butil::HostToNet16(
        static_cast<uint16_t>(HELLO_MAGIC_LEN + sizeof(uint16_t) +
                              payload.size()));
    memcpy(body, &total_be, sizeof(total_be));
    memcpy(body + sizeof(total_be), payload.data(), base_payload_len);

    HelloMessage msg{};
    msg.Deserialize(body);
    if (!ValidHelloMessage(msg)) {
        return handshake::STEP_FALLBACK;
    }
    TranslateHello(msg, remote);
    return handshake::STEP_OK;
}

}  // namespace v2_wire

const handshake::FrameSpec&
RdmaClientHandshakeAdapterV2::HelloFrameSpec() const {
    return RdmaHelloFrameSpec(2);
}

handshake::StepResult RdmaClientHandshakeAdapterV2::BuildLocalHello(
    bool enabled, std::string* payload) {
    CHECK(enabled);
    ParsedHello local{};
    FillLocalHello(&local);
    v2_wire::HelloMessage msg{};
    v2_wire::FillMessage(local, &msg);
    return v2_wire::SerializePayload(msg, payload);
}

handshake::StepResult RdmaClientHandshakeAdapterV2::ParseRemoteHello(
    const std::string& payload, ParsedHello* remote) {
    return v2_wire::ParsePayload(payload, remote);
}

const handshake::FrameSpec&
RdmaServerHandshakeAdapterV2::HelloFrameSpec() const {
    return RdmaHelloFrameSpec(2);
}

handshake::StepResult RdmaServerHandshakeAdapterV2::BuildLocalHello(
    bool enabled, std::string* payload) {
    v2_wire::HelloMessage msg{};
    msg.msg_len = HELLO_V2_MSG_LEN_MIN;
    if (enabled) {
        ParsedHello local{};
        FillLocalHello(&local);
        v2_wire::FillMessage(local, &msg);
    }
    return v2_wire::SerializePayload(msg, payload);
}

handshake::StepResult RdmaServerHandshakeAdapterV2::ParseRemoteHello(
    const std::string& payload, ParsedHello* remote) {
    return v2_wire::ParsePayload(payload, remote);
}

namespace v3_wire {

static bool ValidRdmaHello(const RdmaHello& msg) {
    if (msg.gid().size() != sizeof(ibv_gid)) {
        return false;
    }
    const uint16_t max_uint16 = std::numeric_limits<uint16_t>::max();
    if (msg.sq_size() > max_uint16 || msg.rq_size() > max_uint16 ||
        msg.lid() > max_uint16) {
        return false;
    }
    if (msg.block_size() < MIN_BLOCK_SIZE || msg.sq_size() < MIN_QP_SIZE ||
        msg.rq_size() < MIN_QP_SIZE) {
        return false;
    }
    return msg.qp_num() != 0 || g_skip_rdma_init;
}

static void FillLocalRdmaHello(const ParsedHello& local, RdmaHello* msg) {
    msg->set_block_size(local.block_size);
    msg->set_sq_size(local.sq_size);
    msg->set_rq_size(local.rq_size);
    msg->set_lid(local.lid);
    msg->set_gid(reinterpret_cast<const char*>(local.gid.raw),
                 sizeof(local.gid.raw));
    msg->set_qp_num(local.qp_num);
    if (FLAGS_rdma_ece && local.ece.has_value()) {
        RdmaEce* ece = msg->mutable_ece();
        ece->set_vendor_id(local.ece->vendor_id);
        ece->set_options(local.ece->options);
        ece->set_comp_mask(local.ece->comp_mask);
    }
}

static void TranslateHello(const RdmaHello& msg, ParsedHello* out) {
    out->block_size = msg.block_size();
    out->sq_size = static_cast<uint16_t>(msg.sq_size());
    out->rq_size = static_cast<uint16_t>(msg.rq_size());
    out->lid = static_cast<uint16_t>(msg.lid());
    fast_memcpy(out->gid.raw, msg.gid().data(), sizeof(out->gid.raw));
    out->qp_num = msg.qp_num();
    if (FLAGS_rdma_ece && msg.has_ece()) {
        ibv_ece ece;
        ece.vendor_id = msg.ece().vendor_id();
        ece.options = msg.ece().options();
        ece.comp_mask = msg.ece().comp_mask();
        out->ece = ece;
    }
}

static handshake::StepResult SerializePayload(
    const RdmaHello& msg, std::string* payload) {
    if (!msg.SerializeToString(payload) ||
        payload->size() > HELLO_V3_MAX_PB_SIZE) {
        errno = EPROTO;
        return handshake::STEP_ERROR;
    }
    return handshake::STEP_OK;
}

static handshake::StepResult ParsePayload(
    const std::string& payload, ParsedHello* remote) {
    RdmaHello msg;
    if (!msg.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        errno = EPROTO;
        return handshake::STEP_ERROR;
    }
    if (!ValidRdmaHello(msg)) {
        return handshake::STEP_FALLBACK;
    }
    TranslateHello(msg, remote);
    return handshake::STEP_OK;
}

static void FillDisabledHello(RdmaHello* msg) {
    msg->set_block_size(0);
    msg->set_sq_size(0);
    msg->set_rq_size(0);
    msg->set_lid(0);
    msg->set_gid(std::string(sizeof(ibv_gid), '\0'));
    msg->set_qp_num(0);
}

}  // namespace v3_wire

const handshake::FrameSpec&
RdmaClientHandshakeAdapterV3::HelloFrameSpec() const {
    return RdmaHelloFrameSpec(3);
}

handshake::StepResult RdmaClientHandshakeAdapterV3::BuildLocalHello(
    bool enabled, std::string* payload) {
    CHECK(enabled);
    PrepareClientEce();
    ParsedHello local{};
    FillLocalHello(&local);
    RdmaHello msg;
    v3_wire::FillLocalRdmaHello(local, &msg);
    return v3_wire::SerializePayload(msg, payload);
}

handshake::StepResult RdmaClientHandshakeAdapterV3::ParseRemoteHello(
    const std::string& payload, ParsedHello* remote) {
    return v3_wire::ParsePayload(payload, remote);
}

const handshake::FrameSpec&
RdmaServerHandshakeAdapterV3::HelloFrameSpec() const {
    return RdmaHelloFrameSpec(3);
}

handshake::StepResult RdmaServerHandshakeAdapterV3::BuildLocalHello(
    bool enabled, std::string* payload) {
    RdmaHello msg;
    if (enabled) {
        ParsedHello local{};
        FillLocalHello(&local);
        v3_wire::FillLocalRdmaHello(local, &msg);
    } else {
        v3_wire::FillDisabledHello(&msg);
    }
    return v3_wire::SerializePayload(msg, payload);
}

handshake::StepResult RdmaServerHandshakeAdapterV3::ParseRemoteHello(
    const std::string& payload, ParsedHello* remote) {
    return v3_wire::ParsePayload(payload, remote);
}

std::unique_ptr<RdmaHandshakeAdapter> CreateClientHandshakeAdapter(
    RdmaEndpoint* ep) {
    if (FLAGS_rdma_client_handshake_version == 3) {
        return std::unique_ptr<RdmaHandshakeAdapter>(
            new RdmaClientHandshakeAdapterV3(ep));
    }
    return std::unique_ptr<RdmaHandshakeAdapter>(
        new RdmaClientHandshakeAdapterV2(ep));
}

std::vector<std::unique_ptr<RdmaHandshakeAdapter> >
CreateServerHandshakeAdapters(RdmaEndpoint* ep) {
    std::vector<std::unique_ptr<RdmaHandshakeAdapter> > adapters;
    adapters.emplace_back(new RdmaServerHandshakeAdapterV2(ep));
    adapters.emplace_back(new RdmaServerHandshakeAdapterV3(ep));
    return adapters;
}

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_WITH_RDMA

namespace brpc {
namespace handshake {

class RdmaServerHandshakeAdapter : public StandardHandshakeAdapter {
public:
    RdmaServerHandshakeAdapter() = default;

protected:
    StepResult RunServerStep(
        butil::IOBuf* source, Socket* socket) override;
    HandshakeSession* GetSession(Socket* socket) const override;

private:
    StepResult RunFallbackServerHandshake(
        butil::IOBuf* source, Socket* socket);
#if BRPC_WITH_RDMA
    StepResult RunRdmaServerHandshake(
        butil::IOBuf* source, Socket* socket);
#endif

    DISALLOW_COPY_AND_ASSIGN(RdmaServerHandshakeAdapter);
};

static constexpr uint16_t V2_HELLO_VERSION_INVALID =
    std::numeric_limits<uint16_t>::max();
static constexpr size_t V3_GID_LEN = 16;

class RdmaFallbackProtocol : public FallbackHandshakeProtocol {
public:
    explicit RdmaFallbackProtocol(int version) : _version(version) {}

    int ProtocolVersion() const override { return _version; }
    const FrameSpec& HelloFrameSpec() const override {
        return rdma::RdmaHelloFrameSpec(_version);
    }
    const FrameSpec& AckFrameSpec() const override {
        return rdma::RdmaAckFrameSpec();
    }
    StepResult BuildHello(bool enabled, std::string* payload) override {
        if (enabled) {
            errno = EPROTO;
            return STEP_ERROR;
        }
        if (_version == 2) {
            payload->assign(
                rdma::HELLO_V2_MSG_LEN_MIN - rdma::HELLO_MAGIC_LEN -
                    sizeof(uint16_t),
                '\0');
            butil::RawPacker(&(*payload)[0])
                .pack16(V2_HELLO_VERSION_INVALID);
            return STEP_OK;
        }

        rdma::RdmaHello reply;
        reply.set_block_size(0);
        reply.set_sq_size(0);
        reply.set_rq_size(0);
        reply.set_lid(0);
        reply.set_gid(std::string(V3_GID_LEN, '\0'));
        reply.set_qp_num(0);
        if (!reply.SerializeToString(payload)) {
            errno = EPROTO;
            return STEP_ERROR;
        }
        return STEP_OK;
    }

private:
    int _version;
};

HandshakeAdapter* GetRdmaServerHandshakeAdapter() {
    static RdmaServerHandshakeAdapter adapter;
    return &adapter;
}

HandshakeSession* RdmaServerHandshakeAdapter::GetSession(
    Socket* socket) const {
    return AdapterTransport::Get(socket)->handshake_session();
}

StepResult RdmaServerHandshakeAdapter::RunFallbackServerHandshake(
    butil::IOBuf* source, Socket* socket) {
    IOBufHandshakeInput input(source);
    RdmaFallbackProtocol v2(2);
    RdmaFallbackProtocol v3(3);
    std::vector<HandshakeProtocol*> protocols;
    protocols.push_back(&v2);
    protocols.push_back(&v3);
    FallbackHandshakeTransport transport;
    return GetSession(socket)->RunServer(
        protocols, &input, &transport, false);
}

#if BRPC_WITH_RDMA
class RdmaServerHandshakeTransport : public HandshakeTransport {
public:
    RdmaServerHandshakeTransport(
        RdmaTransport* transport, Socket* socket, butil::IOBuf* source)
        : _transport(transport), _socket(socket), _source(source),
          _protocol(NULL) {}

    void OnProtocolSelected(HandshakeProtocol* protocol) override {
        _protocol = static_cast<rdma::RdmaHandshakeAdapter*>(protocol);
    }

    StepResult PrepareResources() override {
        if (_transport->PrepareUpgradeResources() == 0) {
            return STEP_OK;
        }
        PLOG(WARNING) << "Fail to allocate rdma resources, fallback to tcp:"
                      << _socket->description();
        _transport->DeactivateUpgrade();
        return STEP_FALLBACK;
    }

    StepResult NegotiateResources() override {
        CHECK(_protocol != NULL);
        if (_transport->NegotiateUpgradeResources(
                _protocol->remote(), true) == 0) {
            return STEP_OK;
        }
        PLOG(WARNING) << "Fail to negotiate rdma resources, fallback to tcp:"
                      << _socket->description();
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
    RdmaTransport* _transport;
    Socket* _socket;
    butil::IOBuf* _source;
    rdma::RdmaHandshakeAdapter* _protocol;
};

StepResult RdmaServerHandshakeAdapter::RunRdmaServerHandshake(
    butil::IOBuf* source, Socket* socket) {
    RdmaTransport* transport = RdmaTransport::Get(socket);
    CHECK(transport->GetRdmaEp() != NULL);

    std::vector<std::unique_ptr<rdma::RdmaHandshakeAdapter> > protocols =
        transport->CreateServerHandshakeAdapters();
    std::vector<HandshakeProtocol*> protocol_ptrs;
    protocol_ptrs.reserve(protocols.size());
    for (size_t i = 0; i < protocols.size(); ++i) {
        protocol_ptrs.push_back(protocols[i].get());
    }
    IOBufHandshakeInput input(source);
    RdmaServerHandshakeTransport participant(transport, socket, source);
    return GetSession(socket)->RunServer(
        protocol_ptrs, &input, &participant, false);
}
#endif

StepResult RdmaServerHandshakeAdapter::RunServerStep(
    butil::IOBuf* source, Socket* socket) {
#if BRPC_WITH_RDMA
    if (AdapterTransport::Get(socket)->upgrade_capable(
            SOCKET_MODE_RDMA)) {
        return RunRdmaServerHandshake(source, socket);
    }
#endif
    return RunFallbackServerHandshake(source, socket);
}

}  // namespace handshake
}  // namespace brpc
