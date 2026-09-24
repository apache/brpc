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

#ifndef BRPC_TRANSPORT_HANDSHAKE_H
#define BRPC_TRANSPORT_HANDSHAKE_H

#include <cstddef>
#include <string>
#include <vector>

#include "butil/atomicops.h"
#include "butil/macros.h"
#include "brpc/destroyable.h"
#include "brpc/handshake/handshake_frame.h"

namespace brpc {

class Socket;

namespace handshake {

class HandshakeAdapter;

// Context retained by InputMessenger between the hello and ACK parse calls.
// Remembering the selected stateless adapter is necessary because ACK frames
// have no magic and cannot be dispatched from their bytes alone.
struct ServerHandshakeContext : public Destroyable {
    ServerHandshakeContext() : _adapter(NULL) {}
    static ServerHandshakeContext* Create(HandshakeAdapter* adapter);
    HandshakeAdapter* adapter() const { return _adapter; }
    void Destroy() override;

private:
    HandshakeAdapter* _adapter;
};

// Protocol adapters may use transport-specific intermediate values, but the
// terminal values are shared so that AdapterTransport can make the same
// acquire-side decision for RDMA, URMA and UBSHM.
enum Phase {
    UNINITIALIZED = 0,
    PREPARING = 1,
    HELLO_SEND = 2,
    HELLO_WAIT = 3,
    NEGOTIATING = 4,
    ACK_SEND = 5,
    ACK_WAIT = 6,
    EXTENSION_SEND = 7,
    EXTENSION_WAIT = 8,
    ESTABLISHED = 0x100,
    FALLBACK_TCP = 0x200,
    FAILED = 0x300,
};

enum StepResult {
    STEP_OK = 0,
    STEP_FALLBACK,
    STEP_NEED_MORE,
    STEP_NOT_MINE,
    STEP_ERROR,
};

// Wire-level participant in a transport upgrade. Implementations own parsed
// protocol state; HandshakeSession owns framing and phase orchestration.
class HandshakeProtocol {
public:
    virtual ~HandshakeProtocol() = default;

    virtual int ProtocolVersion() const = 0;
    virtual const FrameSpec& HelloFrameSpec() const = 0;
    virtual const FrameSpec& AckFrameSpec() const = 0;
    virtual StepResult BuildHello(bool enabled, std::string* payload) = 0;
    virtual StepResult ParseHello(const std::string& payload) = 0;

    // RDMA and UBSHM use the same four-byte, network-order ACK. Protocols
    // with a different ACK format may override these methods.
    virtual StepResult BuildAck(bool enabled, std::string* payload);
    virtual StepResult ParseAck(const std::string& payload, bool* enabled);

    virtual bool HasExtension() const { return false; }
    virtual const FrameSpec& ExtensionFrameSpec() const;
    virtual StepResult BuildExtension(bool, std::string*) {
        return STEP_ERROR;
    }
    virtual StepResult ParseExtension(const std::string&) {
        return STEP_ERROR;
    }
};

// Resource-level participant in a transport upgrade. Cleanup remains part of
// this contract: resources may already exist when negotiation falls back or
// a framing/I/O error terminates the handshake.
class HandshakeTransport {
public:
    virtual ~HandshakeTransport() = default;

