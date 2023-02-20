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

#include "brpc/policy/mongo_protocol.h"

#include <bson/bson.h>
#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>  // MethodDescriptor
#include <google/protobuf/message.h>     // Message

#include "brpc/controller.h"  // Controller
#include "brpc/details/controller_private_accessor.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/usercode_backup_pool.h"
#include "brpc/mongo.h"
#include "brpc/mongo_head.h"
#include "brpc/mongo_service_adaptor.h"
#include "brpc/policy/mongo.pb.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/nshead_protocol.h"
#include "brpc/server.h"  // Server
#include "brpc/socket.h"  // Socket
#include "brpc/span.h"
#include "butil/atomicops.h"
#include "butil/bson_util.h"
#include "butil/iobuf.h"  // butil::IOBuf
#include "butil/time.h"

extern "C" {
void bthread_assign_data(void* data);
}


namespace brpc {
namespace policy {

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
    MongoRequest req;
    MongoResponse res;
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
        adaptor->SerializeError(res.header().response_to(), &res_buf);
    } else if (res.has_message()) {
        mongo_head_t header = {
            res.header().message_length(),
            res.header().request_id(),
            res.header().response_to(),
            res.header().op_code()
        };
        res_buf.append(static_cast<const void*>(&header), sizeof(mongo_head_t));
        int32_t response_flags = res.response_flags();
        int64_t cursor_id = res.cursor_id();
        int32_t starting_from = res.starting_from();
        int32_t number_returned = res.number_returned();
        res_buf.append(&response_flags, sizeof(response_flags));
        res_buf.append(&cursor_id, sizeof(cursor_id));
        res_buf.append(&starting_from, sizeof(starting_from));
        res_buf.append(&number_returned, sizeof(number_returned));
        res_buf.append(res.message());
    }

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

// butil::atomic<unsigned int> global_request_id(0);

ParseResult ParseMongoMessage(butil::IOBuf* source,
                              Socket* socket, bool /*read_eof*/, const void *arg) {
  const MongoServiceAdaptor* adaptor = nullptr;
  if (arg) {
      // server side
      const Server* server = static_cast<const Server*>(arg);
      adaptor = server->options().mongo_service_adaptor;
      if (NULL == adaptor) {
          // The server does not enable mongo adaptor.
          return MakeParseError(PARSE_ERROR_TRY_OTHERS);
      }
  }

    char buf[sizeof(mongo_head_t)];
    const char *p = (const char *)source->fetch(buf, sizeof(buf));
    if (NULL == p) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }
    mongo_head_t header = *(const mongo_head_t*)p;
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
    if (!arg) {
        MongoInputResponse* response_msg = new MongoInputResponse;
        // client side
        // 前面已经读取了mongo_head
        source->pop_front(sizeof(buf));
        if (header.op_code == MONGO_OPCODE_REPLY) {
            LOG(WARNING) << "ParseMongoMessage not support op_code: REPLY";
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        } else if (header.op_code == MONGO_OPCODE_MSG) {
            response_msg->opcode = MONGO_OPCODE_MSG;
            MongoMsg& mongo_msg = response_msg->msg;
            butil::IOBuf msg_buf;
            size_t act_body_len = source->cutn(&msg_buf, body_len - sizeof(buf));
            if (act_body_len != body_len - sizeof(buf)) {
                CHECK(false);
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
            }
            // flagbits  4bytes
            bool flagbits_cut_ret = msg_buf.cutn(&(mongo_msg.flagbits), 4);
            if (!flagbits_cut_ret) {
                CHECK(false);
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
            }
            while (!msg_buf.empty()) {
                Section section;
                bool parse_ret = ParseMongoSection(&msg_buf, &section);
                if (!parse_ret) {
                    LOG(WARNING) << "parse mongo section failed";
                    return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
                }
                mongo_msg.sections.push_back(section);
            }
            return MakeMessage(response_msg);
        } else {
            LOG(WARNING) << "ParseMongoMessage not support op_code:"
                         << header.op_code;
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
    }
    // server side
    // Mongo protocol is a protocol with state. Each connection has its own
    // mongo context. (e.g. last error occured on the connection, the cursor
    // created by the last Query). The context is stored in
    // socket::_input_message, and created at the first time when msg
    // comes over the socket.
    Destroyable *socket_context_msg = socket->parsing_context();
    if (NULL == socket_context_msg) {
        MongoContext *context = adaptor->CreateSocketContext();
        if (NULL == context) {
            return MakeParseError(PARSE_ERROR_NO_RESOURCE);
        }
        socket_context_msg = new MongoContextMessage(context);
        socket->reset_parsing_context(socket_context_msg);
    }
    policy::MostCommonMessage* msg = policy::MostCommonMessage::Get();
    source->cutn(&msg->meta, sizeof(buf));
    size_t act_body_len = source->cutn(&msg->payload, body_len - sizeof(buf));
    if (act_body_len != body_len - sizeof(buf)) {
        CHECK(false);     // Very unlikely, unless memory is corrupted.
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    return MakeMessage(msg);
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
        LOG(WARNING) << "method count:" << srv_des->method_count()
                     << " of MongoService should be equal to 1!";
    }
  
    const Server::MethodProperty *mp =
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
            mongo_done->cntl.SetFailed(ENOMETHOD, "Fail to find default_method");
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
  
        if (!MongoOp_IsValid(header->op_code)) {
            mongo_done->cntl.SetFailed(EREQUEST, "Unknown op_code:%d", header->op_code);
            break;
        }
  
        mongo_done->cntl.set_log_id(header->request_id);
        const std::string &body_str = msg->payload.to_string();
        mongo_done->req.set_message(body_str.c_str(), body_str.size());
        mongo_done->req.mutable_header()->set_message_length(header->message_length);
        mongo_done->req.mutable_header()->set_request_id(header->request_id);
        mongo_done->req.mutable_header()->set_response_to(header->response_to);
        mongo_done->req.mutable_header()->set_op_code(
                static_cast<MongoOp>(header->op_code));
        mongo_done->res.mutable_header()->set_response_to(header->request_id);
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

bool ParseReplicaSetMember(BsonPtr member_ptr, ReplicaSetMember* member) {
  // _id
  bool has_id = butil::bson::bson_get_int32(member_ptr, "_id", &(member->id));
  if (!has_id) {
    LOG(DEBUG) << "not has _id";
    return false;
  }
  // name/addr
  bool has_name =
      butil::bson::bson_get_str(member_ptr, "name", &(member->addr));
  if (!has_name) {
    LOG(DEBUG) << "not has name";
    return false;
  }
  // health
  double health;
  bool has_health = butil::bson::bson_get_double(member_ptr, "health", &health);
  if (!has_health) {
    LOG(DEBUG) << "not has health";
    return false;
  }
  member->health = (health == 1.0);
  // state
  bool has_state =
      butil::bson::bson_get_int32(member_ptr, "state", &(member->state));
  if (!has_state) {
    LOG(DEBUG) << "not has state";
    return false;
  }
  // stateStr
  bool has_stateStr =
      butil::bson::bson_get_str(member_ptr, "stateStr", &(member->state_str));
  if (!has_stateStr) {
    LOG(DEBUG) << "not has stateStr";
    return false;
  }
  return true;
}

static BsonPtr ParseMongoResponseBody(MongoInputResponse* msg, brpc::Controller *cntl)
{
    if (msg->opcode != MONGO_OPCODE_MSG) {
        cntl->SetFailed(ERESPONSE, "msg not msg type");
        return NULL;
    }

    MongoMsg& reply_msg = msg->msg;
    if (reply_msg.sections.size() != 1 || reply_msg.sections[0].type != 0) {
        cntl->SetFailed(ERESPONSE, "error query response");
        return NULL;
    }

    assert(reply_msg.sections[0].body_document);
    BsonPtr document = reply_msg.sections[0].body_document;
    double ok_value = 0.0;
    if (!butil::bson::bson_get_double(document, "ok", &ok_value)) {
        cntl->SetFailed(ERESPONSE, "query response no ok field");
        return NULL;
    }

    if (ok_value != 1) {
        int32_t error_code = 0;
        std::string code_name, errmsg;
        if (butil::bson::bson_get_int32(document, "code", &error_code) &&
            butil::bson::bson_get_str(document, "codeName", &code_name) &&
            butil::bson::bson_get_str(document, "errmsg", &errmsg)) {
            cntl->SetFailed(error_code, "%s(%d), %s",
                            code_name.c_str(), error_code, errmsg.c_str());
        } else {
            cntl->SetFailed(ERESPONSE, "query response failed");
        }
        return NULL;
    }

    return document;
}

static void ProcessMongoQueryResponse(BsonPtr& document,
                                      brpc::Controller* cntl,
                                      bool next_batch) {
    // query success
    BsonPtr cursor_doc;
    if (!butil::bson::bson_get_doc(document, "cursor", &cursor_doc)) {
        cntl->SetFailed(ERESPONSE, "query response no cursor");
        return;
    }

    std::vector<BsonPtr> batch;
    const char* batch_element = next_batch ? "nextBatch" : "firstBatch";

    if (!butil::bson::bson_get_array(cursor_doc, batch_element, &batch)) {
        cntl->SetFailed(ERESPONSE, "query response return null");
        return;
    }

    int64_t cursor_id = 0;
    if (!butil::bson::bson_get_int64(cursor_doc, "id", &cursor_id)) {
        cntl->SetFailed(ERESPONSE, "query response no cursor id");
        return;
    }

    std::string ns;
    if (!butil::bson::bson_get_str(cursor_doc, "ns", &ns)) {
        cntl->SetFailed(ERESPONSE, "query response no ns");
        return;
    }

    // build response
    MongoQueryResponse* response = static_cast<MongoQueryResponse*>(cntl->response());
    if (cursor_id) {
        response->set_cursorid(cursor_id);
    }
    response->set_number_returned(batch.size());
    for (auto element : batch) {
        response->add_documents(element);
    }
    response->set_ns(ns);
}

static void ProcessMongoCountResponse(BsonPtr& document, brpc::Controller* cntl) {
    // count success
    int32_t count = 0;
    if (!butil::bson::bson_get_int32(document, "n", &count)) {
        cntl->SetFailed(ERESPONSE, "count response no n");
        return;
    }

    // build response
    MongoCountResponse* response = static_cast<MongoCountResponse*>(cntl->response());
    response->set_number(count);
}

static void MongoParseUpsertedDocs(BsonPtr& document, std::vector<UpsertedDoc>* upsertedDocs) {
    std::vector<BsonPtr> upserted_docs;
    const char* upserted_docs_element = "upserted";
    if (butil::bson::bson_get_array(document, upserted_docs_element, &upserted_docs)) {
        // build response upserted_docs
        for (BsonPtr upserted_doc_ptr : upserted_docs) {
            UpsertedDoc upserted_doc;
            int32_t index = 0;
            bson_oid_t id;
            if (!butil::bson::bson_get_int32(upserted_doc_ptr, "index", &index)) {
                LOG(WARNING) << "unrecognize update upserted:"
                             << bson_as_canonical_extended_json(
                                    upserted_doc_ptr.get(), nullptr);
                continue;
            }

            upserted_doc.index = index;
            if (!butil::bson::bson_get_oid(upserted_doc_ptr, "_id", &id)) {
                LOG(WARNING) << "unrecognize update upserted:"
                             << bson_as_canonical_extended_json(
                                    upserted_doc_ptr.get(), nullptr);
                continue;
            }
            upserted_doc._id = id;
            upsertedDocs->push_back(upserted_doc);
        }
    }
}

static void MongoParseWriteErrors(BsonPtr& document, std::vector<WriteError>* writeErrors) {
    const char* write_errors_element = "writeErrors";
    std::vector<BsonPtr> write_errors;

    if (!butil::bson::bson_get_array(document, write_errors_element, &write_errors)) {
        return;
    }

    // build response write_errors
    for (BsonPtr write_error_ptr : write_errors) {
        WriteError write_error_record;
        int32_t index = 0;
        int32_t code = 0;
        std::string errmsg;
        if (!butil::bson::bson_get_int32(write_error_ptr, "index", &index)) {
            LOG(WARNING) << "unrecognize insert write_error:"
                         << bson_as_canonical_extended_json(
                                write_error_ptr.get(), nullptr);
            continue;
        }

        write_error_record.index = index;
        if (!butil::bson::bson_get_int32(write_error_ptr, "code", &code)) {
            LOG(WARNING) << "unrecognize insert write_error:"
                         << bson_as_canonical_extended_json(
                                write_error_ptr.get(), nullptr);
            continue;
        }

        write_error_record.code = code;
        if (!butil::bson::bson_get_str(write_error_ptr, "errmsg", &errmsg)) {
            LOG(WARNING) << "unrecognize insert write_error:"
                         << bson_as_canonical_extended_json(write_error_ptr.get(), nullptr);
            continue;
        }

        write_error_record.errmsg = errmsg;
        writeErrors->push_back(write_error_record);
    }
} 

static void ProcessMongoInsertResponse(BsonPtr& document, brpc::Controller* cntl) {
    // insert success
    int32_t insert_number = 0;
    if (!butil::bson::bson_get_int32(document, "n", &insert_number)) {
        cntl->SetFailed(ERESPONSE, "insert response no n");
        return;
    }

    MongoInsertResponse* response = static_cast<MongoInsertResponse*>(cntl->response());
    std::vector<WriteError> write_errors;
    std::vector<UpsertedDoc> upserted_docs;

    MongoParseWriteErrors(document, &write_errors); 

    response->set_number(insert_number);
    for (WriteError& we : write_errors) {
        response->add_write_errors(we);
    }
}

static void ProcessMongoDeleteResponse(BsonPtr& document, brpc::Controller* cntl) {
   // delete success
    int32_t delete_number = 0;
    if (!butil::bson::bson_get_int32(document, "n", &delete_number)) {
        cntl->SetFailed(ERESPONSE, "delete response no n");
        return;
    }

    // build response number
    MongoDeleteResponse* response = static_cast<MongoDeleteResponse*>(cntl->response());
    response->set_number(delete_number);
}

static void ProcessMongoUpdateResponse(BsonPtr& document, brpc::Controller* cntl) {
    // n
    int32_t matched_number = 0;
    if (!butil::bson::bson_get_int32(document, "n", &matched_number)) {
        cntl->SetFailed(ERESPONSE, "update response no n");
        return;
    }

    // nModified
    int32_t modified_number = 0;
    if (!butil::bson::bson_get_int32(document, "nModified", &modified_number)) {
        cntl->SetFailed(ERESPONSE, "update response no nModified");
        return;
    }

    // build response number
    MongoUpdateResponse* response =
        static_cast<MongoUpdateResponse*>(cntl->response());
    std::vector<WriteError> write_errors;
    std::vector<UpsertedDoc> upserted_docs;

    response->set_matched_number(matched_number);
    response->set_modified_number(modified_number);
    MongoParseWriteErrors(document, &write_errors); 
    MongoParseUpsertedDocs(document, &upserted_docs);
    for (WriteError& we : write_errors) {
        response->add_write_errors(we);
    }
    for (UpsertedDoc& ud : upserted_docs) {
        response->add_upserted_docs(ud);
    }

}

static void ProcessMongoFindAndModifyResponse(BsonPtr& document, brpc::Controller* cntl) {
    // lastErrorObject
    BsonPtr last_error_object_ptr;
    if (!butil::bson::bson_get_doc(document, "lastErrorObject", &last_error_object_ptr)) {
        cntl->SetFailed(ERESPONSE, "find_and_modify response no lastErrorObject");
        return;
    }

    // updatedExisting
    bool update_existing = false;
    butil::bson::bson_get_bool(last_error_object_ptr, "updatedExisting",
                               &update_existing);
    // upserted
    bson_oid_t upserted_oid;
    bool has_upserted = butil::bson::bson_get_oid(last_error_object_ptr,
                                                  "upserted", &upserted_oid);
    // value
    std::pair<bool, bson_type_t> value_type_result =
        butil::bson::bson_get_type(document, "value");
    if (!value_type_result.first) {
        cntl->SetFailed(ERESPONSE, "find_and_modify response no value");
        return;
    }

    BsonPtr value;
    if (value_type_result.second == BSON_TYPE_DOCUMENT) {
        bool has_value = butil::bson::bson_get_doc(document, "value", &value);
        if (!has_value) {
            cntl->SetFailed(ERESPONSE, "find_and_modify response no value");
            return;
        }
    } else if (!update_existing &&
               value_type_result.second == BSON_TYPE_NULL) {
    } else {
        cntl->SetFailed(ERESPONSE,
                        "find_and_modify response with updateExisting=true but "
                        "wrong value");
        return;
    }

    // build response
    MongoFindAndModifyResponse* response =
        static_cast<MongoFindAndModifyResponse*>(cntl->response());
    if (value) {
        response->set_value(value);
    }
    if (has_upserted) {
        response->set_upserted(upserted_oid);
    }
}

static void ProcessMongoGetReplSetStatusReponse(BsonPtr& document, brpc::Controller* cntl) {
    std::string set;
    if (!butil::bson::bson_get_str(document, "set", &set)) {
        cntl->SetFailed(ERESPONSE, "get_repl_set_status response no set");
        return;
    }

    int32_t myState;
    if (!butil::bson::bson_get_int32(document, "myState", &myState)) {
        cntl->SetFailed(ERESPONSE, "get_repl_set_status response no myState");
        return;
    }

    // members
    std::vector<BsonPtr> members_ptr;
    if (!butil::bson::bson_get_array(document, "members", &members_ptr)) {
        cntl->SetFailed(ERESPONSE, "get_repl_set_status response no members");
        return;
    }

    // parse member
    std::vector<ReplicaSetMember> members(members_ptr.size());
    for (size_t i = 0; i < members_ptr.size(); ++i) {
        if (!ParseReplicaSetMember(members_ptr[i], &(members[i]))) {
            cntl->SetFailed(ERESPONSE,
                            "parse get_repl_set_status response member fail");
            return;
        }
    }

    // build response
    brpc::MongoGetReplSetStatusResponse* response =
        static_cast<brpc::MongoGetReplSetStatusResponse*>(cntl->response());
    response->set_ok(true);
    response->set_set(set);
    response->set_myState(myState);
    for (ReplicaSetMember member : members) {
        response->add_members(member);
    }
}

// Actions to a server response in mongo format
void ProcessMongoResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MongoInputResponse> msg(
        static_cast<MongoInputResponse*>(msg_base));
  
    const CallId cid = {static_cast<uint64_t>(msg->socket()->correlation_id())};
    Controller* cntl = NULL;
    LOG(DEBUG) << "process mongo response, cid:" << cid.value;
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
        // span->set_response_size(msg->response.ByteSize());
        span->set_start_parse_us(start_parse_us);
    }
  
 
    BsonPtr body = ParseMongoResponseBody(msg.get(), cntl);
    if (!body) {
        accessor.OnResponse(cid, cntl->ErrorCode());
        return;
    }

