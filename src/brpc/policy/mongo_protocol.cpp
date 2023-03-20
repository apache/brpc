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
#include "brpc/controller.h"               // Controller
#include "brpc/socket.h"                   // Socket
#include "brpc/server.h"                   // Server
#include "brpc/span.h"
#include "brpc/mongo.h"
#include "brpc/mongo_head.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/mongo_service_adaptor.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/mongo_protocol.h"
#include "brpc/policy/mongo.pb.h"
#include "brpc/details/usercode_backup_pool.h"

extern "C" {
void bthread_assign_data(void* data);
}


namespace brpc {
namespace policy {

class MongoStreamData : public StreamUserData {
public:
    // @StreamUserData
    virtual void DestroyStreamUserData(SocketUniquePtr& sending_sock,
                                       Controller* cntl,
                                       int error_code,
                                       bool end_of_rpc) override;

    void set_request_id(uint32_t request_id) {
        _request_id = request_id;
    }

    void set_correlation_id(uint64_t correlation_id) {
        _correlation_id = correlation_id;
    }

    uint32_t request_id() const {
        return _request_id;
    }
    uint64_t correlation_id() const {
        return _correlation_id;
    }

private:
    uint64_t _correlation_id;
    uint32_t _request_id;
};

class MongoStreamCreator : public StreamCreator {
public:
    // @StreamCreator
    virtual StreamUserData* OnCreatingStream(SocketUniquePtr* inout,
                                             Controller* cntl) override;
    // @StreamCreator
    virtual void DestroyStreamCreator(Controller* cntl) override;
};


struct SendMongoResponse : public google::protobuf::Closure {
    SendMongoResponse(const Server *server) :
        status(NULL),
        received_us(0L),
        server(server) {}
    ~SendMongoResponse();
    void Run();

