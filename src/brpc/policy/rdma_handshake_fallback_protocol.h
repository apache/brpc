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

#ifndef BRPC_POLICY_RDMA_HANDSHAKE_FALLBACK_PROTOCOL_H
#define BRPC_POLICY_RDMA_HANDSHAKE_FALLBACK_PROTOCOL_H

// NOTE: This file is intentionally INDEPENDENT of BRPC_WITH_RDMA. A server may
// run in TCP mode either because it was built without RDMA, or because RDMA was
// not enabled at runtime. In both cases an RDMA client that connects to it will
// send an RDMA handshake magic ("RDMA" for v2, "RDM3" for v3) first. Without
// special handling the server treats those bytes as an unknown protocol and
// closes the connection, so the client (blocked reading the server hello) only
// sees EOF and cannot fall back to TCP.
//
// To let the client fall back on the SAME connection, the server recognizes the
// RDMA handshake as a "first-class" protocol (magic in the first 4 bytes,
// PROTOCOL_RDMA_HANDSHAKE is ordered before PROTOCOL_HTTP), replies a hello with
// an incompatible version so the client rejects it and downgrades to TCP, then
// drains the client's subsequent ACK and lets normal RPC parsing continue.

#include "butil/iobuf.h"
#include "brpc/input_message_base.h"
#include "brpc/parse_result.h"
#include "brpc/socket.h"

namespace brpc {
namespace policy {

// Parse binary format of rdma handshake.
ParseResult ParseRdmaHandshake(butil::IOBuf* source, Socket* socket,
                               bool read_eof, const void* arg);

// Process callback placeholder. The parser never emits a real message (it
// replies inline and only returns NOT_ENOUGH_DATA / TRY_OTHERS), so this must
// never be invoked; it only exists to satisfy server-side protocol registration
// which requires a non-NULL process_request.
void ProcessRdmaHandshake(InputMessageBase* msg);

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_RDMA_HANDSHAKE_FALLBACK_PROTOCOL_H
