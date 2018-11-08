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
#include <thrift/TApplicationException.h>

// _THRIFT_STDCXX_H_ is defined by thrift/stdcxx.h which was added since thrift 0.11.0
#include <thrift/TProcessor.h> // to include stdcxx.h if present
#ifndef THRIFT_STDCXX
 #if defined(_THRIFT_STDCXX_H_)
 # define THRIFT_STDCXX apache::thrift::stdcxx
 #else
 # define THRIFT_STDCXX boost
 # include <boost/make_shared.hpp>
 #endif
#endif

extern "C" {
void bthread_assign_data(void* data);
}

namespace brpc {
namespace policy {

static const uint32_t MAX_THRIFT_METHOD_NAME_LENGTH = 256; // reasonably large
static const uint32_t THRIFT_HEAD_VERSION_MASK = (uint32_t)0xffffff00;
static const uint32_t THRIFT_HEAD_VERSION_1 = (uint32_t)0x80010000;
struct thrift_head_t {
    uint32_t body_len;
};

// A faster implementation of TProtocol::readMessageBegin without depending
// on thrift stuff.
static butil::Status
ReadThriftMessageBegin(butil::IOBuf* body,
                       std::string* method_name,
                       ::apache::thrift::protocol::TMessageType* mtype,
                       uint32_t* seq_id) {
    // Thrift protocol format:
    // Version + Message type + Length + Method + Sequence Id
    //   |             |          |        |          |
    //   3     +       1      +   4    +   >0   +     4
    uint32_t version_and_len_buf[2];
    size_t k = body->copy_to(version_and_len_buf, sizeof(version_and_len_buf));
    if (k != sizeof(version_and_len_buf) ) {
        return butil::Status(-1, "Fail to copy %" PRIu64 " bytes from body",
                             sizeof(version_and_len_buf));
    }
    *mtype = (apache::thrift::protocol::TMessageType)
        (ntohl(version_and_len_buf[0]) & 0x000000FF);
    const uint32_t method_name_length = ntohl(version_and_len_buf[1]);
    if (method_name_length > MAX_THRIFT_METHOD_NAME_LENGTH) {
        return butil::Status(-1, "method_name_length=%u is too long",
                             method_name_length);
    }

    char buf[sizeof(version_and_len_buf) + method_name_length + 4];
    k = body->cutn(buf, sizeof(buf));
    if (k != sizeof(buf)) {
        return butil::Status(-1, "Fail to cut %" PRIu64 " bytes", sizeof(buf));
    }
    method_name->assign(buf + sizeof(version_and_len_buf), method_name_length);
    // suppress strict-aliasing warning
    uint32_t* p_seq_id = (uint32_t*)(buf + sizeof(version_and_len_buf) + method_name_length);
    *seq_id = ntohl(*p_seq_id);
    return butil::Status::OK();
}

inline size_t ThriftMessageBeginSize(const std::string& method_name) {
    return 12 + method_name.size();
}

static void
WriteThriftMessageBegin(char* buf,
                        const std::string& method_name,
                        ::apache::thrift::protocol::TMessageType mtype,
                        uint32_t seq_id) {
    char* p = buf;
    *(uint32_t*)p = htonl(THRIFT_HEAD_VERSION_1 | (((uint32_t)mtype) & 0x000000FF));
    p += 4;
    *(uint32_t*)p = htonl(method_name.size());
    p += 4;
    memcpy(p, method_name.data(), method_name.size());
    p += method_name.size();
    *p = htonl(seq_id);
}

bool ReadThriftStruct(const butil::IOBuf& body,
                      ThriftMessageBase* raw_msg,
                      int16_t expected_fid) {
    const size_t body_len  = body.size();
    uint8_t* thrift_buffer = (uint8_t*)malloc(body_len);
    body.copy_to(thrift_buffer, body_len);
    auto in_buffer =
        THRIFT_STDCXX::make_shared<apache::thrift::transport::TMemoryBuffer>(
            thrift_buffer, body_len,
            ::apache::thrift::transport::TMemoryBuffer::TAKE_OWNERSHIP);
    apache::thrift::protocol::TBinaryProtocolT<apache::thrift::transport::TMemoryBuffer> iprot(in_buffer);

    // The following code was taken from thrift auto generate code
    std::string fname;

    uint32_t xfer = 0;
    ::apache::thrift::protocol::TType ftype;
    int16_t fid;

    xfer += iprot.readStructBegin(fname);
    bool success = false;
    while (true) {
        xfer += iprot.readFieldBegin(fname, ftype, fid);
        if (ftype == ::apache::thrift::protocol::T_STOP) {
            break;
        }
        if (fid == expected_fid) {
            if (ftype == ::apache::thrift::protocol::T_STRUCT) {
                xfer += raw_msg->Read(&iprot);
                success = true;
            } else {
                xfer += iprot.skip(ftype);
            }
        } else {
            xfer += iprot.skip(ftype);
        }
        xfer += iprot.readFieldEnd();
    }

    xfer += iprot.readStructEnd();
    iprot.getTransport()->readEnd();
    return success;
}

void ReadThriftException(const butil::IOBuf& body,
                         ::apache::thrift::TApplicationException* x) {
    size_t body_len  = body.size();
    uint8_t* thrift_buffer = (uint8_t*)malloc(body_len);
    body.copy_to(thrift_buffer, body_len);
    auto in_buffer =
        THRIFT_STDCXX::make_shared<apache::thrift::transport::TMemoryBuffer>(
            thrift_buffer, body_len,
            ::apache::thrift::transport::TMemoryBuffer::TAKE_OWNERSHIP);
    apache::thrift::protocol::TBinaryProtocolT<apache::thrift::transport::TMemoryBuffer> iprot(in_buffer);

    x->read(&iprot);
    iprot.readMessageEnd();
    iprot.getTransport()->readEnd();
}

// The continuation of request processing. Namely send response back to client.
class ThriftClosure : public google::protobuf::Closure {
public:
    explicit ThriftClosure();
    ~ThriftClosure();

