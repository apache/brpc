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

#ifndef BRPC_POLICY_MONGO_PROTOCOL_H
#define BRPC_POLICY_MONGO_PROTOCOL_H

#include "brpc/input_messenger.h"
#include "brpc/mongo.h"
#include "brpc/protocol.h"

namespace brpc {
namespace policy {

struct MongoInputResponse : public InputMessageBase {
  int32_t opcode;
  MongoReply reply;
  MongoMsg msg;

  // @InputMessageBase
  void DestroyImpl() { delete this; }
};

// Parse binary format of mongo
ParseResult ParseMongoMessage(butil::IOBuf* source, Socket* socket,
                              bool read_eof, const void* arg);

// Actions to a (client) request in mongo format
void ProcessMongoRequest(InputMessageBase* msg);

// Actions to a server response in mongo format
void ProcessMongoResponse(InputMessageBase* msg_base);

// Serialize query request
void SerializeMongoQueryRequest(butil::IOBuf* request_buf, Controller* cntl,
                                const MongoQueryRequest* request);

// Serialize getMore request
void SerializeMongoGetMoreRequest(butil::IOBuf* request_buf, Controller* cntl,
                                  const MongoGetMoreRequest* request);

// Serialize count request
void SerializeMongoCountRequest(butil::IOBuf* request_buf, Controller* cntl,
                                const MongoCountRequest* request);

// Serialize insert request
void SerializeMongoInsertRequest(butil::IOBuf* request_buf, Controller* cntl,
                                 const MongoInsertRequest* request);

// Serialize delete request
void SerializeMongoDeleteRequest(butil::IOBuf* request_buf, Controller* cntl,
                                 const MongoDeleteRequest* request);

// Serialize update request
void SerializeMongoUpdateRequest(butil::IOBuf* request_buf, Controller* cntl,
                                 const MongoUpdateRequest* request);

// Serialize request into request_buf
void SerializeMongoRequest(butil::IOBuf* request_buf, Controller* cntl,
                           const google::protobuf::Message* request);

// Pack request_buf into msg, call after serialize
void PackMongoRequest(butil::IOBuf* msg, SocketMessage** user_message_out,
                      uint64_t correlation_id,
                      const google::protobuf::MethodDescriptor* method,
                      Controller* controller, const butil::IOBuf& request_buf,
                      const Authenticator* auth);

// Parse Mongo Sections
bool ParseMongoSection(butil::IOBuf* source, Section* section);

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_MONGO_PROTOCOL_H
