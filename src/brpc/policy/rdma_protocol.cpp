// Copyright (c) 2014 baidu-rpc authors.
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

// Author: Li,Zhaogeng (lizhaogeng01@baidu.com)

#ifdef BRPC_RDMA

#include <butil/iobuf.h>
#include <butil/logging.h>                      // LOG()
#include <butil/raw_pack.h>                     // RawPacker RawUnpacker
#include <butil/time.h>
#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include "brpc/controller.h"               // Controller
#include "brpc/protocol.h"
#include "brpc/rpc_dump.h"                 // SampledRequest
#include "brpc/serialized_request.h"
#include "brpc/server.h"                   // Server
#include "brpc/socket.h"                   // Socket
#include "brpc/span.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/usercode_backup_pool.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/policy/rdma_message.h"
#include "brpc/policy/rdma_meta.pb.h"
#include "brpc/policy/rdma_protocol.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_global.h"

extern "C" {
void bthread_assign_data(void* data) __THROW;
}

namespace brpc {
namespace policy {

DECLARE_bool(baidu_protocol_use_fullname);

// Notes:
// 1. 12-byte header [RDMA][body_size][meta_size]
// 2. body_size and meta_size are in network byte order
// 3. Use service->full_name() + method_name to specify the method to call
// 4. `attachment_size' is set iff request/response has attachment
// 5. Not supported: chunk_info
inline void PackRdmaRpcHeader(char* rpc_header, int meta_size,
                                     int payload_size) {
    // supress strict-aliasing warning.
    uint32_t* dummy = (uint32_t*)rpc_header;
    *dummy = *(uint32_t*)"RDMA";
    butil::RawPacker(rpc_header + 4)
        .pack32(meta_size + payload_size)
        .pack32(meta_size);
}

static bool SerializeIntoRdmaBuffer(const google::protobuf::Message* request,
                                    butil::IOBuf* buf) {
    uint32_t size = (uint32_t)request->ByteSize();
    if (size > butil::IOBuf::DEFAULT_PAYLOAD) {
        LOG(WARNING) << "We only support a message (excluding attachment) "
                     << "smaller than "
                     << butil::IOBuf::DEFAULT_PAYLOAD;
        return false;
    }
    butil::IOBufAsZeroCopyOutputStream buf_stream(buf, size + 
            butil::IOBuf::DEFAULT_BLOCK_SIZE - butil::IOBuf::DEFAULT_PAYLOAD);
    void* addr = NULL;
    int len;
    if (!buf_stream.Next(&addr, &len)) {
        LOG(WARNING) << "Fail to serialize request due to no memory";
        return false;
    }
    if (!addr) {
        buf->clear();
        LOG(WARNING) << "Fail to serialize request due to no memory";
        return false;
    }
    if (!request->SerializeToArray((char*)addr, size)) {
        buf->clear();
        LOG(WARNING) << "Fail to serialize request due to protobuf error";
        return false;
    }
    return true;
}

void SerializeRdmaRpcRequest(butil::IOBuf* buf,
                             Controller* cntl,
                             const google::protobuf::Message* request) {
    // Check sanity of request.
    if (!request) {
        cntl->SetFailed(EREQUEST, "`request' is NULL");
        return;
    }
    if (request->GetDescriptor() == SerializedRequest::descriptor()) {
        buf->append(((SerializedRequest*)request)->serialized_data());
        return;
    }
    if (!request->IsInitialized()) {
        cntl->SetFailed(
            EREQUEST, "Missing required fields in request: %s",
            request->InitializationErrorString().c_str());
        return;
    }
    // We give up compression in rdma.
    if (!SerializeIntoRdmaBuffer(request, buf)) {
        cntl->SetFailed(EREQUEST, "Fail to serialize request");
        return;
    }
}

// used by rdma_endpoint.cpp, do not mark it as static
bool SerializeRpcHeaderAndMeta(butil::IOBuf* out,
                               const RdmaRpcMeta& meta,
                               int payload_size) {
    uint32_t meta_size = meta.ByteSize();
    if (meta_size + 12 > butil::IOBuf::DEFAULT_PAYLOAD) {
        LOG(WARNING) << "We only support a meta smaller than "
                     << butil::IOBuf::DEFAULT_PAYLOAD;
        return false;
    }
    butil::IOBufAsZeroCopyOutputStream buf_stream(out, meta_size + 12 +
            butil::IOBuf::DEFAULT_BLOCK_SIZE - butil::IOBuf::DEFAULT_PAYLOAD);
    void* addr = NULL;
    int len = 0;
    if (!buf_stream.Next(&addr, &len)) {
        LOG(WARNING) << "Fail to serialize request due to no memory";
        return false;
    }
    if (!addr) {
        out->clear();
        LOG(WARNING) << "Fail to serialize request due to no memory";
        return false;
    }
    PackRdmaRpcHeader((char*)addr, meta_size, payload_size);
    if (!meta.SerializeToArray((char*)addr + 12, meta_size)) {
        out->clear();
        LOG(WARNING) << "Fail to serialize request due to protobuf error";
        return false;
    }
    return true;
}

ParseResult ParseRdmaRpcMessage(butil::IOBuf* source, Socket* socket,
                                bool, const void* arg) {
    char header_buf[12];
    const size_t n = source->copy_to(header_buf, sizeof(header_buf));
    if (n >= 4) {
        void* dummy = header_buf;
        if (*(const uint32_t*)dummy != *(const uint32_t*)"RDMA") {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    } else {
        if (memcmp(header_buf, "RDMA", n) != 0) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    }
    if (n < sizeof(header_buf)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    uint32_t body_size;
    uint32_t meta_size;
    butil::RawUnpacker(header_buf + 4).
        unpack32(body_size).unpack32(meta_size);
    if (body_size > FLAGS_max_body_size) {
        LOG(ERROR) << "body_size=" << body_size << " is too large";
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(header_buf) + body_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    if (meta_size > body_size) {
        LOG(ERROR) << "meta_size=" << meta_size
            << " is bigger than body_size="
            << body_size;
        // Pop the message
        source->pop_front(sizeof(header_buf) + body_size);
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    RdmaMessage* msg = RdmaMessage::Get();
    if (!msg) {
        LOG(ERROR) << "memory is not enough when parse rdma protocol";
        return MakeParseError(PARSE_ERROR_NO_RESOURCE);
    }
    source->pop_front(sizeof(header_buf));
    source->cutn(&msg->meta, meta_size);
    source->cutn(&msg->payload, body_size - meta_size);
    if (socket) {  // from tcp channel, similar with baidu_std
        msg->from_rdma = false;
    } else {
        msg->from_rdma = true;
    }
    return MakeMessage(msg);
}

void SendRdmaRpcResponse(int64_t correlation_id,
                         Controller* cntl, 
                         const google::protobuf::Message* req,
                         const google::protobuf::Message* res,
                         Socket* socket_raw,
                         const Server* server,
                         MethodStatus* method_status_raw,
                         long start_parse_us,
                         bool from_rdma) {
    ControllerPrivateAccessor accessor(cntl);
    Span* span = accessor.span();
    if (span) {
        span->set_start_send_us(butil::cpuwide_time_us());
    }
    SocketUniquePtr sock(socket_raw);
    ScopedMethodStatus method_status(method_status_raw);
    std::unique_ptr<Controller, LogErrorTextAndDelete> recycle_cntl(cntl);
    std::unique_ptr<const google::protobuf::Message> recycle_req(req);
    std::unique_ptr<const google::protobuf::Message> recycle_res(res);
    ScopedRemoveConcurrency remove_concurrency_dummy(server, cntl);
    
    if (cntl->IsCloseConnection()) {
        sock->SetFailed();
        return;
    }
    bool append_body = false;
    butil::IOBuf res_body;
    // `res' can be NULL here, in which case we don't serialize it
    // If user calls `SetFailed' on Controller, we don't serialize
    // response either
    if (res != NULL && !cntl->Failed()) {
        if (!res->IsInitialized()) {
            cntl->SetFailed(
                ERESPONSE, "Missing required fields in response: %s", 
                res->InitializationErrorString().c_str());
        } else if (!SerializeIntoRdmaBuffer(res, &res_body)) {
            cntl->SetFailed(ERESPONSE, "Fail to serialize response");
        } else {
            append_body = true;
        }
    }

    size_t res_size = 0;
    if (append_body) {
        res_size = res_body.length();
    }

    int error_code = cntl->ErrorCode();
    if (error_code == -1) {
        // replace general error (-1) with INTERNAL_SERVER_ERROR to make a
        // distinction between server error and client error
        error_code = EINTERNAL;
    }
    RdmaRpcMeta meta;
    RdmaRpcResponseMeta* response_meta = meta.mutable_response();
    response_meta->set_error_code(error_code);
    if (!cntl->ErrorText().empty()) {
        // Only set error_text when it's not empty since protobuf Message
        // always new the string no matter if it's empty or not.
        response_meta->set_error_text(cntl->ErrorText());
    }
    meta.set_correlation_id(correlation_id);

    size_t attached_size = cntl->response_attachment().length();
    if (attached_size) {
        meta.set_attachment_size(attached_size);
    }

    butil::IOBuf res_buf;
    if (!SerializeRpcHeaderAndMeta(&res_buf, meta, res_size + attached_size)) {
        cntl->SetFailed(ERESPONSE, "Fail to serialize header and meta");
        return;
    }
    if (append_body) {
        res_buf.append(res_body.movable());
        if (attached_size) {
            res_buf.append(cntl->response_attachment().movable());
        }
    }

    if (span) {
        span->set_response_size(res_buf.size());
    }

    Socket::WriteOptions wopt;
    if (from_rdma) {
        wopt.use_rdma = true;
    }
    wopt.ignore_eovercrowded = true;
    if (sock->Write(&res_buf, &wopt) != 0) {
        const int errcode = errno;
        PLOG_IF(WARNING, errcode != EPIPE) << "Fail to write into " << *sock;
        cntl->SetFailed(errcode, "Fail to write into %s",
                sock->description().c_str());
        return;
    }

    if (span) {
        span->set_sent_us(butil::cpuwide_time_us());
    }
    if (method_status) {
        method_status.release()->OnResponded(
            !cntl->Failed(), butil::cpuwide_time_us() - start_parse_us);
    }
}

struct CallMethodInBackupThreadArgs {
    ::google::protobuf::Service* service;
    const ::google::protobuf::MethodDescriptor* method;
    ::google::protobuf::RpcController* controller;
    const ::google::protobuf::Message* request;
    ::google::protobuf::Message* response;
    ::google::protobuf::Closure* done;
};

// defined in baidu_rpc_protocol.cpp
extern void EndRunningCallMethodInPool(
    ::google::protobuf::Service* service,
    const ::google::protobuf::MethodDescriptor* method,
    ::google::protobuf::RpcController* controller,
    const ::google::protobuf::Message* request,
    ::google::protobuf::Message* response,
    ::google::protobuf::Closure* done);

void ProcessRdmaRpcRequest(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<RdmaMessage> msg(static_cast<RdmaMessage*>(msg_base));
    SocketUniquePtr socket(msg->ReleaseSocket());
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);

    RdmaRpcMeta meta;
    if (!ParsePbFromIOBuf(&meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse RpcMeta from " << *socket;
        socket->SetFailed(EREQUEST, "Fail to parse RpcMeta from %s",
                socket->description().c_str());
        return;
    }

    bool from_rdma = msg->from_rdma;

    const RdmaRpcRequestMeta &request_meta = meta.request();

    SampledRequest* sample = AskToBeSampled();
    if (sample) {
        sample->set_service_name(request_meta.service_name());
        sample->set_method_name(request_meta.method_name());
        sample->set_protocol_type(PROTOCOL_RDMA);
        sample->set_attachment_size(meta.attachment_size());
        sample->set_authentication_data(meta.authentication_data());
        sample->request = msg->payload;
        sample->submit(start_parse_us);
    }

    std::unique_ptr<Controller> cntl(new (std::nothrow) Controller);
    if (NULL == cntl.get()) {
        LOG(WARNING) << "Fail to new Controller";
        return;
    }

    std::unique_ptr<google::protobuf::Message> req;
    std::unique_ptr<google::protobuf::Message> res;

    ServerPrivateAccessor server_accessor(server);
    ControllerPrivateAccessor accessor(cntl.get());
    const bool security_mode = server->options().security_mode() &&
                               socket->user() == server_accessor.acceptor();
    if (request_meta.has_log_id()) {
        cntl->set_log_id(request_meta.log_id());
    }
    accessor.set_server(server)
        .set_security_mode(security_mode)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_auth_context(socket->auth_context())
        .set_request_protocol(PROTOCOL_RDMA);

    // Tag the bthread with this server's key for
    // brpc::thread_local_data().
    if (server->thread_local_options().thread_local_data_factory) {
        bthread_assign_data((void*)&server->thread_local_options());
    }

    Span* span = NULL;
    if (IsTraceable(request_meta.has_trace_id())) {
        span = Span::CreateServerSpan(
            request_meta.trace_id(), request_meta.span_id(),
            request_meta.parent_span_id(), msg->base_real_us());
        accessor.set_span(span);
        span->set_log_id(request_meta.log_id());
        span->set_remote_side(socket->remote_side());
        span->set_protocol(PROTOCOL_RDMA);
        span->set_received_us(msg->received_us());
        span->set_start_parse_us(start_parse_us);
        span->set_request_size(msg->payload.size() + msg->meta.size() + 12);
    }

    MethodStatus* method_status = NULL;
    do {
        if (!server->IsRunning()) {
            cntl->SetFailed(ELOGOFF, "Server is stopping");
            break;
        }
        
        if (!server_accessor.AddConcurrency(cntl.get())) {
            cntl->SetFailed(ELIMIT, "Reached server's max_concurrency=%d",
                            server->options().max_concurrency);
            break;
        }
        if (FLAGS_usercode_in_pthread && TooManyUserCode()) {
            cntl->SetFailed(ELIMIT, "Too many user code to run when"
                            " -usercode_in_pthread is on");
            break;
        }

        butil::StringPiece svc_name(request_meta.service_name());
        if (svc_name.find('.') == butil::StringPiece::npos) {
            const Server::ServiceProperty* sp =
                server_accessor.FindServicePropertyByName(svc_name);
            if (NULL == sp) {
                cntl->SetFailed(ENOSERVICE, "Fail to find service=%s",
                                request_meta.service_name().c_str());
                break;
            }
            svc_name = sp->service->GetDescriptor()->full_name();
        }
        const Server::MethodProperty* mp =
            server_accessor.FindMethodPropertyByFullName(
                svc_name, request_meta.method_name());
        if (NULL == mp) {
            LOG(DEBUG) << "Fail to find method";
            cntl->SetFailed(ENOMETHOD, "Fail to find method=%s/%s",
                            request_meta.service_name().c_str(),
                            request_meta.method_name().c_str());
            break;
        } else if (mp->service->GetDescriptor()
                   == BadMethodService::descriptor()) {
            LOG(DEBUG) << "Fail to find method";
            BadMethodRequest breq;
            BadMethodResponse bres;
            breq.set_service_name(request_meta.service_name());
            mp->service->CallMethod(mp->method, cntl.get(), &breq, &bres, NULL);
            break;
        }
        // Switch to service-specific error.
        non_service_error.release();
        method_status = mp->status;
        if (method_status) {
            if (!method_status->OnRequested()) {
                cntl->SetFailed(ELIMIT, "Reached %s's max_concurrency=%d",
                                mp->method->full_name().c_str(),
                                method_status->max_concurrency());
                break;
            }
        }
        google::protobuf::Service* svc = mp->service;
        const google::protobuf::MethodDescriptor* method = mp->method;
        accessor.set_method(method);
        if (span) {
            span->ResetServerSpanName(method->full_name());
        }

        const int reqsize = static_cast<int>(msg->payload.size());
        butil::IOBuf req_buf;
        butil::IOBuf* req_buf_ptr = &msg->payload;
        if (meta.has_attachment_size()) {
            if (reqsize < meta.attachment_size()) {
                cntl->SetFailed(EREQUEST,
                        "attachment_size=%d is larger than request_size=%d",
                        meta.attachment_size(), reqsize);
                break;
            }
            int att_size = reqsize - meta.attachment_size();
            msg->payload.cutn(&req_buf, att_size);
            req_buf_ptr = &req_buf;
            cntl->request_attachment().swap(msg->payload);
        }
        req.reset(svc->GetRequestPrototype(method).New());
        if (!ParsePbFromIOBuf(req.get(), *req_buf_ptr)) {
            cntl->SetFailed(EREQUEST, 
                    "Fail to parse request message, request_size=%d",
                    reqsize);
            break;
        }

        // optional, just release resourse ASAP
        msg.reset();
        req_buf.clear();

        // `socket' will be held until response has been sent
        res.reset(svc->GetResponsePrototype(method).New());
        google::protobuf::Closure* done = brpc::NewCallback<
            int64_t, Controller*, const google::protobuf::Message*,
            const google::protobuf::Message*, Socket*, const Server*,
            MethodStatus*, long>(
                &SendRdmaRpcResponse, meta.correlation_id(), cntl.get(), 
                req.get(), res.get(), socket.release(), server,
                method_status, start_parse_us, from_rdma);
        if (span) {
            span->set_start_callback_us(butil::cpuwide_time_us());
            span->AsParent();
        }
        if (!FLAGS_usercode_in_pthread) {
            return svc->CallMethod(method, cntl.release(), 
                                   req.release(), res.release(), done);
        }
        if (BeginRunningUserCode()) {
            svc->CallMethod(method, cntl.release(), 
                            req.release(), res.release(), done);
            return EndRunningUserCodeInPlace();
        } else {
            return EndRunningCallMethodInPool(
                svc, method, cntl.release(),
                req.release(), res.release(), done);
        }
    } while (false);
    
    // `cntl', `req' and `res' will be deleted inside `SendRpcResponse'
    // `socket' will be held until response has been sent
    SendRdmaRpcResponse(meta.correlation_id(), cntl.release(), 
                        req.release(), res.release(), socket.release(), server,
                        method_status, -1, from_rdma);
}

void ProcessRdmaRpcResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<RdmaMessage> msg(static_cast<RdmaMessage*>(msg_base));

    RdmaRpcMeta meta;
    if (!ParsePbFromIOBuf(&meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse from response meta";
        return;
    }

    const bthread_id_t cid = { static_cast<uint64_t>(meta.correlation_id()) };
    Controller* cntl = NULL;
    const int rc = bthread_id_lock(cid, (void**)&cntl);
    if (rc != 0) {
        LOG_IF(ERROR, rc != EINVAL && rc != EPERM)
            << "Fail to lock correlation_id=" << cid << ": " << berror(rc);
        return;
    }
    
    ControllerPrivateAccessor accessor(cntl);
    Span* span = accessor.span();
    if (span) {
        span->set_base_real_us(msg->base_real_us());
        span->set_received_us(msg->received_us());
        span->set_response_size(msg->payload.size() + msg->meta.size() + 12);
        span->set_start_parse_us(start_parse_us);
    }
    const RdmaRpcResponseMeta &response_meta = meta.response();
    const int saved_error = cntl->ErrorCode();
    do {
        if (response_meta.error_code() != 0) {
            // If error_code is unset, default is 0 = success.
            cntl->SetFailed(response_meta.error_code(), 
                                  "%s", response_meta.error_text().c_str());
            break;
        } 

        butil::IOBuf res_buf;
        const int res_size = msg->payload.length();
        butil::IOBuf* res_buf_ptr = &msg->payload;
        if (meta.attachment_size()) {
            if (meta.attachment_size() > res_size) {
                cntl->SetFailed(ERESPONSE,
                        "attachment_size=%d is larger than "
                        "response_size=%d",
                        meta.attachment_size(), res_size);
                break;
            }
            int att_size = res_size - meta.attachment_size();
            msg->payload.cutn(&res_buf, att_size);
            res_buf_ptr = &res_buf;
            cntl->response_attachment().swap(msg->payload);
        }
        if (cntl->response()) {
            if (!ParsePbFromIOBuf(cntl->response(), *res_buf_ptr)) {
                cntl->SetFailed(EREQUEST,
                        "Fail to parse request message, "
                        "request_size=%d", res_size);
            }
        } // else silently ignore the response
    } while (0);
    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

void PackRdmaRpcRequest(butil::IOBuf* req_buf,
                        SocketMessage**,
                        uint64_t correlation_id,
                        const google::protobuf::MethodDescriptor* method,
                        Controller* cntl,
                        const butil::IOBuf& request_body,
                        const Authenticator* auth) {
    RdmaRpcMeta meta;
    if (auth && auth->GenerateCredential(
                meta.mutable_authentication_data()) != 0) {
        return cntl->SetFailed(EREQUEST, "Fail to generate credential");
    }

    ControllerPrivateAccessor accessor(cntl);
    RdmaRpcRequestMeta* request_meta = meta.mutable_request();
    if (method) {
        request_meta->set_service_name(FLAGS_baidu_protocol_use_fullname ?
                                       method->service()->full_name() :
                                       method->service()->name());
        request_meta->set_method_name(method->name());
    } else if (cntl->rpc_dump_meta()) {
        // Replaying. Keep service-name as the one seen by server.
        request_meta->set_service_name(cntl->rpc_dump_meta()->service_name());
        request_meta->set_method_name(cntl->rpc_dump_meta()->method_name());
    } else {
        return cntl->SetFailed(ENOMETHOD, "%s.method is NULL", __FUNCTION__);
    }
    if (cntl->has_log_id()) {
        request_meta->set_log_id(cntl->log_id());
    }
    meta.set_correlation_id(correlation_id);

    const size_t req_size = request_body.length(); 
    size_t attached_size = cntl->request_attachment().length();
    if (attached_size) {
        meta.set_attachment_size(attached_size);
    }

    Span* span = accessor.span();
    if (span) {
        request_meta->set_trace_id(span->trace_id());
        request_meta->set_span_id(span->span_id());
        request_meta->set_parent_span_id(span->parent_span_id());
    }

    if (!SerializeRpcHeaderAndMeta(req_buf, meta, req_size + attached_size)) {
        cntl->SetFailed(EREQUEST, "Fail to serialize header and meta");
        return;
    }
    req_buf->append(request_body);
    if (attached_size) {
        req_buf->append(cntl->request_attachment());
    }
}

bool VerifyRdmaRpcRequest(const InputMessageBase* msg_base) {
    const RdmaMessage* msg =
        static_cast<const RdmaMessage*>(msg_base);
    const Server* server = static_cast<const Server*>(msg->arg());
    Socket* socket = msg->socket();

    RdmaRpcMeta meta;
    if (!ParsePbFromIOBuf(&meta, msg->meta)) {
        LOG(WARNING) << "Fail to parse RpcRequestMeta";
        return false;
    }
    const Authenticator* auth = server->options().auth;
    if (NULL == auth) {
        // Fast pass (no authentication)
        return true;
    }
    if (auth->VerifyCredential(
                meta.authentication_data(), socket->remote_side(),
                socket->mutable_auth_context()) != 0) {
        return false;
    }
    return true;
}

}  // namespace policy
}  // namespace brpc

#endif