    // [Required] Call this to send response back to the client.
    void Run() override;

    // Suspend/resume calling DoRun().
    void SuspendRunning();
    void ResumeRunning();

private:
    void DoRun();
friend void ProcessThriftRequest(InputMessageBase* msg_base);

    butil::atomic<int> _run_counter;
    int64_t _received_us;
    ThriftFramedMessage _request;
    ThriftFramedMessage _response;
    Controller _controller;
};

inline ThriftClosure::ThriftClosure()
    : _run_counter(1), _received_us(0) {
}

ThriftClosure::~ThriftClosure() {
    LogErrorTextAndDelete(false)(&_controller);
}

inline void ThriftClosure::SuspendRunning() {
    _run_counter.fetch_add(1, butil::memory_order_relaxed);
}

inline void ThriftClosure::ResumeRunning() {
    if (_run_counter.fetch_sub(1, butil::memory_order_relaxed) == 1) {
        DoRun();
    }
}

void ThriftClosure::Run() {
    if (_run_counter.fetch_sub(1, butil::memory_order_relaxed) == 1) {
        DoRun();
    }
}

void ThriftClosure::DoRun() {
    // Recycle itself after `Run'
    std::unique_ptr<ThriftClosure> recycle_ctx(this);
    const Server* server = _controller.server();

    ControllerPrivateAccessor accessor(&_controller);
    Span* span = accessor.span();
    if (span) {
        span->set_start_send_us(butil::cpuwide_time_us());
    }
    Socket* sock = accessor.get_sending_socket();
    MethodStatus* method_status = (server->options().thrift_service ? 
        server->options().thrift_service->_status : NULL);
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
            ServerPrivateAccessor(server).AddError();
        }
    }

    if (_controller.IsCloseConnection() ||
        // seq_id is not read yet, no valid response can be sent back
        !_controller.has_log_id()) {
        sock->SetFailed();
        return;
    }

