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

#include "brpc/handshake/handshake_adapter.h"

#include "brpc/socket.h"

namespace brpc {
namespace handshake {

// InputMessenger may call this entry repeatedly while bytes arrive. Keep all
// connection state in HandshakeSession and Socket rather than in the adapter,
// so a single stateless adapter can serve every connection. The parsing
// context retains that selected adapter between the server hello and the peer
// ACK, whose frame has no protocol magic of its own.
ParseResult StandardHandshakeAdapter::ExecuteServerHandshake(
    butil::IOBuf* source, Socket* socket) {
    const StepResult result = RunServerStep(source, socket);
    if (result == STEP_NEED_MORE) {
        if (GetSession(socket)->phase() == ACK_WAIT &&
            socket->parsing_context() == NULL) {
            ServerHandshakeContext* context =
                ServerHandshakeContext::Create(this);
            if (context == NULL) {
                GetSession(socket)->MarkFailed();
                return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
            }
            socket->reset_parsing_context(context);
        }
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    socket->reset_parsing_context(NULL);
    if (result == STEP_ERROR) {
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
    }
    return MakeParseError(PARSE_ERROR_TRY_OTHERS);
}

}  // namespace handshake
}  // namespace brpc
