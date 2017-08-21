// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Thu Nov 13 21:37:29 CST 2014

#ifndef BRPC_POLICY_BRPC_PROTOCOL_H
#define BRPC_POLICY_BRPC_PROTOCOL_H

#include "brpc/protocol.h"


namespace brpc {
namespace policy {

// Parse binary format of baidu_std
ParseResult ParseRpcMessage(base::IOBuf* source, Socket *socket, bool read_eof,
                            const void *arg);

// Actions to a (client) request in baidu_std format
void ProcessRpcRequest(InputMessageBase* msg);

// Actions to a (server) response in baidu_std format.
void ProcessRpcResponse(InputMessageBase* msg);

// Verify authentication information in baidu_std format
bool VerifyRpcRequest(const InputMessageBase* msg);

// Pack `request' to `method' into `buf'.
void PackRpcRequest(base::IOBuf* buf,
                    SocketMessage**,
                    uint64_t correlation_id,
                    const google::protobuf::MethodDescriptor* method,
                    Controller* controller,
                    const base::IOBuf& request,
                   const Authenticator* auth);

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_BRPC_PROTOCOL_H
