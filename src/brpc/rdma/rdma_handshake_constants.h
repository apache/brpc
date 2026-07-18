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

#ifndef BRPC_RDMA_RDMA_HANDSHAKE_CONSTANTS_H
#define BRPC_RDMA_RDMA_HANDSHAKE_CONSTANTS_H

namespace brpc {
namespace rdma {

// Handshake magic strings and their (shared) length.
//   v2 ("RDMA"): [ "RDMA" 4B ][ msg_len 2B ][ 34B body ], total 40B.
//   v3 ("RDM3"): [ "RDM3" 4B ][ pb_size 4B (big-endian) ][ RdmaHello bytes ].
constexpr const char* HELLO_MAGIC = "RDMA";
constexpr const char* HELLO_MAGIC_V3 = "RDM3";
constexpr size_t HELLO_MAGIC_LEN = 4;

// v2 hello version fields. A valid v2 hello must carry exactly these.
constexpr uint16_t HELLO_V2_VERSION = 2;
constexpr uint16_t IMPL_V2_VERSION = 1;

// v2 hello length bounds. HELLO_V2_MSG_LEN_MIN is the total length of the
// base v2 hello (4B magic + 36B HelloMessage); anything shorter is malformed.
// HELLO_V2_MSG_LEN_MAX caps the msg_len declared by the peer; anything
// beyond it is treated as a protocol error and the connection is closed without
// attempting to drain.
constexpr size_t HELLO_V2_MSG_LEN_MIN = 40;
constexpr size_t HELLO_V2_MSG_LEN_MAX = 4096;

// v3 hello framing: a 4B big-endian protobuf size prefix, capped to avoid
// waiting forever for bytes that never come.
constexpr size_t HELLO_V3_PB_SIZE_LEN = 4;
constexpr size_t HELLO_V3_MAX_PB_SIZE = 8192;

// Handshake ACK (shared by all versions): a single 4B big-endian flags word;
// bit 0 (HELLO_ACK_RDMA_OK) means the sender wants to use RDMA.
constexpr size_t HELLO_ACK_LEN = 4;
constexpr uint32_t HELLO_ACK_RDMA_OK = 0x1;

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_RDMA_RDMA_HANDSHAKE_CONSTANTS_H
