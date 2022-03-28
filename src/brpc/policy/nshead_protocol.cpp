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
#include "brpc/rpc_dump.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/nshead_service.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/nshead_protocol.h"
#include "brpc/details/usercode_backup_pool.h"

extern "C" {
void bthread_assign_data(void* data);
}


namespace brpc {

NsheadClosure::NsheadClosure(void* additional_space)
    : _server(NULL)
    , _received_us(0)
    , _do_respond(true)
    , _additional_space(additional_space) {
}

NsheadClosure::~NsheadClosure() {
    LogErrorTextAndDelete(false)(&_controller);
}

void NsheadClosure::DoNotRespond() {
    _do_respond = false;
}

class DeleteNsheadClosure {
public:
    void operator()(NsheadClosure* done) const {
        done->~NsheadClosure();
        free(done);
    }
};

void NsheadClosure::Run() {
    // Recycle itself after `Run'
    std::unique_ptr<NsheadClosure, DeleteNsheadClosure> recycle_ctx(this);

    ControllerPrivateAccessor accessor(&_controller);
    Span* span = accessor.span();
    if (span) {
        span->set_start_send_us(butil::cpuwide_time_us());
    }
    Socket* sock = accessor.get_sending_socket();
    MethodStatus* method_status = _server->options().nshead_service->_status;
    ConcurrencyRemover concurrency_remover(method_status, &_controller, _received_us);
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
        // Notice that the response use request.head.log_id directly rather
        // than _controller.log_id() which may be different and packed in
        // the meta or user messages.
        _response.head = _request.head;
        _response.head.magic_num = NSHEAD_MAGICNUM;
        _response.head.body_len = _response.body.length();
        if (span) {
            int response_size = sizeof(nshead_t) + _response.head.body_len;
            span->set_response_size(response_size);
        }
        butil::IOBuf write_buf;
        write_buf.append(&_response.head, sizeof(nshead_t));
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
}

void NsheadClosure::SetMethodName(const std::string& full_method_name) {
    ControllerPrivateAccessor accessor(&_controller);
    Span* span = accessor.span();
    if (span) {
        span->ResetServerSpanName(full_method_name);
    }
}

namespace policy {

ParseResult ParseNsheadMessage(butil::IOBuf* source,
                               Socket*, bool /*read_eof*/, const void* /*arg*/) {
    char header_buf[sizeof(nshead_t)];
    const size_t n = source->copy_to(header_buf, sizeof(header_buf));
    if (n < offsetof(nshead_t, magic_num) + 4) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    const void* dummy = header_buf + offsetof(nshead_t, magic_num);
    const unsigned int magic_num = *(unsigned int*)dummy;
    if (magic_num != NSHEAD_MAGICNUM) {
        RPC_VLOG << "magic_num=" << magic_num
                 << " doesn't match NSHEAD_MAGICNUM=" << NSHEAD_MAGICNUM;
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    if (n < sizeof(nshead_t)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    const nshead_t* nshead = (const nshead_t *)header_buf;
    uint32_t body_len = nshead->body_len;
    if (body_len > FLAGS_max_body_size) {
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(header_buf) + body_len) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    policy::MostCommonMessage* msg = policy::MostCommonMessage::Get();
    source->cutn(&msg->meta, sizeof(header_buf));
    source->cutn(&msg->payload, body_len);
    return MakeMessage(msg);
}

struct CallMethodInBackupThreadArgs {
    NsheadService* service;
    const Server* server;
    Controller* controller;
    const NsheadMessage* request;
    NsheadMessage* response;
    NsheadClosure* done;
};

static void CallMethodInBackupThread(void* void_args) {
    CallMethodInBackupThreadArgs* args = (CallMethodInBackupThreadArgs*)void_args;
    args->service->ProcessNsheadRequest(*args->server, args->controller,
                                        *args->request, args->response,
                                        args->done);
    delete args;
}

static void EndRunningCallMethodInPool(NsheadService* service,
                                       const Server& server,
                                       Controller* controller,
                                       const NsheadMessage& request,
                                       NsheadMessage* response,
                                       NsheadClosure* done) {
    CallMethodInBackupThreadArgs* args = new CallMethodInBackupThreadArgs;
    args->service = service;
    args->server = &server;
    args->controller = controller;
    args->request = &request;
    args->response = response;
    args->done = done;
    return EndRunningUserCodeInPool(CallMethodInBackupThread, args);
};

void ProcessNsheadRequest(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();   

    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket_guard(msg->ReleaseSocket());
    Socket* socket = socket_guard.get();
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);
    
    char buf[sizeof(nshead_t)];
    const char *p = (const char *)msg->meta.fetch(buf, sizeof(buf));
    const nshead_t *req_head = (const nshead_t *)p;

    NsheadService* service = server->options().nshead_service;
    if (service == NULL) {
        LOG_EVERY_SECOND(WARNING) 
            << "Received nshead request however the server does not set"
            " ServerOptions.nshead_service, close the connection.";
        socket->SetFailed();
        return;
    }
    void* space = malloc(sizeof(NsheadClosure) + service->_additional_space);
    if (!space) {
        LOG(FATAL) << "Fail to new NsheadClosure";
        socket->SetFailed();
        return;
    }

    // for nshead sample request
    SampledRequest* sample = AskToBeSampled();
    if (sample) {
        sample->meta.set_protocol_type(PROTOCOL_NSHEAD);
        sample->meta.set_nshead(p, sizeof(nshead_t)); // nshead
        sample->request = msg->payload;
        sample->submit(start_parse_us);
    }

    // Switch to service-specific error.
    non_service_error.release();
    MethodStatus* method_status = service->_status;
    if (method_status) {
        CHECK(method_status->OnRequested());
    }
    
    void* sub_space = NULL;
    if (service->_additional_space) {
        sub_space = (char*)space + sizeof(NsheadClosure);
    }
    NsheadClosure* nshead_done = new (space) NsheadClosure(sub_space);
    Controller* cntl = &(nshead_done->_controller);
    NsheadMessage* req = &(nshead_done->_request);
    NsheadMessage* res = &(nshead_done->_response);

    req->head = *req_head;
    msg->payload.swap(req->body);
    nshead_done->_received_us = msg->received_us();
    nshead_done->_server = server;
    
    ServerPrivateAccessor server_accessor(server);
    ControllerPrivateAccessor accessor(cntl);
    const bool security_mode = server->options().security_mode() &&
                               socket->user() == server_accessor.acceptor();
    // Initialize log_id with the log_id in nshead. Notice that the protocols
    // on top of NsheadService may pack log_id in meta or user messages and
    // overwrite the value.
    cntl->set_log_id(req_head->log_id);
    accessor.set_server(server)
        .set_security_mode(security_mode)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_request_protocol(PROTOCOL_NSHEAD)
        .set_begin_time_us(msg->received_us())
        .move_in_server_receiving_sock(socket_guard);

    // Tag the bthread with this server's key for thread_local_data().
    if (server->thread_local_options().thread_local_data_factory) {
        bthread_assign_data((void*)&server->thread_local_options());
    }

    Span* span = NULL;
    if (IsTraceable(false)) {
        span = Span::CreateServerSpan(0, 0, 0, msg->base_real_us());
        accessor.set_span(span);
        span->set_log_id(req_head->log_id);
        span->set_remote_side(cntl->remote_side());
        span->set_protocol(PROTOCOL_NSHEAD);
        span->set_received_us(msg->received_us());
        span->set_start_parse_us(start_parse_us);
        span->set_request_size(sizeof(nshead_t) + req_head->body_len);
    }

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
        if (!server_accessor.AddConcurrency(cntl)) {
            cntl->SetFailed(
                ELIMIT, "Reached server's max_concurrency=%d",
                server->options().max_concurrency);
            break;
        }
        if (FLAGS_usercode_in_pthread && TooManyUserCode()) {
            cntl->SetFailed(ELIMIT, "Too many user code to run when"
                            " -usercode_in_pthread is on");
            break;
        }
    } while (false);

    msg.reset();  // optional, just release resource ASAP
    if (span) {
        span->ResetServerSpanName(service->_cached_name);
        span->set_start_callback_us(butil::cpuwide_time_us());
        span->AsParent();
    }
    if (!FLAGS_usercode_in_pthread) {
        return service->ProcessNsheadRequest(*server, cntl, *req, res, nshead_done);
    }
    if (BeginRunningUserCode()) {
        service->ProcessNsheadRequest(*server, cntl, *req, res, nshead_done);
        return EndRunningUserCodeInPlace();
    } else {
        return EndRunningCallMethodInPool(
            service, *server, cntl, *req, res, nshead_done);
    }
}

void ProcessNsheadResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    
    // Fetch correlation id that we saved before in `PackNsheadRequest'
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
    // MUST be NsheadMessage (checked in SerializeNsheadRequest)
    NsheadMessage* response = (NsheadMessage*)cntl->response();
    const int saved_error = cntl->ErrorCode();
    if (response != NULL) {
        msg->meta.copy_to(&response->head, sizeof(nshead_t));
        msg->payload.swap(response->body);
    } // else just ignore the response.

    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resource ASAP
    accessor.OnResponse(cid, saved_error);
}

bool VerifyNsheadRequest(const InputMessageBase* msg_base) {
    Server* server = (Server*)msg_base->arg();
    if (server->options().auth) {
        LOG(WARNING) << "nshead does not support authentication";
        return false;
    }
    return true;
}

void SerializeNsheadRequest(butil::IOBuf* request_buf, Controller* cntl,
                            const google::protobuf::Message* req_base) {
    if (req_base == NULL) {
        return cntl->SetFailed(EREQUEST, "request is NULL");
    }
    if (req_base->GetDescriptor() != NsheadMessage::descriptor()) {
        return cntl->SetFailed(EINVAL, "Type of request must be NsheadMessage");
    }
    if (cntl->response() != NULL &&
        cntl->response()->GetDescriptor() != NsheadMessage::descriptor()) {
        return cntl->SetFailed(EINVAL, "Type of response must be NsheadMessage");
    }
    const NsheadMessage* req = (const NsheadMessage*)req_base;
    nshead_t nshead = req->head;
    if (cntl->has_log_id()) {
        nshead.log_id = cntl->log_id();
    }
    nshead.magic_num = NSHEAD_MAGICNUM;
    nshead.body_len = req->body.size();
    request_buf->append(&nshead, sizeof(nshead));
    request_buf->append(req->body);
}

void PackNsheadRequest(
    butil::IOBuf* packet_buf,
    SocketMessage**,
    uint64_t correlation_id,
    const google::protobuf::MethodDescriptor*,
    Controller* cntl,
    const butil::IOBuf& request,
    const Authenticator*) {
    ControllerPrivateAccessor accessor(cntl);
    if (cntl->connection_type() == CONNECTION_TYPE_SINGLE) {
        return cntl->SetFailed(
            EINVAL, "nshead protocol can't work with CONNECTION_TYPE_SINGLE");
    }
    // Store `correlation_id' into the socket since nshead protocol can't
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