    const std::string& method_name = _controller.thrift_method_name();
    if (method_name.empty() || method_name[0] == ' ') {
        _controller.SetFailed(ENOMETHOD, "Invalid thrift_method_name!");
    }
    if (method_name.size() > MAX_THRIFT_METHOD_NAME_LENGTH) {
        _controller.SetFailed(ENOMETHOD, "thrift_method_name is too long");
    }
    if (_controller.log_id() > (uint64_t)0xffffffff) {
        _controller.SetFailed(ERESPONSE, "Invalid thrift seq_id=%" PRIu64,
                              _controller.log_id());
    }
    const uint32_t seq_id = (uint32_t)_controller.log_id();

    butil::IOBuf write_buf;

    // The following code was taken and modified from thrift auto generated code
    if (_controller.Failed()) {
        auto out_buffer =
            THRIFT_STDCXX::make_shared<apache::thrift::transport::TMemoryBuffer>();
        apache::thrift::protocol::TBinaryProtocolT<apache::thrift::transport::TMemoryBuffer> oprot(out_buffer);
        ::apache::thrift::TApplicationException x(_controller.ErrorText());
        oprot.writeMessageBegin(
            method_name, ::apache::thrift::protocol::T_EXCEPTION, seq_id);
        x.write(&oprot);
        oprot.writeMessageEnd();
        oprot.getTransport()->writeEnd();
        oprot.getTransport()->flush();

        uint8_t* buf;
        uint32_t sz;
        out_buffer->getBuffer(&buf, &sz);
        const thrift_head_t head = { htonl(sz) };
        write_buf.append(&head, sizeof(head));
        write_buf.append(buf, sz);
    } else if (_response.raw_instance()) {
        auto out_buffer =
            THRIFT_STDCXX::make_shared<apache::thrift::transport::TMemoryBuffer>();
        apache::thrift::protocol::TBinaryProtocolT<apache::thrift::transport::TMemoryBuffer> oprot(out_buffer);
        oprot.writeMessageBegin(
            method_name, ::apache::thrift::protocol::T_REPLY, seq_id);

        uint32_t xfer = 0;
        xfer += oprot.writeStructBegin("rpc_result"); // can be any valid name
        xfer += oprot.writeFieldBegin("success",
                                      ::apache::thrift::protocol::T_STRUCT,
                                      THRIFT_RESPONSE_FID);
        xfer += _response.raw_instance()->Write(&oprot);
        xfer += oprot.writeFieldEnd();
        xfer += oprot.writeFieldStop();
        xfer += oprot.writeStructEnd();

        oprot.writeMessageEnd();
        oprot.getTransport()->writeEnd();
        oprot.getTransport()->flush();

        uint8_t* buf;
        uint32_t sz;
        out_buffer->getBuffer(&buf, &sz);
        const thrift_head_t head = { htonl(sz) };
        write_buf.append(&head, sizeof(head));
        write_buf.append(buf, sz);
    } else {
        const size_t mb_size = ThriftMessageBeginSize(method_name);
        char buf[sizeof(thrift_head_t) + mb_size];
        // suppress strict-aliasing warning
        thrift_head_t* head = (thrift_head_t*)buf;
        head->body_len = htonl(mb_size + _response.body.size());
        WriteThriftMessageBegin(buf + sizeof(thrift_head_t), method_name,
                                ::apache::thrift::protocol::T_REPLY, seq_id);
        write_buf.append(buf, sizeof(buf));
        write_buf.append(_response.body.movable());
    }
    
    if (span) {
        span->set_response_size(write_buf.size());
    }
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

    if (span) {
        // TODO: this is not sent
        span->set_sent_us(butil::cpuwide_time_us());
    }
}

