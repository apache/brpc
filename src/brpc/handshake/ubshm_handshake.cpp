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

#include "brpc/handshake/ubshm_handshake.h"

#include <errno.h>
#include <cstdio>

#include "butil/raw_pack.h"
#include "butil/sys_byteorder.h"
#include "brpc/adapter_transport.h"
#include "brpc/socket.h"

#if BRPC_WITH_UBRING

#include <array>
#include <cstring>

#include "butil/logging.h"
#include "brpc/reloadable_flags.h"
#include "brpc/ubshm/common/common.h"
#include "brpc/ubshm/ub_endpoint.h"
#include "brpc/ubshm/ub_helper.h"
#include "brpc/ubshm/ubr_trx.h"
#include "brpc/ubshm_transport.h"

#endif

namespace brpc {
namespace handshake {
namespace ubshm_wire {

static const char* const MAGIC = "UB";
static const size_t MAGIC_LEN = 2;
static const size_t HELLO_LEN = 64;
static const size_t ACK_LEN = 4;
#if BRPC_WITH_UBRING
static const uint16_t HELLO_VERSION = 2;
static const uint16_t IMPL_VERSION = 1;
#endif  // BRPC_WITH_UBRING
static const uint32_t ACK_OK = 0x1;

static const FrameSpec& HelloFrameSpec() {
    static const FrameSpec spec(
        MAGIC, MAGIC_LEN, HELLO_LEN, HELLO_LEN, FrameSpec::FIXED);
    return spec;
}

static const FrameSpec& AckFrameSpec() {
    static const FrameSpec spec(
        NULL, 0, ACK_LEN, ACK_LEN, FrameSpec::FIXED);
    return spec;
}

}  // namespace ubshm_wire
}  // namespace handshake
}  // namespace brpc

#if BRPC_WITH_UBRING

