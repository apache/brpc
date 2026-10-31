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

// Prints the layout of the classes that applications embed. The FlatBuffers
// ON validation builds this file against the ON configuration and against an
// OFF copy of butil/config.h and requires identical output, so enabling the
// option never changes the ABI of Channel, Controller or Server.

#include <cstddef>
#include <cstdio>
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"

#define PRINT_SIZE(type) \
    std::printf("%s size=%zu align=%zu\n", #type, sizeof(type), alignof(type))
#define PRINT_OFFSET(type, member) \
    std::printf("%s::%s offset=%zu\n", #type, #member, offsetof(type, member))

int main() {
    std::printf("BRPC_WITH_FLATBUFFERS=%d\n", BRPC_WITH_FLATBUFFERS);
    PRINT_SIZE(brpc::Channel);
    PRINT_SIZE(brpc::Controller);
    PRINT_SIZE(brpc::Server);
    PRINT_SIZE(brpc::Server::MethodProperty);
    // Members declared after the FlatBuffers members must not move either.
    PRINT_OFFSET(brpc::Channel, _options);
    PRINT_OFFSET(brpc::Controller, _flatbuffers_method);
    PRINT_OFFSET(brpc::Controller, _auth);
    PRINT_OFFSET(brpc::Server, _flatbuffers_services);
    PRINT_OFFSET(brpc::Server, _method_map);
    return 0;
}
