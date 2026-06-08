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

#if BRPC_WITH_RDMA

#include "brpc/rdma/rdma_handshake.h"

#include <string.h>
#include <algorithm>            // std::min
#include <string>
#include <limits>
#include <gflags/gflags.h>
#include "butil/iobuf.h"        // IOBuf, IOPortal, IOBufAsZeroCopy*Stream
#include "butil/sys_byteorder.h"
#include "brpc/socket.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma_transport.h"
#include "brpc/rdma/rdma_handshake.pb.h"

namespace brpc {
namespace rdma {

DEFINE_int32(rdma_client_handshake_version, 2,
             "RDMA handshake protocol version used by client. "
             "2 = legacy 'RDMA' magic (default, compatible with all servers); "
             "3 = new 'RDM3' protobuf-based handshake "
             "(MUST only be enabled after target servers support v3).");

extern const uint16_t MIN_QP_SIZE;
extern const uint16_t MIN_BLOCK_SIZE;
extern uint32_t g_rdma_recv_block_size;
extern bool g_skip_rdma_init;

// Wire-level constants for the v2 handshake.
static const char* MAGIC_STR = "RDMA";
static constexpr uint16_t RDMA_HELLO_V2_MSG_LEN = 40;  // In Byte
extern const uint16_t RDMA_HELLO_V2_VERSION = 2;
extern const uint16_t RDMA_IMPL_V2_VERSION = 1;

// Wire-level constants for the v3 handshake.
static const char* MAGIC_STR_V3 = "RDM3";
static const size_t RDMA_HELLO_V3_PB_SIZE_LEN = 4;
static const size_t RDMA_HELLO_V3_MAX_PB_SIZE = 4096;

namespace v2_wire {

void HelloMessage::Serialize(void* data) const {
    uint16_t* current_pos = (uint16_t*)data;
    *(current_pos++) = butil::HostToNet16(msg_len);
    *(current_pos++) = butil::HostToNet16(hello_ver);
    *(current_pos++) = butil::HostToNet16(impl_ver);
    uint32_t* block_size_pos = (uint32_t*)current_pos;
    *block_size_pos = butil::HostToNet32(block_size);
    current_pos += 2; // move forward 4 Bytes
    *(current_pos++) = butil::HostToNet16(sq_size);
    *(current_pos++) = butil::HostToNet16(rq_size);
    *(current_pos++) = butil::HostToNet16(lid);
    fast_memcpy(current_pos, gid.raw, 16);
    uint32_t* qp_num_pos = (uint32_t*)((char*)current_pos + 16);
    *qp_num_pos = butil::HostToNet32(qp_num);
}

void HelloMessage::Deserialize(void* data) {
    uint16_t* current_pos = (uint16_t*)data;
    msg_len = butil::NetToHost16(*current_pos++);
    hello_ver = butil::NetToHost16(*current_pos++);
    impl_ver = butil::NetToHost16(*current_pos++);
    block_size = butil::NetToHost32(*(uint32_t*)current_pos);
    current_pos += 2; // move forward 4 Bytes
    sq_size = butil::NetToHost16(*current_pos++);
    rq_size = butil::NetToHost16(*current_pos++);
    lid = butil::NetToHost16(*current_pos++);
    fast_memcpy(gid.raw, current_pos, 16);
    qp_num = butil::NetToHost32(*(uint32_t*)((char*)current_pos + 16));
}

static bool ValidHelloMessage(const HelloMessage& msg) {
    return msg.hello_ver == RDMA_HELLO_V2_VERSION &&
           msg.impl_ver == RDMA_IMPL_V2_VERSION &&
           msg.block_size >= MIN_BLOCK_SIZE &&
           msg.sq_size >= MIN_QP_SIZE &&
           msg.rq_size >= MIN_QP_SIZE;
}

static void TranslateV2Hello(const HelloMessage& msg, ParsedHello* out) {
    out->block_size = msg.block_size;
    out->sq_size = msg.sq_size;
    out->rq_size = msg.rq_size;
    out->lid = msg.lid;
    out->gid = msg.gid;
    out->qp_num = msg.qp_num;
}

int ReadBodyAndNegotiate(RdmaEndpoint* ep, ParsedHello* remote, bool* negotiated) {
    uint8_t data[HELLO_MSG_LEN_MIN];
    if (ep->ReadFromFd(data, HELLO_MSG_LEN_MIN - MAGIC_STR_LEN) < 0) {
        return -1;
    }
    HelloMessage remote_msg{};
    remote_msg.Deserialize(data);
    if (remote_msg.msg_len < HELLO_MSG_LEN_MIN ||
        remote_msg.msg_len > HELLO_MSG_LEN_MAX) {
        errno = EPROTO;
        return -1;
    }
    if (remote_msg.msg_len > HELLO_MSG_LEN_MIN) {
        // Drain unknown trailing bytes so they don't pollute subsequent
        // reads (e.g. the upcoming ACK message). v2 base fields already
        // carry enough information for negotiation; unknown trailing
        // bytes are treated as optional hints that v2 safely ignores.
        size_t ext_len = remote_msg.msg_len - HELLO_MSG_LEN_MIN;
        if (DrainBytes(ep, ext_len) < 0) {
            return -1;
        }
    }
    if (!ValidHelloMessage(remote_msg)) {
        *negotiated = false;
        return 0;
    }
    *negotiated = true;
    TranslateV2Hello(remote_msg, remote);
    return 0;
}

int DrainBytes(RdmaEndpoint* ep, size_t n) {
    uint8_t scratch[64];
    while (n > 0) {
        size_t chunk = std::min(n, sizeof(scratch));
        if (ep->ReadFromFd(scratch, chunk) < 0) {
            return -1;
        }
        n -= chunk;
    }
    return 0;
}

}  // namespace v2_wire

int RdmaHandshakeClientV2::SendLocalHello() {
    RdmaEndpoint* ep = _ep;
    uint8_t data[RDMA_HELLO_V2_MSG_LEN];

    v2_wire::HelloMessage local_msg{};
    local_msg.msg_len = RDMA_HELLO_V2_MSG_LEN;
    local_msg.hello_ver = RDMA_HELLO_V2_VERSION;
    local_msg.impl_ver = RDMA_IMPL_V2_VERSION;
    local_msg.block_size = g_rdma_recv_block_size;
    local_msg.sq_size = ep->_sq_size;
    local_msg.rq_size = ep->_rq_size;
    local_msg.lid = GetRdmaLid();
    local_msg.gid = GetRdmaGid();
    if (BAIDU_LIKELY(ep->_resource)) {
        local_msg.qp_num = ep->_resource->qp->qp_num;
    } else {
        // Only happens in UT
        local_msg.qp_num = 0;
    }
    fast_memcpy(data, MAGIC_STR, 4);
    local_msg.Serialize((char*)data + 4);
    return ep->WriteToFd(data, RDMA_HELLO_V2_MSG_LEN);
}

int RdmaHandshakeClientV2::ReceiveAndParseRemoteHello(ParsedHello* remote,
                                                     bool* negotiated) {
    RdmaEndpoint* ep = _ep;

    // Read and verify magic (the endpoint did NOT pre-read magic on the client side).
    uint8_t magic[MAGIC_STR_LEN];
    if (ep->ReadFromFd(magic, MAGIC_STR_LEN) < 0) {
        return -1;
    }
    if (memcmp(magic, MAGIC_STR, MAGIC_STR_LEN) != 0) {
        errno = EPROTO;
        return -1;
    }
    return v2_wire::ReadBodyAndNegotiate(ep, remote, negotiated);
}

int RdmaHandshakeServerV2::ReceiveAndParseRemoteHello(ParsedHello* remote, bool* negotiated) {
    // Magic already consumed by ProcessHandshakeAtServer.
    return v2_wire::ReadBodyAndNegotiate(_ep, remote, negotiated);
}

int RdmaHandshakeServerV2::SendLocalHello() {
    uint8_t data[RDMA_HELLO_V2_MSG_LEN];
    v2_wire::HelloMessage local_msg{};
    local_msg.msg_len = RDMA_HELLO_V2_MSG_LEN;
    auto rdma_transport = static_cast<RdmaTransport*>(_ep->_socket->_transport.get());
    if (rdma_transport->_rdma_state == RdmaTransport::RDMA_OFF) {
        local_msg.hello_ver = 0;
        local_msg.impl_ver = 0;
        local_msg.block_size = 0;
        local_msg.sq_size = 0;
        local_msg.rq_size = 0;
        local_msg.lid = 0;
        memset(local_msg.gid.raw, 0, sizeof(local_msg.gid.raw));
        local_msg.qp_num     = 0;
    } else {
        local_msg.hello_ver = RDMA_HELLO_V2_VERSION;
        local_msg.impl_ver = RDMA_IMPL_V2_VERSION;
        local_msg.block_size = g_rdma_recv_block_size;
        local_msg.sq_size = _ep->_sq_size;
        local_msg.rq_size = _ep->_rq_size;
        local_msg.lid = GetRdmaLid();
        local_msg.gid = GetRdmaGid();
        if (BAIDU_LIKELY(_ep->_resource)) {
            local_msg.qp_num = _ep->_resource->qp->qp_num;
        } else {
            // Only happens in UT
            local_msg.qp_num = 0;
        }
    }
    fast_memcpy(data, MAGIC_STR, 4);
    local_msg.Serialize((char*)data + 4);
    return _ep->WriteToFd(data, RDMA_HELLO_V2_MSG_LEN);
}

namespace v3_wire {

bool ValidRdmaHello(const RdmaHello& msg) {
    if (msg.gid().size() != sizeof(ibv_gid)) {
        return false;
    }
    // ParsedHello stores these as uint16_t; reject values that would truncate.
    constexpr uint16_t MAX_UINT16 = std::numeric_limits<uint16_t>::max();
    if (msg.sq_size() > MAX_UINT16 || msg.rq_size() > MAX_UINT16 || msg.lid() > MAX_UINT16) {
        return false;
    }
    if (msg.block_size() < MIN_BLOCK_SIZE) {
        return false;
    }
    if (msg.sq_size() < MIN_QP_SIZE) {
        return false;
    }
    if (msg.rq_size() < MIN_QP_SIZE) {
        return false;
    }
    // qp_num == 0 only happens in UT (no real QP allocated).
    if (msg.qp_num() == 0 && !g_skip_rdma_init) {
        return false;
    }
    return true;
}

void FillLocalRdmaHello(const RdmaEndpoint* ep, RdmaHello* msg) {
    msg->set_block_size(g_rdma_recv_block_size);
    msg->set_sq_size(ep->_sq_size);
    msg->set_rq_size(ep->_rq_size);
    msg->set_lid(GetRdmaLid());
    ibv_gid gid = GetRdmaGid();
    msg->set_gid(std::string(reinterpret_cast<const char*>(gid.raw),
                             sizeof(gid.raw)));
    if (BAIDU_LIKELY(ep->_resource)) {
        msg->set_qp_num(ep->_resource->qp->qp_num);
    } else {
        // Only happens in UT
        msg->set_qp_num(0);
    }
}

int ReadAndParseV3Hello(RdmaEndpoint* ep, RdmaHello* out) {
    uint8_t size_buf[RDMA_HELLO_V3_PB_SIZE_LEN];
    if (ep->ReadFromFd(size_buf, RDMA_HELLO_V3_PB_SIZE_LEN) < 0) {
        return -1;
    }
    uint32_t pb_size = butil::NetToHost32(
        *reinterpret_cast<const uint32_t*>(size_buf));
    if (pb_size == 0 || pb_size > RDMA_HELLO_V3_MAX_PB_SIZE) {
        errno = EPROTO;
        return -1;
    }
    butil::IOPortal body;
    if (ep->ReadFromFd(&body, pb_size) < 0) {
        return -1;
    }

    butil::IOBufAsZeroCopyInputStream input(body);
    if (!out->ParseFromZeroCopyStream(&input)) {
        LOG(ERROR) << "Failed to parse RdmaHello";
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int WriteV3Hello(RdmaEndpoint* ep, const RdmaHello& msg) {
    uint32_t pb_size = static_cast<uint32_t>(msg.ByteSizeLong());
    if (pb_size > RDMA_HELLO_V3_MAX_PB_SIZE) {
        errno = EPROTO;
        return -1;
    }

    // [ "RDM3" 4B ][ pb_size 4B (big-endian) ][ RdmaHello protobuf bytes ]
    butil::IOBuf packet;
    packet.append(MAGIC_STR_V3, MAGIC_STR_LEN);
    uint32_t pb_size_be = butil::HostToNet32(pb_size);
    packet.append(&pb_size_be, RDMA_HELLO_V3_PB_SIZE_LEN);
    butil::IOBufAsZeroCopyOutputStream output(&packet);
    if (!msg.SerializeToZeroCopyStream(&output)) {
        LOG(ERROR) << "Failed to serialize RdmaHello";
        errno = EPROTO;
        return -1;
    }
    return ep->WriteToFd(&packet);
}

void TranslateHello(const RdmaHello& msg, ParsedHello* out) {
    out->block_size = msg.block_size();
    out->sq_size = static_cast<uint16_t>(msg.sq_size());
    out->rq_size = static_cast<uint16_t>(msg.rq_size());
    out->lid = static_cast<uint16_t>(msg.lid());
    fast_memcpy(out->gid.raw, msg.gid().data(), sizeof(out->gid.raw));
    out->qp_num = msg.qp_num();
}

}  // namespace v3_wire

int RdmaHandshakeClientV3::SendLocalHello() {
    RdmaHello local_msg{};
    v3_wire::FillLocalRdmaHello(_ep, &local_msg);
    return v3_wire::WriteV3Hello(_ep, local_msg);
}

int RdmaHandshakeClientV3::ReceiveAndParseRemoteHello(ParsedHello* remote,
                                                     bool* negotiated) {
    uint8_t magic[MAGIC_STR_LEN];
    if (_ep->ReadFromFd(magic, MAGIC_STR_LEN) < 0) {
        return -1;
    }
    if (memcmp(magic, MAGIC_STR_V3, MAGIC_STR_LEN) != 0) {
        errno = EPROTO;
        return -1;
    }

    RdmaHello remote_msg{};
    if (v3_wire::ReadAndParseV3Hello(_ep, &remote_msg) < 0) {
        return -1;
    }
    if (!v3_wire::ValidRdmaHello(remote_msg)) {
        *negotiated = false;
        return 0;
    }
    *negotiated = true;
    v3_wire::TranslateHello(remote_msg, remote);
    return 0;
}

int RdmaHandshakeServerV3::ReceiveAndParseRemoteHello(ParsedHello* remote, bool* negotiated) {
    // Magic already consumed by ProcessHandshakeAtServer.
    RdmaHello remote_msg{};
    if (v3_wire::ReadAndParseV3Hello(_ep, &remote_msg) < 0) {
        return -1;
    }
    if (!v3_wire::ValidRdmaHello(remote_msg)) {
        *negotiated = false;
        return 0;
    }
    *negotiated = true;
    v3_wire::TranslateHello(remote_msg, remote);
    return 0;
}

int RdmaHandshakeServerV3::SendLocalHello() {
    RdmaHello local_msg{};
    v3_wire::FillLocalRdmaHello(_ep, &local_msg);
    return v3_wire::WriteV3Hello(_ep, local_msg);
}

std::unique_ptr<RdmaHandshake> CreateClientHandshake(RdmaEndpoint* ep) {
    switch (FLAGS_rdma_client_handshake_version) {
    case 3:
        return std::unique_ptr<RdmaHandshake>(new RdmaHandshakeClientV3(ep));
    case 2:
    default:
        return std::unique_ptr<RdmaHandshake>(new RdmaHandshakeClientV2(ep));
    }
}

std::unique_ptr<RdmaHandshake> CreateServerHandshakeByMagic(
    RdmaEndpoint* ep, const uint8_t magic[MAGIC_STR_LEN]) {
    if (memcmp(magic, MAGIC_STR, MAGIC_STR_LEN) == 0) {
        return std::unique_ptr<RdmaHandshake>(new RdmaHandshakeServerV2(ep));
    }
    if (memcmp(magic, MAGIC_STR_V3, MAGIC_STR_LEN) == 0) {
        return std::unique_ptr<RdmaHandshake>(new RdmaHandshakeServerV3(ep));
    }
    return nullptr;
}

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_WITH_RDMA
