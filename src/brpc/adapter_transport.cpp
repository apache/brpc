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
#include "brpc/destroyable.h"
#include "brpc/handshake/rdma_handshake.h"
#include "brpc/handshake/ubshm_handshake.h"
#if BRPC_WITH_RDMA
#include "brpc/rdma/rdma_helper.h"
#endif
#if BRPC_WITH_UBRING
#include "brpc/ubshm/ub_helper.h"
#include "brpc/ubshm/ubr_trx.h"
#endif
#include "brpc/rdma_transport.h"
#include "brpc/tcp_transport.h"
#include "brpc/ubshm_transport.h"

namespace brpc {

namespace {

class AdapterConnect : public AppConnect {
public:
    explicit AdapterConnect(const std::shared_ptr<AppConnect>& app_connect)
        : _app_connect(app_connect) {}

    void StartConnect(const Socket* socket,
                      void (*done)(int, void*), void* data) override {
        ApplicationConnectTask* task = new ApplicationConnectTask{
            socket, _app_connect, done, data};
        if (AdapterTransport::StartClientUpgrade(
                socket, OnUpgradeComplete, task) != 0) {
            AdapterTransport::Get(const_cast<Socket*>(socket))->CompleteConnection(
                handshake::FAILED);
            const int error = errno != 0 ? errno : EAGAIN;
            delete task;
            done(error, data);
        }
    }

