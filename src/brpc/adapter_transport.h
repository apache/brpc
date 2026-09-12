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

#ifndef BRPC_ADAPTER_TRANSPORT_H
#define BRPC_ADAPTER_TRANSPORT_H

#include <memory>

#include "brpc/socket_mode.h"
#include "brpc/transport.h"
#include "brpc/transport_handshake.h"
#include "brpc/parse_result.h"

namespace brpc {

class TcpTransport;
class RdmaTransport;
class UBShmTransport;

// The top-level Transport installed in Socket. It starts on TcpTransport and
// may switch to an independent RDMA/URMA/UBSHM Transport after a successful
// handshake. TCP remains usable before negotiation and after fallback.
class AdapterTransport : public Transport {
    friend class TransportFactory;
    friend class RdmaTransport;
    friend class UBShmTransport;
public:
    void Init(Socket* socket, const SocketOptions& options) override;
    void Release() override;
    int Reset(int32_t expected_nref) override;
    std::shared_ptr<AppConnect> Connect() override;
    int CutFromIOBuf(butil::IOBuf* buf) override;
    ssize_t CutFromIOBufList(butil::IOBuf** buf, size_t ndata) override;
    int WaitEpollOut(butil::atomic<int>* epollout_butex,
                     bool pollin, timespec duetime) override;
    void ProcessEvent(bthread_attr_t attr) override;
    void QueueMessage(InputMessageClosure& input_msg,
                      int* num_bthread_created, bool last_msg) override;
    void Debug(std::ostream& os) override;

    int handshake_phase() const { return _handshake.phase(); }
    int handshake_version() const { return _handshake.protocol_version(); }
    handshake::HandshakeSession* handshake_session() { return &_handshake; }
    Transport* high_speed_transport() const {
        return _high_speed_transport.get();
    }
    bool upgrade_capable() const { return _high_speed_transport != NULL; }

    static AdapterTransport* Get(Socket* socket);
    static const AdapterTransport* Get(const Socket* socket);

    // The only client-side upgrade entry point. Concrete transports provide
    // resources; AdapterTransport owns the handshake orchestration.
    static int StartClientUpgrade(const Socket* socket,
                                  void (*done)(int, void*), void* data);

    ParseResult ProcessUpgradeReadable(butil::IOBuf* source);
    void CompleteConnection(handshake::Phase terminal_phase);
    bool connection_completed() const {
        return _connection_completed.load(butil::memory_order_acquire) != 0;
    }

    static void OnNewDataFromTcp(Socket* socket);

private:
    explicit AdapterTransport(SocketMode mode);
    ~AdapterTransport() override;

    Transport* ActiveTransport() const;
    void SetHighSpeedAvailable(bool available);
    void FallbackToTcp();
    void TryReadOnTcp();
    void ProcessTcpEvent();
    void CheckUnexpectedTcpData();
    static void OnNewMessagesAfterUpgrade(Socket* socket);
    static void* ProcessClientHandshake(void* arg);

    SocketMode _mode;
    handshake::HandshakeSession _handshake;
    std::unique_ptr<TcpTransport> _tcp_transport;
    std::unique_ptr<Transport> _high_speed_transport;
    butil::atomic<int> _connection_completed;
};

}  // namespace brpc

#endif  // BRPC_ADAPTER_TRANSPORT_H