namespace brpc {
namespace ubring {

DEFINE_int32(data_queue_size, 4, "data queue size for UB");
DEFINE_bool(ub_trace_verbose, false, "Print log message verbosely");
BRPC_VALIDATE_GFLAG(ub_trace_verbose, brpc::PassValidate);

void HelloMessage::Serialize(void* data) const {
    char* current_pos = static_cast<char*>(data);
    const uint16_t net_msg_len = butil::HostToNet16(msg_len);
    memcpy(current_pos, &net_msg_len, sizeof(net_msg_len));
    current_pos += sizeof(net_msg_len);
    const uint16_t net_hello_ver = butil::HostToNet16(hello_ver);
    memcpy(current_pos, &net_hello_ver, sizeof(net_hello_ver));
    current_pos += sizeof(net_hello_ver);
    const uint16_t net_impl_ver = butil::HostToNet16(impl_ver);
    memcpy(current_pos, &net_impl_ver, sizeof(net_impl_ver));
    current_pos += sizeof(net_impl_ver);
    const uint64_t net_len = butil::HostToNet64(len);
    memcpy(current_pos, &net_len, sizeof(net_len));
    current_pos += sizeof(net_len);
    memcpy(current_pos, shm_name, SHM_MAX_NAME_BUFF_LEN);
}

void HelloMessage::Deserialize(const void* data) {
    const char* current_pos = static_cast<const char*>(data);
    uint16_t net_msg_len;
    memcpy(&net_msg_len, current_pos, sizeof(net_msg_len));
    msg_len = butil::NetToHost16(net_msg_len);
    current_pos += sizeof(net_msg_len);
    uint16_t net_hello_ver;
    memcpy(&net_hello_ver, current_pos, sizeof(net_hello_ver));
    hello_ver = butil::NetToHost16(net_hello_ver);
    current_pos += sizeof(net_hello_ver);
    uint16_t net_impl_ver;
    memcpy(&net_impl_ver, current_pos, sizeof(net_impl_ver));
    impl_ver = butil::NetToHost16(net_impl_ver);
    current_pos += sizeof(net_impl_ver);
    uint64_t net_len;
    memcpy(&net_len, current_pos, sizeof(net_len));
    len = butil::NetToHost64(net_len);
    current_pos += sizeof(net_len);
    memcpy(shm_name, current_pos, SHM_MAX_NAME_BUFF_LEN);
}

std::string HelloMessage::toString() const {
    constexpr size_t MAX_LEN =
        16 + 6 + 16 + 6 + 16 + 6 + 20 + 6 + SHM_MAX_NAME_BUFF_LEN + 32;
    std::array<char, MAX_LEN> buf;
    const int n = snprintf(
        buf.data(), buf.size(),
        "msg_len=%u, hello_ver=%u, impl_ver=%u, len=%lu, shm_name=%.*s",
        msg_len, hello_ver, impl_ver,
        static_cast<unsigned long>(len),
        static_cast<int>(SHM_MAX_NAME_BUFF_LEN), shm_name);
    return std::string(buf.data(), static_cast<size_t>(n));
}

handshake::HandshakeCodec UBShmHandshakeAdapter::MakeCodec() const {
    handshake::HandshakeCodec codec{};
    codec.protocol_version = 2;
    codec.hello_frame = handshake::ubshm_wire::HelloFrameSpec();
    codec.ack_frame = handshake::ubshm_wire::AckFrameSpec();
    codec.build_ack = [](bool enabled, std::string* payload) {
        const uint32_t flags_be = butil::HostToNet32(
            enabled ? handshake::ubshm_wire::ACK_OK : 0);
        payload->assign(reinterpret_cast<const char*>(&flags_be),
                        sizeof(flags_be));
        return handshake::STEP_OK;
    };
    codec.parse_ack = [](const std::string& payload, bool* enabled) {
        if (payload.size() != handshake::ubshm_wire::ACK_LEN) {
            errno = EPROTO;
            return handshake::STEP_ERROR;
        }
        uint32_t flags_be = 0;
        memcpy(&flags_be, payload.data(), sizeof(flags_be));
        *enabled = (butil::NetToHost32(flags_be) &
                    handshake::ubshm_wire::ACK_OK) != 0;
        return handshake::STEP_OK;
    };
    return codec;
}

handshake::StepResult UBShmHandshakeAdapter::BuildHello(
    bool enabled, uint64_t len, const char* shm_name,
    std::string* payload) const {
    HelloMessage message{};
    message.msg_len = static_cast<uint16_t>(
        handshake::ubshm_wire::HELLO_LEN);
    if (enabled) {
        message.hello_ver = handshake::ubshm_wire::HELLO_VERSION;
        message.impl_ver = handshake::ubshm_wire::IMPL_VERSION;
        message.len = len;
        memcpy(message.shm_name, shm_name, SHM_MAX_NAME_BUFF_LEN);
    }
    payload->assign(
        handshake::ubshm_wire::HELLO_LEN -
            handshake::ubshm_wire::MAGIC_LEN,
        '\0');
    message.Serialize(&(*payload)[0]);
    return handshake::STEP_OK;
}

handshake::StepResult UBShmHandshakeAdapter::ParseHello(
    const std::string& payload, HelloMessage* message) const {
    if (payload.size() != handshake::ubshm_wire::HELLO_LEN -
                              handshake::ubshm_wire::MAGIC_LEN) {
        errno = EPROTO;
        return handshake::STEP_ERROR;
    }
    message->Deserialize(payload.data());
    if (message->msg_len < handshake::ubshm_wire::HELLO_LEN) {
        errno = EPROTO;
        return handshake::STEP_ERROR;
    }
    return NegotiationValid(*message) ?
        handshake::STEP_OK : handshake::STEP_FALLBACK;
}

bool UBShmHandshakeAdapter::NegotiationValid(
    const HelloMessage& message) const {
    return message.hello_ver == handshake::ubshm_wire::HELLO_VERSION &&
           message.impl_ver == handshake::ubshm_wire::IMPL_VERSION;
}

}  // namespace ubring
}  // namespace brpc

#endif  // BRPC_WITH_UBRING

