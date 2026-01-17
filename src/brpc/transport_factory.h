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

#ifndef BRPC_TRANSPORT_FACTORY_H
#define BRPC_TRANSPORT_FACTORY_H

#include "brpc/errno.pb.h"
#include "brpc/common.h"
#include "brpc/transport.h"

#if BRPC_WITH_RDMA
BAIDU_REGISTER_ERRNO(brpc::ERDMA, "RDMA verbs error");
BAIDU_REGISTER_ERRNO(brpc::ERDMAMEM, "Memory not registered for RDMA");
#endif

namespace brpc {
    // transport factory to create transport instance with socket_mode {TCP、RDMA}
    class TransportFactory {
    public:
        static int ContextInitOrDie(Mode mode, bool serverOrNot, const void* _options);
        // create transport instance with socket mode
        static std::shared_ptr<Transport> CreateTransport(Mode mode);
    };
}

#endif //BRPC_TRANSPORT_FACTORY_H