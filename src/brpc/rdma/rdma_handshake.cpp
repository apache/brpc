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
#include "brpc/rdma/rdma_handshake_constants.h"

#include <string.h>
#include <algorithm>            // std::min
#include <string>
#include <limits>
#include <gflags/gflags.h>
#include "butil/iobuf.h"        // IOBuf, IOPortal, IOBufAsZeroCopy*Stream
#include "butil/sys_byteorder.h"
#include "butil/raw_pack.h"      // RawPacker, RawUnpacker
#include "brpc/socket.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma_transport.h"
#include "brpc/rdma/rdma_handshake.pb.h"
#include "butil/object_pool.h"

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
extern uint32_t g_rdma_recv_block_size;
extern bool g_skip_rdma_init;

extern int (*IbvQueryEce)(ibv_qp*, ibv_ece*);

DEFINE_bool(rdma_ece, false, "Enable end-to-end ECE (Enhanced Connection Establishment) "
                             "negotiation in the RDMA v3 handshake. Automatically degrades "
                             "to no-ECE when the peer, the local libibverbs, or set_ece "
                             "does not support it. Acts as a kill switch (default off).");

DECLARE_bool(rdma_trace_verbose);

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
        // gid is a raw 16-byte identifier and must NOT be byte-swapped.
        .pack_bytes(gid.raw, sizeof(gid.raw))
        .pack32(qp_num);
}

