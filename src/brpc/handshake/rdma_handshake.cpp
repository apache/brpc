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
#include <new>
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
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/srq_worker.h"

namespace brpc {
namespace rdma {

DEFINE_int32(rdma_client_handshake_version, 3,
             "Deprecated and ignored. Retained for command-line compatibility. "
             "Clients always start with v3 and retry v2 once if the peer rejects v3.");
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

handshake::StepResult RdmaClientHandshakeAdapterV2::BuildHello(
    bool enabled, std::string* payload) {
    CHECK(enabled);
    ParsedHello local{};
    FillLocalHello(&local);
    v2_wire::HelloMessage msg{};
    v2_wire::FillMessage(local, &msg);
    return v2_wire::SerializePayload(msg, payload);
}

handshake::StepResult RdmaServerHandshakeAdapterV2::BuildHello(
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

namespace v3_wire {

bool ValidRdmaHello(const RdmaHello& msg) {
    if (msg.gid().size() != sizeof(ibv_gid)) {
        return false;
    }
    const uint16_t max_uint16 = std::numeric_limits<uint16_t>::max();
    if (msg.sq_size() > max_uint16 || msg.rq_size() > max_uint16 ||
        msg.lid() > max_uint16) {
        return false;
    }
    if (msg.block_size() < MIN_BLOCK_SIZE || msg.sq_size() < MIN_QP_SIZE) {
        return false;
    }
    if (msg.srq_protocol() == 1) {
        if (msg.rq_size() != 0 || msg.fc_window() < 4 ||
            msg.fc_window() > max_uint16) {
            return false;
        }
    } else if (msg.srq_protocol() != 0 || msg.has_fc_window() ||
               msg.rq_size() < MIN_QP_SIZE) {
        return false;
    }
    if (msg.has_mtu() &&
        (msg.mtu() < IBV_MTU_256 || msg.mtu() > IBV_MTU_4096)) {
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
    if (local.srq_protocol != 0) {
        msg->set_srq_protocol(local.srq_protocol);
        msg->set_fc_window(local.fc_window);
    }
    if (local.path_mtu.has_value()) {
        msg->set_mtu(*local.path_mtu);
    }
    if (FLAGS_rdma_ece && local.ece.has_value()) {
        RdmaEce* ece = msg->mutable_ece();
        ece->set_vendor_id(local.ece->vendor_id);
        ece->set_options(local.ece->options);
        ece->set_comp_mask(local.ece->comp_mask);
    }
}

void TranslateHello(const RdmaHello& msg, ParsedHello* out) {
    out->srq_protocol = msg.srq_protocol();
    out->fc_window = msg.fc_window();
    out->block_size = msg.block_size();
    out->sq_size = static_cast<uint16_t>(msg.sq_size());
    out->rq_size = static_cast<uint16_t>(msg.rq_size());
    out->lid = static_cast<uint16_t>(msg.lid());
    fast_memcpy(out->gid.raw, msg.gid().data(), sizeof(out->gid.raw));
    out->qp_num = msg.qp_num();
    if (msg.has_mtu()) {
        out->path_mtu = msg.mtu();
    }
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

handshake::StepResult RdmaClientHandshakeAdapterV3::BuildHello(
    bool enabled, std::string* payload) {
    CHECK(enabled);
    PrepareClientEce();
    ParsedHello local{};
    FillLocalHello(&local);
    local.path_mtu = GetRdmaActiveMtu();
    RdmaHello msg;
    v3_wire::FillLocalRdmaHello(local, &msg);
    return v3_wire::SerializePayload(msg, payload);
}

handshake::StepResult RdmaServerHandshakeAdapterV3::BuildHello(
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

handshake::StepResult RdmaHandshakeAdapter::ParseHello(
    const std::string& payload) {
    return _version == 2 ? v2_wire::ParsePayload(payload, &_remote)
                         : v3_wire::ParsePayload(payload, &_remote);
}

std::unique_ptr<RdmaHandshakeAdapter> CreateClientHandshakeAdapter(
    RdmaEndpoint* ep) {
    return std::make_unique<RdmaClientHandshakeAdapterV3>(ep);
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

class RdmaServerHandshakeAdapter : public HandshakeAdapter {
public:
    RdmaServerHandshakeAdapter() = default;

protected:
    StepResult RunServerStep(
        butil::IOBuf* source, Socket* socket) override;
    HandshakeSession* GetSession(Socket* socket) const override;

private:
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

#if BRPC_WITH_RDMA
namespace {

class RdmaClientHandshakeTransport : public HandshakeTransport {
public:
    RdmaClientHandshakeTransport(
        RdmaTransport* transport, Socket* socket)
        : _transport(transport), _socket(socket), _protocol(nullptr) {}

    void OnProtocolSelected(HandshakeProtocol* protocol) override {
        _protocol = static_cast<rdma::RdmaHandshakeAdapter*>(protocol);
    }

    HandshakeProtocol* RetryClientHello(
        HandshakeProtocol* protocol, bool hello_sent,
        FrameResult frame, const std::string& magic) override {
        if (protocol->ProtocolVersion() != 3) {
            return nullptr;
        }
        const bool retry = hello_sent
            ? ((frame == FRAME_IO_ERROR && magic.empty() &&
                (errno == EEOF || errno == ECONNRESET)) ||
               (frame == FRAME_NOT_MINE &&
                magic == rdma::HELLO_MAGIC))
            : (errno == EPIPE || errno == ECONNRESET);
        if (!retry) {
            return nullptr;
        }
        LOG_IF(INFO, rdma::FLAGS_rdma_trace_verbose)
            << "Retry RDMA handshake with v2 on " << _socket->description();
        auto* ep = _transport->GetRdmaEp();
        if (ep->ReconnectForHandshake() != 0) {
            return nullptr;
        }
        _retry_protocol = std::make_unique<rdma::RdmaClientHandshakeAdapterV2>(ep);
        return _retry_protocol.get();
    }

    StepResult PrepareResources() override {
        const bool use_srq = _protocol->ProtocolVersion() == 3 &&
                             rdma::FLAGS_rdma_use_srq;
        const auto mode = use_srq ? rdma::RdmaQueueMode::SHARED_RQ
                                  : rdma::RdmaQueueMode::PRIVATE_RQ;
        if (_transport->PrepareUpgradeResources(mode) == 0) {
            return STEP_OK;
        }
        errno = 0;
        return STEP_FALLBACK;
    }

    StepResult NegotiateResources() override {
        return _transport->NegotiateUpgradeResources(
                   _protocol->remote(), false) == 0
            ? STEP_OK : STEP_FALLBACK;
    }

    void OnEstablished() override { _transport->ActivateUpgrade(); }

    void OnFallback() override { _transport->DeactivateUpgrade(); }

    // AfterAppConnected applies the socket's connection timeout policy.
    void OnFailed() override { _transport->DeactivateUpgrade(); }

private:
    RdmaTransport* _transport;
    Socket* _socket;
    rdma::RdmaHandshakeAdapter* _protocol;
    std::unique_ptr<rdma::RdmaClientHandshakeAdapterV2> _retry_protocol;
};

int RunRdmaClientHandshake(Transport* high_speed, Socket* socket,
                           HandshakeSession& session) {
    auto* transport = static_cast<RdmaTransport*>(high_speed);
    auto protocol = transport->CreateClientHandshakeAdapter();
    CHECK(protocol != nullptr);
    RdmaClientHandshakeTransport participant(transport, socket);
    const auto result = session.RunClient(session.socket_io(), protocol.get(),
                                          &participant);
    return result == STEP_ERROR ? errno : 0;
}

int StartRdmaReceive(Transport* high_speed) {
    auto* transport = static_cast<RdmaTransport*>(high_speed);
    if (transport->StartUpgradeEvents() >= 0) {
        return 0;
    }
    const int error = errno != 0 ? errno : ERDMA;
    transport->DeactivateUpgrade();
    return error;
}

}  // namespace
#endif

const HandshakeRegistration& GetRdmaHandshakeRegistration() {
    static RdmaServerHandshakeAdapter server;
#if BRPC_WITH_RDMA
    static const auto operations =
        HandshakeRegistration::ForTransport<RdmaTransport>(
            rdma::IsRdmaAvailable, RunRdmaClientHandshake, StartRdmaReceive);
    const auto* const upgrade = &operations;
#else
    const HandshakeRegistration::Upgrade* upgrade = nullptr;
#endif
    static const HandshakeRegistration registration = {
        SOCKET_MODE_RDMA, &server,
        {&rdma::RdmaHelloFrameSpec(2), &rdma::RdmaHelloFrameSpec(3)}, upgrade,
        nullptr,
    };
    return registration;
}

HandshakeSession* RdmaServerHandshakeAdapter::GetSession(
    Socket* socket) const {
    return AdapterTransport::Get(socket)->handshake_session();
}

static StepResult RunRdmaFallbackServerHandshake(
    butil::IOBuf* source, HandshakeSession* session) {
    RdmaFallbackProtocol v2(2);
    RdmaFallbackProtocol v3(3);
    std::vector<HandshakeProtocol*> protocols;
    protocols.push_back(&v2);
    protocols.push_back(&v3);
    FallbackHandshakeTransport transport;
    return session->RunServer(
        session->socket_io(), protocols, source, &transport, false);
}

#if BRPC_WITH_RDMA
class RdmaServerHandshakeTransport : public HandshakeTransport {
public:
    RdmaServerHandshakeTransport(
        RdmaTransport* transport, Socket* socket, butil::IOBuf* source)
        : _transport(transport), _socket(socket), _source(source),
          _protocol(nullptr) {}

    void OnProtocolSelected(HandshakeProtocol* protocol) override {
        _protocol = static_cast<rdma::RdmaHandshakeAdapter*>(protocol);
    }

    StepResult PrepareResources() override {
        const bool use_srq = _protocol->remote().srq_protocol == 1;
        if (use_srq && !rdma::FLAGS_rdma_use_srq) {
            _transport->DeactivateUpgrade();
            return STEP_FALLBACK;
        }
        const auto mode = use_srq ? rdma::RdmaQueueMode::SHARED_RQ
                                  : rdma::RdmaQueueMode::PRIVATE_RQ;
        if (_transport->PrepareUpgradeResources(mode) == 0) {
            return STEP_OK;
        }
        PLOG(WARNING) << "Fail to allocate rdma resources, fallback to tcp:"
                      << _socket->description();
        _transport->DeactivateUpgrade();
        return STEP_FALLBACK;
    }

    StepResult NegotiateResources() override {
        CHECK(_protocol != nullptr);
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
    CHECK(transport->GetRdmaEp() != nullptr);

    std::vector<std::unique_ptr<rdma::RdmaHandshakeAdapter> > protocols =
        transport->CreateServerHandshakeAdapters();
    std::vector<HandshakeProtocol*> protocol_ptrs;
    protocol_ptrs.reserve(protocols.size());
    for (size_t i = 0; i < protocols.size(); ++i) {
        protocol_ptrs.push_back(protocols[i].get());
    }
    RdmaServerHandshakeTransport participant(transport, socket, source);
    auto* session = GetSession(socket);
    return session->RunServer(
        session->socket_io(), protocol_ptrs, source, &participant, false);
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
    return RunRdmaFallbackServerHandshake(source, GetSession(socket));
}

// Transports outside AdapterTransport only retain state while waiting for ACK.
struct RdmaFallbackContext : Destroyable {
    explicit RdmaFallbackContext(Socket* socket) : session(socket) {}
    void Destroy() override { delete this; }
    HandshakeSession session;
};

ParseResult ParseRdmaFallbackHandshake(butil::IOBuf* source, Socket* socket) {
    auto* context = static_cast<RdmaFallbackContext*>(socket->parsing_context());
    std::unique_ptr<RdmaFallbackContext> local;
    if (context == nullptr) {
        if (source->empty() || HandshakeRegistration::MatchHello(*source) !=
                                   GetRdmaHandshakeRegistration().server) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
        local.reset(new (std::nothrow) RdmaFallbackContext(socket));
        if (!local) {
            return MakeParseError(PARSE_ERROR_NO_RESOURCE);
        }
        context = local.get();
    }
    const StepResult result =
        RunRdmaFallbackServerHandshake(source, &context->session);
    if (result == STEP_NEED_MORE) {
        if (local && context->session.phase(butil::memory_order_acquire) ==
                         ACK_WAIT) {
            socket->reset_parsing_context(local.release());
        }
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    socket->reset_parsing_context(nullptr);
    return MakeParseError(result == STEP_ERROR ? PARSE_ERROR_ABSOLUTELY_WRONG
                                               : PARSE_ERROR_TRY_OTHERS);
}

}  // namespace handshake
}  // namespace brpc
