// Copyright (c) 2014 baidu-rpc authors.
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

// Author: Li,Zhaogeng (lizhaogeng01@baidu.com)

#ifdef BRPC_RDMA

#ifndef BRPC_POLICY_RDMA_PROTOCOL_H
#define BRPC_POLICY_RDMA_PROTOCOL_H

#include "brpc/protocol.h"

namespace brpc {
namespace policy {

// Parse binary format of rdma protocol.
ParseResult ParseRdmaRpcMessage(butil::IOBuf* source, Socket* socket,
                                bool, const void* arg);

// Actions to a (client) request in rdma protocol format.
void ProcessRdmaRpcRequest(InputMessageBase* msg);

// Actions to a (server) response in rdma protocol format.
void ProcessRdmaRpcResponse(InputMessageBase* msg);

// Pack `request' to `method' into `buf'.
void PackRdmaRpcRequest(butil::IOBuf* buf,
                        SocketMessage**,
                        uint64_t correlation_id,
                        const google::protobuf::MethodDescriptor* method,
                        Controller* controller,
                        const butil::IOBuf& request,
                        const Authenticator*);

// Serialize request of rdma protocol.
void SerializeRdmaRpcRequest(butil::IOBuf* buf,
                             Controller* cntl,
                             const google::protobuf::Message* request);

// Verify authentication information in baidu_std format
bool VerifyRdmaRpcRequest(const InputMessageBase* msg);

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_RDMA_PROTOCOL_H

#endif

