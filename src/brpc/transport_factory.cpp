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

#include "transport_factory.h"
#include "brpc/tcp_transport.h"
#include "brpc/rdma_transport.h"
namespace brpc {
    int TransportFactory::ContextInitOrDie(Mode mode, bool serverOrNot, const void* _options) {
        if (mode == TCP) {
            return 0;
        }
#if BRPC_WITH_RDMA
        else if (mode == RDMA) {
            return RdmaTransport::ContextInitOrDie(serverOrNot, _options);
        }
#endif
        else {
            LOG(ERROR) << "unknow transport type  " << mode;
            return 1;
        }
    }

    std::shared_ptr<Transport> TransportFactory::CreateTransport(Mode mode) {
        if (mode == TCP) {
            // 使用共享指针创建对象
            return std::make_shared<TcpTransport>();
        }
#if BRPC_WITH_RDMA
        else if (mode == RDMA) {
            return std::make_shared<RdmaTransport>();
        }
#endif
        else {
            LOG(ERROR) << "socket_mode set error";
            return nullptr;
        }
    }
} // namespace brpc