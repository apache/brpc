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

// Authors: Rujie Jiang (jiangrujie@baidu.com)

#include <google/protobuf/descriptor.h>            // MethodDescriptor
#include <google/protobuf/message.h>               // Message
#include <gflags/gflags.h>
#include "butil/third_party/snappy/snappy.h"        // snappy::Compress
#include "butil/time.h"
#include "brpc/controller.h"                       // Controller
#include "brpc/socket.h"                           // Socket
#include "brpc/server.h"                           // Server
#include "brpc/details/server_private_accessor.h"
#include "brpc/span.h"
#include "brpc/compress.h"                          // ParseFromCompressedData
#include "brpc/details/controller_private_accessor.h"
#include "brpc/policy/public_pbrpc_meta.pb.h"       // PublicRpcRequestMeta
#include "brpc/policy/public_pbrpc_protocol.h"
#include "brpc/policy/most_common_message.h"


namespace brpc {
namespace policy {

// Notes on public_pbrpc Protocol:
// 1 - It's based on nshead whose request has special `version' and
//     `provider' field. However, these fields are not checked at
//     server side
// 2 - The whole nshead body is a single protobuf `PublicPbrpcRequest',
//     which contains `RequestHead' and `RequestBody' (no more than 1
//     `RequestBody' although it's repeated). The user protobuf request
//     is jammed in `RequestBody::serialized_request'. These rules are
//     the same for `PublicPbrpcResponse'
// 3 - Contains unique `id' field in each request/response
// 4 - Lots of fields such as `charset' seem to be useless
// 5 - Only support snappy compression. No authentication

static const std::string VERSION = "pbrpc=1.0";
static const std::string CHARSET = "utf-8";
static const std::string SUCCESS_TEXT = "success";
static const char* TIME_FORMAT = "%Y%m%d%H%M%S";
static const char* PROVIDER = "__pbrpc__";
static const uint32_t CONTENT_TYPE = 1;
static const uint32_t COMPRESS_TYPE = 1;
static const uint32_t NSHEAD_VERSION = 1000;

void PublicPbrpcServiceAdaptor::ParseNsheadMeta(
    const Server& svr, const NsheadMessage& request, Controller* cntl,
    NsheadMeta* out_meta) const {
    PublicPbrpcRequest pbreq;
    if (!ParsePbFromIOBuf(&pbreq, request.body)) {
        cntl->CloseConnection("Fail to parse from PublicPbrpcRequest");
        return;
    }
    if (pbreq.requestbody_size() == 0) {
        cntl->CloseConnection("Missing request body inside PublicPbrpcRequest");
        return;
    }
    const RequestHead& head = pbreq.requesthead();
    const RequestBody& body = pbreq.requestbody(0);
    const Server::MethodProperty *sp = ServerPrivateAccessor(&svr)
        .FindMethodPropertyByNameAndIndex(body.service(), body.method_id());
    if (NULL == sp) {
        cntl->SetFailed(ENOMETHOD, "Fail to find method by service=%s method_id=%u",
                        body.service().c_str(), body.method_id());
        return;
    }
    out_meta->set_full_method_name(sp->method->full_name());
    out_meta->set_correlation_id(body.id());
    if (head.has_log_id()) {
        out_meta->set_log_id(head.log_id());
    }
    if (head.compress_type() == COMPRESS_TYPE) {
        out_meta->set_compress_type(COMPRESS_TYPE_SNAPPY);
    }
    // Store `RequestBody::version' field into `NsheadMeta::user_string'
    out_meta->set_user_string(body.version());

    // HACK! Clear `request.body' and append only protobuf request data into it,
    // which will be passed into `ParseRequestFromIOBuf'. As a result, we can
    // avoid parsing `PublicPbrpcRequest' twice
    NsheadMessage& mutable_req = const_cast<NsheadMessage&>(request);
    mutable_req.body.clear();
    mutable_req.body.append(body.serialized_request());
}

void PublicPbrpcServiceAdaptor::ParseRequestFromIOBuf(
    const NsheadMeta& meta, const NsheadMessage& raw_req,
    Controller* cntl, google::protobuf::Message* pb_req) const {
    CompressType type = meta.compress_type();
    if (!ParseFromCompressedData(raw_req.body, pb_req, type)) {
        cntl->SetFailed(EREQUEST, "Fail to parse request message, "
                        "CompressType=%s, request_size=%" PRIu64,
                        CompressTypeToCStr(type),
                        (uint64_t)raw_req.body.length());
    } else {
        cntl->set_request_compress_type(type);
    }
}

void PublicPbrpcServiceAdaptor::SerializeResponseToIOBuf(
    const NsheadMeta& meta, Controller* cntl,
    const google::protobuf::Message* pb_res, NsheadMessage* raw_res) const {
    PublicPbrpcResponse whole_res;
    ResponseHead* head = whole_res.mutable_responsehead();
    ResponseBody* body = whole_res.add_responsebody();

    head->set_from_host(butil::ip2str(butil::my_ip()).c_str());
    body->set_version(meta.user_string());
    body->set_id(meta.correlation_id());
    if (cntl->Failed()) {
        head->set_code(cntl->ErrorCode());
        head->set_text(cntl->ErrorText());
    } else {
        head->set_code(0);
        head->set_text(SUCCESS_TEXT);
        std::string* response_str = body->mutable_serialized_response();
        if (!pb_res->SerializeToString(response_str)) {
            cntl->CloseConnection("Close connection due to failure of "
                                     "serializing user's response");
            return;
        }
        if (cntl->response_compress_type() == COMPRESS_TYPE_SNAPPY) {
            std::string tmp;
            butil::snappy::Compress(response_str->data(), response_str->size(), &tmp);
            response_str->swap(tmp);
            head->set_compress_type(COMPRESS_TYPE);
        }
    }
    butil::IOBufAsZeroCopyOutputStream wrapper(&raw_res->body);
    if (!whole_res.SerializeToZeroCopyStream(&wrapper)) {
        cntl->CloseConnection("Close connection due to failure of "
                                 "serializing the whole response");
        return;
    }
}

void ProcessPublicPbrpcResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    
    PublicPbrpcResponse pbres;
    if (!ParsePbFromIOBuf(&pbres, msg->payload)) {
        LOG(WARNING) << "Fail to parse from PublicPbrpcResponse";
        return;
    }
    if (pbres.responsebody_size() == 0) {
        LOG(WARNING) << "Missing response body inside PublicPbrpcResponse";
        return;
    }
    const ResponseHead& head = pbres.responsehead();
    const ResponseBody& body = pbres.responsebody(0);
    const bthread_id_t cid = { static_cast<uint64_t>(body.id()) };
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
    if (head.code() != 0) {
        // If error_code is unset, default is 0 = success.
        cntl->SetFailed(head.code(), "%s", head.text().c_str());
    } else {
        // Parse response message iff error code from meta is 0
        const std::string& res_data = body.serialized_response();
        CompressType type = (head.compress_type() == COMPRESS_TYPE ?
                             COMPRESS_TYPE_SNAPPY : COMPRESS_TYPE_NONE);
        bool parse_result = false;
        if (type == COMPRESS_TYPE_SNAPPY) {
            butil::IOBuf tmp;
            tmp.append(res_data);
            parse_result = ParseFromCompressedData(tmp, cntl->response(), type);
        } else {
            parse_result = ParsePbFromString(cntl->response(), res_data);
        }
        if (!parse_result) {
            cntl->SetFailed(ERESPONSE, "Fail to parse response message, "
                                  "CompressType=%s, response_size=%" PRIu64, 
                                  CompressTypeToCStr(type),
                                  (uint64_t)res_data.length());
        } else {
            cntl->set_response_compress_type(type);
        }
    }
    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

void SerializePublicPbrpcRequest(butil::IOBuf* buf, Controller* cntl,
                                 const google::protobuf::Message* request) {
    CompressType type = cntl->request_compress_type();
    if (type != COMPRESS_TYPE_NONE && type != COMPRESS_TYPE_SNAPPY) {
        cntl->SetFailed(EREQUEST, "public_pbrpc doesn't support "
                        "compress type=%d", type);
        return;
    }
    return SerializeRequestDefault(buf, cntl, request);
}
       
void PackPublicPbrpcRequest(butil::IOBuf* buf,
                            SocketMessage**,
                            uint64_t correlation_id,
                            const google::protobuf::MethodDescriptor* method,
                            Controller* controller,
                            const butil::IOBuf& request,
                            const Authenticator* /*not supported*/) {
    PublicPbrpcRequest pbreq;
    RequestHead* head = pbreq.mutable_requesthead();
    RequestBody* body = pbreq.add_requestbody();
    butil::IOBufAsZeroCopyOutputStream request_stream(buf);

    head->set_from_host(butil::ip2str(butil::my_ip()).c_str());
    head->set_content_type(CONTENT_TYPE);
    bool short_connection = (controller->connection_type() == CONNECTION_TYPE_SHORT);
    head->set_connection(!short_connection);
    head->set_charset(CHARSET);
    char time_buf[128];
    time_t now = time(NULL);
    strftime(time_buf, sizeof(time_buf), TIME_FORMAT, localtime(&now));
    head->set_create_time(time_buf);
    if (controller->has_log_id()) {
        head->set_log_id(controller->log_id());
    }
    if (controller->request_compress_type() == COMPRESS_TYPE_SNAPPY) {
        head->set_compress_type(COMPRESS_TYPE);
    }

    body->set_version(VERSION);
    body->set_charset(CHARSET);
    body->set_service(method->service()->name());
    body->set_method_id(method->index());    
    body->set_id(correlation_id);
    std::string* request_str = body->mutable_serialized_request();
    request.copy_to(request_str);

    nshead_t nshead;
    memset(&nshead, 0, sizeof(nshead_t));
    nshead.log_id = controller->log_id();
    nshead.magic_num = NSHEAD_MAGICNUM;
    snprintf(nshead.provider, sizeof(nshead.provider), "%s", PROVIDER);
    nshead.version = NSHEAD_VERSION;
    nshead.body_len = pbreq.ByteSize();
    buf->append(&nshead, sizeof(nshead));

    Span* span = ControllerPrivateAccessor(controller).span();
    if (span) {
        // TODO: Nowhere to set tracing ids.
        // request_meta->set_trace_id(span->trace_id());
        // request_meta->set_span_id(span->span_id());
        // request_meta->set_parent_span_id(span->parent_span_id());
    }

    if (!pbreq.SerializeToZeroCopyStream(&request_stream)) {
        controller->SetFailed(EREQUEST, "Fail to serialize PublicPbrpcRequest");
        return;
    }
}

}  // namespace policy
} // namespace brpc