    void StopConnect(Socket*) override {}

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

AdapterTransport::AdapterTransport(SocketMode mode)
    : _mode(mode), _connection_completed(0) {}
AdapterTransport::~AdapterTransport() = default;

AdapterTransport* AdapterTransport::Get(Socket* socket) {
    CHECK(socket != NULL);
    return static_cast<AdapterTransport*>(socket->_transport.get());
}

const AdapterTransport* AdapterTransport::Get(const Socket* socket) {
    CHECK(socket != NULL);
    return static_cast<const AdapterTransport*>(socket->_transport.get());
}

int AdapterTransport::StartClientUpgrade(const Socket* socket,
                                         void (*done)(int, void*),
                                         void* data) {
    AdapterTransport* adapter = Get(const_cast<Socket*>(socket));
    ClientHandshakeTask* task = new ClientHandshakeTask{adapter, done, data, SocketUniquePtr()};
    if (Socket::Address(socket->id(), &task->socket) != 0) {
        delete task;
        return -1;
    }
    bthread_t tid;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    bthread_attr_set_name(&attr, "StartClientUpgrade");
    if (bthread_start_background(&tid, &attr,
                                 ProcessClientHandshake, task) < 0) {
        delete task;
        return -1;
    }
    return 0;
}

ParseResult AdapterTransport::ProcessUpgradeReadable(butil::IOBuf* source) {
    ParseResult result(PARSE_ERROR_NOT_ENOUGH_DATA);
    if (_socket->parsing_context() != NULL) {
        handshake::ServerHandshakeContext* context =
            static_cast<handshake::ServerHandshakeContext*>(
                _socket->parsing_context());
        CHECK(context->adapter() != NULL);
        result = context->adapter()->ExecuteServerHandshake(source, _socket);
    } else {
        const char* first = static_cast<const char*>(source->fetch1());
        handshake::HandshakeAdapter* adapter =
            first != NULL && *first == 'U'
            ? handshake::GetUBShmServerHandshakeAdapter()
            : handshake::GetRdmaServerHandshakeAdapter();
        result = adapter->ExecuteServerHandshake(source, _socket);
    }
    const int phase = _handshake.phase();
    if (!connection_completed() &&
        (phase == handshake::ESTABLISHED ||
         phase == handshake::FALLBACK_TCP || phase == handshake::FAILED)) {
        CompleteConnection(static_cast<handshake::Phase>(phase));
    }
    return result;
}

void AdapterTransport::CompleteConnection(handshake::Phase terminal_phase) {
    CHECK(terminal_phase == handshake::ESTABLISHED ||
          terminal_phase == handshake::FALLBACK_TCP ||
          terminal_phase == handshake::FAILED);
    if (terminal_phase == handshake::FAILED &&
        _handshake.phase() != handshake::FAILED) {
        _handshake.MarkFailed();
    }
    int expected = 0;
    _connection_completed.compare_exchange_strong(
        expected, 1, butil::memory_order_release,
        butil::memory_order_relaxed);
}

void* AdapterTransport::ProcessClientHandshake(void* arg) {
    std::unique_ptr<ClientHandshakeTask> task(
        static_cast<ClientHandshakeTask*>(arg));
    AdapterTransport* adapter = task->adapter;
    Socket* socket = task->socket.get();
    int connect_error = 0;
    (void)connect_error;

#if BRPC_WITH_RDMA
    if (adapter->_mode == SOCKET_MODE_RDMA) {
        RdmaTransport* transport = static_cast<RdmaTransport*>(
            adapter->_high_speed_transport.get());
        if (!rdma::IsRdmaAvailable()) {
            adapter->FallbackToTcp();
            adapter->CompleteConnection(handshake::FALLBACK_TCP);
            task->done(0, task->data);
            return NULL;
        }

        std::unique_ptr<rdma::RdmaHandshakeAdapter> protocol =
            transport->CreateClientHandshakeAdapter();
        CHECK(protocol != NULL);
        rdma::ParsedHello remote{};
        handshake::ClientHandshakeCallbacks callbacks{};
        callbacks.codec = protocol->MakeCodec(&remote);
        callbacks.transport.prepare_resources = [&]() {
            if (transport->PrepareUpgradeResources() == 0) {
                return handshake::STEP_OK;
            }
            errno = 0;
            return handshake::STEP_FALLBACK;
        };
        callbacks.transport.negotiate_resources = [&]() {
            return transport->NegotiateUpgradeResources(remote, false) == 0
                ? handshake::STEP_OK : handshake::STEP_FALLBACK;
        };
        callbacks.transport.set_high_speed_active = [transport]() {
            transport->ActivateUpgrade();
        };
        callbacks.transport.set_tcp_active = [transport]() {
            transport->DeactivateUpgrade();
        };
        callbacks.transport.on_failed = [&]() {
            const int saved_errno = errno != 0 ? errno : EPROTO;
            connect_error = saved_errno;
            socket->SetFailed(saved_errno,
                              "Fail to complete rdma handshake from %s: %s",
                              socket->description().c_str(),
                              berror(saved_errno));
        };
        const handshake::StepResult result = adapter->_handshake.RunClient(callbacks);
        if (result == handshake::STEP_OK &&
            transport->StartUpgradeEvents() < 0) {
            const int saved_errno = errno != 0 ? errno : ERDMA;
            transport->DeactivateUpgrade();
            adapter->_handshake.MarkFailed();
            socket->SetFailed(
                saved_errno,
                "Fail to start RDMA CQ events from %s: %s",
                socket->description().c_str(), berror(saved_errno));
            connect_error = saved_errno;
        }
        if (result == handshake::STEP_ERROR && connect_error == 0) {
            connect_error = errno != 0 ? errno : EPROTO;
        }
        adapter->CompleteConnection(static_cast<handshake::Phase>(
            adapter->_handshake.phase()));
        task->done(connect_error, task->data);
        return NULL;
    }
#endif

#if BRPC_WITH_UBRING
    if (adapter->_mode == SOCKET_MODE_UBRING) {
        UBShmTransport* transport = static_cast<UBShmTransport*>(
            adapter->_high_speed_transport.get());
        if (!ubring::IsUBAvailable()) {
            adapter->FallbackToTcp();
            adapter->CompleteConnection(handshake::FALLBACK_TCP);
            task->done(0, task->data);
            return NULL;
        }

        const size_t local_shm_len =
            static_cast<size_t>(ubring::FLAGS_data_queue_size) * MB_TO_BYTE;
        ubring::SHM local_trx_shm = {
            NULL, local_shm_len, 0, {0}, static_cast<uint32_t>(socket->fd())};
        const auto shm_name_str =
            butil::endpoint2str(socket->local_side());
        ubring::HelloMessage remote{};
        ubring::UBShmHandshakeAdapter wire;
        handshake::ClientHandshakeCallbacks callbacks{};
        callbacks.codec = wire.MakeCodec();
        callbacks.codec.build_hello = [&](bool enabled, std::string* payload) {
            CHECK(enabled);
            return wire.BuildHello(true, local_shm_len, shm_name_str.c_str(),
                                   payload);
        };
        callbacks.codec.parse_hello = [&](const std::string& payload) {
            return wire.ParseHello(payload, &remote);
        };
        callbacks.transport.prepare_resources = [&]() {
            return transport->PrepareUpgradeResources(
                       &local_trx_shm, shm_name_str.c_str()) == 0
                ? handshake::STEP_OK : handshake::STEP_FALLBACK;
        };
        callbacks.transport.negotiate_resources = [&]() {
            return transport->NegotiateUpgradeResources(
                       &local_trx_shm, shm_name_str.c_str()) == 0
                ? handshake::STEP_OK : handshake::STEP_FALLBACK;
        };
        callbacks.transport.set_high_speed_active = [transport]() {
            transport->ActivateUpgrade();
        };
        callbacks.transport.set_tcp_active = [transport]() {
            transport->DeactivateUpgrade();
        };
        callbacks.transport.on_failed = [&]() {
            const int saved_errno = errno != 0 ? errno : EPROTO;
            connect_error = saved_errno;
            socket->SetFailed(saved_errno,
                              "Fail to complete ubring handshake from %s: %s",
                              socket->description().c_str(),
                              berror(saved_errno));
        };
        const handshake::StepResult result = adapter->_handshake.RunClient(callbacks);
        if (result == handshake::STEP_OK) {
            transport->FinishUpgrade();
        }
        if (result == handshake::STEP_ERROR && connect_error == 0) {
            connect_error = errno != 0 ? errno : EPROTO;
        }
        adapter->CompleteConnection(static_cast<handshake::Phase>(
            adapter->_handshake.phase()));
        task->done(connect_error, task->data);
        return NULL;
    }
#endif

    socket->SetFailed(EPROTO, "Unsupported client transport handshake");
    adapter->CompleteConnection(handshake::FAILED);
    task->done(EPROTO, task->data);
    return NULL;
}

void AdapterTransport::Init(Socket* socket, const SocketOptions& options) {
    CHECK_EQ(_mode, options.socket_mode);
    _socket = socket;
    _default_connect = options.app_connect;
    _on_edge_trigger = options.on_edge_triggered_events;
    if (options.need_on_edge_trigger && _on_edge_trigger == NULL) {
        if (_mode == SOCKET_MODE_TCP) {
            _on_edge_trigger = InputMessenger::OnNewMessages;
#if BRPC_WITH_RDMA
        } else if (_mode == SOCKET_MODE_RDMA &&
                   options.user != static_cast<SocketUser*>(
                       get_client_side_messenger())) {
            // RDMA server handshake is parsed by InputMessenger.
            _on_edge_trigger = OnNewMessagesAfterUpgrade;
#endif
#if BRPC_WITH_UBRING
        } else if (_mode == SOCKET_MODE_UBRING &&
                   options.user != static_cast<SocketUser*>(
                       get_client_side_messenger())) {
            // UBSHM server handshake is parsed by InputMessenger.
            _on_edge_trigger = InputMessenger::OnNewMessages;
#endif
        } else {
            _on_edge_trigger = OnNewDataFromTcp;
        }
    }
    _handshake.Reset(socket);
    _connection_completed.store(0, butil::memory_order_relaxed);
    _tcp_transport.reset(new TcpTransport);
    _tcp_transport->Init(socket, options);

    switch (_mode) {
#if BRPC_WITH_RDMA
    case SOCKET_MODE_RDMA:
        _high_speed_transport.reset(new RdmaTransport);
        break;
#endif
#if BRPC_WITH_UBRING
    case SOCKET_MODE_UBRING:
        _high_speed_transport.reset(new UBShmTransport);
        break;
#endif
    default:
        break;
    }
    if (_high_speed_transport) {
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
    _connection_completed.store(0, butil::memory_order_relaxed);
    return 0;
}

std::shared_ptr<AppConnect> AdapterTransport::Connect() {
    if (_high_speed_transport) {
        return std::make_shared<AdapterConnect>(_default_connect);
    }
    return _tcp_transport->Connect();
}

Transport* AdapterTransport::ActiveTransport() const {
    if (_high_speed_transport &&
        _handshake.phase() == handshake::ESTABLISHED) {
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
    switch (_handshake.phase()) {
    case handshake::UNINITIALIZED: state = "UNINITIALIZED"; break;
    case handshake::PREPARING: state = "PREPARING"; break;
    case handshake::HELLO_SEND: state = "HELLO_SEND"; break;
    case handshake::HELLO_WAIT: state = "HELLO_WAIT"; break;
    case handshake::NEGOTIATING: state = "NEGOTIATING"; break;
    case handshake::ACK_SEND: state = "ACK_SEND"; break;
    case handshake::ACK_WAIT: state = "ACK_WAIT"; break;
    case handshake::ESTABLISHED: state = "ESTABLISHED"; break;
    case handshake::FALLBACK_TCP: state = "FALLBACK_TCP"; break;
    case handshake::FAILED: state = "FAILED"; break;
    }
    os << "\nhandshake_state=" << state
       << "\nhandshake_version=" << _handshake.protocol_version();
}

void AdapterTransport::FallbackToTcp() {
    _handshake.PublishFallback([this]() {
        SetHighSpeedAvailable(false);
    });
}

void AdapterTransport::SetHighSpeedAvailable(bool available) {
    if (!_high_speed_transport) {
        return;
    }
    switch (_mode) {
#if BRPC_WITH_RDMA
    case SOCKET_MODE_RDMA:
        static_cast<RdmaTransport*>(_high_speed_transport.get())
            ->SetHighSpeedAvailable(available);
        break;
#endif
#if BRPC_WITH_UBRING
    case SOCKET_MODE_UBRING:
        static_cast<UBShmTransport*>(_high_speed_transport.get())
            ->SetHighSpeedAvailable(available);
        break;
#endif
    default:
        break;
    }
}

void AdapterTransport::OnNewMessagesAfterUpgrade(Socket* socket) {
#if BRPC_WITH_RDMA
    AdapterTransport* adapter = Get(socket);
    if (adapter->_mode == SOCKET_MODE_RDMA &&
        adapter->_handshake.phase() == handshake::ESTABLISHED) {
        adapter->CheckUnexpectedTcpData();
        return;
    }
#endif

    InputMessenger::OnNewMessages(socket);

#if BRPC_WITH_RDMA
    if (adapter->_mode == SOCKET_MODE_RDMA &&
        adapter->_handshake.phase() == handshake::ESTABLISHED) {
        RdmaTransport* transport = static_cast<RdmaTransport*>(
            adapter->_high_speed_transport.get());
        if (transport->StartUpgradeEvents() < 0) {
            const int saved_errno = errno != 0 ? errno : ERDMA;
            transport->DeactivateUpgrade();
            adapter->_handshake.MarkFailed();
            adapter->CompleteConnection(handshake::FAILED);
            socket->SetFailed(
                saved_errno,
                "Fail to start RDMA CQ events from %s: %s",
                socket->description().c_str(), berror(saved_errno));
        }
    }
#endif
}

void AdapterTransport::OnNewDataFromTcp(Socket* socket) {
    static_cast<AdapterTransport*>(socket->_transport.get())->ProcessTcpEvent();
}

void AdapterTransport::ProcessTcpEvent() {
    int progress = Socket::PROGRESS_INIT;
    while (true) {
        const int phase = _handshake.phase();
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

void AdapterTransport::TryReadOnTcp() {
    if (_socket->_nevent.fetch_add(1, butil::memory_order_acq_rel) != 0) {
        return;
    }
    const int phase = _handshake.phase();
    if (phase == handshake::FALLBACK_TCP) {
        InputMessenger::OnNewMessages(_socket);
    } else if (phase == handshake::ESTABLISHED) {
        CheckUnexpectedTcpData();
    }
}

}  // namespace brpc
