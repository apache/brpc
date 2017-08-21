// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Sep  2 21:38:04 CST 2014

#ifndef BRPC_POLICY_SOFA_PBRPC_PROTOCOL_H
#define BRPC_POLICY_SOFA_PBRPC_PROTOCOL_H

#include "brpc/policy/sofa_pbrpc_meta.pb.h"
#include "brpc/protocol.h"             


namespace brpc {
namespace policy {

// Parse binary format of sofa-pbrpc.
ParseResult ParseSofaMessage(base::IOBuf* source, Socket *socket, bool read_eof, const void *arg);

// Actions to a (client) request in sofa-pbrpc format.
void ProcessSofaRequest(InputMessageBase* msg);

// Actions to a (server) response in sofa-pbrpc format.
void ProcessSofaResponse(InputMessageBase* msg);

// Verify authentication information in sofa-pbrpc format
bool VerifySofaRequest(const InputMessageBase* msg);

// Pack `request' to `method' into `buf'.
void PackSofaRequest(base::IOBuf* buf,
                     SocketMessage**,
                     uint64_t correlation_id,
                     const google::protobuf::MethodDescriptor* method,
                     Controller* controller,
                     const base::IOBuf& request,
                     const Authenticator* auth);

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_SOFA_PBRPC_PROTOCOL_H
