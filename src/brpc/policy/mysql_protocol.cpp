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
#include "brpc/details/server_private_accessor.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/details/usercode_backup_pool.h"
#include "butil/sys_byteorder.h"
#include "brpc/policy/mysql_protocol.h"


namespace brpc {
namespace policy {

void ServerSendInitialPacketDemo(Socket* socket) {
    Socket::WriteOptions wopt;
    butil::IOBuf init_packet;
    init_packet.append("server initial packet");
    socket->Write(&init_packet, &wopt);
}

ParseResult ParseMysqlMessage(
        butil::IOBuf* source,
        Socket* socket, 
        bool /*read_eof*/, 
        const void* /*arg*/) {
    LOG(INFO) << source->to_string();
    
    Socket::WriteOptions wopt;
    butil::IOBuf content1;
    content1.append("the first packet");
    socket->Write(&content1, &wopt);
//
//    EspHead head;
//    const size_t n = source->copy_to((char *)&head, sizeof(head));
//    if (n < sizeof(head)) {
//        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
//    }
//
//    uint32_t body_len = head.body_len;
//    if (body_len > FLAGS_max_body_size) {
//        return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
//    } else if (source->length() < sizeof(head) + body_len) {
//        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
//    }
//
//    policy::MostCommonMessage* msg = policy::MostCommonMessage::Get();
//    source->cutn(&msg->meta, sizeof(head));
//    source->cutn(&msg->payload, body_len);
//    return MakeMessage(msg);

      return MakeParseError(PARSE_ERROR_TOO_BIG_DATA);
}

void ProcessMysqlRequest(InputMessageBase* msg_base) {
}

bool VerifyMysqlRequest(const InputMessageBase* msg_base) {
    const MostCommonMessage* msg =
        static_cast<const MostCommonMessage*>(msg_base);
    const Server* server = static_cast<const Server*>(msg->arg());
    const Authenticator* auth = server->options().auth;
    if (!auth) {
        // Fast pass (no authentication)
        return true;
    }

    Socket* socket = msg->socket();
    if (auth->VerifyCredential(
            msg->payload.to_string(), // payload is mysql auth repoonse packet body
            socket->remote_side(),
            socket->mutable_auth_context()) != 0) {
        return false;
    }

    return true;
}

} // namespace policy
} // namespace brpc
