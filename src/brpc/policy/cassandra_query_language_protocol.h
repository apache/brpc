// Copyright (c) 2019 Iqiyi, Inc.
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

// Authors: Daojin, Cai (caidaojin@qiyi.com)

#ifndef BRPC_POLICY_CASSANDRA_QUERY_LANGUAGE_PROTOCOL_H
#define BRPC_POLICY_CASSANDRA_QUERY_LANGUAGE_PROTOCOL_H

#include "brpc/protocol.h"

namespace brpc {
namespace policy {

// Parse cql response.
ParseResult ParseCqlMessage(butil::IOBuf* source, Socket *socket, bool read_eof,
                            const void *arg);

// Actions to a cql response.
void ProcessCqlResponse(InputMessageBase* msg);

// Serialize a cql request.
void SerializeCqlRequest(butil::IOBuf* buf,
                         Controller* cntl,
                         const google::protobuf::Message* request);

// Pack `request' to `method' into `buf'.
void PackCqlRequest(butil::IOBuf* buf,
                    SocketMessage**,
                    uint64_t correlation_id,
                    const google::protobuf::MethodDescriptor* method,
                    Controller* controller,
                    const butil::IOBuf& request,
                    const Authenticator* auth);

const std::string& GetCqlMethodName(
    const google::protobuf::MethodDescriptor*,
    const Controller*);

}  // namespace policy
} // namespace brpc

#endif  // BRPC_POLICY_CASSANDRA_QUERY_LANGUAGE_PROTOCOL_H
