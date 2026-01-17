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

#ifndef BRPC_RDMA_TRANSPORT_H
#define BRPC_RDMA_TRANSPORT_H

#if BRPC_WITH_RDMA
#include "brpc/socket.h"
#include "brpc/channel.h"
#include "brpc/transport.h"

namespace brpc {
    class RdmaTransport : public Transport {
        friend class TransportFactory;
        friend class rdma::RdmaEndpoint;
        friend class rdma::RdmaConnect;
    public:
        void Init(Socket* socket, const SocketOptions& options) override;
        void Release() override;
        int Reset(int32_t expected_nref) override;
        std::shared_ptr<AppConnect> Connect() override;
        int CutFromIOBuf(butil::IOBuf* buf) override;
        ssize_t CutFromIOBufList(butil::IOBuf** buf, size_t ndata) override;
        int WaitEpollOut(butil::atomic<int>* _epollout_butex, bool pollin, const timespec duetime) override;
        void ProcessEvent(bthread_attr_t attr) override;
        void QueueMessage(InputMessageClosure& inputMsg, int* num_bthread_created, bool last_msg) override;
        void Debug(std::ostream &os, Socket* ptr) override;
        rdma::RdmaEndpoint* GetRdmaEp() {
            CHECK(_rdma_ep != NULL);
            return _rdma_ep;
        }
        static int ContextInitOrDie(bool serverOrNot, const void* _options);
    private:
        static bool OptionsAvailableForRdma(const ChannelOptions* opt);
        static bool OptionsAvailableOverRdma(const ServerOptions* opt);
    private:
        // The on/off state of RDMA
        enum RdmaState {
            RDMA_ON,
            RDMA_OFF,
            RDMA_UNKNOWN
        };
        // The RdmaEndpoint
        rdma::RdmaEndpoint* _rdma_ep = NULL;
        // Should use RDMA or not
        RdmaState _rdma_state;
        std::shared_ptr<TcpTransport>  _tcp_transport;
    };
}
#endif // BRPC_WITH_RDMA
#endif //BRPC_RDMA_TRANSPORT_H