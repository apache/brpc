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

#if BRPC_WITH_GDR

#include "brpc/gdr_transport.h"
#include "brpc/rdma/rdma_helper.h"

namespace brpc {

void GdrTransport::Init(Socket *socket, const SocketOptions &options) {
    DoInit(socket, options, true);
}

int GdrTransport::GdrContextInitOrDie() {
    rdma::GlobalGdrInitializeOrDie();
    return 0;
}

} // namespace brpc
#endif
