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
#include "brpc/details/server_private_accessor.h"
#include "brpc/span.h"
#include "brpc/errno.pb.h"                 // EREQUEST, ERESPONSE
#include "brpc/details/controller_private_accessor.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/ubrpc2pb_protocol.h"
#include "brpc/compress.h"


namespace brpc {

extern const int64_t IDL_VOID_RESULT;

namespace policy {

static const unsigned int UBRPC_NSHEAD_VERSION = 1000;

void UbrpcAdaptor::ParseNsheadMeta(
    const Server&, const NsheadMessage& request, Controller* cntl,
    NsheadMeta* out_meta) const {
    butil::IOBufAsZeroCopyInputStream zc_stream(request.body);
    mcpack2pb::InputStream stream(&zc_stream);
    if (!::mcpack2pb::unbox(&stream)) {
        cntl->SetFailed(EREQUEST, "Request is not a compack/mcpack2 object");
        return;
    }
    mcpack2pb::ObjectIterator it1(&stream, request.body.size() - stream.popped_bytes());
    bool found_content = false;
    for (; it1 != NULL; ++it1) {
        if (it1->name == "content") {
            found_content = true;
            break;
        }
    }
    if (!found_content) {
        cntl->SetFailed(EREQUEST, "Fail to find request.content");
        return;
    }
    if (it1->value.type() != mcpack2pb::FIELD_ARRAY) {
        cntl->SetFailed(EREQUEST, "Expect request.content to be array, "
                        "actually %s", mcpack2pb::type2str(it1->value.type()));
        return;
    }

    mcpack2pb::ArrayIterator it2(it1->value);
    if (it2 == NULL) {
        cntl->SetFailed(EREQUEST, "Fail to parse request.content as array");
        return;
    }
    std::string service_name;
    std::string method_name;
    bool has_params = false;
    size_t user_req_offset = 0;
    size_t user_req_size = 0;
    for (mcpack2pb::ObjectIterator it3(*it2); it3 != NULL; ++it3) {
        if (it3->name == "service_name") {
            if (it3->value.type() != mcpack2pb::FIELD_STRING) {
                cntl->SetFailed(EREQUEST, "Expect request.content[0].service_name"
                                " to be string, actually %s",
                                mcpack2pb::type2str(it3->value.type()));
                return;
            }
            it3->value.as_string(&service_name, "request.content[0].service_name");
        } else if (it3->name == "method") {
            if (it3->value.type() != mcpack2pb::FIELD_STRING) {
                cntl->SetFailed(EREQUEST, "Expect request.content[0].method"
                                " to be string, actually %s",
                                mcpack2pb::type2str(it3->value.type()));
                return;
            }
            it3->value.as_string(&method_name, "request.content[0].method");
        } else if (it3->name == "id") {
            if (!mcpack2pb::is_primitive(it3->value.type()) ||
                !mcpack2pb::is_integral((mcpack2pb::PrimitiveFieldType)it3->value.type())) {
                cntl->SetFailed(ERESPONSE, "request.content[0].id must be "
                                "integer, actually %s",
                                mcpack2pb::type2str(it3->value.type()));
                return;
            }
            out_meta->set_correlation_id(
                it3->value.as_int64("request.content[0].id"));
        } else if (it3->name == "params") {
            if (it3->value.type() != mcpack2pb::FIELD_OBJECT) {
                cntl->SetFailed(EREQUEST, "Expect request.content[0].params "
                                "to be object, actually %s",
                                mcpack2pb::type2str(it3->value.type()));
                return;
            }
            has_params = true;
            user_req_offset = stream.popped_bytes();
            user_req_size = it3->value.size();
            const size_t stream_end = stream.popped_bytes() + it3->value.size();
            mcpack2pb::ObjectIterator it4(it3->value);
            if (it4 == NULL || it4.field_count() == 0) {
                cntl->SetFailed(EREQUEST, "Nothing in request.content[0].params");
                return;
            }
            if (it4.field_count() == 1) {
                user_req_offset = stream.popped_bytes();
                user_req_size = it4->value.size();
            }
            // Pop left bytes, otherwise ++it3 may complain about
            // "not fully consumed".
            if (stream_end > stream.popped_bytes()) {
                stream.popn(stream_end - stream.popped_bytes());
            }
        }
    }
    if (service_name.empty()) {
        cntl->SetFailed(EREQUEST, "Fail to find request.content[0].service_name");
        return;
    }
    if (method_name.empty()) {
        cntl->SetFailed(EREQUEST, "Fail to find request.content[0].method");
        return;
    }
    if (!has_params) {
        cntl->SetFailed(EREQUEST, "Fail to find request.content[0].params");
        return;
    }

    // Change request.body with the user's request.
    butil::IOBuf& buf = const_cast<butil::IOBuf&>(request.body);
    buf.pop_front(user_req_offset);
    if (buf.size() != user_req_size) {
        if (buf.size() < user_req_size) {
            cntl->SetFailed(EREQUEST, "request_size=%" PRIu64 " is shorter than"
                            "specified=%" PRIu64, (uint64_t)buf.size(),
                            (uint64_t)user_req_size);
            return;
        }
        buf.pop_back(buf.size() - user_req_size);
    }
    std::string full_method_name;
    full_method_name.reserve(service_name.size() + 1 + method_name.size());
    full_method_name.append(service_name);
    full_method_name.push_back('.');
    full_method_name.append(method_name);
    out_meta->set_full_method_name(full_method_name);
}

void UbrpcAdaptor::ParseRequestFromIOBuf(
    const NsheadMeta&, const NsheadMessage& raw_req,
    Controller* cntl, google::protobuf::Message* pb_req) const {
    const std::string& msg_name = pb_req->GetDescriptor()->full_name();
    mcpack2pb::MessageHandler handler = mcpack2pb::find_message_handler(msg_name);
    if (handler.parse_body == NULL) {
        return cntl->SetFailed(EREQUEST, "Fail to find parser of %s",
                               msg_name.c_str());
    }
    butil::IOBufAsZeroCopyInputStream bodystream(raw_req.body);
    if (!handler.parse_body(pb_req, &bodystream, raw_req.body.size())) {
        cntl->SetFailed(EREQUEST, "Fail to parse %s", msg_name.c_str());
        return;
    }
}

static void AppendError(const NsheadMeta& meta,
                        Controller* cntl, butil::IOBuf& buf) {
    butil::IOBufAsZeroCopyOutputStream wrapper(&buf);
    mcpack2pb::OutputStream ostream(&wrapper);
    mcpack2pb::Serializer sr(&ostream);
    sr.begin_object();
    {
        sr.begin_mcpack_array("content", mcpack2pb::FIELD_OBJECT);
        sr.begin_object();
        {
            sr.add_int64("id", meta.correlation_id());
            sr.begin_object("error");
            sr.add_int32("code", cntl->ErrorCode());
            sr.add_string("message", cntl->ErrorText());
            sr.end_object();
        }
        sr.end_object();
        sr.end_array();
    }
    sr.end_object();
    ostream.done();
}

void UbrpcAdaptor::SerializeResponseToIOBuf(
    const NsheadMeta& meta, Controller* cntl,
    const google::protobuf::Message* pb_res, NsheadMessage* raw_res) const {
    CompressType type = cntl->response_compress_type();
    if (type != COMPRESS_TYPE_NONE) {
        LOG(WARNING) << "ubrpc protocol doesn't support compression";
        type = COMPRESS_TYPE_NONE;
    }

    if (pb_res == NULL || cntl->Failed()) {
        if (!cntl->Failed()) {
            cntl->SetFailed(ERESPONSE, "response was not created yet");
        }
        return AppendError(meta, cntl, raw_res->body);
    }
    // TODO: This is optional since serializing already checks.
    // if (!pb_res->IsInitialized()) {
    //     cntl->SetFailed(ERESPONSE, "Missing required fields in response: %s",
    //                     pb_res->InitializationErrorString().c_str());
    //     return AppendError(meta, cntl, raw_res->body);
    // }

    const std::string& msg_name = pb_res->GetDescriptor()->full_name();
    mcpack2pb::MessageHandler handler = mcpack2pb::find_message_handler(msg_name);
    if (handler.serialize_body == NULL) {
        cntl->SetFailed(ERESPONSE, "Fail to find serializer of %s",
                        msg_name.c_str());
        return AppendError(meta, cntl, raw_res->body);
    }

    butil::IOBufAsZeroCopyOutputStream owrapper(&raw_res->body);
    mcpack2pb::OutputStream ostream(&owrapper);
    mcpack2pb::Serializer sr(&ostream);
    sr.begin_object();
    {
        sr.begin_mcpack_array("content", mcpack2pb::FIELD_OBJECT);
        sr.begin_object();
        {
            sr.add_int64("id", meta.correlation_id());
            if (cntl->idl_result() != IDL_VOID_RESULT) {
                // Set `result' only when idl_result is (probably) changed.
                // There's definitely false negative, but it should be OK in
                // most cases.
                sr.add_int64("result", cntl->idl_result());
            }
            sr.begin_object("result_params");
            const char* const response_name = cntl->idl_names().response_name;
            if (response_name != NULL && *response_name) {
                sr.begin_object(response_name);
                handler.serialize_body(*pb_res, sr, _format);
                sr.end_object();
            } else {
                handler.serialize_body(*pb_res, sr, _format);
            }
            sr.end_object();
        }
        sr.end_object();
        sr.end_array();
    }
    sr.end_object();
    ostream.done();
    if (!sr.good()) {
        cntl->SetFailed(ERESPONSE, "Fail to serialize %s", msg_name.c_str());
        raw_res->body.clear();
        return AppendError(meta, cntl, raw_res->body);
    }
}

static void ParseResponse(Controller* cntl, butil::IOBuf& buf,
                          google::protobuf::Message* res) {
    if (res == NULL) {
        // silently ignore response.
        return;
    }
    const std::string& msg_name = res->GetDescriptor()->full_name();
    mcpack2pb::MessageHandler handler = mcpack2pb::find_message_handler(msg_name);
    if (handler.parse_body == NULL) {
        return cntl->SetFailed(ERESPONSE, "Fail to find parser of %s",
                               msg_name.c_str());
    }
    butil::IOBufAsZeroCopyInputStream zc_stream(buf);
    mcpack2pb::InputStream stream(&zc_stream);
    if (!::mcpack2pb::unbox(&stream)) {
        cntl->SetFailed(ERESPONSE, "Response is not a compack/mcpack2 object");
        return;
    }
    mcpack2pb::ObjectIterator it1(&stream, buf.size() - stream.popped_bytes());
    bool found_content = false;
    for (; it1 != NULL; ++it1) {
        if (it1->name == "content") {
            found_content = true;
            break;
        }
    }
    if (!found_content) {
        cntl->SetFailed(ERESPONSE, "Fail to find response.content");
        return;
    }
    if (it1->value.type() != mcpack2pb::FIELD_ARRAY) {
        cntl->SetFailed(ERESPONSE, "Expect response.content to be array,"
                        " actually %s", mcpack2pb::type2str(it1->value.type()));
        return;
    }
    mcpack2pb::ArrayIterator it2(it1->value);
    if (it2 == NULL) {
        cntl->SetFailed("Fail to parse response.content as array");
        return;
    }
    bool has_result_params = false;
    size_t user_res_offset = 0;
    size_t user_res_size = 0;
    const char* response_name = "result_params";
    for (mcpack2pb::ObjectIterator it3(*it2); it3 != NULL; ++it3) {
        if (it3->name == "error") {
            if (it3->value.type() != mcpack2pb::FIELD_OBJECT) {
                cntl->SetFailed(ERESPONSE, "Expect response.content[0].error"
                                " to be object, actually %s",
                                mcpack2pb::type2str(it3->value.type()));
                return;
            }
            int32_t code = 0;
            std::string msg;
            for (mcpack2pb::ObjectIterator it4(it3->value); it4 != NULL; ++it4) {
                if (it4->name == "code") {
                    if (!mcpack2pb::is_primitive(it4->value.type()) ||
                        !mcpack2pb::is_integral(
                            (mcpack2pb::PrimitiveFieldType)it4->value.type())) {
                        cntl->SetFailed(
                            ERESPONSE, "Expect response.content[0].error.code "
                            "to be integer, actually %s",
                            mcpack2pb::type2str(it4->value.type()));
                        return;
                    }
                    code = it4->value.as_int32("response.content[0].error.code");
                    if (code == 0) {
                        cntl->SetFailed(ERESPONSE,
                                        "response.content[0].error.code is 0");
                        return;
                    }
                } else if (it4->name == "message") {
                    if (it4->value.type() != mcpack2pb::FIELD_STRING) {
                        cntl->SetFailed(
                            ERESPONSE, "Expect response.content[0].error."
                            "message to be string, actually %s",
                            mcpack2pb::type2str(it4->value.type()));
                        return;
                    }
                    it4->value.as_string(&msg, "response.content[0].error.message");
                } // else field "data" (probably non-ASCII) is ignored.
            }
            if (code == 0) {
                cntl->SetFailed(ERESPONSE,
                                "Fail to find response.content[0].error.code");
                return;
            }
            if (msg.empty()) {
                cntl->SetFailed(ERESPONSE,
                                "Fail to find response.content[0].error.message");
                return;
            }
            cntl->SetFailed(code, "%s", msg.c_str());
            return; // no need to parse left fields.
        } else if (it3->name == "result") {
            if (!mcpack2pb::is_primitive(it3->value.type()) ||
                !mcpack2pb::is_integral((mcpack2pb::PrimitiveFieldType)it3->value.type())) {
                cntl->SetFailed(ERESPONSE, "Expect response.content[0].result"
                                " to be integer, actually %s",
                                mcpack2pb::type2str(it3->value.type()));
                return;
            }
            cntl->set_idl_result(it3->value.as_int64("response.content[0].result"));
        } else if (it3->name == "result_params") {
            if (it3->value.type() != mcpack2pb::FIELD_OBJECT) {
                cntl->SetFailed(ERESPONSE, "Expect response.content[0].result_params"
                                " to be object, actually %s",
                                mcpack2pb::type2str(it3->value.type()));
                return;
            }
            has_result_params = true;
            user_res_offset = stream.popped_bytes();
            user_res_size = it3->value.size();
            const size_t stream_end = stream.popped_bytes() + it3->value.size();
            const char* const expname = cntl->idl_names().response_name;
            if (expname != NULL && *expname) {
                mcpack2pb::ObjectIterator it4(it3->value);
                bool found_response_name = false;
                for (; it4 != NULL; ++it4) {
                    if (it4->name == expname) {
                        found_response_name = true;
                        break;
                    }
                }
                if (!found_response_name) {
                    cntl->SetFailed(ERESPONSE, "Fail to find response."
                                    "content[0].result_params.%s", expname);
                    return;
                }
                response_name = expname;
                user_res_offset = stream.popped_bytes();
                user_res_size = it4->value.size();
            }
            // Pop left bytes, otherwise ++it3 may complain about
            // "not fully consumed".
            if (stream_end > stream.popped_bytes()) {
                stream.popn(stream_end - stream.popped_bytes());
            }
        }
    }
    if (!has_result_params) {
        cntl->SetFailed(ERESPONSE,
                        "Fail to find response.content[0].result_params");
        return;
    }

    buf.pop_front(user_res_offset);
    if (buf.size() != user_res_size) {
        if (buf.size() < user_res_size) {
            cntl->SetFailed(ERESPONSE, "response_size=%" PRIu64 " is shorter "
                            "than specified=%" PRIu64, (uint64_t)buf.size(),
                            (uint64_t)user_res_size);
            return;
        }
        buf.pop_back(buf.size() - user_res_size);
    }
    butil::IOBufAsZeroCopyInputStream bufstream(buf);
    if (!handler.parse_body(res, &bufstream, buf.size())) {
        cntl->SetFailed(ERESPONSE, "Fail to parse %s from response.content[0]."
                        "result_params.%s", msg_name.c_str(), response_name);
        return;
    }
}

void ProcessUbrpcResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    Socket* socket = msg->socket();
    
