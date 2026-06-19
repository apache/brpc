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

#include "brpc/policy/rdma_handshake_fallback_protocol.h"

#include <limits>
#include <string>
#include <string.h>
#include "butil/object_pool.h"
#include "butil/logging.h"
#include "butil/raw_pack.h"
#include "brpc/destroyable.h"
#include "brpc/rdma/rdma_handshake.pb.h"
#include "brpc/rdma/rdma_handshake_constants.h"

namespace brpc {
namespace policy {

// Both handshake versions are handled:
//   - v2 ("RDMA"): [ "RDMA" 4B ][ msg_len 2B ][ body ], total >= 40B.
//   - v3 ("RDM3"): [ "RDM3" 4B ][ pb_size 4B (big-endian) ][ RdmaHello bytes ].

// v2 header: 4B magic + 2B msg_len.
static constexpr size_t RDMA_V2_HEADER_LEN = rdma::HELLO_MAGIC_LEN + 2;
// v3 header: 4B magic + 4B pb_size.
static constexpr size_t RDMA_V3_HEADER_LEN =
    rdma::HELLO_MAGIC_LEN + rdma::HELLO_V3_PB_SIZE_LEN;
// An intentionally-invalid v2 hello_ver written into the reply: the client's
// ValidHelloMessage() requires hello_ver==2, so it rejects this and falls back.
static constexpr uint16_t RDMA_HELLO_V2_VERSION_INVALID =
    std::numeric_limits<uint16_t>::max();

// Length of the gid field (== sizeof(ibv_gid)). Spelled as a literal so this
// header stays free of <infiniband/verbs.h>.
constexpr size_t RDMA_GID_LEN = 16;

namespace {
struct RdmaFallbackContext : public Destroyable {
    static RdmaFallbackContext* create() { return butil::get_object<RdmaFallbackContext>(); }
    void Destroy() override { butil::return_object(this); }
};

ParseResult HandleRdmaV2Hello(butil::IOBuf* source, Socket* socket) {
    char header_buf[RDMA_V2_HEADER_LEN];
    if (source->copy_to(header_buf, sizeof(header_buf)) < sizeof(header_buf)) {
        // Magic matched, but msg_len has not fully arrived yet.
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    // msg_len (big-endian, right after the magic) is the total hello length.
    uint16_t hello_size = 0;
    butil::RawUnpacker(header_buf + rdma::HELLO_MAGIC_LEN).unpack16(hello_size);
    // A too-small value is malformed; a too-large one would make us wait
    // forever for bytes that never come (the real hello is 40B).
    if (hello_size < rdma::HELLO_V2_MSG_LEN_MIN || hello_size > rdma::HELLO_V2_MSG_LEN_MAX) {
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
    }
    // During the handshake the client sends exactly one hello and then blocks
    // waiting for the server hello: fewer bytes -> wait; more bytes -> the peer
    // is not a well-behaved RDMA client, bail out.
    if (source->size() < hello_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    if (source->size() > hello_size) {
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
    }
    source->pop_front(hello_size);

    // Reply an incompatible hello so the client downgrades to TCP on THIS
    // connection: magic "RDMA" + msg_len(=40) + an invalid hello_ver. msg_len
    // must be valid, otherwise the client errors out at the IO layer instead of
    // negotiating. The rest of the 36B body stays zero (value-initialized);
    // the client's ValidHelloMessage() rejects hello_ver!=2 -> negotiated=false.
    char reply[rdma::HELLO_V2_MSG_LEN_MIN]{};
    fast_memcpy(reply, rdma::HELLO_MAGIC, rdma::HELLO_MAGIC_LEN);
    butil::RawPacker(reply + rdma::HELLO_MAGIC_LEN)
        .pack16(sizeof(reply)).pack16(RDMA_HELLO_V2_VERSION_INVALID);

    butil::IOBuf out;
    out.append(reply, sizeof(reply));
    if (socket->Write(&out) != 0) {
        PLOG(WARNING) << "Fail to send RDMA v2 fallback hello to "
                      << socket->description();
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG,
                              "fail to send RDMA v2 fallback hello");
    }

    // Remember we are now draining the client's ACK across subsequent reads.
    socket->reset_parsing_context(RdmaFallbackContext::create());
    return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
}

ParseResult HandleRdmaV3Hello(butil::IOBuf* source, Socket* socket) {
    char header_buf[RDMA_V3_HEADER_LEN];
    if (source->copy_to(header_buf, sizeof(header_buf)) < sizeof(header_buf)) {
        // Magic matched, but pb_size has not fully arrived yet.
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    uint32_t pb_size = 0;
    butil::RawUnpacker(header_buf + rdma::HELLO_MAGIC_LEN).unpack32(pb_size);
    // Zero is malformed; an over-large value would make us wait forever. The
    // upper bound also keeps the size_t addition below well within range.
    if (pb_size == 0 || pb_size > rdma::HELLO_V3_MAX_PB_SIZE) {
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
    }
    // size_t (>= 64-bit on supported platforms), so this addition cannot
    // overflow given the bound above.
    const size_t hello_size = RDMA_V3_HEADER_LEN + pb_size;
    // During the handshake the client sends exactly one hello and then blocks
    // waiting for the server hello: fewer bytes -> wait; more bytes -> the peer
    // is not a well-behaved RDMA client, bail out.
    if (source->size() < hello_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    if (source->size() > hello_size) {
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
    }
    source->pop_front(hello_size);

    // Build a v3 hello the client will reject so it downgrades to TCP. All
    // `required` fields are populated so the client's ParseFromArray() succeeds
    // (a parse failure would surface as an IO error, not a clean fallback), but
    // block_size==0 (< MIN_BLOCK_SIZE) and qp_num==0 make its ValidRdmaHello()
    // return false -> negotiated=false regardless of g_skip_rdma_init.
    rdma::RdmaHello reply_msg;
    reply_msg.set_block_size(0);
    reply_msg.set_sq_size(0);
    reply_msg.set_rq_size(0);
    reply_msg.set_lid(0);
    reply_msg.set_gid(std::string(RDMA_GID_LEN, '\0'));
    reply_msg.set_qp_num(0);

    // [ "RDM3" 4B ][ pb_size 4B (big-endian) ][ RdmaHello protobuf bytes ]
    butil::IOBuf packet;
    packet.append(rdma::HELLO_MAGIC_V3, rdma::HELLO_MAGIC_LEN);
    uint32_t pb_size_be = butil::HostToNet32(static_cast<uint32_t>(reply_msg.ByteSizeLong()));
    packet.append(&pb_size_be, sizeof(pb_size_be));

    butil::IOBufAsZeroCopyOutputStream output(&packet);
    if (!reply_msg.SerializeToZeroCopyStream(&output)) {
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG,
                              "Fail to serialize RDMA v3 fallback hello");
    }

    if (socket->Write(&packet) != 0) {
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG,
                              "Fail to write RDMA v3 fallback hello");
    }

    // Remember we are now draining the client's ACK across subsequent reads.
    socket->reset_parsing_context(RdmaFallbackContext::create());
    return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
}
}  // namespace

ParseResult ParseRdmaHandshake(butil::IOBuf* source, Socket* socket,
                                       bool /*read_eof*/, const void* /*arg*/) {
    // It is a small state machine kept across parse calls via Socket's
    // parsing_context() (same mechanism HTTP uses):
    //   - no context yet: if the leading bytes are an RDMA magic ("RDMA" for v2 or
    //     "RDM3" for v3), consume the client hello, reply a version-matched but
    //     un-negotiable hello (so the client downgrades to TCP on this connection),
    //     store a context and return NOT_ENOUGH_DATA to wait for the client's ACK;
    //   - context present: drain the 4-byte ACK, drop the context and return
    //     TRY_OTHERS so InputMessenger parses the following real request from the
    //     first protocol.
    if (socket->parsing_context() != NULL) {
        if (source->size() < rdma::HELLO_ACK_LEN) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        source->pop_front(rdma::HELLO_ACK_LEN);
        socket->reset_parsing_context(NULL);
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }

    char magic_buf[rdma::HELLO_MAGIC_LEN];
    const size_t mn = source->copy_to(magic_buf, sizeof(magic_buf));
    if (mn < rdma::HELLO_MAGIC_LEN) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    if (*(const uint32_t*)magic_buf == *(const uint32_t*)rdma::HELLO_MAGIC) {
        return HandleRdmaV2Hello(source, socket);
    }
    if (*(const uint32_t*)magic_buf == *(const uint32_t*)rdma::HELLO_MAGIC_V3) {
        return HandleRdmaV3Hello(source, socket);
    }
    return MakeParseError(PARSE_ERROR_TRY_OTHERS);
}

void ProcessRdmaHandshake(InputMessageBase* msg) {
    // ParseRdmaHandshake replies inline and only ever returns
    // NOT_ENOUGH_DATA / TRY_OTHERS / hard errors, never a real message, so this
    // must never run. Keep a placeholder (required for server registration).
    DestroyingPtr<InputMessageBase> destroying_msg(msg);
    CHECK(false) << "ProcessRdmaHandshake should never be called";
}

}  // namespace policy
}  // namespace brpc
