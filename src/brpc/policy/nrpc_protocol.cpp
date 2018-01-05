// Copyright (c) 2014 Baidu, Inc.
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

// Authors: Kevin.XU (xuhuahai@sogou-inc.com)

#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/io/coded_stream.h>
#include "butil/logging.h"                       // LOG()
#include "butil/time.h"
#include "butil/iobuf.h"                         // butil::IOBuf
#include "butil/raw_pack.h"                      // RawPacker RawUnpacker
#include "brpc/controller.h"                    // Controller
#include "brpc/socket.h"                        // Socket
#include "brpc/server.h"                        // Server
#include "brpc/span.h"
#include "brpc/compress.h"                      // ParseFromCompressedData
#include "brpc/stream_impl.h"
#include "brpc/rpc_dump.h"                      // SampledRequest
#include "brpc/policy/nrpc.pb.h"      // RpcRequestMeta
#include "brpc/policy/nrpc_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/streaming_rpc_protocol.h"
#include "brpc/details/usercode_backup_pool.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/server_private_accessor.h"

extern "C" {
void bthread_assign_data(void* data) __THROW;
}


namespace brpc {
namespace policy {


// Notes:
// 1. 8-byte header [PRPC][body_size][meta_size]
// 2. body_size and meta_size are in network byte order
// 3. Use service->full_name() + method_name to specify the method to call
// 4. `attachment_size' is set iff request/response has attachment
// 5. Not supported: chunk_info

const int HEADER_LEN = 12;

enum NRPC_MESSAGE_TYPE {
  NRPC_REQUEST = 0,
  NRPC_RESPONSE = 1
};

// Pack header into `buf'
static void PackRpcHeader(char* rpc_header, NRPC_MESSAGE_TYPE messge_type, int payload_size) {
    // supress strict-aliasing warning.
    *rpc_header       = 'N';
    *(rpc_header + 1) = 'R';
    *(rpc_header + 2) = 'P';
    *(rpc_header + 3) = 'C';
    butil::RawPacker(rpc_header+4).pack32(payload_size + HEADER_LEN);
    *(rpc_header + 8) = messge_type;
    *(rpc_header + 9) = 0;
    *(rpc_header + 10) = 0;
    *(rpc_header + 11) = 0;
}

static void SerializeRpcHeader(
    butil::IOBuf* out, NRPC_MESSAGE_TYPE messge_type, int payload_size) {
    char header[HEADER_LEN];
    memset(header,0,HEADER_LEN);
    PackRpcHeader(header, messge_type, payload_size);
    out->append(header, sizeof(header));
}

ParseResult ParseNrpcMessage(butil::IOBuf* source, Socket* socket,
                            bool read_eof, const void*) {
    
    char header_buf[HEADER_LEN];
    const size_t n = source->copy_to(header_buf, sizeof(header_buf));

    if (n >= 4) {
        void* dummy = header_buf;
        if (*(const uint32_t*)dummy != *(const uint32_t*)"NRPC") {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    } else {
        if (memcmp(header_buf, "NRPC", n) != 0) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
    }
    if (n < sizeof(header_buf)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    uint32_t body_size;
    butil::RawUnpacker(header_buf + 4).unpack32(body_size);
    //char msg_type = *(header_buf + 8);

    if (body_size > FLAGS_max_body_size) {
        // We need this log to report the body_size to give users some clues
        // which is not printed in InputMessenger.
        LOG(ERROR) << "body_size=" << body_size << " from "
                   << socket->remote_side() << " is too large";
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < body_size) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    source->pop_front(sizeof(header_buf));
    MostCommonMessage* msg = MostCommonMessage::Get();
    source->cutn(&msg->meta, body_size - HEADER_LEN);
    //LOG(WARNING) << "ParseNrpcMessage";
    return MakeMessage(msg);
}

// Used by UT, can't be static.
void SendNrpcResponse(int64_t correlation_id,
                     Controller* cntl, 
                     const google::protobuf::Message* req,
                     const google::protobuf::Message* res,
                     Socket* socket_raw,
                     const Server* server,
                     MethodStatus* method_status_raw,
                     long start_parse_us) {
                        
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
    
    StreamId response_stream_id = accessor.response_stream();

    if (cntl->IsCloseConnection()) {
        StreamClose(response_stream_id);
        sock->SetFailed();
        return;
    }
    bool append_body = false;
    butil::IOBuf res_body;
    // `res' can be NULL here, in which case we don't serialize it
    // If user calls `SetFailed' on Controller, we don't serialize
    // response either
    CompressType type = cntl->response_compress_type();
    if (res != NULL && !cntl->Failed()) {
        if (!res->IsInitialized()) {
            cntl->SetFailed(
                ERESPONSE, "Missing required fields in response: %s", 
                res->InitializationErrorString().c_str());
        } else if (!SerializeAsCompressedData(*res, &res_body, type)) {
            cntl->SetFailed(ERESPONSE, "Fail to serialize response, "
                            "CompressType=%s", CompressTypeToCStr(type));
        } else {
            append_body = true;
        }
    }

    // Don't use res->ByteSize() since it may be compressed

    int error_code = cntl->ErrorCode();
    if (error_code == -1) {
        // replace general error (-1) with INTERNAL_SERVER_ERROR to make a
        // distinction between server error and client error
        error_code = EINTERNAL;
    }

    sogou::nlu::rpc::NrpcMeta nrpcMeta;
    sogou::nlu::rpc::NrpcResponseMeta* response_meta = nrpcMeta.mutable_response();
    response_meta->set_error_code(error_code);
    if (!cntl->ErrorText().empty()) {
        // Only set error_text when it's not empty since protobuf Message
        // always new the string no matter if it's empty or not.
        response_meta->set_error_text(cntl->ErrorText());
    }
    nrpcMeta.set_correlation_id(correlation_id);

    SocketUniquePtr stream_ptr;
    if (response_stream_id != INVALID_STREAM_ID) {
        if (Socket::Address(response_stream_id, &stream_ptr) == 0) {
            Stream* s = (Stream*)stream_ptr->conn();
            s->SetHostSocket(sock.get());
        } else {
            LOG(WARNING) << "Stream=" << response_stream_id 
                         << " was closed before sending response";
        }
    }

    butil::IOBuf res_buf;
    butil::IOBufAsZeroCopyOutputStream response_stream(&res_buf);

    // Copy resonse proto bytes into response_meta
    if(append_body){
        std::string* response_str = response_meta->mutable_response_body();
        res_body.copy_to(response_str);
    }

    // Write header
    const size_t res_size = nrpcMeta.ByteSizeLong(); 
    SerializeRpcHeader(&res_buf, NRPC_RESPONSE, res_size);

    // Serialize & copy nrpcMeta into res_buf
    if (!nrpcMeta.SerializeToZeroCopyStream(&response_stream)) {
        cntl->SetFailed(ERESPONSE, "Fail to serialize sogou::nlu::rpc::Response");
        return;
    }

    if (stream_ptr) {
        CHECK(accessor.remote_stream_settings() != NULL);
        // Send the response over stream to notify that this stream connection
        // is successfully built.
        if (SendStreamData(sock.get(), &res_buf, 
                           accessor.remote_stream_settings()->stream_id(),
                           accessor.response_stream()) != 0) {
            const int errcode = errno;
            PLOG_IF(WARNING, errcode != EPIPE) << "Fail to write into " << *sock;
            cntl->SetFailed(errcode, "Fail to write into %s",
                            sock->description().c_str());
            ((Stream*)stream_ptr->conn())->Close();
            return;
        }
        // Now it's ok the mark this server-side stream as connectted as all the
        // written user data would follower the RPC response.
        ((Stream*)stream_ptr->conn())->SetConnected();
    } else {
        // Have the risk of unlimited pending responses, in which case, tell
        // users to set max_concurrency.
        Socket::WriteOptions wopt;
        wopt.ignore_eovercrowded = true;
        if (sock->Write(&res_buf, &wopt) != 0) {
            const int errcode = errno;
            PLOG_IF(WARNING, errcode != EPIPE) << "Fail to write into " << *sock;
            cntl->SetFailed(errcode, "Fail to write into %s",
                            sock->description().c_str());
            return;
        }
    }

    if (span) {
        // TODO: this is not sent
        span->set_sent_us(butil::cpuwide_time_us());
    }

    if (method_status) {
        method_status.release()->OnResponded(
            !cntl->Failed(), butil::cpuwide_time_us() - start_parse_us);
    }
    //LOG(WARNING) << "SendNrpcResponse";
}

struct CallMethodInBackupThreadArgs2 {
    ::google::protobuf::Service* service;
    const ::google::protobuf::MethodDescriptor* method;
    ::google::protobuf::RpcController* controller;
    const ::google::protobuf::Message* request;
    ::google::protobuf::Message* response;
    ::google::protobuf::Closure* done;
};

static void CallMethodInBackupThread2(void* void_args) {
    CallMethodInBackupThreadArgs2* args = (CallMethodInBackupThreadArgs2*)void_args;
    args->service->CallMethod(args->method, args->controller, args->request,
                              args->response, args->done);
    delete args;
}

// Used by other protocols as well.
void EndRunningCallMethodInPool2(
    ::google::protobuf::Service* service,
    const ::google::protobuf::MethodDescriptor* method,
    ::google::protobuf::RpcController* controller,
    const ::google::protobuf::Message* request,
    ::google::protobuf::Message* response,
    ::google::protobuf::Closure* done) {
    CallMethodInBackupThreadArgs2* args = new CallMethodInBackupThreadArgs2;
    args->service = service;
    args->method = method;
    args->controller = controller;
    args->request = request;
    args->response = response;
    args->done = done;
    return EndRunningUserCodeInPool(CallMethodInBackupThread2, args);
};

void ProcessNrpcRequest(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket(msg->ReleaseSocket());
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);

    sogou::nlu::rpc::NrpcMeta nrpcMeta;
    if (!ParsePbFromIOBuf(&nrpcMeta, msg->meta)) {
        LOG(WARNING) << "Fail to parse RpcMeta from " << *socket;
        socket->SetFailed(EREQUEST, "Fail to parse RpcMeta from %s",
                          socket->description().c_str());
        return;
    }
    const sogou::nlu::rpc::NrpcRequestMeta &request_meta = nrpcMeta.request();

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
        .set_request_protocol(PROTOCOL_NRPC);    

    // Tag the bthread with this server's key for thread_local_data().
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
        span->set_protocol(PROTOCOL_BAIDU_STD);
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

        // NOTE(gejun): jprotobuf sends service names without packages. So the
        // name should be changed to full when it's not.
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
            cntl->SetFailed(ENOMETHOD, "Fail to find method=%s/%s",
                            request_meta.service_name().c_str(),
                            request_meta.method_name().c_str());
            break;
        } else if (mp->service->GetDescriptor()
                   == BadMethodService::descriptor()) {
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
        const std::string& req_data = request_meta.request_body();
        const int req_size = req_data.size();
        req.reset(svc->GetRequestPrototype(method).New());
        if (!ParsePbFromString(req.get(), req_data)) {
            cntl->SetFailed(EREQUEST, "Fail to parse request message, "
                            "request_size=%d", 
                            req_size);
            break;
        }
        
        // optional, just release resourse ASAP
        msg.reset();

        res.reset(svc->GetResponsePrototype(method).New());
        // `socket' will be held until response has been sent
        google::protobuf::Closure* done = ::brpc::NewCallback<
            int64_t, Controller*, const google::protobuf::Message*,
            const google::protobuf::Message*, Socket*, const Server*,
            MethodStatus*, long>(
                &SendNrpcResponse, nrpcMeta.correlation_id(), cntl.get(), 
                req.get(), res.get(), socket.release(), server,
                method_status, start_parse_us);

        if (!FLAGS_usercode_in_pthread) {
            return svc->CallMethod(method, cntl.release(), 
                                   req.release(), res.release(), done);
        }
        if (BeginRunningUserCode()) {
            svc->CallMethod(method, cntl.release(), 
                            req.release(), res.release(), done);
            return EndRunningUserCodeInPlace();
        } else {
            return EndRunningCallMethodInPool2(
                svc, method, cntl.release(),
                req.release(), res.release(), done);
        }
    } while (false);
    
    // `cntl', `req' and `res' will be deleted inside `SendRpcResponse'
    // `socket' will be held until response has been sent
    SendNrpcResponse(nrpcMeta.correlation_id(), cntl.release(), 
                    req.release(), res.release(), socket.release(), server,
                    method_status, -1);
    //LOG(WARNING) << "ProcessNrpcRequest";
}

bool VerifyNrpcRequest(const InputMessageBase* msg_base) {
    return true;
}

void ProcessNrpcResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));

    sogou::nlu::rpc::NrpcMeta nrpcMeta;
    if (!ParsePbFromIOBuf(&nrpcMeta, msg->meta)) {
        LOG(WARNING) << "Fail to parse from response meta";
        return;
    }

    // Fetch correlation id that we saved before in `PackUbrpcRequest'
    const bthread_id_t cid = { static_cast<uint64_t>(nrpcMeta.correlation_id()) };
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
        span->set_response_size(msg->meta.size() + msg->payload.size() + 12);
        span->set_start_parse_us(start_parse_us);
    }
    const sogou::nlu::rpc::NrpcResponseMeta &response_meta = nrpcMeta.response();
    const int saved_error = cntl->ErrorCode();
    do {
        if (response_meta.error_code() != 0) {
            // If error_code is unset, default is 0 = success.
            cntl->SetFailed(response_meta.error_code(), 
                                  "%s", response_meta.error_text().c_str());
            break;
        }
        // Parse response message iff error code from meta is 0
        const std::string& res_data = response_meta.response_body();
        const int resp_size = res_data.size();
        if (cntl->response()) {
            if (!ParsePbFromString(cntl->response(), res_data)) {
                cntl->SetFailed(
                    ERESPONSE, "Fail to parse response message, "
                    "response_size=%d", 
                    resp_size);
            }
        } // else silently ignore the response.        
    } while (0);
    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
    //LOG(WARNING) << "ProcessNrpcResponse";    
}

