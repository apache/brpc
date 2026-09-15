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

#ifndef BRPC_HANDSHAKE_UBSHM_HANDSHAKE_H
#define BRPC_HANDSHAKE_UBSHM_HANDSHAKE_H

#include "brpc/handshake/handshake_adapter.h"

namespace brpc {
namespace handshake {

// Returns the adapter used by the common transport-handshake policy parser.
// The concrete server executor is private to the implementation.
HandshakeAdapter* GetUBShmServerHandshakeAdapter();

}  // namespace handshake
}  // namespace brpc

#if BRPC_WITH_UBRING

#include <cstdint>
#include <string>

#include <gflags/gflags.h>

#include "butil/macros.h"
#include "brpc/transport_handshake.h"
#include "brpc/ubshm/shm/shm_def.h"

namespace brpc {
namespace ubring {

DECLARE_int32(data_queue_size);
DECLARE_bool(ub_trace_verbose);

// UBSHM v2 wire payload. HandshakeSession owns framing and ACK exchange;
// this type and UBShmHandshakeAdapter only handle protocol fields.
struct HelloMessage {
    void Serialize(void* data) const;
    void Deserialize(const void* data);
    std::string toString() const;

    uint16_t msg_len;
    uint16_t hello_ver;
    uint16_t impl_ver;
    uint64_t len;
    char shm_name[SHM_MAX_NAME_BUFF_LEN];
};

class UBShmHandshakeAdapter {
public:
    UBShmHandshakeAdapter() = default;

    handshake::HandshakeCodec MakeCodec() const;
    handshake::StepResult BuildHello(
        bool enabled, uint64_t len, const char* shm_name,
        std::string* payload) const;
    handshake::StepResult ParseHello(
        const std::string& payload, HelloMessage* message) const;

private:
    bool NegotiationValid(const HelloMessage& message) const;
    DISALLOW_COPY_AND_ASSIGN(UBShmHandshakeAdapter);
};

}  // namespace ubring
}  // namespace brpc

#endif  // BRPC_WITH_UBRING
#endif  // BRPC_HANDSHAKE_UBSHM_HANDSHAKE_H
