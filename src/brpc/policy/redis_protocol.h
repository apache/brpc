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


#ifndef BRPC_POLICY_REDIS_PROTOCOL_H
#define BRPC_POLICY_REDIS_PROTOCOL_H

#include "brpc/protocol.h"


namespace brpc {
namespace policy {

// Parse redis response.
ParseResult ParseRedisMessage(butil::IOBuf* source, Socket *socket, bool read_eof,
                              const void *arg);

// Actions to a redis response.
void ProcessRedisResponse(InputMessageBase* msg);

// Actions to a redis request, which is left unimplemented.
// All requests are processed in execution queue pushed in
// the parsing process. This function must be declared since
// server only enables redis as a server-side protocol when
// this function is declared.
void ProcessRedisRequest(InputMessageBase* msg);

// Serialize a redis request.
void SerializeRedisRequest(butil::IOBuf* buf,
                           Controller* cntl,
                           const google::protobuf::Message* request);

// Pack `request' to `method' into `buf'.
void PackRedisRequest(butil::IOBuf* buf,
                      SocketMessage**,
                      uint64_t correlation_id,
                      const google::protobuf::MethodDescriptor* method,
                      Controller* controller,
                      const butil::IOBuf& request,
                      const Authenticator* auth);

const std::string& GetRedisMethodName(
    const google::protobuf::MethodDescriptor*,
    const Controller*);

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_REDIS_PROTOCOL_H