ParseResult ParseThriftMessage(butil::IOBuf* source,
                               Socket*, bool /*read_eof*/, const void* /*arg*/) {
    char header_buf[sizeof(thrift_head_t) + 4];
    const size_t n = source->copy_to(header_buf, sizeof(header_buf));
    if (n < sizeof(header_buf)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    const uint32_t sz = ntohl(*(uint32_t*)(header_buf + sizeof(thrift_head_t)));
    uint32_t version = sz & THRIFT_HEAD_VERSION_MASK;
    if (version != THRIFT_HEAD_VERSION_1) {
        RPC_VLOG << "version=" << version
                 << " doesn't match THRIFT_VERSION=" << THRIFT_HEAD_VERSION_1;
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    // suppress strict-aliasing warning
    thrift_head_t* head = (thrift_head_t*)header_buf;
    const uint32_t body_len = ntohl(head->body_len);
    if (body_len > FLAGS_max_body_size) {
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(thrift_head_t) + body_len) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    MostCommonMessage* msg = MostCommonMessage::Get();
    source->pop_front(sizeof(thrift_head_t));
    source->cutn(&msg->payload, body_len);
    return MakeMessage(msg);
}

inline void ProcessThriftFramedRequestNoExcept(ThriftService* service,
                                                   Controller* cntl,
                                                   ThriftFramedMessage* req,
                                                   ThriftFramedMessage* res,
                                                   ThriftClosure* done) {
    // NOTE: done is not actually run before ResumeRunning() is called so that
    // we can still set `cntl' in the catch branch.
    done->SuspendRunning();
    try {
        service->ProcessThriftFramedRequest(cntl, req, res, done);
    } catch (std::exception& e) {
        cntl->SetFailed(EINTERNAL, "Catched exception: %s", e.what());
    } catch (std::string& e) {
        cntl->SetFailed(EINTERNAL, "Catched std::string: %s", e.c_str());
    } catch (const char* e) {
        cntl->SetFailed(EINTERNAL, "Catched const char*: %s", e);
    } catch (...) {
        cntl->SetFailed(EINTERNAL, "Catched unknown exception");
    }
    done->ResumeRunning();
}

struct CallMethodInBackupThreadArgs {
    ThriftService* service;
    Controller* controller;
    ThriftFramedMessage* request;
    ThriftFramedMessage* response;
    ThriftClosure* done;
};

static void CallMethodInBackupThread(void* void_args) {
    CallMethodInBackupThreadArgs* args = (CallMethodInBackupThreadArgs*)void_args;
    ProcessThriftFramedRequestNoExcept(args->service,
                                           args->controller,
                                           args->request,
                                           args->response,
                                           args->done);
    delete args;
}

static void EndRunningCallMethodInPool(ThriftService* service,
                                       Controller* controller,
                                       ThriftFramedMessage* request,
                                       ThriftFramedMessage* response,
                                       ThriftClosure* done) {
    CallMethodInBackupThreadArgs* args = new CallMethodInBackupThreadArgs;
    args->service = service;
    args->controller = controller;
    args->request = request;
    args->response = response;
    args->done = done;
    return EndRunningUserCodeInPool(CallMethodInBackupThread, args);
};

void ProcessThriftRequest(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();   

    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket_guard(msg->ReleaseSocket());
    Socket* socket = socket_guard.get();
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);

    ThriftClosure* thrift_done = new ThriftClosure;
    ClosureGuard done_guard(thrift_done);
    Controller* cntl = &(thrift_done->_controller);
    ThriftFramedMessage* req = &(thrift_done->_request);
    ThriftFramedMessage* res = &(thrift_done->_response);
    thrift_done->_received_us = msg->received_us();

    ServerPrivateAccessor server_accessor(server);
    const bool security_mode = server->options().security_mode() &&
                               socket->user() == server_accessor.acceptor();
    ControllerPrivateAccessor accessor(cntl);
    accessor.set_server(server)
        .set_security_mode(security_mode)
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_request_protocol(PROTOCOL_THRIFT)
        .set_begin_time_us(msg_base->received_us())
        .move_in_server_receiving_sock(socket_guard);

    uint32_t seq_id;
    ::apache::thrift::protocol::TMessageType mtype;
    butil::Status st = ReadThriftMessageBegin(
        &msg->payload, &cntl->_thrift_method_name, &mtype, &seq_id);
    if (!st.ok()) {
        return cntl->SetFailed(EREQUEST, "%s", st.error_cstr());
    }
    msg->payload.swap(req->body);
    req->field_id = THRIFT_REQUEST_FID;
    cntl->set_log_id(seq_id);    // Pass seq_id by log_id

    ThriftService* service = server->options().thrift_service;
    if (service == NULL) {
        LOG_EVERY_SECOND(ERROR)
            << "Received thrift request however the server does not set"
            " ServerOptions.thrift_service, close the connection.";
        return cntl->SetFailed(EINTERNAL, "ServerOptions.thrift_service is NULL");
    }

    // Switch to service-specific error.
    non_service_error.release();
    MethodStatus* method_status = service->_status;
    if (method_status) {
        if (!method_status->OnRequested()) {
            return cntl->SetFailed(ELIMIT, "Reached %s's max_concurrency=%d",
                            cntl->thrift_method_name().c_str(),
                            method_status->MaxConcurrency());
        }
    }

    // Tag the bthread with this server's key for thread_local_data().
    if (server->thread_local_options().thread_local_data_factory) {
        bthread_assign_data((void*)&server->thread_local_options());
    }

    Span* span = NULL;
    if (IsTraceable(false)) {
        span = Span::CreateServerSpan(0, 0, 0, msg->base_real_us());
        accessor.set_span(span);
        span->set_log_id(seq_id);
        span->set_remote_side(cntl->remote_side());
        span->set_protocol(PROTOCOL_THRIFT);
        span->set_received_us(msg->received_us());
        span->set_start_parse_us(start_parse_us);
        span->set_request_size(sizeof(thrift_head_t) + req->body.size());
    }

    if (!server->IsRunning()) {
        return cntl->SetFailed(ELOGOFF, "Server is stopping");
    }
    if (socket->is_overcrowded()) {
        return cntl->SetFailed(EOVERCROWDED, "Connection to %s is overcrowded",
                butil::endpoint2str(socket->remote_side()).c_str());
    }
    if (!server_accessor.AddConcurrency(cntl)) {
        return cntl->SetFailed(ELIMIT, "Reached server's max_concurrency=%d",
                server->options().max_concurrency);
    }
    if (FLAGS_usercode_in_pthread && TooManyUserCode()) {
        return cntl->SetFailed(ELIMIT, "Too many user code to run when"
                " -usercode_in_pthread is on");
    }

    msg.reset();  // optional, just release resourse ASAP

    if (span) {
        span->ResetServerSpanName(cntl->thrift_method_name());
        span->set_start_callback_us(butil::cpuwide_time_us());
        span->AsParent();
    }

    done_guard.release();

    if (!FLAGS_usercode_in_pthread) {
        return ProcessThriftFramedRequestNoExcept(service, cntl, req, res, thrift_done);
    }

    if (BeginRunningUserCode()) {
        ProcessThriftFramedRequestNoExcept(service, cntl, req, res, thrift_done);
        return EndRunningUserCodeInPlace();
    } else {
        return EndRunningCallMethodInPool(service, cntl, req, res, thrift_done);
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

    const int saved_error = cntl->ErrorCode();
    do {
        // The following code was taken from thrift auto generate code
        std::string fname;
        ::apache::thrift::protocol::TMessageType mtype;
        uint32_t seq_id = 0; // unchecked
        
        butil::Status st = ReadThriftMessageBegin(&msg->payload, &fname, &mtype, &seq_id);
        if (!st.ok()) {
            cntl->SetFailed(ERESPONSE, "%s", st.error_cstr());
            break;
        }
        if (mtype == ::apache::thrift::protocol::T_EXCEPTION) {
            ::apache::thrift::TApplicationException x;
            ReadThriftException(msg->payload, &x);
            // TODO: Convert exception type to brpc errors.
            cntl->SetFailed(x.what());
            break;
        }
        if (mtype != ::apache::thrift::protocol::T_REPLY) {
            cntl->SetFailed(ERESPONSE, "message_type is not T_REPLY");
            break;
        }
        if (fname != cntl->thrift_method_name()) {
            cntl->SetFailed(ERESPONSE,
                            "response.method_name=%s does not match request.method_name=%s",
                            fname.c_str(), cntl->thrift_method_name().c_str());
            break;
        }

        // Read presult
        
        // MUST be ThriftFramedMessage (checked in SerializeThriftRequest)
        ThriftFramedMessage* response = (ThriftFramedMessage*)cntl->response();
        if (response) {
            if (response->raw_instance()) {
                if (!ReadThriftStruct(msg->payload, response->raw_instance(),
                                      THRIFT_RESPONSE_FID)) {
                    cntl->SetFailed(ERESPONSE, "Fail to read presult");
                    break;
                }
            } else {
                msg->payload.swap(response->body);
                response->field_id = THRIFT_RESPONSE_FID;
            }
        } // else just ignore the response.
    } while (false);

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
    if (req_base->GetDescriptor() != ThriftFramedMessage::descriptor()) {
        return cntl->SetFailed(EINVAL, "Type of request must be ThriftFramedMessage");
    }
    if (cntl->response() != NULL &&
        cntl->response()->GetDescriptor() != ThriftFramedMessage::descriptor()) {
        return cntl->SetFailed(EINVAL, "Type of response must be ThriftFramedMessage");
    }

    const std::string& method_name = cntl->thrift_method_name();
    // we should do more check on the thrift method name, but since it is rare when
    // the method_name is just some white space or something else
    if (method_name.empty() || method_name[0] == ' ') {
        return cntl->SetFailed(ENOMETHOD, "Invalid thrift_method_name!");
    }
    if (method_name.size() > MAX_THRIFT_METHOD_NAME_LENGTH) {
        return cntl->SetFailed(ENOMETHOD, "thrift_method_name is too long");
    }

    const ThriftFramedMessage* req = (const ThriftFramedMessage*)req_base;

    // xxx_pargs write
    if (req->raw_instance()) {
        auto out_buffer =
            THRIFT_STDCXX::make_shared<apache::thrift::transport::TMemoryBuffer>();
        apache::thrift::protocol::TBinaryProtocolT<apache::thrift::transport::TMemoryBuffer> oprot(out_buffer);

        oprot.writeMessageBegin(
            method_name, ::apache::thrift::protocol::T_CALL, 0/*seq_id*/);

        uint32_t xfer = 0;
        char struct_begin_str[32 + method_name.size()];
        char* p = struct_begin_str;
        memcpy(p, "ThriftService_", 14);
        p += 14;
        memcpy(p, method_name.data(), method_name.size());
        p += method_name.size();
        memcpy(p, "_pargs", 6);
        p += 6;
        *p = '\0';
        xfer += oprot.writeStructBegin(struct_begin_str);
        xfer += oprot.writeFieldBegin("request", ::apache::thrift::protocol::T_STRUCT,
                                      THRIFT_REQUEST_FID);

        // request's write
        xfer += req->raw_instance()->Write(&oprot);
        
        xfer += oprot.writeFieldEnd();
        xfer += oprot.writeFieldStop();
        xfer += oprot.writeStructEnd();

        oprot.writeMessageEnd();
        oprot.getTransport()->writeEnd();
        oprot.getTransport()->flush();

        uint8_t* buf;
        uint32_t sz;
        out_buffer->getBuffer(&buf, &sz);

        const thrift_head_t head = { htonl(sz) };
        request_buf->append(&head, sizeof(head));
        request_buf->append(buf, sz);
    } else {
        const size_t mb_size = ThriftMessageBeginSize(method_name);
        char buf[sizeof(thrift_head_t) + mb_size];
        // suppress strict-aliasing warning
        thrift_head_t* head = (thrift_head_t*)buf;
        head->body_len = htonl(mb_size + req->body.size());
        WriteThriftMessageBegin(buf + sizeof(thrift_head_t), method_name,
                                ::apache::thrift::protocol::T_CALL, 0/*seq_id*/);
        request_buf->append(buf, sizeof(buf));
        request_buf->append(req->body);
    }
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
    if (cntl->connection_type() == CONNECTION_TYPE_SINGLE) {
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
