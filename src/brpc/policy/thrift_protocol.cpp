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

// Authors: Ge,Jun (gejun@baidu.com)

#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <gflags/gflags.h>
#include "butil/time.h" 
#include "butil/iobuf.h"                         // butil::IOBuf
#include "brpc/log.h"
#include "brpc/controller.h"               // Controller
#include "brpc/socket.h"                   // Socket
#include "brpc/server.h"                   // Server
#include "brpc/span.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/thrift_service.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/thrift_protocol.h"
#include "brpc/details/usercode_backup_pool.h"

extern "C" {
void bthread_assign_data(void* data) __THROW;
}


namespace brpc {

ThriftFramedClosure::ThriftFramedClosure(void* additional_space)
    : _socket_ptr(NULL)
    , _server(NULL)
    , _start_parse_us(0)
    , _do_respond(true)
    , _additional_space(additional_space) {
}

ThriftFramedClosure::~ThriftFramedClosure() {
    LogErrorTextAndDelete(false)(&_controller);
}

void ThriftFramedClosure::DoNotRespond() {
    _do_respond = false;
}

class DeleteThriftFramedClosure {
public:
    void operator()(ThriftFramedClosure* done) const {
        done->~ThriftFramedClosure();
        free(done);
    }
};

void ThriftFramedClosure::Run() {
    // Recycle itself after `Run'
    std::unique_ptr<ThriftFramedClosure, DeleteThriftFramedClosure> recycle_ctx(this);
    SocketUniquePtr sock(_socket_ptr);
    ScopedRemoveConcurrency remove_concurrency_dummy(_server, &_controller);

    ControllerPrivateAccessor accessor(&_controller);
    Span* span = accessor.span();
    if (span) {
        span->set_start_send_us(butil::cpuwide_time_us());
    }
    ScopedMethodStatus method_status(_server->options().thrift_service->_status);
    if (!method_status) {
        // Judge errors belongings.
        // may not be accurate, but it does not matter too much.
        const int error_code = _controller.ErrorCode();
        if (error_code == ENOSERVICE ||
            error_code == ENOMETHOD ||
            error_code == EREQUEST ||
            error_code == ECLOSE ||
            error_code == ELOGOFF ||
            error_code == ELIMIT) {
            ServerPrivateAccessor(_server).AddError();
        }
    }

    if (_controller.IsCloseConnection()) {
        sock->SetFailed();
        return;
    }

    if (_do_respond) {
        // response uses request's head as default.
        _response.head = _request.head;
        uint32_t length = _response.body.length();
        _response.head.body_len = htonl(length);
    
        if (span) {
            int response_size = sizeof(thrift_binary_head_t) + _response.head.body_len;
            span->set_response_size(response_size);
        }
        butil::IOBuf write_buf;
        write_buf.append(&_response.head, sizeof(thrift_binary_head_t));
        write_buf.append(_response.body.movable());
        // Have the risk of unlimited pending responses, in which case, tell
        // users to set max_concurrency.
        Socket::WriteOptions wopt;
        wopt.ignore_eovercrowded = true;
        if (sock->Write(&write_buf, &wopt) != 0) {
            const int errcode = errno;
            PLOG_IF(WARNING, errcode != EPIPE) << "Fail to write into " << *sock;
            _controller.SetFailed(errcode, "Fail to write into %s",
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
            !_controller.Failed(), butil::cpuwide_time_us() - cpuwide_start_us());
    }
}

void ThriftFramedClosure::SetMethodName(const std::string& full_method_name) {
    ControllerPrivateAccessor accessor(&_controller);
    Span* span = accessor.span();
    if (span) {
        span->ResetServerSpanName(full_method_name);
    }
}

namespace policy {

ParseResult ParseThriftBinaryMessage(butil::IOBuf* source,
                               Socket*, bool /*read_eof*/, const void* /*arg*/) {

    char header_buf[sizeof(thrift_binary_head_t) + 3];
    const size_t n = source->copy_to(header_buf, sizeof(thrift_binary_head_t) + 3);

    if (n < 7) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    const void* dummy = header_buf + sizeof(thrift_binary_head_t);
    const int32_t sz = ntohl(*(int32_t*)dummy);
    int32_t version = sz & VERSION_MASK;
    if (version != VERSION_1) {
        RPC_VLOG << "magic_num=" << version
                 << " doesn't match THRIFT_MAGIC_NUM=" << VERSION_1;
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }

    thrift_binary_head_t* thrift = (thrift_binary_head_t *)header_buf;
    thrift->body_len = ntohl(thrift->body_len);
    uint32_t body_len = thrift->body_len;
    if (body_len > FLAGS_max_body_size) {
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(header_buf) + body_len - 3) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    policy::MostCommonMessage* msg = policy::MostCommonMessage::Get();
    source->cutn(&msg->meta, sizeof(thrift_binary_head_t));
    source->cutn(&msg->payload, body_len);
    return MakeMessage(msg);
}

struct CallMethodInBackupThreadArgs {
    ThriftFramedService* service;
    const Server* server;
    Controller* controller;
    const ThriftBinaryMessage* request;
    ThriftBinaryMessage* response;
    ThriftFramedClosure* done;
};

static void CallMethodInBackupThread(void* void_args) {
    CallMethodInBackupThreadArgs* args = (CallMethodInBackupThreadArgs*)void_args;
    args->service->ProcessThriftBinaryRequest(*args->server, args->controller,
                                        *args->request, args->response,
                                        args->done);
    delete args;
}

static void EndRunningCallMethodInPool(ThriftFramedService* service,
                                       const Server& server,
                                       Controller* controller,
                                       const ThriftBinaryMessage& request,
                                       ThriftBinaryMessage* response,
                                       ThriftFramedClosure* done) {
    CallMethodInBackupThreadArgs* args = new CallMethodInBackupThreadArgs;
    args->service = service;
    args->server = &server;
    args->controller = controller;
    args->request = &request;
    args->response = response;
    args->done = done;
    return EndRunningUserCodeInPool(CallMethodInBackupThread, args);
};

void ProcessThriftBinaryRequest(InputMessageBase* msg_base) {

    const int64_t start_parse_us = butil::cpuwide_time_us();   

    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket(msg->ReleaseSocket());
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);

    char buf[sizeof(thrift_binary_head_t)];
    const char *p = (const char *)msg->meta.fetch(buf, sizeof(buf));
    thrift_binary_head_t *req_head = (thrift_binary_head_t *)p;
    req_head->body_len = ntohl(req_head->body_len);

    ThriftFramedService* service = server->options().thrift_service;
    if (service == NULL) {
        LOG_EVERY_SECOND(WARNING) 
            << "Received thrift request however the server does not set"
            " ServerOptions.thrift_service, close the connection.";
        socket->SetFailed();
        return;
    }

    void* space = malloc(sizeof(ThriftFramedClosure) + service->_additional_space);
    if (!space) {
        LOG(FATAL) << "Fail to new ThriftFramedClosure";
        socket->SetFailed();
        return;
    }

    // Switch to service-specific error.
    non_service_error.release();
    MethodStatus* method_status = service->_status;
    if (method_status) {
        CHECK(method_status->OnRequested());
    }
    
    void* sub_space = NULL;
    if (service->_additional_space) {
        sub_space = (char*)space + sizeof(ThriftFramedClosure);
    }
    ThriftFramedClosure* thrift_done = new (space) ThriftFramedClosure(sub_space);
    Controller* cntl = &(thrift_done->_controller);
    ThriftBinaryMessage* req = &(thrift_done->_request);
    ThriftBinaryMessage* res = &(thrift_done->_response);

    req->head = *req_head;
    msg->payload.swap(req->body);
    thrift_done->_start_parse_us = start_parse_us;
    thrift_done->_socket_ptr = socket.get();
    thrift_done->_server = server;
    
    ServerPrivateAccessor server_accessor(server);
    ControllerPrivateAccessor accessor(cntl);
    const bool security_mode = server->options().security_mode() &&
                               socket->user() == server_accessor.acceptor();
    // Initialize log_id with the log_id in thrift. Notice that the protocols
    // on top of ThriftFramedService may pack log_id in meta or user messages and
    // overwrite the value.
    //cntl->set_log_id(req_head->log_id);
    accessor.set_server(server)
        .set_security_mode(security_mode)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_request_protocol(PROTOCOL_NSHEAD);

    // Tag the bthread with this server's key for thread_local_data().
    if (server->thread_local_options().thread_local_data_factory) {
        bthread_assign_data((void*)&server->thread_local_options());
    }

    Span* span = NULL;
    if (IsTraceable(false)) {
        span = Span::CreateServerSpan(0, 0, 0, msg->base_real_us());
        accessor.set_span(span);
        //span->set_log_id(req_head->log_id);
        span->set_remote_side(cntl->remote_side());
        span->set_protocol(PROTOCOL_NSHEAD);
        span->set_received_us(msg->received_us());
        span->set_start_parse_us(start_parse_us);
        span->set_request_size(sizeof(thrift_binary_head_t) + req_head->body_len);
    }

    do {
        if (!server->IsRunning()) {
            cntl->SetFailed(ELOGOFF, "Server is stopping");
            break;
        }
        if (!server_accessor.AddConcurrency(cntl)) {
            cntl->SetFailed(ELIMIT, "Reached server's max_concurrency=%d",
                            server->options().max_concurrency);
            break;
        }
        if (FLAGS_usercode_in_pthread && TooManyUserCode()) {
            cntl->SetFailed(ELIMIT, "Too many user code to run when"
                            " -usercode_in_pthread is on");
            break;
        }
    } while (false);

    msg.reset();  // optional, just release resourse ASAP
    // `socket' will be held until response has been sent
    socket.release();
    if (span) {
        span->ResetServerSpanName(service->_cached_name);
        span->set_start_callback_us(butil::cpuwide_time_us());
        span->AsParent();
    }
    if (!FLAGS_usercode_in_pthread) {
        return service->ProcessThriftBinaryRequest(*server, cntl, *req, res, thrift_done);
    }
    if (BeginRunningUserCode()) {
        service->ProcessThriftBinaryRequest(*server, cntl, *req, res, thrift_done);
        return EndRunningUserCodeInPlace();
    } else {
        return EndRunningCallMethodInPool(
            service, *server, cntl, *req, res, thrift_done);
    }

}

void ProcessThriftBinaryResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    
    // Fetch correlation id that we saved before in `PacThriftBinaryRequest'
    const CallId cid = { static_cast<uint64_t>(msg->socket()->correlation_id()) };
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
        span->set_response_size(msg->payload.length());
        span->set_start_parse_us(start_parse_us);
    }

