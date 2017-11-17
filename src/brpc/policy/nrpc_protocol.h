// Copyright (c) 2014 Baidu, Inc.
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

// Authors: Kevin.XU (xuhuahai@sogou-inc.com)


#ifndef BRPC_POLICY_NRPC_PROTOCOL_H
#define BRPC_POLICY_NRPC_PROTOCOL_H

#include "brpc/protocol.h"

namespace brpc {
namespace policy {

// Parse binary format of nrpc
ParseResult ParseNrpcMessage(butil::IOBuf* source, Socket *socket, bool read_eof,
                            const void *arg);

// Actions to a (client) request in nrpc format
void ProcessNrpcRequest(InputMessageBase* msg);

// Actions to a (server) response in nrpc format.
void ProcessNrpcResponse(InputMessageBase* msg);

// Verify authentication information in nrpc format
bool VerifyNrpcRequest(const InputMessageBase* msg);

// Pack `request' to `method' into `buf'.
void PackNrpcRequest(butil::IOBuf* buf,
                    SocketMessage**,
                    uint64_t correlation_id,
                    const google::protobuf::MethodDescriptor* method,
                    Controller* controller,
                    const butil::IOBuf& request,
                   const Authenticator* auth);

}  // namespace policy
} // namespace brpc

#endif  // BRPC_POLICY_NRPC_PROTOCOL_H
