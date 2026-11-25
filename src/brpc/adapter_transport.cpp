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

#include "brpc/adapter_transport.h"

#include <cstdint>
#include <errno.h>
#include <unistd.h>

#include "brpc/input_messenger.h"
#include "brpc/handshake/handshake_adapter.h"
#include "brpc/tcp_transport.h"

namespace brpc {
namespace {

class AdapterConnect : public AppConnect {
public:
    explicit AdapterConnect(const std::shared_ptr<AppConnect>& app_connect)
        : _app_connect(app_connect) {}

    static std::shared_ptr<AppConnect> Wrap(
        const std::shared_ptr<AppConnect>& app_connect) {
        if (std::dynamic_pointer_cast<AdapterConnect>(app_connect)) {
            return app_connect;
        }
        return std::make_shared<AdapterConnect>(app_connect);
    }

    static std::shared_ptr<AppConnect> Unwrap(
        const std::shared_ptr<AppConnect>& app_connect) {
        const std::shared_ptr<AdapterConnect> adapter =
            std::dynamic_pointer_cast<AdapterConnect>(app_connect);
        return adapter ? adapter->_app_connect : app_connect;
    }

    void StartConnect(const Socket* socket,
                      void (*done)(int, void*), void* data) override {
        ApplicationConnectTask* task = new ApplicationConnectTask{
            socket, _app_connect, done, data};
        if (AdapterTransport::StartClientUpgrade(
                socket, OnUpgradeComplete, task) != 0) {
            const int error = errno != 0 ? errno : EAGAIN;
            AdapterTransport::Get(const_cast<Socket*>(socket))
                ->handshake_session()->MarkFailed();
            delete task;
            done(error, data);
        }
    }

    void StopConnect(Socket* socket) override {
        if (_app_connect) {
            _app_connect->StopConnect(socket);
        }
    }

private:
    struct ApplicationConnectTask {
        const Socket* socket;
        std::shared_ptr<AppConnect> app_connect;
        void (*done)(int, void*);
        void* data;
    };

    static void OnApplicationComplete(int error, void* arg) {
        std::unique_ptr<ApplicationConnectTask> task(
            static_cast<ApplicationConnectTask*>(arg));
        task->done(error, task->data);
    }

    static void OnUpgradeComplete(int error, void* arg) {
        ApplicationConnectTask* task =
            static_cast<ApplicationConnectTask*>(arg);
        if (error == 0 && task->socket->Failed()) {
            error = EFAILEDSOCKET;
        }
        if (error != 0 || !task->app_connect) {
            std::unique_ptr<ApplicationConnectTask> owned(task);
            task->done(error, task->data);
            return;
        }
        task->app_connect->StartConnect(
            task->socket, OnApplicationComplete, task);
    }

