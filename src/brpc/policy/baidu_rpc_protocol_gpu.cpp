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

#if BRPC_WITH_GDR

#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/text_format.h>

#include "butil/gpu/gpu_block_pool.h"
#include "butil/iobuf.h"                         // butil::IOBuf
#include "butil/logging.h"                       // LOG()
#include "butil/raw_pack.h"                      // RawPacker RawUnpacker
#include "butil/memory/scope_guard.h"
#include "butil/raw_pack.h"                      // RawPacker RawUnpacker
#include "butil/strings/string_util.h"

#include "json2pb/json_to_pb.h"
#include "json2pb/pb_to_json.h"
#include "brpc/controller.h"                    // Controller
#include "brpc/socket.h"                        // Socket
#include "brpc/server.h"                        // Server
#include "brpc/span.h"
#include "brpc/compress.h"                      // ParseFromCompressedData
#include "brpc/checksum.h"
#include "brpc/stream_impl.h"
#include "brpc/rpc_dump.h"                      // SampledRequest
#include "brpc/rpc_pb_message_factory.h"
#include "brpc/policy/baidu_rpc_meta.pb.h"      // RpcRequestMeta
#include "brpc/policy/baidu_rpc_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/streaming_rpc_protocol.h"
#include "brpc/details/usercode_backup_pool.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/server_private_accessor.h"

