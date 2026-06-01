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

#ifndef BRPC_IOURING_TRANSPORT_H
#define BRPC_IOURING_TRANSPORT_H

#if BRPC_WITH_IOURING

#include "brpc/socket.h"
#include "brpc/channel.h"
#include "brpc/transport.h"
#include "brpc/tcp_transport.h"

// Forward-declare IouringEndpoint to break the circular include cycle between
// iouring_endpoint.h (which would include iouring_transport.h) and this file.
namespace brpc {
namespace iouring {
class IouringEndpoint;
}  // namespace iouring
}  // namespace brpc

namespace brpc {

// IouringTransport wraps an IouringEndpoint and implements the Transport
// interface so that bRPC's Socket can use io_uring for async I/O instead of
// the default epoll-based mechanism.
//
// Design mirrors RdmaTransport:
//  - On the write path, CutFromIOBufList delegates to IouringEndpoint which
//    submits writev SQEs to the io_uring ring.
//  - On the read path, the poller drives IouringEndpoint::PollCq, which reaps
//    CQEs and appends received bytes to socket->_read_buf.
//  - ProcessEvent / QueueMessage are identical to TcpTransport.
class IouringTransport : public Transport {
    friend class TransportFactory;
    friend class iouring::IouringEndpoint;
public:
    void Init(Socket* socket, const SocketOptions& options) override;
    void Release() override;
    int  Reset(int32_t expected_nref) override;
    std::shared_ptr<AppConnect> Connect() override;
    int  CutFromIOBuf(butil::IOBuf* buf) override;
    ssize_t CutFromIOBufList(butil::IOBuf** buf, size_t ndata) override;
    int  WaitEpollOut(butil::atomic<int>* _epollout_butex,
                      bool pollin, timespec duetime) override;
    void ProcessEvent(bthread_attr_t attr) override;
    void QueueMessage(InputMessageClosure& input_msg,
                      int* num_bthread_created, bool last_msg) override;
    void Debug(std::ostream& os) override;

    iouring::IouringEndpoint* GetIouringEp() {
        return _iouring_ep;
    }

    // Global context initialization (called once per server/channel start)
    static int ContextInitOrDie(bool serverOrNot, const void* _options);

private:
    // The io_uring endpoint
    iouring::IouringEndpoint* _iouring_ep = nullptr;

    // Fallback TCP transport (used when io_uring is unavailable at runtime)
    std::shared_ptr<TcpTransport> _tcp_transport;
};

}  // namespace brpc

#endif  // BRPC_WITH_IOURING
#endif  // BRPC_IOURING_TRANSPORT_H