    MethodStatus* status;
    int64_t received_us;
    const Server *server;
    Controller cntl;
    MongoMessage req;
    MongoMessage res;
};

SendMongoResponse::~SendMongoResponse() {
    LogErrorTextAndDelete(false)(&cntl);
}

void SendMongoResponse::Run() {
    std::unique_ptr<SendMongoResponse> delete_self(this);
    ConcurrencyRemover concurrency_remover(status, &cntl, received_us);
    Socket* socket = ControllerPrivateAccessor(&cntl).get_sending_socket();

    if (cntl.IsCloseConnection()) {
        socket->SetFailed();
        return;
    }
    
    const MongoServiceAdaptor* adaptor =
            server->options().mongo_service_adaptor;
    butil::IOBuf res_buf;
    if (cntl.Failed()) {
        adaptor->SerializeError(res.head().response_to, &res_buf);
    } else if (res.IsInitialized()) {
        mongo_head_t head = res.head();
        head.make_host_endian();
        res_buf.append(&head, sizeof(head));
        res.SerializeToIOBuf(&res_buf);
    }

    // TODO: handle compress
    if (!res_buf.empty()) {
        // Have the risk of unlimited pending responses, in which case, tell
        // users to set max_concurrency.
        Socket::WriteOptions wopt;
        wopt.ignore_eovercrowded = true;
        if (socket->Write(&res_buf, &wopt) != 0) {
            PLOG(WARNING) << "Fail to write into " << *socket;
            return;
        }
    }
}

ParseResult ParseMongoMessage(butil::IOBuf* source,
                              Socket* socket, bool /*read_eof*/, const void *arg) {
    const Server* server = static_cast<const Server*>(arg);
    const MongoServiceAdaptor* adaptor =
        server ? server->options().mongo_service_adaptor : nullptr;
    if (server && !adaptor) {
        // The server does not enable mongo adaptor.
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }

    if (source->size() < sizeof(mongo_head_t)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    mongo_head_t header;
    source->copy_to(&header, sizeof(header));
    header.make_host_endian();
    if (!is_mongo_opcode(header.op_code)) {
        // The op_code plays the role of "magic number" here.
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    if (header.message_length < (int32_t)sizeof(mongo_head_t)) {
        // definitely not a valid mongo packet.
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    uint32_t body_len = static_cast<uint32_t>(header.message_length);
    if (body_len > FLAGS_max_body_size) {
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < body_len) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    // Mongo protocol is a protocol with state. Each connection has its own
    // mongo context. (e.g. last error occured on the connection, the cursor
    // created by the last Query). The context is stored in
    // socket::_input_message, and created at the first time when msg
    // comes over the socket.
    Destroyable *socket_context_msg = socket->parsing_context();
    if (NULL == socket_context_msg && server) {
        MongoContext *context = adaptor->CreateSocketContext();
        if (NULL == context) {
            return MakeParseError(PARSE_ERROR_NO_RESOURCE);
        }
        socket_context_msg = new MongoContextMessage(context);
        socket->reset_parsing_context(socket_context_msg);
    }
    policy::MostCommonMessage* msg = policy::MostCommonMessage::Get();
    source->cutn(&msg->meta, sizeof(header));
    size_t act_body_len = source->cutn(&msg->payload, body_len - sizeof(header));
    if (act_body_len != body_len - sizeof(header)) {
        CHECK(false);     // Very unlikely, unless memory is corrupted.
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    return MakeMessage(msg);
}

void SerializeMongoRequest(butil::IOBuf* buf,
                           Controller* cntl,
                           const google::protobuf::Message* request) {
    if (request == NULL) {
        return cntl->SetFailed(EREQUEST, "Request is NULL");
    }
    if (request->GetDescriptor() != brpc::MongoMessage::descriptor()) {
        return cntl->SetFailed(EREQUEST, "The request is not a MongoMessage");
    }
    const brpc::MongoMessage* mm = static_cast<const brpc::MongoMessage*>(request);
    if (mm->ByteSize() == 0) {
        return cntl->SetFailed(EREQUEST, "request byte size is empty");
    }
    if (!mm->SerializeToIOBuf(buf)) {
        return cntl->SetFailed(EREQUEST, "failed to serialize request");
    }
    cntl->set_stream_creator(butil::get_leaky_singleton<MongoStreamCreator>());
}

void PackMongoRequest(butil::IOBuf *req_buf,
                      SocketMessage** user_message,
                      uint64_t correlation_id,
                      const google::protobuf::MethodDescriptor*,
                      Controller* cntl,
                      const butil::IOBuf& request_body,
                      const Authenticator* auth) {
    ControllerPrivateAccessor accessor(cntl);
    MongoStreamData *stream_data = static_cast<MongoStreamData*>(accessor.get_stream_user_data());
    stream_data->set_correlation_id(correlation_id);
    // TODO(helei): handle compress
    mongo_head_t head;
    head.message_length = sizeof(mongo_head_t) + request_body.size();
    head.request_id = stream_data->request_id();
    head.response_to = 0;
    head.op_code = MONGO_OPCODE_MSG;
    head.make_host_endian();
    req_buf->append(&head, sizeof(head));
    req_buf->append(request_body);
}

// Defined in baidu_rpc_protocol.cpp
void EndRunningCallMethodInPool(
    ::google::protobuf::Service* service,
    const ::google::protobuf::MethodDescriptor* method,
    ::google::protobuf::RpcController* controller,
    const ::google::protobuf::Message* request,
    ::google::protobuf::Message* response,
    ::google::protobuf::Closure* done);

void ProcessMongoRequest(InputMessageBase* msg_base) {
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    SocketUniquePtr socket_guard(msg->ReleaseSocket());
    Socket* socket = socket_guard.get();
    const Server* server = static_cast<const Server*>(msg_base->arg());
    ScopedNonServiceError non_service_error(server);

    char buf[sizeof(mongo_head_t)];
    const char *p = (const char *)msg->meta.fetch(buf, sizeof(buf));
    const mongo_head_t *header = (const mongo_head_t*)p;

    const google::protobuf::ServiceDescriptor* srv_des = MongoService::descriptor();
    if (1 != srv_des->method_count()) {
        LOG(WARNING) << "method count: " << srv_des->method_count()
                     << "of MongoService should be equal to 1!";
    }

    const Server::MethodProperty* mp = 
        ServerPrivateAccessor(server)
        .FindMethodPropertyByFullName(srv_des->method(0)->full_name());

    MongoContextMessage *context_msg =
        dynamic_cast<MongoContextMessage*>(socket->parsing_context());
    if (NULL == context_msg) {
        LOG(WARNING) << "socket context wasn't set correctly";
        return;
    }

    SendMongoResponse* mongo_done = new SendMongoResponse(server);
    mongo_done->cntl.set_mongo_session_data(context_msg->context());

    ControllerPrivateAccessor accessor(&(mongo_done->cntl));
    accessor.set_server(server)
        .set_security_mode(server->options().security_mode())
        .set_peer_id(socket->id())
        .set_remote_side(socket->remote_side())
        .set_local_side(socket->local_side())
        .set_auth_context(socket->auth_context())
        .set_request_protocol(PROTOCOL_MONGO)
        .set_begin_time_us(msg->received_us())
        .move_in_server_receiving_sock(socket_guard);

    // Tag the bthread with this server's key for
    // thread_local_data().
    if (server->thread_local_options().thread_local_data_factory) {
        bthread_assign_data((void*)&server->thread_local_options());
    }
    do {
        if (!server->IsRunning()) {
            mongo_done->cntl.SetFailed(ELOGOFF, "Server is stopping");
            break;
        }

        if (!ServerPrivateAccessor(server).AddConcurrency(&(mongo_done->cntl))) {
            mongo_done->cntl.SetFailed(
                ELIMIT, "Reached server's max_concurrency=%d",
                server->options().max_concurrency);
            break;
        }
        if (FLAGS_usercode_in_pthread && TooManyUserCode()) {
            mongo_done->cntl.SetFailed(ELIMIT, "Too many user code to run when"
                                       " -usercode_in_pthread is on");
            break;
        }

        if (NULL == mp ||
            mp->service->GetDescriptor() == BadMethodService::descriptor()) {
            mongo_done->cntl.SetFailed(ENOMETHOD, "Failed to find default_method");
            break;
        }
        // Switch to service-specific error.
        non_service_error.release();
        MethodStatus* method_status = mp->status;
        mongo_done->status = method_status;
        if (method_status) {
            int rejected_cc = 0;
            if (!method_status->OnRequested(&rejected_cc)) {
                mongo_done->cntl.SetFailed(
                    ELIMIT, "Rejected by %s's ConcurrencyLimiter, concurrency=%d",
                    mp->method->full_name().c_str(), rejected_cc);
                break;
            }
        }
        
        if (!is_mongo_opcode(header->op_code)) {
            mongo_done->cntl.SetFailed(EREQUEST, "Unknown op_code:%d", header->op_code);
            break;
        }
        
        mongo_done->cntl.set_log_id(header->request_id);
        mongo_done->req.head() = *header;
        mongo_done->received_us = msg->received_us();

        google::protobuf::Service* svc = mp->service;
        const google::protobuf::MethodDescriptor* method = mp->method;
        accessor.set_method(method);
        
        if (!FLAGS_usercode_in_pthread) {
            return svc->CallMethod(
                method, &(mongo_done->cntl), &(mongo_done->req),
                &(mongo_done->res), mongo_done);
        }
        if (BeginRunningUserCode()) {
            return svc->CallMethod(
                method, &(mongo_done->cntl), &(mongo_done->req),
                &(mongo_done->res), mongo_done);
            return EndRunningUserCodeInPlace();
        } else {
            return EndRunningCallMethodInPool(
                svc, method, &(mongo_done->cntl), &(mongo_done->req),
                &(mongo_done->res), mongo_done);
        }
    } while (false);

    mongo_done->Run();
}

void MongoStreamData::DestroyStreamUserData(SocketUniquePtr& sending_sock,
                                            Controller* cntl,
                                            int error_code,
                                            bool end_of_rpc) {
    butil::ResourceId<MongoStreamData> slot{ _request_id };
    butil::return_resource(slot);
}

StreamUserData* MongoStreamCreator::OnCreatingStream(SocketUniquePtr* inout,
                                                     Controller* cntl) {
    butil::ResourceId<MongoStreamData> slot;
    MongoStreamData *stream_data = butil::get_resource<MongoStreamData>(&slot);
    stream_data->set_request_id(slot.value);
    return stream_data;
}

void MongoStreamCreator::DestroyStreamCreator(Controller* cntl) {
    // MongoStreamCreator is a global singleton value, Don't delete
    // it in this function.
}

void ProcessMongoResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    mongo_head_t head;
    msg->meta.copy_to(&head, sizeof(head));
    head.make_host_endian();
    butil::ResourceId<MongoStreamData> slot{ head.response_to };
    MongoStreamData* stream_data = butil::address_resource(slot);
    Controller* cntl = NULL;
    if (!stream_data) {
        LOG(ERROR) << "failed to address stream data";
    }
    const bthread_id_t cid = { stream_data->correlation_id() };
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

    const int saved_error = cntl->ErrorCode();
    do {
        if (cntl->response() == NULL) {
            break;
        }
        if (cntl->response()->GetDescriptor() != MongoMessage::descriptor()) {
            cntl->SetFailed(ERESPONSE, "must be mongo response");
            break;
        }
        MongoMessage* rsp = dynamic_cast<MongoMessage*>(cntl->response());
        if (!rsp) {
            cntl->SetFailed(ERESPONSE, "must be mongo response");
            break;
        }
        msg->meta.copy_to(&rsp->head(), sizeof(rsp->head()));
        rsp->head().make_host_endian();
        if (!rsp->ParseFromIOBuf(&msg->payload)) {
            cntl->SetFailed(ERESPONSE, "failed to parse mongo response");
            break;
        }
    } while (false);
    msg.reset();
    accessor.OnResponse(cid, saved_error);
}

}  // namespace policy
} // namespace brpc
