// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Fri Jun  5 18:25:40 CST 2015

#ifndef BRPC_POLICY_REDIS_PROTOCOL_H
#define BRPC_POLICY_REDIS_PROTOCOL_H

#include "brpc/protocol.h"


namespace brpc {
namespace policy {

// Parse redis response.
ParseResult ParseRedisMessage(base::IOBuf* source, Socket *socket, bool read_eof,
                              const void *arg);

// Actions to a redis response.
void ProcessRedisResponse(InputMessageBase* msg);

// Serialize a redis request.
void SerializeRedisRequest(base::IOBuf* buf,
                           Controller* cntl,
                           const google::protobuf::Message* request);

// Pack `request' to `method' into `buf'.
void PackRedisRequest(base::IOBuf* buf,
                      SocketMessage**,
                      uint64_t correlation_id,
                      const google::protobuf::MethodDescriptor* method,
                      Controller* controller,
                      const base::IOBuf& request,
                      const Authenticator* auth);

const std::string& GetRedisMethodName(
    const google::protobuf::MethodDescriptor*,
    const Controller*);

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_REDIS_PROTOCOL_H
