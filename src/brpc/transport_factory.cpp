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

#include "brpc/transport_factory.h"
#include "brpc/rdma_transport.h"
#include "brpc/tcp_transport.h"
#include "brpc/ubshm_transport.h"
#include "brpc/urma_transport.h"

namespace brpc {

int TransportFactory::ContextInitOrDie(
    SocketMode mode, bool server_or_not, const void* options) {
    if (mode == SOCKET_MODE_TCP) {
        return 0;
    }
#if BRPC_WITH_RDMA
    if (mode == SOCKET_MODE_RDMA) {
        return RdmaTransport::ContextInitOrDie(server_or_not, options);
    }
#endif
#if BRPC_WITH_URMA
    if (mode == SOCKET_MODE_URMA) {
        return UrmaTransport::ContextInitOrDie(server_or_not, options);
    }
#endif
#if BRPC_WITH_UBRING
    if (mode == SOCKET_MODE_UBRING) {
        return UBShmTransport::ContextInitOrDie(server_or_not, options);
    }
#endif
    LOG(ERROR) << "Unknown transport type " << mode;
    return 1;
}

std::unique_ptr<Transport> TransportFactory::CreateTransport(SocketMode mode) {
    if (mode == SOCKET_MODE_TCP) {
        return std::unique_ptr<TcpTransport>(new TcpTransport());
    }
#if BRPC_WITH_RDMA
    if (mode == SOCKET_MODE_RDMA) {
        return std::unique_ptr<RdmaTransport>(new RdmaTransport());
    }
#endif
#if BRPC_WITH_URMA
    if (mode == SOCKET_MODE_URMA) {
        return std::unique_ptr<UrmaTransport>(new UrmaTransport());
    }
#endif
#if BRPC_WITH_UBRING
    if (mode == SOCKET_MODE_UBRING) {
        return std::unique_ptr<UBShmTransport>(new UBShmTransport());
    }
#endif
    LOG(ERROR) << "Unknown transport type " << mode;
    return nullptr;
}

}  // namespace brpc