    // handle response
    if (cntl->request_id() == "query") {
        ProcessMongoQueryResponse(body, cntl, false);
    } else if (cntl->request_id() == "query_getMore") {
        ProcessMongoQueryResponse(body, cntl, true);
    } else if (cntl->request_id() == "count") {
        ProcessMongoCountResponse(body, cntl);
    } else if (cntl->request_id() == "insert") {
        ProcessMongoInsertResponse(body, cntl);
    } else if (cntl->request_id() == "delete") {
        ProcessMongoDeleteResponse(body, cntl);
    } else if (cntl->request_id() == "update") {
        ProcessMongoUpdateResponse(body, cntl);
    } else if (cntl->request_id() == "find_and_modify") {
        ProcessMongoFindAndModifyResponse(body, cntl);
    } else if (cntl->request_id() == "get_repl_set_status") {
        ProcessMongoGetReplSetStatusReponse(body, cntl);
    } else {
        cntl->SetFailed(ERESPONSE, "unsupported msg type: %s", cntl->request_id().c_str());
    }

    accessor.OnResponse(cid, cntl->ErrorCode());
}

// Serialize request into request_buf
void SerializeMongoRequest(butil::IOBuf* request_buf, Controller* cntl,
                           const google::protobuf::Message* request) {
    if (request == nullptr) {
        return cntl->SetFailed(EREQUEST, "request is null");
    }
    if (request->GetDescriptor() != brpc::MongoRequestBase::descriptor()) {
        return cntl->SetFailed(EREQUEST, "Fail to parse request");
    }

    const brpc::MongoRequest* mongo_request = dynamic_cast<const brpc::MongoRequest*>(request);
    if (!mongo_request) {
        return cntl->SetFailed(EREQUEST, "Fail to parse request");
    }

    if (!mongo_request->SerializeTo(request_buf)) {
        return cntl->SetFailed(EREQUEST, "MongoRequest not initialize");
    }
    cntl->set_request_id(mongo_request->RequestId());
    LOG(DEBUG) << "Serialize MongoRequest, length:" << request_buf->length();
    return;
}

