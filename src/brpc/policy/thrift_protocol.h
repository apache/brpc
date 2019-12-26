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

// Authors: wangxuefeng (wangxuefeng@didichuxing.com)

#ifndef BRPC_POLICY_THRIFT_PROTOCOL_H
#define BRPC_POLICY_THRIFT_PROTOCOL_H

#include "brpc/protocol.h"

namespace brpc {
namespace policy {

enum class ThriftProtocolType {
    THRIFT_PROTOCOL_BINARY,
    THRIFT_PROTOCOL_COMPACT,
};

// Parse binary protocol format of thrift framed
ParseResult ParseThriftBinaryMessage(butil::IOBuf* source, Socket* socket,
                                     bool read_eof, const void *arg);

// Parse compact protocol format of thrift framed
ParseResult ParseThriftCompactMessage(butil::IOBuf* source, Socket* socket,
                                      bool read_eof, const void *arg);

// Actions to a (client) request in thrift binary framed format
void ProcessThriftRequest(InputMessageBase* msg);

// Actions to a (client) request in thrift compact framed format
void ProcessThriftCompactRequest(InputMessageBase* msg);

// Actions to a (server) response in thrift binary framed format
void ProcessThriftResponse(InputMessageBase* msg);

// Actions to a (server) response in thrift compact framed format
void ProcessThriftCompactResponse(InputMessageBase* msg);

void SerializeThriftBinaryRequest(butil::IOBuf* request_buf,
                                  Controller* controller,
                                  const google::protobuf::Message* request);

void SerializeThriftCompactRequest(butil::IOBuf* request_buf,
                                   Controller* controller,
                                   const google::protobuf::Message* request);

void PackThriftRequest(
    butil::IOBuf* packet_buf,
    SocketMessage**,
    uint64_t correlation_id,
    const google::protobuf::MethodDescriptor*,
    Controller* controller,
    const butil::IOBuf&,
    const Authenticator*);

// Verify authentication information in thrift binary format
bool VerifyThriftRequest(const InputMessageBase *msg);

} // namespace policy
} // namespace brpc

#endif // BRPC_POLICY_THRIFT_PROTOCOL_H