namespace brpc {
namespace policy {

// Notes:
// 1. 12-byte header [PRPC][body_size][meta_size]
// 2. body_size and meta_size are in network byte order
// 3. Use service->full_name() + method_name to specify the method to call
// 4. `attachment_size' is set iff request/response has attachment
// 5. Not supported: chunk_info

// Pack header into `buf'

const int header_size = 12;
// if we recv data into gpu, the header/meta/body will be copied to cpu and processed.
// in to to limit the count of d2h, we will prefetch 512B from gpu to cpu.
// if header_size + meta_size + body_size(without attachment) is less than 512, then one
// d2h is enough for one rpc.

const int prefetch_d2h_size = 512;

ParseResult ParseRpcMessageGpu(butil::IOBuf* source, Socket* socket,
                            bool /*read_eof*/, const void*) {

    char header_buf[12];
    size_t n = 0;
    uint32_t body_size;
    uint32_t meta_size;
    ParseError pe = PARSE_OK;

    void* prefetch_d2h_data = NULL;
    bool is_gpu_memory = source->is_gpu_memory();
    if (!is_gpu_memory) {
        LOG(FATAL) << "RpcMessage is not in gpu!!!";
    }
    butil::gdr::BlockPoolAllocator* host_allocator = butil::gdr::BlockPoolAllocators::singleton()->get_cpu_allocator();
    prefetch_d2h_data = host_allocator->AllocateRaw(prefetch_d2h_size);
    if (prefetch_d2h_data == NULL) {
        LOG(FATAL) << "alloc host data failed!!!";
    }

    // n is the bytes we real frefetch, n maybe less than prefetch_d2h_size;
    n = source->copy_from_gpu(prefetch_d2h_data, prefetch_d2h_size);
    size_t copy_size = n > 12 ? 12 : n;
    memcpy(header_buf, prefetch_d2h_data, copy_size);

    do {
        if (n >= 4) {
            void* dummy = header_buf;
            if (*(const uint32_t*)dummy != *(const uint32_t*)"PRPC") {
                pe = PARSE_ERROR_TRY_OTHERS;
                break;
            }
        } else {
            if (memcmp(header_buf, "PRPC", n) != 0) {
                pe = PARSE_ERROR_TRY_OTHERS;
                break;
            }
        }
        if (n < sizeof(header_buf)) {
            pe = PARSE_ERROR_NOT_ENOUGH_DATA;
            break;
        }
        butil::RawUnpacker(header_buf + 4).unpack32(body_size).unpack32(meta_size);
        if (body_size > FLAGS_max_body_size) {
            // We need this log to report the body_size to give users some clues
            // which is not printed in InputMessenger.
            LOG(ERROR) << "body_size=" << body_size << " from "
                       << socket->remote_side() << " is too large";
            pe = PARSE_ERROR_TOO_BIG_DATA;
            break;
        } else if (source->length() < sizeof(header_buf) + body_size) {
            pe = PARSE_ERROR_NOT_ENOUGH_DATA;
            break;
        }
        if (meta_size > body_size) {
            LOG(ERROR) << "meta_size=" << meta_size << " is bigger than body_size="
                       << body_size;
            // Pop the message
            source->pop_front(sizeof(header_buf) + body_size);
            pe = PARSE_ERROR_TRY_OTHERS;
            break;
        }
    } while (0);

    if (pe != PARSE_OK) {
        host_allocator->DeallocateRaw(prefetch_d2h_data);
        return MakeParseError(pe);
    }

    source->pop_front(sizeof(header_buf));
    MostCommonMessage* msg = MostCommonMessage::Get();

    if (header_size + meta_size <= n) {
        auto deleter = [host_allocator, prefetch_d2h_data](void* data) { host_allocator->DeallocateRaw(prefetch_d2h_data); };
        // n is the bytes we real frefetch. We set n as the meta and n will be used in ProcessRpcRequest/ProcessRpcResponse.
        // This is a trick, we should keep n in another better way.
        msg->meta.append_user_data_with_meta((char*)prefetch_d2h_data + header_size, meta_size, deleter, n);
        source->pop_front(meta_size);
    } else {
        host_allocator->DeallocateRaw(prefetch_d2h_data);
        source->cutn_from_gpu(&msg->meta, meta_size);
    }
    source->cutn(&msg->payload, body_size - meta_size);
    return MakeMessage(msg);
}


void FillReqBufGpu(butil::IOBuf* req_buf, MostCommonMessage* msg, int body_without_attachment_size) {
    int meta_size = msg->meta.size();
    bool is_gpu_memory = msg->payload.is_gpu_memory();
    if (!is_gpu_memory) {
        LOG(FATAL) << "message is not on gpu!!!";
    }
    int64_t real_prefetch_d2h_size = msg->meta.get_first_data_meta();
    if (header_size + meta_size + body_without_attachment_size <= real_prefetch_d2h_size) {
        void* data = msg->meta.get_first_data_ptr();
        if (data == nullptr) {
            LOG(FATAL) << "illegal data!!!";
        }
        req_buf->append((char*)data + meta_size, body_without_attachment_size);
        msg->payload.pop_front(body_without_attachment_size);
    } else {
        msg->payload.cutn_from_gpu(req_buf, body_without_attachment_size);
    }
}

void FillResBufGpu(butil::IOBuf* res_buf, MostCommonMessage* msg, const RpcMeta& meta,
                   butil::IOBuf** res_buf_ptr, Controller* cntl) {
    const int res_size = msg->payload.length();
    int meta_size = msg->meta.size();
    bool is_gpu_memory = msg->payload.is_gpu_memory();
    if (!is_gpu_memory) {
        LOG(FATAL) << "message is not on gpu!!!";
    }
    if (meta.has_attachment_size()) {
        if (meta.attachment_size() > res_size) {
            cntl->SetFailed(
                    ERESPONSE, "attachment_size=%d is larger than response_size=%d",
                    meta.attachment_size(), res_size);
            return;
        }
        int body_without_attachment_size = res_size - meta.attachment_size();

        int64_t real_prefetch_d2h_size = msg->meta.get_first_data_meta();
        if (header_size + meta_size + body_without_attachment_size <= real_prefetch_d2h_size) {
            void* data = msg->meta.get_first_data_ptr();
            if (data == nullptr) {
                LOG(FATAL) << "illegal data!!!";
            }
            res_buf->append((char*)data + meta_size, body_without_attachment_size);
            msg->payload.pop_front(body_without_attachment_size);
        } else {
            msg->payload.cutn_from_gpu(res_buf, body_without_attachment_size);
        }
        *res_buf_ptr = res_buf;
        cntl->response_attachment().swap(msg->payload);
    } else {
        int64_t real_prefetch_d2h_size = msg->meta.get_first_data_meta();
        if (header_size + meta_size + res_size <= real_prefetch_d2h_size) {
            void* data = msg->meta.get_first_data_ptr();
            if (data == nullptr) {
                LOG(FATAL) << "illegal data!!!";
            }
            res_buf->append((char*)data + meta_size, res_size);
            msg->payload.pop_front(res_size);
        } else {
            msg->payload.cutn_from_gpu(res_buf, res_size);
        }
        *res_buf_ptr = res_buf;
    }
}

}  // namespace policy
} // namespace brpc

#endif