    // Fetch correlation id that we saved before in `PackUbrpcRequest'
    const bthread_id_t cid = { static_cast<uint64_t>(socket->correlation_id()) };
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
        span->set_response_size(msg->meta.size() + msg->payload.size());
        span->set_start_parse_us(start_parse_us);
    }
    const int saved_error = cntl->ErrorCode();
    ParseResponse(cntl, msg->payload, cntl->response());
    
    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
} 

static void SerializeUbrpcRequest(butil::IOBuf* buf, Controller* cntl,
                                  const google::protobuf::Message* request,
                                  mcpack2pb::SerializationFormat format) {
    CompressType type = cntl->request_compress_type();
    if (type != COMPRESS_TYPE_NONE) {
        return cntl->SetFailed(
            EREQUEST, "ubrpc protocol doesn't support compression");
    }
    if (cntl->method() == NULL) {
        return cntl->SetFailed(ENOMETHOD, "method is NULL");
    }
    const std::string& msg_name = request->GetDescriptor()->full_name();
    mcpack2pb::MessageHandler handler = mcpack2pb::find_message_handler(msg_name);
    if (handler.serialize_body == NULL) {
        return cntl->SetFailed(EREQUEST, "Fail to find serializer of %s",
                               msg_name.c_str());
    }

    butil::IOBufAsZeroCopyOutputStream owrapper(buf);
    mcpack2pb::OutputStream ostream(&owrapper);
    mcpack2pb::Serializer sr(&ostream);
    sr.begin_object();
    {
        sr.begin_object("header");
        sr.add_bool("connection",
                    cntl->connection_type() == CONNECTION_TYPE_POOLED);
        sr.end_object();

        sr.begin_mcpack_array("content", mcpack2pb::FIELD_OBJECT);
        sr.begin_object();
        {
            sr.add_string("service_name", cntl->method()->service()->name());
            sr.add_int64("id", cntl->call_id().value);
            sr.add_string("method", cntl->method()->name());
            sr.begin_object("params");
            const char* const request_name = cntl->idl_names().request_name;
            if (request_name != NULL && *request_name) {
                sr.begin_object(request_name);
                handler.serialize_body(*request, sr, format);
                sr.end_object();
            } else {
                handler.serialize_body(*request, sr, format);
            }
            sr.end_object();
        }
        sr.end_object();
        sr.end_array();
    }
    sr.end_object();
    ostream.done();
    if (!sr.good()) {
        return cntl->SetFailed(EREQUEST, "Fail to serialize %s",
                               msg_name.c_str());
    }
}