    // MUST be ThriftBinaryMessage (checked in SerializeThriftBinaryRequest)
    ThriftBinaryMessage* response = (ThriftBinaryMessage*)cntl->response();
    const int saved_error = cntl->ErrorCode();
    if (response != NULL) {
        msg->meta.copy_to(&response->head, sizeof(thrift_binary_head_t));
        response->head.body_len = ntohl(response->head.body_len);
        msg->payload.swap(response->body);
    } // else just ignore the response.

    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

bool VerifyThriftBinaryRequest(const InputMessageBase* msg_base) {
    Server* server = (Server*)msg_base->arg();
    if (server->options().auth) {
        LOG(WARNING) << "thrift does not support authentication";
        return false;
    }
    return true;
}

void SerializeThriftBinaryRequest(butil::IOBuf* request_buf, Controller* cntl,
                            const google::protobuf::Message* req_base) {
    if (req_base == NULL) {
        return cntl->SetFailed(EREQUEST, "request is NULL");
    }
    ControllerPrivateAccessor accessor(cntl);

    const ThriftBinaryMessage* req = (const ThriftBinaryMessage*)req_base;
    thrift_binary_head_t head = req->head;

    head.body_len = ntohl(req->body.size());
    request_buf->append(&head, sizeof(head));
    request_buf->append(req->body);
}

void PackThriftBinaryRequest(
    butil::IOBuf* packet_buf,
    SocketMessage**,
    uint64_t correlation_id,
    const google::protobuf::MethodDescriptor*,
    Controller* cntl,
    const butil::IOBuf& request,
    const Authenticator*) {
    ControllerPrivateAccessor accessor(cntl);
    if (accessor.connection_type() == CONNECTION_TYPE_SINGLE) {
        return cntl->SetFailed(
            EINVAL, "thrift protocol can't work with CONNECTION_TYPE_SINGLE");
    }
    // Store `correlation_id' into the socket since thrift protocol can't
    // pack the field.
    accessor.get_sending_socket()->set_correlation_id(correlation_id);

    Span* span = accessor.span();
    if (span) {
        span->set_request_size(request.length());
        // TODO: Nowhere to set tracing ids.
        // request_meta->set_trace_id(span->trace_id());
        // request_meta->set_span_id(span->span_id());
        // request_meta->set_parent_span_id(span->parent_span_id());
    }
    packet_buf->append(request);
}

} // namespace policy
} // namespace brpc
