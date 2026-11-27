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

#include <initializer_list>
#include <memory>

#include "butil/macros.h"
#include "brpc/socket_mode.h"
#include "brpc/parse_result.h"
#include "brpc/transport_handshake.h"

namespace butil {
class IOBuf;
}

namespace brpc {

class Socket;
class Transport;

namespace handshake {

// Common InputMessenger parser lifecycle. Protocol adapters provide the server
// step and connection session.
class HandshakeAdapter {
public:
    virtual ~HandshakeAdapter() = default;
    virtual ParseResult ExecuteServerHandshake(
        butil::IOBuf* source, Socket* socket);

protected:
    HandshakeAdapter() = default;
    virtual StepResult RunServerStep(
        butil::IOBuf* source, Socket* socket) = 0;
    virtual HandshakeSession* GetSession(Socket* socket) const = 0;

private:
    DISALLOW_COPY_AND_ASSIGN(HandshakeAdapter);
};

struct HandshakeRegistration {
    struct Upgrade {
        std::unique_ptr<Transport> (*create)();
        int (*init_context)(bool server, const void* options);
        bool (*ready)(Transport*);
        bool (*available)();
        void (*disable)(Transport*);
        // The client handshake and receive startup return 0 or an error code.
        int (*run_client)(Transport*, Socket*, HandshakeSession&);
        int (*start_receive)(Transport*);
    };

    SocketMode mode;
    HandshakeAdapter* server;
    std::initializer_list<const FrameSpec*> hello_frames;
    const Upgrade* upgrade;
    bool (*match_hello_fields)(const butil::IOBuf&);

    static const HandshakeRegistration* Find(SocketMode mode);
    static HandshakeAdapter* MatchHello(const butil::IOBuf& input);

    template <typename T>
    static Upgrade ForTransport(
        bool (*available)(),
        int (*run_client)(Transport*, Socket*, HandshakeSession&),
        int (*start_receive)(Transport*)) {
        return {
            []() -> std::unique_ptr<Transport> { return std::make_unique<T>(); },
            T::ContextInitOrDie,
            [](Transport* t) { return static_cast<T*>(t)->UpgradeReady(); },
            available,
            [](Transport* t) { static_cast<T*>(t)->SetHighSpeedAvailable(false); },
            run_client,
            start_receive,
        };
    }
};

}  // namespace handshake
}  // namespace brpc

#endif  // BRPC_HANDSHAKE_HANDSHAKE_ADAPTER_H
