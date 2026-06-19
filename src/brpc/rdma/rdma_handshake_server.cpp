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

#include "brpc/rdma/rdma_handshake_server.h"

#include <limits>
#include <string>
#include <string.h>
#include "butil/iobuf.h"
#include "butil/logging.h"
#include "butil/object_pool.h"
#include "butil/raw_pack.h"
#include "butil/sys_byteorder.h"
#include "brpc/socket.h"
#include "brpc/rdma/rdma_handshake.pb.h"
#include "brpc/rdma/rdma_handshake_constants.h"
#if BRPC_WITH_RDMA
#include "brpc/rdma/rdma_endpoint.h"
#endif

namespace brpc {
namespace rdma {

ServerHandshakeContext* ServerHandshakeContext::Create() {
    return butil::get_object<ServerHandshakeContext>();
}

void ServerHandshakeContext::Destroy() {
    butil::return_object(this);
}

// Fallback-only server handshake. Used for any connection that is NOT in RDMA
// mode: builds without RDMA (no RdmaEndpoint exists at all), and RDMA-enabled
// builds where this particular connection is plain TCP. Every RDMA client that
// reaches here is answered with an un-negotiable hello and asked to downgrade
// to TCP on the same connection, then its ACK is drained.

// An intentionally-invalid v2 hello_ver: the client's ValidHelloMessage()
// requires hello_ver==2, so it rejects this and falls back.
static constexpr uint16_t V2_HELLO_VERSION_INVALID = std::numeric_limits<uint16_t>::max();
// Length of the gid field (== sizeof(ibv_gid)), spelled as a literal so this
// compile-switch-independent file stays free of <infiniband/verbs.h>.
static constexpr size_t V3_GID_LEN = 16;

// Consume one complete v2 client hello ("RDMA" + 2B msg_len + body) from
// `source` without interpreting its content (fallback does not negotiate).
// Returns 1 (consumed), 0 (not enough data yet, nothing consumed) or -1 (error).
static int DrainClientHelloV2(butil::IOBuf* source) {
    constexpr size_t HDR_LEN = HELLO_MAGIC_LEN + 2;
    if (source->size() < HDR_LEN) {
        return 0;
    }

    uint8_t hdr[HDR_LEN];
    CHECK_EQ(source->copy_to(hdr, sizeof(hdr)), sizeof(hdr));

    uint16_t msg_len = 0;
    butil::RawUnpacker(hdr + HELLO_MAGIC_LEN).unpack16(msg_len);
    if (msg_len < HELLO_V2_MSG_LEN_MIN || msg_len > HELLO_V2_MSG_LEN_MAX) {
        return -1;
    }

    if (source->size() < msg_len) {
        return 0;
    }

    CHECK_EQ(source->pop_front(msg_len), msg_len);
    return 1;
}

// Consume one complete v3 client hello ("RDM3" + 4B pb_size + protobuf body)
// from `source` without interpreting its content.
// Returns 1 (consumed), 0 (not enough data yet, nothing consumed) or -1 (error).
static int DrainClientHelloV3(butil::IOBuf* source) {
    constexpr size_t HDR_LEN = HELLO_MAGIC_LEN + HELLO_V3_PB_SIZE_LEN;
    if (source->size()< HDR_LEN) {
        return 0;
    }

    uint8_t hdr[HDR_LEN];
    CHECK_EQ(source->copy_to(hdr, sizeof(hdr)), sizeof(hdr));

    uint32_t pb_size = butil::NetToHost32(
        *reinterpret_cast<const uint32_t*>(hdr + HELLO_MAGIC_LEN));
    if (pb_size == 0 || pb_size > HELLO_V3_MAX_PB_SIZE) {
        return -1;
    }

    const size_t total = HDR_LEN + pb_size;
    if (source->size() < total) {
        return 0;
    }

    CHECK_EQ(source->pop_front(total), total);
    return 1;
}

// Reply an un-negotiable hello so the client downgrades to TCP. All body fields
// are zero/invalid so the client's validity check rejects it.
// Returns 0 on success, -1 otherwise.
static int SendUnnegotiableHello(Socket* socket, int version) {
    butil::IOBuf packet;
    if (version == 2) {
        // magic "RDMA" + msg_len(=40) + invalid hello_ver; rest stays zero.
        // NOTE: msg_len is the length of the WHOLE hello INCLUDING the 4B magic
        // (== HELLO_V2_MSG_LEN_MIN), not just the body; the client rejects any
        // msg_len < HELLO_V2_MSG_LEN_MIN as a protocol error.
        packet.append(HELLO_MAGIC, HELLO_MAGIC_LEN);
        char reply[HELLO_V2_MSG_LEN_MIN - HELLO_MAGIC_LEN]{};
        butil::RawPacker(reply).pack16(HELLO_V2_MSG_LEN_MIN)
                               .pack16(V2_HELLO_VERSION_INVALID);
        packet.append(reply, sizeof(reply));
    } else {
        // "RDM3" + pb_size + RdmaHello with block_size==0 & qp_num==0 so the
        // client's ValidRdmaHello() returns false.
        RdmaHello reply_msg;
        reply_msg.set_block_size(0);
        reply_msg.set_sq_size(0);
        reply_msg.set_rq_size(0);
        reply_msg.set_lid(0);
        reply_msg.set_gid(std::string(V3_GID_LEN, '\0'));
        reply_msg.set_qp_num(0);
        packet.append(HELLO_MAGIC_V3, HELLO_MAGIC_LEN);
        uint32_t pb_size_be =
            butil::HostToNet32(static_cast<uint32_t>(reply_msg.ByteSizeLong()));
        packet.append(&pb_size_be, sizeof(pb_size_be));
        butil::IOBufAsZeroCopyOutputStream output(&packet);
        if (!reply_msg.SerializeToZeroCopyStream(&output)) {
            LOG(WARNING) << "Fail to serialize RDMA v3 fallback hello";
            return -1;
        }
    }

    if (socket->Write(&packet) != 0) {
        PLOG(WARNING) << "Fail to send RDMA fallback hello to " << socket->description();
        return -1;
    }
    return 0;
}

// Fallback handshake for connections that are NOT in RDMA mode.
static ParseResult FallbackServerHandshake(butil::IOBuf* source, Socket* socket) {
    if (socket->parsing_context() == NULL) {
        if (source->size() < HELLO_MAGIC_LEN) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        // Phase 1: consume the client hello and reply an un-negotiable hello.
        uint8_t magic[HELLO_MAGIC_LEN];
        CHECK_EQ(source->copy_to(magic, HELLO_MAGIC_LEN), HELLO_MAGIC_LEN);

        int version;
        if (memcmp(magic, HELLO_MAGIC, HELLO_MAGIC_LEN) == 0) {
            version = 2;
        } else if (memcmp(magic, HELLO_MAGIC_V3, HELLO_MAGIC_LEN) == 0) {
            version = 3;
        } else {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }

        const int r = version == 2 ? DrainClientHelloV2(source) : DrainClientHelloV3(source);
        if (r == 0) {
            // Hello not complete yet; keep the buffer intact and retry later.
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        if (r < 0) {
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
        if (SendUnnegotiableHello(socket, version) < 0) {
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
        // Wait for the client ACK across subsequent reads.
        socket->reset_parsing_context(ServerHandshakeContext::Create());
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    // Phase 2: drain the 4B ACK.
    if (source->size() < HELLO_ACK_LEN) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    CHECK_EQ(source->pop_front(HELLO_ACK_LEN), HELLO_ACK_LEN);
    // Handshake done (downgraded to TCP); drop the context and let
    // InputMessenger parse the following real RPC.
    socket->reset_parsing_context(NULL);
    return MakeParseError(PARSE_ERROR_TRY_OTHERS);
}

ParseResult ExecuteServerHandshake(butil::IOBuf* source, Socket* socket) {
    // Only RDMA-mode connections carry a live RdmaEndpoint and run the real
    // handshake. A connection that is not in RDMA mode (RDMA compiled in but
    // this connection is plain TCP, or RDMA not compiled at all) falls back.
#if BRPC_WITH_RDMA
    if (socket->socket_mode() == SOCKET_MODE_RDMA) {
        return RdmaEndpoint::ExecuteServerHandshake(source, socket);
    }
#endif
    return FallbackServerHandshake(source, socket);
}

}  // namespace rdma
}  // namespace brpc