void HelloMessage::Deserialize(void* data) {
    butil::RawUnpacker(data)
        .unpack16(msg_len)
        .unpack16(hello_ver)
        .unpack16(impl_ver)
        .unpack32(block_size)
        .unpack16(sq_size)
        .unpack16(rq_size)
        .unpack16(lid)
        // gid is a raw 16-byte identifier and must NOT be byte-swapped.
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

static void TranslateV2Hello(const HelloMessage& msg, ParsedHello* out) {
    out->block_size = msg.block_size;
    out->sq_size = msg.sq_size;
    out->rq_size = msg.rq_size;
    out->lid = msg.lid;
    out->gid = msg.gid;
    out->qp_num = msg.qp_num;
}

RemoteHelloResult ReadBodyAndNegotiate(RdmaEndpoint* ep, ParsedHello* remote) {
    uint8_t data[HELLO_V2_MSG_LEN_MIN];
    if (ep->ReadFromFd(data, HELLO_V2_MSG_LEN_MIN - HELLO_MAGIC_LEN) < 0) {
        return RemoteHelloResult::ERROR;
    }
    HelloMessage remote_msg{};
    remote_msg.Deserialize(data);
    if (remote_msg.msg_len < HELLO_V2_MSG_LEN_MIN ||
        remote_msg.msg_len > HELLO_V2_MSG_LEN_MAX) {
        errno = EPROTO;
        return RemoteHelloResult::ERROR;
    }
    if (remote_msg.msg_len > HELLO_V2_MSG_LEN_MIN) {
        // Drain unknown trailing bytes so they don't pollute subsequent
        // reads (e.g. the upcoming ACK message). v2 base fields already
        // carry enough information for negotiation; unknown trailing
        // bytes are treated as optional hints that v2 safely ignores.
        size_t ext_len = remote_msg.msg_len - HELLO_V2_MSG_LEN_MIN;
        if (DrainBytes(ep, ext_len) < 0) {
            return RemoteHelloResult::ERROR;
        }
    }
    if (!ValidHelloMessage(remote_msg)) {
        return RemoteHelloResult::FALLBACK;
    }
    TranslateV2Hello(remote_msg, remote);
    return RemoteHelloResult::NEGOTIATED;
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
    uint8_t data[HELLO_V2_MSG_LEN_MIN];

    v2_wire::HelloMessage local_msg{};
    local_msg.msg_len = HELLO_V2_MSG_LEN_MIN;
    local_msg.hello_ver = HELLO_V2_VERSION;
    local_msg.impl_ver = IMPL_V2_VERSION;
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
    fast_memcpy(data, HELLO_MAGIC, 4);
    local_msg.Serialize((char*)data + 4);
    return ep->WriteToFd(data, HELLO_V2_MSG_LEN_MIN);
}

RemoteHelloResult RdmaHandshakeClientV2::ReceiveAndParseRemoteHello(ParsedHello* remote) {
    uint8_t magic[HELLO_MAGIC_LEN];
    if (_ep->ReadFromFd(magic, HELLO_MAGIC_LEN) < 0) {
        return RemoteHelloResult::ERROR;
    }
    if (memcmp(magic, HELLO_MAGIC, HELLO_MAGIC_LEN) != 0) {
        errno = EPROTO;
        return RemoteHelloResult::ERROR;
    }

    return v2_wire::ReadBodyAndNegotiate(_ep, remote);
}

// Parse one complete v2 client hello out of `_source` (non-blocking).
// v2 hello: [ "RDMA" 4B ][ msg_len 2B ][ 34B ... ], base total = 40B.
RemoteHelloResult RdmaHandshakeServerV2::ReceiveAndParseRemoteHello(ParsedHello* remote) {
    butil::IOBuf* source = _source;
    constexpr size_t HDR_LEN = HELLO_MAGIC_LEN + 2;
    if (source->size() < HDR_LEN) {
        // msg_len has not fully arrived yet.
        return RemoteHelloResult::NEED_MORE;
    }

    uint8_t hdr[HDR_LEN];
    CHECK_EQ(source->copy_to(hdr, sizeof(hdr)), sizeof(hdr));

    uint16_t msg_len = 0;
    butil::RawUnpacker(hdr + HELLO_MAGIC_LEN).unpack16(msg_len);
    if (msg_len < HELLO_V2_MSG_LEN_MIN || msg_len > HELLO_V2_MSG_LEN_MAX) {
        errno = EPROTO;
        return RemoteHelloResult::ERROR;
    }
    if (source->size() < msg_len) {
        // Full message has not fully arrived yet.
        return RemoteHelloResult::NEED_MORE;
    }

    // Consume the whole hello: magic + 36B base body + optional extension.
    CHECK_EQ(source->pop_front(HELLO_MAGIC_LEN), HELLO_MAGIC_LEN);
    uint8_t body[HELLO_V2_MSG_LEN_MIN - HELLO_MAGIC_LEN];  // 36B
    CHECK_EQ(source->cutn(body, sizeof(body)), sizeof(body));
    if (!source->empty()) {
        // Drain unknown trailing bytes.
        source->clear();
    }

    v2_wire::HelloMessage remote_msg{};
    remote_msg.Deserialize(body);
    if (!v2_wire::ValidHelloMessage(remote_msg)) {
        return RemoteHelloResult::FALLBACK;
    }
    v2_wire::TranslateV2Hello(remote_msg, remote);
    return RemoteHelloResult::NEGOTIATED;
}

int RdmaHandshakeServerV2::SendLocalHello() {
    uint8_t data[HELLO_V2_MSG_LEN_MIN];
    v2_wire::HelloMessage local_msg{};
    local_msg.msg_len = HELLO_V2_MSG_LEN_MIN;
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
        local_msg.hello_ver = HELLO_V2_VERSION;
        local_msg.impl_ver = IMPL_V2_VERSION;
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
    fast_memcpy(data, HELLO_MAGIC, 4);
    local_msg.Serialize((char*)data + 4);
    return _ep->WriteToFd(data, HELLO_V2_MSG_LEN_MIN);
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
    msg->set_gid(reinterpret_cast<const char*>(gid.raw), sizeof(gid.raw));
    if (BAIDU_LIKELY(ep->_resource)) {
        msg->set_qp_num(ep->_resource->qp->qp_num);
    } else {
        // Only happens in UT
        msg->set_qp_num(0);
    }

    // Advertise ECE only when enabled. Role-dependent payload:
    //   Client hello: the locally queried ECE capabilities;
    //   Server hello: the reduced/negotiated ECE queried after RTS.
    // When the relevant ECE is not valid (disabled, unsupported, or query
    // failed) the field is simply omitted and the peer degrades to no-ECE.
    // Advertise ECE if there is anything to advertise. The endpoint pre-fills
    // _outgoing_ece in a role-specific way: client side stores its locally
    // queried capabilities; server side stores the reduced/negotiated ECE
    // after RTS. nullopt -> omit the field (peer degrades to no-ECE).
    if (FLAGS_rdma_ece && ep->_outgoing_ece.has_value()) {
        RdmaEce* ece = msg->mutable_ece();
        ece->set_vendor_id(ep->_outgoing_ece->vendor_id);
        ece->set_options(ep->_outgoing_ece->options);
        ece->set_comp_mask(ep->_outgoing_ece->comp_mask);
    }
}

int ReadAndParseV3Hello(RdmaEndpoint* ep, RdmaHello* out) {
    uint8_t size_buf[HELLO_V3_PB_SIZE_LEN];
    if (ep->ReadFromFd(size_buf, HELLO_V3_PB_SIZE_LEN) < 0) {
        return -1;
    }
    uint32_t pb_size = butil::NetToHost32(
        *reinterpret_cast<const uint32_t*>(size_buf));
    if (pb_size == 0 || pb_size > HELLO_V3_MAX_PB_SIZE) {
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
    if (pb_size > HELLO_V3_MAX_PB_SIZE) {
        errno = EPROTO;
        return -1;
    }

    // [ "RDM3" 4B ][ pb_size 4B (big-endian) ][ RdmaHello protobuf bytes ]
    butil::IOBuf packet;
    packet.append(HELLO_MAGIC_V3, HELLO_MAGIC_LEN);
    uint32_t pb_size_be = butil::HostToNet32(pb_size);
    packet.append(&pb_size_be, HELLO_V3_PB_SIZE_LEN);
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
    if (FLAGS_rdma_ece && msg.has_ece()) {
        ibv_ece ece;
        ece.vendor_id = msg.ece().vendor_id();
        ece.options   = msg.ece().options();
        ece.comp_mask = msg.ece().comp_mask();
        out->ece = ece;
    }
}

}  // namespace v3_wire

int RdmaHandshakeClientV3::SendLocalHello() {
    // Query local ECE capabilities so they can be advertised in the client
    // hello. v3-only. Best-effort: any failure or missing API just means we
    // won't advertise ECE (the peer then degrades to no-ECE establishment).
    if (FLAGS_rdma_ece && IbvQueryEce != NULL &&
        _ep->_resource && _ep->_resource->qp) {
        ibv_ece ece;
        if (IbvQueryEce(_ep->_resource->qp, &ece) == 0) {
            _ep->_outgoing_ece = ece;
        } else {
            LOG_IF(WARNING, FLAGS_rdma_trace_verbose)
                << "Fail to IbvQueryEce on client, ECE not advertised: "
                << _ep->_socket->description();
        }
    }

    RdmaHello local_msg{};
    v3_wire::FillLocalRdmaHello(_ep, &local_msg);
    return v3_wire::WriteV3Hello(_ep, local_msg);
}

RemoteHelloResult RdmaHandshakeClientV3::ReceiveAndParseRemoteHello(ParsedHello* remote) {
    uint8_t magic[HELLO_MAGIC_LEN];
    if (_ep->ReadFromFd(magic, HELLO_MAGIC_LEN) < 0) {
        return RemoteHelloResult::ERROR;
    }
    if (memcmp(magic, HELLO_MAGIC_V3, HELLO_MAGIC_LEN) != 0) {
        errno = EPROTO;
        return RemoteHelloResult::ERROR;
    }

    RdmaHello remote_msg{};
    if (v3_wire::ReadAndParseV3Hello(_ep, &remote_msg) < 0) {
        return RemoteHelloResult::ERROR;
    }
    if (!v3_wire::ValidRdmaHello(remote_msg)) {
        return RemoteHelloResult::FALLBACK;
    }
    v3_wire::TranslateHello(remote_msg, remote);
    return RemoteHelloResult::NEGOTIATED;
}

// Parse one complete v3 client hello out of `_source` (non-blocking).
// v3 hello: [ "RDM3" 4B ][ pb_size 4B (big-endian) ][ RdmaHello ]
RemoteHelloResult RdmaHandshakeServerV3::ReceiveAndParseRemoteHello(ParsedHello* remote) {
    constexpr size_t HDR_LEN = HELLO_MAGIC_LEN + HELLO_V3_PB_SIZE_LEN;
    if (_source->size() < HDR_LEN) {
        // pb_size has not fully arrived yet.
        return RemoteHelloResult::NEED_MORE;
    }

    uint8_t hdr[HDR_LEN];
    CHECK_EQ(_source->copy_to(hdr, sizeof(hdr)), sizeof(hdr));

    uint32_t pb_size = butil::NetToHost32(
        *reinterpret_cast<const uint32_t*>(hdr + HELLO_MAGIC_LEN));
    if (pb_size == 0 || pb_size > HELLO_V3_MAX_PB_SIZE) {
        errno = EPROTO;
        return RemoteHelloResult::ERROR;
    }
    size_t total = HDR_LEN + pb_size;
    if (_source->size() < total) {
        // Full message has not fully arrived yet.
        return RemoteHelloResult::NEED_MORE;
    }

    CHECK_EQ(_source->cutn(hdr, HDR_LEN), HDR_LEN);
    butil::IOBuf pb;
    CHECK_EQ(_source->cutn(&pb, pb_size), pb_size);
    RdmaHello remote_msg;
    butil::IOBufAsZeroCopyInputStream input(pb);
    if (!remote_msg.ParseFromZeroCopyStream(&input)) {
        LOG(ERROR) << "Failed to parse RdmaHello";
        errno = EPROTO;
        return RemoteHelloResult::ERROR;
    }
    if (!v3_wire::ValidRdmaHello(remote_msg)) {
        return RemoteHelloResult::FALLBACK;
    }
    v3_wire::TranslateHello(remote_msg, remote);
    return RemoteHelloResult::NEGOTIATED;
}

int RdmaHandshakeServerV3::SendLocalHello() {
    RdmaHello local_msg{};
    auto rdma_transport = static_cast<RdmaTransport*>(_ep->_socket->_transport.get());
    if (rdma_transport->_rdma_state == RdmaTransport::RDMA_OFF) {
        // Un-negotiable hello: all body fields are zero so the client's
        // rejects it and downgrades to TCP on the same connection.
        local_msg.set_block_size(0);
        local_msg.set_sq_size(0);
        local_msg.set_rq_size(0);
        local_msg.set_lid(0);
        local_msg.set_gid(std::string(sizeof(ibv_gid), '\0'));
        local_msg.set_qp_num(0);
    } else {
        v3_wire::FillLocalRdmaHello(_ep, &local_msg);
    }
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
    RdmaEndpoint* ep, butil::IOBuf* source, const uint8_t magic[HELLO_MAGIC_LEN]) {
    if (memcmp(magic, HELLO_MAGIC, HELLO_MAGIC_LEN) == 0) {
        return std::unique_ptr<RdmaHandshake>(
                new RdmaHandshakeServerV2(ep, source));
    }
    if (memcmp(magic, HELLO_MAGIC_V3, HELLO_MAGIC_LEN) == 0) {
        return std::unique_ptr<RdmaHandshake>(
                new RdmaHandshakeServerV3(ep, source));
    }
    return NULL;
}

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_WITH_RDMA