void SerializeUbrpcCompackRequest(butil::IOBuf* buf, Controller* cntl,
                                  const google::protobuf::Message* request) {
    return SerializeUbrpcRequest(buf, cntl, request, mcpack2pb::FORMAT_COMPACK);
}

void SerializeUbrpcMcpack2Request(butil::IOBuf* buf, Controller* cntl,
                                  const google::protobuf::Message* request) {
    return SerializeUbrpcRequest(buf, cntl, request, mcpack2pb::FORMAT_MCPACK_V2);
}

void PackUbrpcRequest(butil::IOBuf* buf,
                      SocketMessage**,
                      uint64_t correlation_id,
                      const google::protobuf::MethodDescriptor*,
                      Controller* controller,
                      const butil::IOBuf& request,
                      const Authenticator* /*not supported*/) {
    ControllerPrivateAccessor accessor(controller);
    if (controller->connection_type() == CONNECTION_TYPE_SINGLE) {
        return controller->SetFailed(
            EINVAL, "ubrpc protocol can't work with CONNECTION_TYPE_SINGLE");
    }
    // Store `correlation_id' into Socket since ubrpc protocol doesn't
    // contain this field
    accessor.get_sending_socket()->set_correlation_id(correlation_id);
        
    nshead_t nshead;
    memset(&nshead, 0, sizeof(nshead_t));
    nshead.log_id = controller->log_id();
    nshead.magic_num = NSHEAD_MAGICNUM;
    nshead.body_len = request.size();
    nshead.version = UBRPC_NSHEAD_VERSION;
    buf->append(&nshead, sizeof(nshead));

    // Span* span = accessor.span();
    // if (span) {
    //     request_meta->set_trace_id(span->trace_id());
    //     request_meta->set_span_id(span->span_id());
    //     request_meta->set_parent_span_id(span->parent_span_id());
    // }
    buf->append(request);
}

}  // namespace policy
} // namespace brpc
