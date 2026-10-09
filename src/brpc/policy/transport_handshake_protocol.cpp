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

#include "brpc/policy/transport_handshake_protocol.h"

#include "butil/logging.h"
#include "brpc/adapter_transport.h"
#include "brpc/socket.h"

namespace brpc {
namespace policy {

ParseResult ParseTransportHandshake(butil::IOBuf* source, Socket* socket,
                                     bool /*read_eof*/, const void* /*arg*/) {
    // URMA still uses its own Transport. Do not cast it to AdapterTransport
    // when this globally registered parser is tried during protocol detection.
    if (socket->socket_mode() == SOCKET_MODE_URMA) {
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    return AdapterTransport::Get(socket)->ProcessUpgradeReadable(source);
}

void ProcessTransportHandshake(InputMessageBase* msg) {
    DestroyingPtr<InputMessageBase> destroying_msg(msg);
    CHECK(false) << "ProcessTransportHandshake should never be called";
}

}  // namespace policy
}  // namespace brpc