namespace brpc {
namespace handshake {

class UBShmServerHandshakeAdapter : public StandardHandshakeAdapter {
public:
    UBShmServerHandshakeAdapter() = default;

protected:
    StepResult RunServerStep(
        butil::IOBuf* source, Socket* socket) override;
    HandshakeSession* GetSession(Socket* socket) const override;

private:
    StepResult RunFallbackServerHandshake(
        butil::IOBuf* source, Socket* socket);
#if BRPC_WITH_UBRING
    StepResult RunUBShmServerHandshake(
        butil::IOBuf* source, Socket* socket);
#endif

    DISALLOW_COPY_AND_ASSIGN(UBShmServerHandshakeAdapter);
};


static HandshakeCodec MakeUBShmFallbackCodec() {
    HandshakeCodec codec{};
    codec.protocol_version = 2;
    codec.hello_frame = ubshm_wire::HelloFrameSpec();
    codec.ack_frame = ubshm_wire::AckFrameSpec();
    codec.parse_hello = [](const std::string&) {
        return STEP_FALLBACK;
    };
    codec.build_hello = [](bool enabled, std::string* payload) {
        if (enabled) {
            errno = EPROTO;
            return STEP_ERROR;
        }
        payload->assign(
            ubshm_wire::HELLO_LEN - ubshm_wire::MAGIC_LEN, '\0');
        butil::RawPacker(&(*payload)[0])
            .pack16(static_cast<uint16_t>(ubshm_wire::HELLO_LEN));
        return STEP_OK;
    };
    codec.build_ack = [](bool enabled, std::string* payload) {
        const uint32_t flags_be = butil::HostToNet32(
            enabled ? ubshm_wire::ACK_OK : 0);
        payload->assign(reinterpret_cast<const char*>(&flags_be),
                        sizeof(flags_be));
        return STEP_OK;
    };
    codec.parse_ack = [](const std::string& payload, bool* enabled) {
        if (payload.size() != ubshm_wire::ACK_LEN) {
            errno = EPROTO;
            return STEP_ERROR;
        }
        *enabled = false;
        return STEP_OK;
    };
    return codec;
}

HandshakeAdapter* GetUBShmServerHandshakeAdapter() {
    static UBShmServerHandshakeAdapter adapter;
    return &adapter;
}

HandshakeSession* UBShmServerHandshakeAdapter::GetSession(
    Socket* socket) const {
    return AdapterTransport::Get(socket)->handshake_session();
}

StepResult UBShmServerHandshakeAdapter::RunFallbackServerHandshake(
    butil::IOBuf* source, Socket* socket) {
    IOBufHandshakeInput input(source);
    ServerHandshakeCallbacks callbacks{};
    callbacks.fallback_on_not_mine = false;
    callbacks.codecs.push_back(MakeUBShmFallbackCodec());
    callbacks.input = &input;
    callbacks.transport.prepare_resources = []() { return STEP_OK; };
    callbacks.transport.negotiate_resources = []() { return STEP_OK; };
    callbacks.transport.set_high_speed_active = []() {};
    callbacks.transport.set_tcp_active = []() {};
    callbacks.transport.on_failed = []() {};
    return GetSession(socket)->RunServer(callbacks);
}

#if BRPC_WITH_UBRING
StepResult UBShmServerHandshakeAdapter::RunUBShmServerHandshake(
    butil::IOBuf* source, Socket* socket) {
    UBShmTransport* transport = UBShmTransport::Get(socket);
    CHECK(transport->GetUBShmEp() != NULL);

    ubring::HelloMessage remote{};
    ubring::UBShmHandshakeAdapter wire;
    IOBufHandshakeInput input(source);
    ServerHandshakeCallbacks callbacks{};
    callbacks.fallback_on_not_mine = false;
    callbacks.input = &input;
    HandshakeCodec codec = wire.MakeCodec();
    codec.parse_hello = [&](const std::string& payload) {
        const StepResult result = wire.ParseHello(payload, &remote);
        if (result == STEP_OK || result == STEP_FALLBACK) {
            LOG_IF(INFO, ubring::FLAGS_ub_trace_verbose)
                << "server receive handshake message : "
                << remote.toString();
        }
        if (result == STEP_FALLBACK) {
            transport->DeactivateUpgrade();
        }
        return result;
    };
    codec.build_hello = [&](bool enabled, std::string* payload) {
        const uint64_t len = enabled
            ? static_cast<uint64_t>(ubring::FLAGS_data_queue_size) *
                  MB_TO_BYTE
            : 0;
        return wire.BuildHello(
            enabled, len, enabled ? remote.shm_name : NULL, payload);
    };
    callbacks.codecs.push_back(codec);
    callbacks.transport.prepare_resources = [&]() {
        if (!ubring::IsUBAvailable()) {
            transport->DeactivateUpgrade();
            return STEP_FALLBACK;
        }
        ubring::SHM remote_trx_shm = {
            NULL, remote.len, 0, {0},
            static_cast<uint32_t>(socket->fd())};
        strncpy(remote_trx_shm.name, remote.shm_name,
                SHM_MAX_NAME_BUFF_LEN);

        const size_t local_shm_len =
            static_cast<size_t>(ubring::FLAGS_data_queue_size) * MB_TO_BYTE;
        ubring::SHM local_trx_shm = {
            NULL, local_shm_len, 0, {0},
            static_cast<uint32_t>(socket->fd())};
        char client_name[SHM_MAX_NAME_BUFF_LEN + 1];
        memcpy(client_name, remote.shm_name, SHM_MAX_NAME_BUFF_LEN);
        client_name[SHM_MAX_NAME_BUFF_LEN] = '\0';
        char* client_ip_port = strrchr(client_name, '_');
        if (client_ip_port != NULL) {
            *client_ip_port = '\0';
        }
        const int result = snprintf(
            local_trx_shm.name, SHM_MAX_NAME_BUFF_LEN, "%s_%s",
            client_name, SERVER_SHM_NAME_SUFFIX);
        if (UNLIKELY(result < 0)) {
            transport->DeactivateUpgrade();
            return STEP_FALLBACK;
        }
        if (transport->PrepareServerUpgradeResources(
                &remote_trx_shm, &local_trx_shm) < 0) {
            LOG(WARNING)
                << "Fail to allocate ub resources, fallback to tcp:"
                << socket->description();
            transport->DeactivateUpgrade();
            return STEP_FALLBACK;
        }
        return STEP_OK;
    };
    callbacks.transport.negotiate_resources = []() { return STEP_OK; };
    callbacks.validate_established = [&]() {
        if (!source->empty() ||
            !transport->UpgradeActive()) {
            return STEP_ERROR;
        }
        return STEP_OK;
    };
    callbacks.transport.set_high_speed_active = [transport]() {
        transport->ActivateUpgrade();
    };
    callbacks.transport.set_tcp_active = [transport]() {
        transport->DeactivateUpgrade();
    };
    callbacks.transport.on_failed = []() {};
    const StepResult result = GetSession(socket)->RunServer(callbacks);
    if (result == STEP_OK) {
        transport->FinishUpgrade();
        LOG_IF(INFO, ubring::FLAGS_ub_trace_verbose)
            << "Server handshake ends (use ubring) on "
            << socket->description();
    } else if (result == STEP_FALLBACK) {
        LOG_IF(INFO, ubring::FLAGS_ub_trace_verbose)
            << "Server handshake ends (use tcp) on "
            << socket->description();
    }
    return result;
}
#endif

StepResult UBShmServerHandshakeAdapter::RunServerStep(
    butil::IOBuf* source, Socket* socket) {
#if BRPC_WITH_UBRING
    if (AdapterTransport::Get(socket)->upgrade_capable()) {
        return RunUBShmServerHandshake(source, socket);
    }
#endif
    return RunFallbackServerHandshake(source, socket);
}

}  // namespace handshake
}  // namespace brpc
