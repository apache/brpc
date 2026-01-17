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

#ifndef BRPC_TCP_TRANSPORT_H
#define BRPC_TCP_TRANSPORT_H

#include "brpc/transport.h"
#include "brpc/socket.h"

namespace brpc {
    class TcpTransport : public Transport {
        friend class TransportFactory;
    public:
        void Init(Socket* socket, const SocketOptions& options) override;
        void Release() override;
        int Reset(int32_t expected_nref) override;
        std::shared_ptr<AppConnect> Connect() override;
        int CutFromIOBuf(butil::IOBuf* buf) override;
        ssize_t CutFromIOBufList(butil::IOBuf** buf, size_t ndata) override;
        int WaitEpollOut(butil::atomic<int>* _epollout_butex, bool pollin, const timespec duetime) override;
        void ProcessEvent(bthread_attr_t attr) override;
        void QueueMessage(InputMessageClosure& input_msg, int* num_bthread_created, bool last_msg) override;
        void Debug(std::ostream &os, Socket* ptr) override;
    };
}

#endif //BRPC_TCP_TRANSPORT_H