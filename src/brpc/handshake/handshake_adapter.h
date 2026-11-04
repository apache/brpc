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

#ifndef BRPC_HANDSHAKE_HANDSHAKE_ADAPTER_H
#define BRPC_HANDSHAKE_HANDSHAKE_ADAPTER_H

#include "butil/macros.h"
#include "brpc/parse_result.h"
#include "brpc/transport_handshake.h"

namespace butil {
class IOBuf;
}

namespace brpc {

class Socket;

namespace handshake {

// The minimal seam between an upgrade protocol and InputMessenger. Protocols
// that cannot use the standard parser lifecycle implement this interface
// directly.
class HandshakeAdapter {
public:
    virtual ~HandshakeAdapter() = default;

    virtual ParseResult ExecuteServerHandshake(
        butil::IOBuf* source, Socket* socket) = 0;

protected:
    HandshakeAdapter() = default;

private:
    DISALLOW_COPY_AND_ASSIGN(HandshakeAdapter);
};

// Reusable InputMessenger implementation. Protocol adapters provide only the
// protocol-specific server step; the common session owns all phases.
class StandardHandshakeAdapter : public HandshakeAdapter {
public:
    ParseResult ExecuteServerHandshake(
        butil::IOBuf* source, Socket* socket) override;

protected:
    StandardHandshakeAdapter() = default;

    virtual StepResult RunServerStep(
        butil::IOBuf* source, Socket* socket) = 0;
    virtual HandshakeSession* GetSession(Socket* socket) const = 0;

private:
    DISALLOW_COPY_AND_ASSIGN(StandardHandshakeAdapter);
};

}  // namespace handshake
}  // namespace brpc

#endif  // BRPC_HANDSHAKE_HANDSHAKE_ADAPTER_H