    std::shared_ptr<AppConnect> _app_connect;
};

struct ClientHandshakeTask {
    AdapterTransport* adapter;
    void (*done)(int, void*);
    void* data;
    SocketUniquePtr socket;
};

}  // namespace

AdapterTransport::AdapterTransport(
    const handshake::HandshakeRegistration::Upgrade* upgrade)
    : _upgrade(upgrade) {}
AdapterTransport::~AdapterTransport() = default;

AdapterTransport* AdapterTransport::Get(Socket* socket) {
    CHECK(socket != nullptr);
    return static_cast<AdapterTransport*>(socket->_transport.get());
}

const AdapterTransport* AdapterTransport::Get(const Socket* socket) {
    CHECK(socket != nullptr);
    return static_cast<const AdapterTransport*>(socket->_transport.get());
}

bool AdapterTransport::upgrade_capable(SocketMode mode) const {
    return _socket->socket_mode() == mode && !_socket->is_ssl() &&
           _high_speed_transport &&
           _upgrade->ready(_high_speed_transport.get());
}

int AdapterTransport::StartClientUpgrade(const Socket* socket,
                                         void (*done)(int, void*),
                                         void* data) {
    return StartClientUpgradeImpl(socket, done, data, bthread_start_background);
}

int AdapterTransport::StartClientUpgradeImpl(
    const Socket* socket, void (*done)(int, void*), void* data,
    StartBackground start_background) {
    AdapterTransport* adapter = Get(const_cast<Socket*>(socket));
    auto* task = new ClientHandshakeTask{adapter, done, data, SocketUniquePtr()};
    if (Socket::Address(socket->id(), &task->socket) != 0) {
        delete task;
        errno = EFAILEDSOCKET;
        return -1;
    }
    bthread_t tid;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    bthread_attr_set_name(&attr, "StartClientUpgrade");
    const int rc = start_background(&tid, &attr, ProcessClientHandshake, task);
    if (rc != 0) {
        delete task;
        errno = rc;
        return -1;
    }
    return 0;
}

ParseResult AdapterTransport::ProcessUpgradeReadable(butil::IOBuf* source) {
    if (_handshake.phase(butil::memory_order_acquire) ==
        handshake::ESTABLISHED) {
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    ParseResult result(PARSE_ERROR_NOT_ENOUGH_DATA);
    if (_socket->parsing_context() != nullptr) {
        handshake::ServerHandshakeContext* context =
            static_cast<handshake::ServerHandshakeContext*>(
                _socket->parsing_context());
        CHECK(context->adapter() != nullptr);
        result = context->adapter()->ExecuteServerHandshake(source, _socket);
    } else if (!source->empty()) {
        auto* handler = handshake::HandshakeRegistration::MatchHello(*source);
        result = handler ? handler->ExecuteServerHandshake(source, _socket)
                         : MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }
    return result;
}

void* AdapterTransport::ProcessClientHandshake(void* arg) {
    std::unique_ptr<ClientHandshakeTask> task(
        static_cast<ClientHandshakeTask*>(arg));
    AdapterTransport* adapter = task->adapter;
    Socket* socket = task->socket.get();
    if (!adapter->_upgrade) {
        socket->SetFailed(EPROTO, "Unsupported client transport handshake");
        adapter->_handshake.MarkFailed();
        task->done(EPROTO, task->data);
        return nullptr;
    }
    int error = 0;
    if (socket->Failed()) {
        error = EFAILEDSOCKET;
        adapter->_handshake.MarkFailed();
    } else if (adapter->_upgrade->available()) {
        error = adapter->_upgrade->run_client(
            adapter->_high_speed_transport.get(), socket, adapter->_handshake);
    } else {
        adapter->FallbackToTcp();
    }
    if (error == 0 && socket->Failed()) {
        adapter->_handshake.MarkFailed();
        error = EFAILEDSOCKET;
    }
    if (error == 0 && adapter->handshake_phase() == handshake::ESTABLISHED) {
        error = adapter->StartUpgradeReceive();
        if (error != 0) {
            LOG(WARNING) << "Fail to start upgrade receive events from "
                         << socket->description() << ": " << berror(error);
        }
    }
    task->done(error, task->data);
    return nullptr;
}

void AdapterTransport::Init(Socket* socket, const SocketOptions& options) {
    _socket = socket;
    _default_connect = options.app_connect;
    _on_edge_trigger = options.on_edge_triggered_events;
    if (options.need_on_edge_trigger && _on_edge_trigger == nullptr) {
        if (!_upgrade) {
            _on_edge_trigger = InputMessenger::OnNewMessages;
        } else if (options.user != static_cast<SocketUser*>(
                       get_client_side_messenger())) {
            _on_edge_trigger = OnNewMessagesAfterUpgrade;
        } else {
            _on_edge_trigger = OnNewDataFromTcp;
        }
    }
    _handshake.Reset(socket);
    _tcp_transport = std::make_unique<TcpTransport>();
    _tcp_transport->Init(socket, options);

    if (_upgrade) {
        _high_speed_transport = _upgrade->create();
        _high_speed_transport->Init(socket, options);
    }
}

void AdapterTransport::Release() {
    if (_high_speed_transport) {
        _high_speed_transport->Release();
    }
    _tcp_transport->Release();
}

int AdapterTransport::Reset(int32_t expected_nref) {
    if (_high_speed_transport) {
        _high_speed_transport->Reset(expected_nref);
    }
    _tcp_transport->Reset(expected_nref);
    _handshake.Reset(_socket);
    return 0;
}

std::shared_ptr<AppConnect> AdapterTransport::Connect() {
    if (upgrade_capable(_socket->socket_mode())) {
        return AdapterConnect::Wrap(_default_connect);
    }
    SocketUser* const client_messenger =
        static_cast<SocketUser*>(get_client_side_messenger());
    if (client_messenger != nullptr &&
        _socket->user() == client_messenger) {
        // This socket keeps the unwrapped AppConnect across health-check
        // resets, so its read path must not depend on another handshake.
        _on_edge_trigger = _tcp_transport->GetOnEdgeTrigger();
        FallbackToTcp();
        return AdapterConnect::Unwrap(_tcp_transport->Connect());
    }
    return _tcp_transport->Connect();
}

Transport* AdapterTransport::ActiveTransport() const {
    if (_high_speed_transport &&
        _handshake.phase(butil::memory_order_acquire) ==
            handshake::ESTABLISHED) {
        return _high_speed_transport.get();
    }
    return _tcp_transport.get();
}

int AdapterTransport::CutFromIOBuf(butil::IOBuf* buf) {
    return ActiveTransport()->CutFromIOBuf(buf);
}

ssize_t AdapterTransport::CutFromIOBufList(
    butil::IOBuf** buf, size_t ndata) {
    return ActiveTransport()->CutFromIOBufList(buf, ndata);
}

int AdapterTransport::WaitEpollOut(butil::atomic<int>* epollout_butex,
                                  bool pollin, timespec duetime) {
    return ActiveTransport()->WaitEpollOut(
        epollout_butex, pollin, duetime);
}

void AdapterTransport::ProcessEvent(bthread_attr_t attr) {
    ActiveTransport()->ProcessEvent(attr);
}

void AdapterTransport::QueueMessage(InputMessageClosure& input_msg,
                                    int* num_bthread_created,
                                    bool last_msg) {
    ActiveTransport()->QueueMessage(
        input_msg, num_bthread_created, last_msg);
}

void AdapterTransport::Debug(std::ostream& os) {
    if (_high_speed_transport) {
        _high_speed_transport->Debug(os);
    }
    const char* state = "UNKNOWN";
    switch (_handshake.phase(butil::memory_order_acquire)) {
    case handshake::UNINITIALIZED: state = "UNINITIALIZED"; break;
    case handshake::PREPARING: state = "PREPARING"; break;
    case handshake::HELLO_SEND: state = "HELLO_SEND"; break;
    case handshake::HELLO_WAIT: state = "HELLO_WAIT"; break;
    case handshake::NEGOTIATING: state = "NEGOTIATING"; break;
    case handshake::ACK_SEND: state = "ACK_SEND"; break;
    case handshake::ACK_WAIT: state = "ACK_WAIT"; break;
    case handshake::EXTENSION_SEND: state = "EXTENSION_SEND"; break;
    case handshake::EXTENSION_WAIT: state = "EXTENSION_WAIT"; break;
    case handshake::ESTABLISHED: state = "ESTABLISHED"; break;
    case handshake::FALLBACK_TCP: state = "FALLBACK_TCP"; break;
    case handshake::FAILED: state = "FAILED"; break;
    }
    os << "\nhandshake_state=" << state
       << "\nhandshake_version=" << _handshake.protocol_version();
}

void AdapterTransport::FallbackToTcp() {
    _handshake.PublishFallback([this]() {
        if (_high_speed_transport) {
            _upgrade->disable(_high_speed_transport.get());
        }
    });
}

int AdapterTransport::StartUpgradeReceive() {
    const int error = _upgrade->start_receive(_high_speed_transport.get());
    if (error != 0) {
        _handshake.MarkFailed();
    }
    return error;
}

void AdapterTransport::OnNewMessagesAfterUpgrade(Socket* socket) {
    AdapterTransport* adapter = Get(socket);
    if (adapter->_handshake.phase(butil::memory_order_acquire) ==
        handshake::ESTABLISHED) {
        adapter->CheckUnexpectedTcpData();
        return;
    }

    InputMessenger::OnNewMessagesUntil(socket, [](Socket* s) {
        AdapterTransport* transport = Get(s);
        if (transport->handshake_phase() != handshake::ESTABLISHED) {
            return false;
        }
        const int error = transport->StartUpgradeReceive();
        if (error == 0) {
            transport->CheckUnexpectedTcpData();
        } else {
            s->SetFailed(
                error, "Fail to start upgrade receive events from %s: %s",
                s->description().c_str(), berror(error));
        }
        return true;
    });
}

void AdapterTransport::OnNewDataFromTcp(Socket* socket) {
    static_cast<AdapterTransport*>(socket->_transport.get())->ProcessTcpEvent();
}

void AdapterTransport::ProcessTcpEvent() {
    int progress = Socket::PROGRESS_INIT;
    while (true) {
        const int phase = _handshake.phase(butil::memory_order_acquire);
        if (phase != handshake::UNINITIALIZED &&
            phase < handshake::ESTABLISHED) {
            _handshake.NotifyReadable();
        } else if (phase == handshake::FALLBACK_TCP) {
            InputMessenger::OnNewMessages(_socket);
            return;
        } else if (phase == handshake::ESTABLISHED) {
            CheckUnexpectedTcpData();
            return;
        }
        if (!_socket->MoreReadEvents(&progress)) {
            break;
        }
    }
}

void AdapterTransport::CheckUnexpectedTcpData() {
    int progress = Socket::PROGRESS_INIT;
    while (true) {
        uint8_t byte;
        const ssize_t nr = read(_socket->fd(), &byte, 1);
        if (nr == 0) {
            _socket->SetEOF();
            return;
        }
        if (nr > 0) {
            _socket->SetFailed(EPROTO, "Read unexpected data from %s",
                               _socket->description().c_str());
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN) {
            const int saved_errno = errno;
            _socket->SetFailed(saved_errno, "Fail to read from %s: %s",
                               _socket->description().c_str(),
                               berror(saved_errno));
            return;
        }
        if (!_socket->MoreReadEvents(&progress)) {
            return;
        }
    }
}

}  // namespace brpc
