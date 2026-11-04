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

#ifndef BRPC_POLICY_TRANSPORT_HANDSHAKE_PROTOCOL_H
#define BRPC_POLICY_TRANSPORT_HANDSHAKE_PROTOCOL_H

// This policy is intentionally independent of BRPC_WITH_RDMA and
// BRPC_WITH_UBRING. A plain TCP server must recognize an upgrade hello and
// return a disabled hello so the client can continue with TCP on the same
// connection.

#include "butil/iobuf.h"
#include "brpc/input_message_base.h"
#include "brpc/parse_result.h"
#include "brpc/socket.h"

namespace brpc {
namespace policy {

ParseResult ParseTransportHandshake(butil::IOBuf* source, Socket* socket,
                                     bool read_eof, const void* arg);

// Upgrade handshakes are completed inline by the parser. This placeholder is
// required for server-side protocol registration and must never be invoked.
void ProcessTransportHandshake(InputMessageBase* msg);

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_TRANSPORT_HANDSHAKE_PROTOCOL_H
