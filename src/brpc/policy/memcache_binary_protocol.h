// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Fri Jun  5 18:25:40 CST 2015

#ifndef BRPC_POLICY_MEMCACHE_BINARY_PROTOCOL_H
#define BRPC_POLICY_MEMCACHE_BINARY_PROTOCOL_H

#include "brpc/protocol.h"


namespace brpc {
namespace policy {

// Parse memcache messags.
ParseResult ParseMemcacheMessage(base::IOBuf* source, Socket *socket, bool read_eof,
        const void *arg);

// Actions to a memcache response.
void ProcessMemcacheResponse(InputMessageBase* msg);

// Serialize a memcache request.
void SerializeMemcacheRequest(base::IOBuf* buf,
                              Controller* cntl,
                              const google::protobuf::Message* request);

// Pack `request' to `method' into `buf'.
void PackMemcacheRequest(base::IOBuf* buf,
                         SocketMessage**,
                         uint64_t correlation_id,
                         const google::protobuf::MethodDescriptor* method,
                         Controller* controller,
                         const base::IOBuf& request,
                         const Authenticator* auth);

const std::string& GetMemcacheMethodName(
    const google::protobuf::MethodDescriptor*,
    const Controller*);

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_MEMCACHE_BINARY_PROTOCOL_H
