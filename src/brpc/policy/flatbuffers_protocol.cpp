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

#include "butil/logging.h"                       // LOG()
#include "butil/iobuf.h"                         // butil::IOBuf
#include "butil/single_iobuf.h"                  // butil::SingleIOBuf
#include "butil/time.h"

#include "butil/raw_pack.h"                      // RawPacker RawUnpacker

#include "brpc/controller.h"                     // Controller
#include "brpc/socket.h"                         // Socket
#include "brpc/server.h"                         // Server
#include "brpc/stream_impl.h"
#include "brpc/rpc_dump.h"                       // SampledRequest
#include "brpc/policy/most_common_message.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/policy/flatbuffers_protocol.h"

namespace brpc {
namespace policy {

struct FBRpcRequestMeta {
    struct {
        uint32_t service_index;
        int32_t method_index;
    } request;
    int32_t message_size;
    int32_t attachment_size;
    int64_t correlation_id;
}__attribute__((packed));

struct FBRpcResponseMeta {
    struct {
        int32_t error_code;
    } response;
    int32_t message_size;
    int32_t attachment_size;
    int64_t correlation_id;
}__attribute__((packed));

struct FBRpcRequestHeader {
    char header[12];
    struct FBRpcRequestMeta meta;
}__attribute__((packed));

struct FBRpcResponseHeader {
    char header[12];
    struct FBRpcResponseMeta meta;
}__attribute__((packed));

bool inline ParseFbFromIOBuf(brpc::flatbuffers::Message* msg, size_t msg_size, const butil::IOBuf& buf) {
    return brpc::flatbuffers::ParseFbFromIOBUF(msg, msg_size, buf);
}

// Notes:
// 1. 12-byte header [BRPC][body_size][meta_size]
// 2. body_size and meta_size are in network byte order
// 3. Use service->service_index + method_index to specify the method to call
// 4. `attachment_size' is set iff request/response has attachment
// 5. Not supported: chunk_info

// Pack header into `buf'

static inline void PackFlatbuffersRpcHeader(char* rpc_header, int meta_size, int payload_size) {
    // supress strict-aliasing warning.
    uint32_t* dummy = (uint32_t*)rpc_header;
    *dummy = *(uint32_t*)"BRPC";
    butil::RawPacker(rpc_header + 4)
        .pack32(meta_size + payload_size)
        .pack32(meta_size);
}

static inline bool ParseMetaBufferFromIOBUF(butil::SingleIOBuf* dest,
            const butil::IOBuf& source, uint32_t msg_size) {
    return dest->assign(source, msg_size);
}

ParseResult ParseFlatBuffersMessage(butil::IOBuf* source, Socket* socket,
                            bool /*read_eof*/, const void*) {
    char header_buf[12];
    const size_t n = source->copy_to(header_buf, sizeof(header_buf));
    if (n >= 4) {
        void* dummy = header_buf;
        if (*(const uint32_t*)dummy != *(const uint32_t*)"BRPC") {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    } else {
        if (memcmp(header_buf, "BRPC", n) != 0) {
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
        // We need this log to report the body_size to give users some clues
        // which is not printed in InputMessenger.
        LOG(ERROR) << "body_size=" << body_size << " from "
                   << socket->remote_side() << " is too large";
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(header_buf) + body_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    if (meta_size > body_size) {
        LOG(ERROR) << "meta_size=" << meta_size << " is bigger than body_size="
                   << body_size;
        // Pop the message
        source->pop_front(sizeof(header_buf) + body_size);
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    source->pop_front(sizeof(header_buf));
    MostCommonMessage* msg = MostCommonMessage::Get();
    source->cutn(&msg->meta, meta_size);
    source->cutn(&msg->payload, body_size - meta_size);
    return MakeMessage(msg);
}

static void SendFlatBuffersRpcResponse(int64_t correlation_id,
                     Controller* cntl,
                     brpc::flatbuffers::Message* req,
                     brpc::flatbuffers::Message* res,
                     const Server* server,
                     MethodStatus* method_status_raw,
                     int64_t received_us) {
    ControllerPrivateAccessor accessor(cntl);
    Socket* sock = accessor.get_sending_socket();
    ConcurrencyRemover concurrency_remover(method_status_raw, cntl, received_us);
    std::unique_ptr<Controller, LogErrorTextAndDelete> recycle_cntl(cntl);
    std::unique_ptr<brpc::flatbuffers::Message> recycle_req(req);
    std::unique_ptr<brpc::flatbuffers::Message> recycle_res(res);
    //ScopedRemoveConcurrency remove_concurrency_dummy(server, cntl);
    if (cntl->IsCloseConnection()) {
        sock->SetFailed();
        return;
    }
    bool append_body = false;
    butil::IOBuf res_body;
    // `res' can be NULL here, in which case we don't serialize it
    // If user calls `SetFailed' on Controller, we don't serialize
    // response either
    struct FBRpcResponseHeader *rpc_header = NULL;
    uint32_t reserve_size = sizeof(struct FBRpcResponseHeader);

    if (res != NULL && !cntl->Failed()) {
        rpc_header = static_cast<struct FBRpcResponseHeader*>(res->reduce_meta_size_and_get_buf(sizeof(struct FBRpcResponseHeader)));
        if (BAIDU_UNLIKELY(rpc_header == NULL)) {
            cntl->SetFailed(ERESPONSE, "Fail to reduce meta size and get buf");
        } else {
            if (!brpc::flatbuffers::SerializeFbToIOBUF(res, res_body)) {
                cntl->SetFailed(ERESPONSE, "Fail to serialize response");
            } else {
                append_body = true;
            }
        }
    }

    // Don't use res->ByteSize() since it may be compressed
    size_t res_size = 0;
    size_t attached_size = 0;
    size_t meta_size = sizeof(struct FBRpcResponseMeta);
    if (append_body && rpc_header != NULL) {
        res_size = res_body.length() - reserve_size;
        attached_size = cntl->response_attachment().length();
        PackFlatbuffersRpcHeader(rpc_header->header,
                    meta_size, res_size + attached_size);
        rpc_header->meta.message_size = res_size;
        rpc_header->meta.attachment_size = attached_size;
        rpc_header->meta.response.error_code = cntl->ErrorCode();
        rpc_header->meta.correlation_id = correlation_id;
        if (attached_size > 0) {
            res_body.append(cntl->response_attachment().movable());
        }
    } else {    // error response
        struct FBRpcResponseHeader tmp_header;
        tmp_header.meta.message_size = 0;
        tmp_header.meta.attachment_size = 0;
        tmp_header.meta.response.error_code = cntl->ErrorCode();
        tmp_header.meta.correlation_id = correlation_id;
        PackFlatbuffersRpcHeader(tmp_header.header,
                                        meta_size, 0);
        res_body.clear();
        res_body.append((void const*) &tmp_header,
            sizeof(struct FBRpcResponseHeader));
    }
    Socket::WriteOptions wopt;
    wopt.ignore_eovercrowded = true;
    if (sock->Write(&res_body, &wopt) != 0) {
        const int errcode = errno;
        PLOG_IF(WARNING, errcode != EPIPE) << "Fail to write into " << *sock;
        cntl->SetFailed(errcode, "Fail to write into %s",
                        sock->description().c_str());
        return;
    }
}

void ProcessFlatBuffersRequest(InputMessageBase* msg_base) {
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket_guard(msg->ReleaseSocket());
    Socket* socket = socket_guard.get();
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);
    butil::SingleIOBuf meta_buf;
    if (!ParseMetaBufferFromIOBUF(&meta_buf,
            msg->meta, sizeof(struct FBRpcRequestMeta))) {
        LOG(WARNING) << "Fail to parse RpcMeta from " << *socket;
        socket->SetFailed(EREQUEST, "Fail to parse RpcMeta from %s",
                                        socket->description().c_str());
        return;
    }
    const struct FBRpcRequestMeta* meta =
        static_cast<const struct FBRpcRequestMeta*>(meta_buf.get_begin());
    if (!meta) {
        LOG(WARNING) << "RpcMeta from " << *socket << " is NULL";
        socket->SetFailed(EREQUEST, "Fail to parse RpcMeta from %s",
                                        socket->description().c_str());
        return;
    }

    std::unique_ptr<Controller> cntl;
    cntl.reset(new (std::nothrow) Controller);
    if (NULL == cntl.get()) {
        LOG(WARNING) << "Fail to new Controller";
        return;
    }

    std::unique_ptr<brpc::flatbuffers::Message> req;
    std::unique_ptr<brpc::flatbuffers::Message> res;

    ServerPrivateAccessor server_accessor(server);

    ControllerPrivateAccessor accessor(cntl.get());
    accessor.set_server(server)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_request_protocol(PROTOCOL_FLATBUFFERS_RPC)
        .move_in_server_receiving_sock(socket_guard);
    MethodStatus* method_status = NULL;
    do {
        if (!server->IsRunning()) {
            cntl->SetFailed(ELOGOFF, "Server is stopping");
            break;
        }

        if (socket->is_overcrowded()) {
            cntl->SetFailed(EOVERCROWDED, "Connection to %s is overcrowded",
                            butil::endpoint2str(socket->remote_side()).c_str());
            break;
        }

        if (!server_accessor.AddConcurrency(cntl.get())) {
            cntl->SetFailed(ELIMIT, "Reached server's max_concurrency=%d",
                            server->options().max_concurrency);
            break;
        }

        const Server::FlatBuffersMethodProperty* mp =
            server_accessor.FindFlatBufferMethodPropertyByIndex(meta->request.service_index,
                                                                        meta->request.method_index);
        if (NULL == mp) {
            cntl->SetFailed(ENOMETHOD, "Fail to find method_index=%d service_index=%u ",
                                                                meta->request.method_index,
                                                                meta->request.service_index);
            break;
        }
        // Switch to service-specific error.
        non_service_error.release();
        if (mp->status) {
            method_status = mp->status;
            if (!method_status->OnRequested()) {
                cntl->SetFailed(ELIMIT, "Reached %s's MaxConcurrency=%d",
                                mp->method->full_name().c_str(),
                                method_status->MaxConcurrency());
                break;
            }
        }
        brpc::flatbuffers::Service* svc = mp->service;
        const brpc::flatbuffers::MethodDescriptor* method = mp->method;
        accessor.set_fb_method(method);
        const int reqsize = static_cast<int>(msg->payload.size());
        butil::IOBuf req_buf;
        butil::IOBuf* req_buf_ptr = &msg->payload;
        if (meta->attachment_size > 0) {
            if (reqsize < meta->attachment_size) {
                cntl->SetFailed(EREQUEST,
                    "attachment_size=%d is larger than request_size=%d",
                     meta->attachment_size, reqsize);
                break;
            }
            int body_without_attachment_size = reqsize - meta->attachment_size;
            msg->payload.cutn(&req_buf, body_without_attachment_size);
            req_buf_ptr = &req_buf;
            cntl->request_attachment().swap(msg->payload);
        }

        req.reset(new brpc::flatbuffers::Message());
        if (!brpc::flatbuffers::ParseFbFromIOBUF(req.get(), meta->message_size, *req_buf_ptr)) {
            cntl->SetFailed(EREQUEST, "Fail to parse request message, "
                            "request_size=%d", reqsize);
            break;
        }
        res.reset(new brpc::flatbuffers::Message());
        // `socket' will be held until response has been sent
        google::protobuf::Closure* done = ::brpc::NewCallback<
            int64_t, Controller*, brpc::flatbuffers::Message*,
            brpc::flatbuffers::Message*, const Server*,
            MethodStatus*, int64_t>(
                &SendFlatBuffersRpcResponse, meta->correlation_id, cntl.get(),
                req.get(), res.get(), server,
                method_status, msg->received_us());

        // optional, just release resourse ASAP
        msg.reset();
        req_buf.clear();
        //only used in polling thread
        svc->FBCallMethod(method, cntl.release(),
                req.release(), res.release(), done);
        return;
    } while (false);
    // `cntl', `req' and `res' will be deleted inside `SendFlatBuffersRpcResponse'
    // `socket' will be held until response has been sent
    SendFlatBuffersRpcResponse(meta->correlation_id, cntl.release(),
                    req.release(), res.release(), server,
                    method_status, -1);
}

void ProcessFlatBuffersResponse(InputMessageBase* msg_base) {
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    butil::SingleIOBuf meta_buf;
    if (!ParseMetaBufferFromIOBUF(&meta_buf,
            msg->meta, sizeof(struct FBRpcResponseMeta))) {
        LOG(WARNING) << "Fail to parse from response meta";
        return;
    }
    const struct FBRpcResponseMeta* meta =
        static_cast<const struct FBRpcResponseMeta*>(meta_buf.get_begin());
    if (!meta) {
        LOG(WARNING) << "Fail to parse from response meta: meta is NULL";
        return;
    }

    const bthread_id_t cid = { static_cast<uint64_t>(meta->correlation_id) };
    Controller* cntl = NULL;
    const int rc = bthread_id_lock(cid, (void**)&cntl);
    if (rc != 0) {
        LOG_IF(ERROR, rc != EINVAL && rc != EPERM)
            << "Fail to lock correlation_id=" << cid << ": " << berror(rc);
        return;
    }

    ControllerPrivateAccessor accessor(cntl);
    const int saved_error = cntl->ErrorCode();
    do {
        if (meta->response.error_code != 0) {
            // If error_code is unset, default is 0 = success.
            cntl->SetFailed(meta->response.error_code,
                                  "server response error");
            break;
        }
        // Parse response message if error code from meta is 0
        butil::IOBuf res_buf;
        const int res_size = msg->payload.length();
        butil::IOBuf* res_buf_ptr = &msg->payload;
        if (meta->attachment_size > 0) {
            if (meta->attachment_size > res_size) {
                cntl->SetFailed(
                    ERESPONSE,
                    "attachment_size=%d is larger than response_size=%d",
                    meta->attachment_size, res_size);
                break;
            }
            int body_without_attachment_size = res_size - meta->attachment_size;
            msg->payload.cutn(&res_buf, body_without_attachment_size);
            res_buf_ptr = &res_buf;
            cntl->response_attachment().swap(msg->payload);
        }

        if (cntl->fb_response()) {
            if (!brpc::flatbuffers::ParseFbFromIOBUF(cntl->fb_response(),
                                         meta->message_size, *res_buf_ptr)) {
                cntl->SetFailed(
                    ERESPONSE, "Fail to parse response message, "
                    " response_size=%d", res_size);
            }
        } // else silently ignore the response.
    } while (0);
    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

void PackFlatBuffersRequest(butil::IOBuf* req_buf,
                    SocketMessage**,
                    uint64_t correlation_id,
                    const void* method_descriptor,
                    Controller* cntl,
                    const butil::IOBuf& request_body,
                    const Authenticator* auth) {
    const brpc::flatbuffers::MethodDescriptor* method =
    static_cast<const brpc::flatbuffers::MethodDescriptor*>(method_descriptor);
    struct FBRpcRequestHeader *rpc_header = NULL;
    size_t req_size = request_body.length();
    rpc_header = (struct FBRpcRequestHeader*)const_cast<void*>(request_body.fetch1());
    if (BAIDU_UNLIKELY(rpc_header == NULL)) {
        return cntl->SetFailed(ERESPONSE, "fail to get fb request rpc header");
    }
    req_size -= sizeof(struct FBRpcRequestHeader);

    //ControllerPrivateAccessor accessor(cntl);
    if (method) {
        rpc_header->meta.request.service_index = method->service()->index();
        rpc_header->meta.request.method_index = method->index();
    } else {
        return cntl->SetFailed(ENOMETHOD, "%s.method is NULL", __FUNCTION__);
    }

    rpc_header->meta.correlation_id = correlation_id;

    size_t meta_size = sizeof(struct FBRpcRequestMeta);
    rpc_header->meta.message_size = req_size;
    const size_t attached_size = cntl->request_attachment().length();
    if (attached_size > 0) {
        rpc_header->meta.attachment_size = attached_size;
    } else {
        rpc_header->meta.attachment_size = 0;
    }
    PackFlatbuffersRpcHeader(rpc_header->header, meta_size, req_size + attached_size);

    req_buf->append(request_body);

    if (attached_size > 0) {
        req_buf->append(cntl->request_attachment());
    }
}

void SerializeFlatBuffersRequest(butil::IOBuf* buf,
                             Controller* cntl,
                             const void* request_obj) {
    brpc::flatbuffers::Message* request = (brpc::flatbuffers::Message*)const_cast<void*>(request_obj);
    // Check sanity of request.
    if (!request) {
        return cntl->SetFailed(EREQUEST, "`request' is NULL");
    }
    uint32_t reserve_size = sizeof(struct FBRpcRequestHeader);
    request->reduce_meta_size_and_get_buf(reserve_size);
    if (!brpc::flatbuffers::SerializeFbToIOBUF(request, *buf)) {
        return cntl->SetFailed(EREQUEST, "Fail to serialize request");
    }
}

}  // namespace policy
}  // namespace brpc