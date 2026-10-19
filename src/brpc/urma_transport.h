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

#ifndef BRPC_URMA_TRANSPORT_H
#define BRPC_URMA_TRANSPORT_H

#if BRPC_WITH_URMA

#include "brpc/channel.h"
#include "brpc/socket.h"
#include "brpc/transport.h"
#include "brpc/urma/urma_endpoint.h"

namespace brpc {

// UrmaTransport is the Transport subclass for URMA (openEuler Unified Remote
// Memory Access). It composes a TcpTransport for fallback (mirroring the
// RdmaTransport / UBShmTransport design) and delegates the URMA data path to
// the per-connection urma::UrmaEndpoint. Negotiation runs over the TCP fd and
// resolves _urma_state to URMA_ON or URMA_OFF.
class UrmaTransport : public Transport {
    friend class TransportFactory;
    friend class urma::UrmaEndpoint;
    friend class urma::UrmaConnect;
    friend class urma::UrmaHandshakeServerV2;
    friend class urma::UrmaHandshakeClientV2;
    friend class urma::UrmaHandshakeServerV3;
    friend class urma::UrmaHandshakeClientV3;

public:
    void Init(Socket* socket, const SocketOptions& options) override;
    void Release() override;
    int Reset(int32_t expected_nref) override;
    std::shared_ptr<AppConnect> Connect() override;
    int CutFromIOBuf(butil::IOBuf* buf) override;
    ssize_t CutFromIOBufList(butil::IOBuf** buf, size_t ndata) override;
    int WaitEpollOut(butil::atomic<int>* epollout_butex, bool pollin,
                     const timespec duetime) override;
    void ProcessEvent(bthread_attr_t attr) override;
    void QueueMessage(InputMessageClosure& input_msg, int* num_bthread_created,
                      bool last_msg) override;
    void Debug(std::ostream& os) override;

    urma::UrmaEndpoint* GetUrmaEp() {
        CHECK(_urma_ep != nullptr);
        return _urma_ep;
    }

    static int ContextInitOrDie(bool server_or_not, const void* options);

private:
    static bool OptionsAvailableForUrma(const ChannelOptions* opt);
    static bool OptionsAvailableOverUrma(const ServerOptions* opt);

    // The on/off state of URMA. UNKNOWN until the handshake resolves.
    enum UrmaState {
        URMA_ON,
        URMA_OFF,
        URMA_UNKNOWN
    };

    urma::UrmaEndpoint* _urma_ep = nullptr;
    butil::atomic<UrmaState> _urma_state{URMA_UNKNOWN};
    std::shared_ptr<TcpTransport> _tcp_transport;
};

}  // namespace brpc

#endif  // BRPC_WITH_URMA
#endif  // BRPC_URMA_TRANSPORT_H