void PackNrpcRequest(butil::IOBuf* req_buf,
                    SocketMessage**,
                    uint64_t correlation_id,
                    const google::protobuf::MethodDescriptor* method,
                    Controller* cntl,
                    const butil::IOBuf& request_body,
                    const Authenticator* auth) {
    
    ControllerPrivateAccessor accessor(cntl);
    butil::IOBufAsZeroCopyOutputStream request_stream(req_buf);

    sogou::nlu::rpc::NrpcMeta nrpcMeta;
    nrpcMeta.set_correlation_id(correlation_id);
    sogou::nlu::rpc::NrpcRequestMeta* request_meta = nrpcMeta.mutable_request();
    request_meta->set_service_name(method->service()->full_name());
    request_meta->set_method_name(method->name());
    // Copy request_body into request_meta
    std::string* request_str = request_meta->mutable_request_body();
    request_body.copy_to(request_str);

    if (cntl->has_log_id()) {
        request_meta->set_log_id(cntl->log_id());
    }

    Span* span = accessor.span();
    if (span) {
        request_meta->set_trace_id(span->trace_id());
        request_meta->set_span_id(span->span_id());
        request_meta->set_parent_span_id(span->parent_span_id());
    }

    // Write header into req_buf
    const size_t req_size = nrpcMeta.ByteSizeLong(); 
    SerializeRpcHeader(req_buf, NRPC_REQUEST, req_size);
    
    // Serialize & copy nrpcrequest into req_buf
    if (!nrpcMeta.SerializeToZeroCopyStream(&request_stream)) {
        cntl->SetFailed(EREQUEST, "Fail to serialize sogou::nlu::rpc::NrpcMeta");
        return;
    }

}

}  // namespace policy
} // namespace brpc
