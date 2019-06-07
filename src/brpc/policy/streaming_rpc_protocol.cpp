// Copyright (c) 2015 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Zhangyi Chen (chenzhangyi01@baidu.com)

#include "brpc/policy/streaming_rpc_protocol.h"

#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <gflags/gflags.h>
#include "butil/macros.h"
#include "butil/logging.h"                       // LOG()
#include "butil/time.h"
#include "butil/iobuf.h"                         // butil::IOBuf
#include "butil/raw_pack.h"                      // RawPacker RawUnpacker
#include "brpc/log.h"
#include "brpc/socket.h"                        // Socket
#include "brpc/streaming_rpc_meta.pb.h"         // StreamFrameMeta
#include "brpc/policy/most_common_message.h"
#include "brpc/stream_impl.h"


namespace brpc {
namespace policy {

// Notes on Streaming RPC Protocol:
// 1 - Header format is [STRM][body_size][meta_size], 12 bytes in total
// 2 - body_size and meta_size are in network byte order
void PackStreamMessage(butil::IOBuf* out,
                       const StreamFrameMeta &fm,
                       const butil::IOBuf *data) {
    const uint32_t data_length = data ? data->length() : 0;
    const uint32_t meta_length = fm.ByteSize();
    char head[12];
    uint32_t* dummy = (uint32_t*)head;  // suppresses strict-alias warning
    *(uint32_t*)dummy = *(const uint32_t*)"STRM";
    butil::RawPacker(head + 4)
        .pack32(data_length + meta_length)
        .pack32(meta_length);
    out->append(head, ARRAY_SIZE(head));
    butil::IOBufAsZeroCopyOutputStream wrapper(out);
    CHECK(fm.SerializeToZeroCopyStream(&wrapper));
    if (data != NULL) {
        out->append(*data);
    }
}

ParseResult ParseStreamingMessage(butil::IOBuf* source,
                            Socket* socket, bool /*read_eof*/, const void* /*arg*/) {
    char header_buf[12];
    const size_t n = source->copy_to(header_buf, sizeof(header_buf));
    if (n >= 4) {
        void* dummy = header_buf;
        if (*(const uint32_t*)dummy != *(const uint32_t*)"STRM") {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    } else {
        if (memcmp(header_buf, "STRM", n) != 0) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    }
    if (n < sizeof(header_buf)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    uint32_t body_size;
    uint32_t meta_size;
    butil::RawUnpacker(header_buf + 4).unpack32(body_size).unpack32(meta_size);
    if (body_size > FLAGS_max_body_size) {
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(header_buf) + body_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    if (BAIDU_UNLIKELY(meta_size > body_size)) {
        LOG(ERROR) << "meta_size=" << meta_size << " is bigger than body_size="
                   << body_size;
        // Pop the message
        source->pop_front(sizeof(header_buf) + body_size);
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    source->pop_front(sizeof(header_buf));
    butil::IOBuf meta_buf;
    source->cutn(&meta_buf, meta_size);
    butil::IOBuf payload;
    source->cutn(&payload, body_size - meta_size);

    do {
        StreamFrameMeta fm;
        if (!ParsePbFromIOBuf(&fm, meta_buf)) {
            LOG(WARNING) << "Fail to Parse StreamFrameMeta from " << *socket;
            break;
        }
        SocketUniquePtr ptr;
        if (Socket::Address((SocketId)fm.stream_id(), &ptr) != 0) {
            RPC_VLOG_IF(fm.frame_type() != FRAME_TYPE_RST 
                            && fm.frame_type() != FRAME_TYPE_CLOSE
                            && fm.frame_type() != FRAME_TYPE_FEEDBACK)
                   << "Fail to find stream=" << fm.stream_id();
            if (fm.has_source_stream_id()) {
                SendStreamRst(socket, fm.source_stream_id());
            }
            break;
        }
        meta_buf.clear();  // to reduce memory resident
        ((Stream*)ptr->conn())->OnReceived(fm, &payload, socket);
    } while (0);

    // Hack input messenger
    return MakeMessage(NULL);
}

void ProcessStreamingMessage(InputMessageBase* /*msg*/) {
    CHECK(false) << "Should never be called";
}

void SendStreamRst(Socket *sock, int64_t remote_stream_id) {
    CHECK(sock != NULL);
    StreamFrameMeta fm;
    fm.set_stream_id(remote_stream_id);
    fm.set_frame_type(FRAME_TYPE_RST);
    butil::IOBuf out;
    PackStreamMessage(&out, fm, NULL);
    sock->Write(&out);
}

void SendStreamClose(Socket *sock, int64_t remote_stream_id,
                     int64_t source_stream_id) {
    CHECK(sock != NULL);
    StreamFrameMeta fm;
    fm.set_stream_id(remote_stream_id);
    fm.set_source_stream_id(source_stream_id);
    fm.set_frame_type(FRAME_TYPE_CLOSE);
    butil::IOBuf out;
    PackStreamMessage(&out, fm, NULL);
    sock->Write(&out);
}

int SendStreamData(Socket* sock, const butil::IOBuf* data,
                   int64_t remote_stream_id, int64_t source_stream_id) {
    StreamFrameMeta fm;
    fm.set_stream_id(remote_stream_id);
    fm.set_source_stream_id(source_stream_id);
    fm.set_frame_type(FRAME_TYPE_DATA);
    fm.set_has_continuation(false);
    butil::IOBuf out;
    PackStreamMessage(&out, fm, data);
    return sock->Write(&out);
}

}  // namespace policy
} // namespace brpc
