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

#ifndef BRPC_RDMA_RDMA_HANDSHAKE_SERVER_H
#define BRPC_RDMA_RDMA_HANDSHAKE_SERVER_H

#include "brpc/destroyable.h"
#include "brpc/parse_result.h"

namespace butil {
class IOBuf;
}

namespace brpc {
class Socket;
namespace rdma {

// State kept across multiple parse calls of the server handshake.
struct ServerHandshakeContext : public Destroyable {
    static ServerHandshakeContext* Create();
    void Destroy() override;
};

// The single server-side RDMA handshake entry for policy::ParseRdmaHandshake.
// Returns a ParseResult ready to be handed back from the protocol parser:
//   - not an RDMA magic / handshake finished -> PARSE_ERROR_TRY_OTHERS;
//   - an RDMA magic but not enough bytes yet  -> PARSE_ERROR_NOT_ENOUGH_DATA;
//   - IO/protocol error                       -> PARSE_ERROR_ABSOLUTELY_WRONG.
ParseResult ExecuteServerHandshake(butil::IOBuf* source, Socket* socket);

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_RDMA_RDMA_HANDSHAKE_SERVER_H
