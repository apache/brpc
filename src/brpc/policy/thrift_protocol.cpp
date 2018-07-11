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

// Authors: wangxuefeng (wangxuefeng@didichuxing.com)

#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <gflags/gflags.h>

#include "butil/time.h" 
#include "butil/iobuf.h"                        // butil::IOBuf
#include "brpc/log.h"
#include "brpc/controller.h"                    // Controller
#include "brpc/socket.h"                        // Socket
#include "brpc/server.h"                        // Server
#include "brpc/span.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/thrift_service.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/thrift_protocol.h"
#include "brpc/details/usercode_backup_pool.h"

#include <thrift/Thrift.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/protocol/TBinaryProtocol.h>

// _THRIFT_STDCXX_H_ is defined by thrift/stdcxx.h which was added since thrift 0.11.0
#include <thrift/TProcessor.h> // to include stdcxx.h if present
#ifndef THRIFT_STDCXX
 #if defined(_THRIFT_STDCXX_H_)
 # define THRIFT_STDCXX apache::thrift::stdcxx
 #else
 # define THRIFT_STDCXX boost
 #endif
#endif

extern "C" {
void bthread_assign_data(void* data) __THROW;
}

namespace brpc {

int32_t get_thrift_method_name(const butil::IOBuf& body, std::string* method_name) {

    // Thrift protocol format:
    // Version + Message type + Length + Method + Sequence Id
    //   |             |          |        |          |
    //   2     +       2      +   4    +   >0   +     4
    if (body.size() < 12) {
        LOG(ERROR) << "No Enough data to get method name, request body size: " << body.size();
        return -1;
    }

    char version_and_len_buf[8];
    size_t k = body.copy_to(version_and_len_buf, sizeof(version_and_len_buf));
    if (k != sizeof(version_and_len_buf) ) {
        LOG(ERROR) << "copy "<< sizeof(version_and_len_buf) << " bytes from body failed";
        return -1;
    }

    uint32_t method_name_length = ntohl(*(int32_t*)(version_and_len_buf + 4));

    char fname[method_name_length];
    k = body.copy_to(fname, method_name_length, sizeof(version_and_len_buf));
    if ( k != method_name_length) {
        LOG(ERROR) << "copy " << method_name_length << " bytes from body failed";
        return -1;
    }

    method_name->assign(fname, method_name_length);
    return sizeof(version_and_len_buf) + method_name_length;

}

ThriftClosure::ThriftClosure(void* additional_space)
    : _socket_ptr(NULL)
    , _server(NULL)
    , _start_parse_us(0)
    , _do_respond(true)
    , _additional_space(additional_space) {
}

ThriftClosure::~ThriftClosure() {
    LogErrorTextAndDelete(false)(&_controller);
}

void ThriftClosure::DoNotRespond() {
    _do_respond = false;
}

class DeleteThriftClosure {
public:
    void operator()(ThriftClosure* done) const {
        done->~ThriftClosure();
        free(done);
    }
};

void ThriftClosure::Run() {
    // Recycle itself after `Run'
    std::unique_ptr<ThriftClosure, DeleteThriftClosure> recycle_ctx(this);
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

        if (_response.thrift_raw_instance) {
            std::string method_name = _controller.thrift_method_name();
            if (method_name == "" ||
                method_name.length() < 1 ||
                method_name[0] == ' ') {
                _controller.SetFailed(ENOMETHOD,
                    "invalid thrift method name or method name empty in server!");
                return;
            }

            auto out_buffer =
                THRIFT_STDCXX::make_shared<apache::thrift::transport::TMemoryBuffer>();
            auto oprot =
                THRIFT_STDCXX::make_shared<apache::thrift::protocol::TBinaryProtocol>(out_buffer);

            // The following code was taken and modified from thrift auto generated code
            oprot->writeMessageBegin(method_name,
                ::apache::thrift::protocol::T_REPLY, _request.thrift_message_seq_id);

            uint32_t xfer = 0;

            xfer += oprot->writeStructBegin("placeholder");

            xfer += oprot->writeFieldBegin("success",
                ::apache::thrift::protocol::T_STRUCT, 0);
            if (_response.thrift_raw_instance && _response.thrift_raw_instance_writer) {
                xfer += _response.thrift_raw_instance_writer(
                    _response.thrift_raw_instance, oprot.get());
            } else {
                _controller.SetFailed(ERESPONSE, "thrift_raw_instance or"
                    "thrift_raw_instance_writer is null!");

            }
            xfer += oprot->writeFieldEnd();

            xfer += oprot->writeFieldStop();
            xfer += oprot->writeStructEnd();

            oprot->writeMessageEnd();
            oprot->getTransport()->writeEnd();
            oprot->getTransport()->flush();
            // End thrfit auto generated code

            uint8_t* buf;
            uint32_t sz;
            out_buffer->getBuffer(&buf, &sz);
            _response.body.append(buf, sz);
        }

        uint32_t length = _response.body.length();
        _response.head.body_len = htonl(length);
    
        if (span) {
            int response_size = sizeof(thrift_head_t) + _response.head.body_len;
            span->set_response_size(response_size);
        }
        butil::IOBuf write_buf;
        write_buf.append(&_response.head, sizeof(thrift_head_t));
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

void ThriftClosure::SetMethodName(const std::string& full_method_name) {
    ControllerPrivateAccessor accessor(&_controller);
    Span* span = accessor.span();
    if (span) {
        span->ResetServerSpanName(full_method_name);
    }
}

namespace policy {

ParseResult ParseThriftMessage(butil::IOBuf* source,
                               Socket*, bool /*read_eof*/, const void* /*arg*/) {

    char header_buf[sizeof(thrift_head_t) + 3];
    const size_t n = source->copy_to(header_buf, sizeof(thrift_head_t) + 3);

    if (n < 7) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    const void* dummy = header_buf + sizeof(thrift_head_t);
    const int32_t sz = ntohl(*(int32_t*)dummy);
    int32_t version = sz & THRIFT_HEAD_VERSION_MASK;
    if (version != THRIFT_HEAD_VERSION_1) {
        RPC_VLOG << "magic_num=" << version
                 << " doesn't match THRIFT_MAGIC_NUM=" << THRIFT_HEAD_VERSION_1;
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }

    thrift_head_t* thrift = (thrift_head_t *)header_buf;
    thrift->body_len = ntohl(thrift->body_len);
    uint32_t body_len = thrift->body_len;
    if (body_len > FLAGS_max_body_size) {
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(header_buf) + body_len - 3) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    policy::MostCommonMessage* msg = policy::MostCommonMessage::Get();
    source->cutn(&msg->meta, sizeof(thrift_head_t));
    source->cutn(&msg->payload, body_len);
    return MakeMessage(msg);
}

struct CallMethodInBackupThreadArgs {
    ThriftService* service;
    const Server* server;
    Controller* controller;
    ThriftFramedMessage* request;
    ThriftFramedMessage* response;
    ThriftClosure* done;
};

static void CallMethodInBackupThread(void* void_args) {
    CallMethodInBackupThreadArgs* args = (CallMethodInBackupThreadArgs*)void_args;

    std::string method_name;
    if (get_thrift_method_name(args->request->body, &method_name) < 0) {
        LOG(FATAL) << "Fail to get thrift method name";
        delete args;
        return;
    }

    args->controller->set_thrift_method_name(method_name);
    args->service->ProcessThriftFramedRequest(*args->server, args->controller,
                                        args->request, args->response,
                                        args->done);
    delete args;
}

static void EndRunningCallMethodInPool(ThriftService* service,
                                       const Server& server,
                                       Controller* controller,
                                       ThriftFramedMessage* request,
                                       ThriftFramedMessage* response,
                                       ThriftClosure* done) {
    CallMethodInBackupThreadArgs* args = new CallMethodInBackupThreadArgs;
    args->service = service;
    args->server = &server;
    args->controller = controller;
    args->request = request;
    args->response = response;
    args->done = done;
    return EndRunningUserCodeInPool(CallMethodInBackupThread, args);
};

void ProcessThriftRequest(InputMessageBase* msg_base) {

    const int64_t start_parse_us = butil::cpuwide_time_us();   

    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket(msg->ReleaseSocket());
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);

    char buf[sizeof(thrift_head_t)];
    const char *p = (const char *)msg->meta.fetch(buf, sizeof(buf));
    thrift_head_t *req_head = (thrift_head_t *)p;
    req_head->body_len = ntohl(req_head->body_len);

    ThriftService* service = server->options().thrift_service;
    if (service == NULL) {
        LOG_EVERY_SECOND(WARNING) 
            << "Received thrift request however the server does not set"
            " ServerOptions.thrift_service, close the connection.";
        socket->SetFailed();
        return;
    }

    void* space = malloc(sizeof(ThriftClosure) + service->_additional_space);
    if (!space) {
        LOG(FATAL) << "Fail to new ThriftClosure";
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
        sub_space = (char*)space + sizeof(ThriftClosure);
    }
    ThriftClosure* thrift_done = new (space) ThriftClosure(sub_space);
    Controller* cntl = &(thrift_done->_controller);
    ThriftFramedMessage* req = &(thrift_done->_request);
    ThriftFramedMessage* res = &(thrift_done->_response);

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
    // on top of ThriftService may pack log_id in meta or user messages and
    // overwrite the value.
    //cntl->set_log_id(req_head->log_id);
    accessor.set_server(server)
        .set_security_mode(security_mode)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_request_protocol(PROTOCOL_THRIFT);

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
        span->set_protocol(PROTOCOL_THRIFT);
        span->set_received_us(msg->received_us());
        span->set_start_parse_us(start_parse_us);
        span->set_request_size(sizeof(thrift_head_t) + req_head->body_len);
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

    try {
        std::string method_name;
        if (get_thrift_method_name(req->body, &method_name) < 0) {
            cntl->SetFailed(EREQUEST, "Fail to get thrift method name!");
        }
        cntl->set_thrift_method_name(method_name);

        if (!FLAGS_usercode_in_pthread) {
            return service->ProcessThriftFramedRequest(*server, cntl,
                req, res, thrift_done);
        }
        if (BeginRunningUserCode()) {
            service->ProcessThriftFramedRequest(*server, cntl, req, res, thrift_done);
            return EndRunningUserCodeInPlace();
        } else {
            return EndRunningCallMethodInPool(
                service, *server, cntl, req, res, thrift_done);
        }
    } catch (::apache::thrift::TException& e) {
        cntl->SetFailed(EREQUEST, "Invalid request data, reason: %s", e.what());
    } catch (...) {
        cntl->SetFailed(EINTERNAL, "Internal server error!");
    }

}

void ProcessThriftResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    
    // Fetch correlation id that we saved before in `PacThriftRequest'
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

    // MUST be ThriftFramedMessage (checked in SerializeThriftRequest)
    ThriftFramedMessage* response = (ThriftFramedMessage*)cntl->response();
    const int saved_error = cntl->ErrorCode();
    if (response != NULL) {
        msg->meta.copy_to(&response->head, sizeof(thrift_head_t));
        response->head.body_len = ntohl(response->head.body_len);
        msg->payload.swap(response->body);

        uint32_t body_len = response->head.body_len;
        // Deserialize binary message to thrift message
        std::unique_ptr<uint8_t[]>thrift_buffer(new uint8_t[body_len]);

        const size_t k = response->body.copy_to(thrift_buffer.get(), body_len);
        if ( k != body_len) {
          cntl->SetFailed("copy response body to thrift buffer failed!");
          return;
        }

        auto in_buffer =
            THRIFT_STDCXX::make_shared<apache::thrift::transport::TMemoryBuffer>();
        auto in_portocol =
            THRIFT_STDCXX::make_shared<apache::thrift::protocol::TBinaryProtocol>(in_buffer);

        in_buffer->resetBuffer(thrift_buffer.get(), body_len);

        // The following code was taken from thrift auto generate code
        int32_t rseqid = 0;
        std::string fname;
        ::apache::thrift::protocol::TMessageType mtype;
        
        in_portocol->readMessageBegin(fname, mtype, rseqid);
        if (mtype == ::apache::thrift::protocol::T_EXCEPTION) {
          cntl->SetFailed("thrift process server response exception!");
          return;
        }
        if (mtype != ::apache::thrift::protocol::T_REPLY) {
          in_portocol->skip(::apache::thrift::protocol::T_STRUCT);
          in_portocol->readMessageEnd();
          in_portocol->getTransport()->readEnd();
        }
        if (fname.compare(cntl->thrift_method_name()) != 0) {
          in_portocol->skip(::apache::thrift::protocol::T_STRUCT);
          in_portocol->readMessageEnd();
          in_portocol->getTransport()->readEnd();
        }

        // presult section
        apache::thrift::protocol::TInputRecursionTracker tracker(*in_portocol);
        uint32_t xfer = 0;
        ::apache::thrift::protocol::TType ftype;
        int16_t fid;
        
        xfer += in_portocol->readStructBegin(fname);
        
        using ::apache::thrift::protocol::TProtocolException;
        bool success = false;

        while (true)
        {
          xfer += in_portocol->readFieldBegin(fname, ftype, fid);
          if (ftype == ::apache::thrift::protocol::T_STOP) {
            break;
          }
          switch (fid)
          {
            case 0:
              if (ftype == ::apache::thrift::protocol::T_STRUCT) {
                xfer += response->read(in_portocol.get());
                success = true;
              } else {
                xfer += in_portocol->skip(ftype);
              }
              break;
            default:
              xfer += in_portocol->skip(ftype);
              break;
          }
          xfer += in_portocol->readFieldEnd();
        }
        
        xfer += in_portocol->readStructEnd();
        // end presult section

        in_portocol->readMessageEnd();
        in_portocol->getTransport()->readEnd();
        
        if (!success) {
          cntl->SetFailed("thrift process server response exception!");
          return;
        }

    } // else just ignore the response.

    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

bool VerifyThriftRequest(const InputMessageBase* msg_base) {
    Server* server = (Server*)msg_base->arg();
    if (server->options().auth) {
        LOG(WARNING) << "thrift does not support authentication";
        return false;
    }
    return true;
}

void SerializeThriftRequest(butil::IOBuf* request_buf, Controller* cntl,
                            const google::protobuf::Message* req_base) {
    if (req_base == NULL) {
        return cntl->SetFailed(EREQUEST, "request is NULL");
    }
    ControllerPrivateAccessor accessor(cntl);

    const ThriftFramedMessage* req = (const ThriftFramedMessage*)req_base;

    thrift_head_t head = req->head;

    auto out_buffer =
        THRIFT_STDCXX::make_shared<apache::thrift::transport::TMemoryBuffer>();
    auto out_portocol =
        THRIFT_STDCXX::make_shared<apache::thrift::protocol::TBinaryProtocol>(out_buffer);

    std::string thrift_method_name = cntl->thrift_method_name();
    // we should do more check on the thrift method name, but since it is rare when
    // the method_name is just some white space or something else
    if (cntl->thrift_method_name() == "" ||
        cntl->thrift_method_name().length() < 1 ||
        cntl->thrift_method_name()[0] == ' ') {
        return cntl->SetFailed(ENOMETHOD,
            "invalid thrift method name or method name empty!");
    }

    // The following code was taken from thrift auto generated code
    // send_xxx
    int32_t cseqid = 0;
    out_portocol->writeMessageBegin(thrift_method_name,
        ::apache::thrift::protocol::T_CALL, cseqid);

    // xxx_pargs write
    uint32_t xfer = 0;
    apache::thrift::protocol::TOutputRecursionTracker tracker(*out_portocol);

    std::string struct_begin_str = "ThriftService_" + thrift_method_name + "_pargs";
    xfer += out_portocol->writeStructBegin(struct_begin_str.c_str());
    xfer += out_portocol->writeFieldBegin("request", ::apache::thrift::protocol::T_STRUCT, 1);

    // request's write
    ThriftFramedMessage* r = const_cast<ThriftFramedMessage*>(req);
    xfer += r->write(out_portocol.get());
    // end request's write

    xfer += out_portocol->writeFieldEnd();
    
    xfer += out_portocol->writeFieldStop();
    xfer += out_portocol->writeStructEnd();
    // end xxx_pargs write

    out_portocol->writeMessageEnd();
    out_portocol->getTransport()->writeEnd();
    out_portocol->getTransport()->flush();
    // end send_xxx
    // end thrift auto generated code

    uint8_t* buf;
    uint32_t sz;
    out_buffer->getBuffer(&buf, &sz);

    head.body_len = ntohl(sz);
    request_buf->append(&head, sizeof(head));
    // end auto generate code

    request_buf->append(buf, sz);

}

void PackThriftRequest(
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

extern "C" {

void RegisterThriftProtocol() {

    brpc::Protocol thrift_binary_protocol = {brpc::policy::ParseThriftMessage,
                                 brpc::policy::SerializeThriftRequest, brpc::policy::PackThriftRequest,
                                 brpc::policy::ProcessThriftRequest, brpc::policy::ProcessThriftResponse,
                                 brpc::policy::VerifyThriftRequest, NULL, NULL,
                                 brpc::CONNECTION_TYPE_POOLED_AND_SHORT, "thrift" };
    if (brpc::RegisterProtocol(brpc::PROTOCOL_THRIFT, thrift_binary_protocol) != 0) {
        exit(1);
    }
}
}