    virtual void OnProtocolSelected(HandshakeProtocol*) {}
    virtual StepResult PrepareResources() = 0;
    virtual StepResult NegotiateResources() = 0;
    virtual void OnEstablished() = 0;
    virtual void OnFallback() = 0;
    virtual void OnFailed() = 0;
    virtual StepResult ValidateEstablished() { return STEP_OK; }
};

// Participant for a server that recognizes an upgrade protocol only to
// negotiate TCP fallback. It owns no high-speed resources.
class FallbackHandshakeTransport : public HandshakeTransport {
public:
    StepResult PrepareResources() override { return STEP_OK; }
    StepResult NegotiateResources() override { return STEP_OK; }
    void OnEstablished() override {}
    void OnFallback() override {}
    void OnFailed() override {}
};

class FallbackHandshakeProtocol : public HandshakeProtocol {
public:
    StepResult ParseHello(const std::string&) override {
        return STEP_FALLBACK;
    }
    StepResult ParseAck(const std::string& payload, bool* enabled) override {
        bool ignored = false;
        const StepResult result = HandshakeProtocol::ParseAck(
            payload, &ignored);
        *enabled = false;
        return result;
    }
};

// Owns one connection-upgrade attempt, invokes the protocol field codec and
// resource callbacks, and provides common framing, TCP control-plane I/O,
// lifecycle and publication ordering.
class HandshakeSession {
public:
    explicit HandshakeSession(Socket* socket = NULL)
        : _socket_io(socket), _io(&_socket_io), _phase(UNINITIALIZED),
          _protocol_version(0), _local_enabled(false) {}

    void Reset(Socket* socket) {
        _socket_io.Reset(socket);
        _io = &_socket_io;
        _protocol_version = 0;
        _local_enabled = false;
        _phase.store(UNINITIALIZED, butil::memory_order_relaxed);
    }

    int phase(butil::memory_order order = butil::memory_order_acquire) const {
        return _phase.load(order);
    }

    void SetPhase(int phase) {
        _phase.store(phase, butil::memory_order_release);
    }

    int protocol_version() const { return _protocol_version; }
    void set_protocol_version(int version) { _protocol_version = version; }

    void MarkEstablished() {
        _phase.store(ESTABLISHED, butil::memory_order_release);
    }

    void MarkFailed() {
        _phase.store(FAILED, butil::memory_order_release);
    }

    // The callback MUST publish the transport's TCP-active state. The release
    // store then makes that state and any pushed-back bytes visible to the
    // event thread that observes FALLBACK_TCP with an acquire load. This is
    // the common form of the ordering fixes from #3347 and #3406.
    template <typename PublishTcpActive>
    void PublishFallback(PublishTcpActive publish_tcp_active) {
        publish_tcp_active();
        _phase.store(FALLBACK_TCP, butil::memory_order_release);
    }

    void NotifyReadable() { _socket_io.NotifyReadable(); }

    // Injects an in-memory stream in common-component unit tests. Reset()
    // restores the Socket-backed implementation.
    void SetIOForTest(HandshakeIO* io) { _io = io; }

    StepResult RunClient(HandshakeProtocol* protocol,
                         HandshakeTransport* transport);
    StepResult RunServer(const std::vector<HandshakeProtocol*>& protocols,
                         HandshakeInput* input,
                         HandshakeTransport* transport,
                         bool fallback_on_not_mine);

private:
    StepResult SendHello(HandshakeProtocol* protocol, bool enabled);
    StepResult ReceiveHello(HandshakeProtocol* protocol,
                            HandshakeInput* input,
                            bool push_back_on_not_mine,
                            bool* magic_matched = NULL);
    StepResult SendAck(HandshakeProtocol* protocol, bool enabled);
    StepResult ReceiveAck(HandshakeProtocol* protocol,
                          HandshakeInput* input, bool* enabled);
    StepResult SendExtension(HandshakeProtocol* protocol, bool enabled);
    StepResult ReceiveExtension(HandshakeProtocol* protocol,
                                HandshakeInput* input);
    StepResult SelectAndReceiveHello(
        const std::vector<HandshakeProtocol*>& protocols,
        HandshakeInput* input, bool push_back_on_not_mine,
        HandshakeProtocol** selected);

    SocketHandshakeIO _socket_io;
    HandshakeIO* _io;
    butil::atomic<int> _phase;
    int _protocol_version;
    bool _local_enabled;

    DISALLOW_COPY_AND_ASSIGN(HandshakeSession);
};

}  // namespace handshake
}  // namespace brpc

#endif  // BRPC_TRANSPORT_HANDSHAKE_H
