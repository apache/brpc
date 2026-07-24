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

#if BRPC_WITH_IOURING
// Forward-declare IouringEndpoint to break the circular include cycle between
// iouring_endpoint.h (which would include tcp_transport.h) and this file.
namespace brpc {
namespace iouring {
class IouringEndpoint;
}  // namespace iouring
}  // namespace brpc
#endif  // BRPC_WITH_IOURING

namespace brpc {
// TcpTransport reads/writes a plain TCP fd.
//
// io_uring is NOT a separate transport medium (unlike RDMA/UBRING, which
// migrate the data path onto a different physical medium and therefore need
// a handshake/negotiation between the two peers). io_uring only changes how
// *this* process submits I/O for the very same TCP fd: instead of calling
// read(2)/write(2) synchronously after an epoll edge-trigger, SQEs are
// submitted to the ring and CQEs are reaped asynchronously by a dedicated
// Poller bthread. The peer is completely unaware of this choice.
//
// Consequently TcpTransport itself owns the optional IouringEndpoint: when
// io_uring is globally enabled (see iouring::IsIouringAvailable()) and
// resource allocation for this socket succeeds, the write path submits SQEs
// via the endpoint and the read path is entirely driven by the Poller
// (ProcessNewMessage is invoked directly from IouringEndpoint::PollCq).
// Otherwise (io_uring disabled, or per-socket allocation failed) every
// method transparently falls back to the synchronous fd path.
class TcpTransport : public Transport {
    friend class TransportFactory;
public:
    void Init(Socket* socket, const SocketOptions& options) override;
    void Release() override;
    int Reset(int32_t expected_nref) override;
    std::shared_ptr<AppConnect> Connect() override;
    int CutFromIOBuf(butil::IOBuf* buf) override;
    ssize_t CutFromIOBufList(butil::IOBuf** buf, size_t ndata) override;
    int WaitEpollOut(butil::atomic<int>* _epollout_butex, bool pollin, timespec duetime) override;
    void ProcessEvent(bthread_attr_t attr) override;
    void QueueMessage(InputMessageClosure& input_msg, int* num_bthread_created, bool last_msg) override;
    void Debug(std::ostream &os) override;

    // Global context initialization (called once per server/channel start).
    // Currently only used to bring up the global io_uring context (probing
    // kernel opcodes, starting Poller bthreads, etc). No-op when
    // BRPC_WITH_IOURING is not compiled in.
    static int ContextInitOrDie(bool serverOrNot, const void* _options);

#if BRPC_WITH_IOURING
    iouring::IouringEndpoint* GetIouringEp() {
        return _iouring_ep;
    }
#endif  // BRPC_WITH_IOURING

private:
#if BRPC_WITH_IOURING
    // The optional io_uring endpoint for this socket. NULL when io_uring is
    // disabled globally or per-socket resource allocation failed; in that
    // case every method falls back to the synchronous fd path below.
    iouring::IouringEndpoint* _iouring_ep = nullptr;
#endif  // BRPC_WITH_IOURING
};
} // namespace brpc

#endif //BRPC_TCP_TRANSPORT_H