// Pack request_buf into msg, call after serialize
void PackMongoRequest(butil::IOBuf* msg, SocketMessage** user_message_out,
                      uint64_t correlation_id,
                      const google::protobuf::MethodDescriptor* method,
                      Controller* controller, const butil::IOBuf& request_buf,
                      const Authenticator* auth) {
  LOG(DEBUG) << "mongo request buf length:" << request_buf.length();
  mongo_head_t request_head;
  request_head.message_length = sizeof(mongo_head_t) + request_buf.length();
  request_head.request_id = static_cast<int32_t>(correlation_id);
  request_head.response_to = 0;
  request_head.op_code = DB_OP_MSG;
  LOG(DEBUG) << "mongo head message_length:" << request_head.message_length
             << ", request_id:" << request_head.request_id;
  request_head.make_network_endian();
  msg->append(static_cast<void*>(&request_head), sizeof(request_head));
  msg->append(request_buf);
  LOG(DEBUG) << "mongo request to send msg length:" << msg->length();
  ControllerPrivateAccessor accessor(controller);
  accessor.get_sending_socket()->set_correlation_id(correlation_id);
}

bool ParseMongoSection(butil::IOBuf* source, Section* section) {
  if (!source || !section) {
    return false;
  }
  if (source->length() < 5) {  // kind(1 byte) + bson size(4 byte)
    return false;
  }
  bool cut_kind_ret = source->cut1(&(section->type));
  if (!cut_kind_ret) {
    return false;
  }
  if (section->type == 0) {
    // Body
    // cut 4byte as bson size
    uint32_t bson_size = 0;
    const void* bson_size_fetch = source->fetch(&bson_size, 4);
    if (!bson_size_fetch) {
      return false;
    }
    bson_size = *(static_cast<const uint32_t*>(bson_size_fetch));
    // tranfrom to host endian
    if (!ARCH_CPU_LITTLE_ENDIAN) {
      bson_size = butil::ByteSwap(bson_size);
    }
    LOG(DEBUG) << "get bson size:" << bson_size
               << " iobuf size:" << source->length();
    if (source->length() < bson_size) {
      return false;
    }
    butil::IOBuf bson_buf;
    bool cut_bson = source->cutn(&bson_buf, bson_size);
    if (!cut_bson) {
      return false;
    }
    std::string bson_str = bson_buf.to_string();
    bson_t* document_ptr = bson_new_from_data(
        reinterpret_cast<const uint8_t*>(bson_str.c_str()), bson_str.length());
    if (!document_ptr) {
      LOG(WARNING) << "bson init failed";
      return false;
    }
    section->body_document = butil::bson::new_bson(document_ptr);
    LOG(DEBUG) << "parse mongo section with type body succ";
    return true;
  } else if (section->type == 1) {
    // Document Sequence
    LOG(WARNING) << "not support document sequence now";
    return false;
  } else {
    return false;
  }
}

}  // namespace policy
}  // namespace brpc
