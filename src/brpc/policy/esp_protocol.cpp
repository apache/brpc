// Copyright (c) 2016 Baidu, Inc.
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

#include <google/protobuf/descriptor.h>         // MethodDescriptor
#include <google/protobuf/message.h>            // Message
#include <gflags/gflags.h>

#include "butil/time.h" 
#include "butil/iobuf.h"                         // butil::IOBuf

#include "brpc/controller.h"               // Controller
#include "brpc/socket.h"                   // Socket
#include "brpc/server.h"                   // Server
#include "brpc/span.h"
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/details/usercode_backup_pool.h"
#include "brpc/policy/esp_protocol.h"
#include "brpc/esp_message.h"


namespace brpc {
namespace policy {

ParseResult ParseEspMessage(
        butil::IOBuf* source,
        Socket*, 
        bool /*read_eof*/, 
        const void* /*arg*/) {

    EspHead head;
    const size_t n = source->copy_to((char *)&head, sizeof(head));
    if (n < sizeof(head)) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    uint32_t body_len = head.body_len;
    if (body_len > FLAGS_max_body_size) {
        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
    } else if (source->length() < sizeof(head) + body_len) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    policy::MostCommonMessage* msg = policy::MostCommonMessage::Get();
    source->cutn(&msg->meta, sizeof(head));
    source->cutn(&msg->payload, body_len);
    return MakeMessage(msg);
}

void SerializeEspRequest(
        butil::IOBuf* request_buf, 
        Controller* cntl,
        const google::protobuf::Message* req_base) {

    if (req_base == NULL) {
        return cntl->SetFailed(EREQUEST, "request is NULL");
    }
    ControllerPrivateAccessor accessor(cntl);
    if (req_base->GetDescriptor() != EspMessage::descriptor()) {
        return cntl->SetFailed(EINVAL, "Type of request must be EspMessage");
    }
    if (cntl->response() != NULL &&
        cntl->response()->GetDescriptor() != EspMessage::descriptor()) {
        return cntl->SetFailed(EINVAL, "Type of response must be EspMessage");
    }
    const EspMessage* req = (const EspMessage*)req_base;

    EspHead head = req->head;
    head.body_len = req->body.size();
    request_buf->append(&head, sizeof(head));
    request_buf->append(req->body);
}

void PackEspRequest(butil::IOBuf* packet_buf,
                    SocketMessage**,
                    uint64_t correlation_id,
                    const google::protobuf::MethodDescriptor*,
                    Controller* cntl,
                    const butil::IOBuf& request,
                    const Authenticator* auth) {

    ControllerPrivateAccessor accessor(cntl);
    if (cntl->connection_type() == CONNECTION_TYPE_SINGLE) {
        return cntl->SetFailed(
            EINVAL, "esp protocol can't work with CONNECTION_TYPE_SINGLE");
    }

    accessor.get_sending_socket()->set_correlation_id(correlation_id);
    Span* span = accessor.span();
    if (span) {
        span->set_request_size(request.length());
    }
    
    if (auth != NULL) {
        std::string auth_str;
        auth->GenerateCredential(&auth_str);
        //means first request in this connect, need to special head
        packet_buf->append(auth_str);
    }

    packet_buf->append(request);
}

void ProcessEspResponse(InputMessageBase* msg_base) {
    const int64_t start_parse_us = butil::cpuwide_time_us();
    DestroyingPtr<MostCommonMessage> msg(static_cast<MostCommonMessage*>(msg_base));
    
    // Fetch correlation id that we saved before in `PackEspRequest'
    const CallId cid = { static_cast<uint64_t>(msg->socket()->correlation_id()) };
    Controller* cntl = NULL;
    const int rc = bthread_id_lock(cid, (void**)&cntl);
    if (rc != 0) {
        LOG_IF(ERROR, rc != EINVAL && rc != EPERM)
            << "Fail to lock correlation_id=" << cid << ", " << berror(rc);
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
    // MUST be EspMessage (checked in SerializeEspRequest)
    EspMessage* response = (EspMessage*)cntl->response();
    const int saved_error = cntl->ErrorCode();

    if (response != NULL) {
        msg->meta.copy_to(&response->head, sizeof(EspHead));
        msg->payload.swap(response->body);
        if (response->head.msg != 0) {
            cntl->SetFailed(ENOENT, "esp response head msg != 0");
            LOG(WARNING) << "Server " << msg->socket()->remote_side()
                << " doesn't contain the right data";
        }
    } // else just ignore the response.

    // Unlocks correlation_id inside. Revert controller's
    // error code if it version check of `cid' fails
    msg.reset();  // optional, just release resourse ASAP
    accessor.OnResponse(cid, saved_error);
}

} // namespace policy
} // namespace brpc
