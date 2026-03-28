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

#ifndef BRPC_TEST_FUZZING_FUZZ_COMMON_H
#define BRPC_TEST_FUZZING_FUZZ_COMMON_H

#include "brpc/socket.h"
#include "butil/endpoint.h"

// Create a valid Socket for use in fuzz harnesses that need a non-NULL Socket*.
// Returns a raw Socket* that remains valid for the lifetime of the process
// (held by the static SocketUniquePtr).
inline brpc::Socket* get_fuzz_socket() {
    static brpc::SocketId sid = 0;
    static brpc::SocketUniquePtr sock_ptr;
    static bool initialized = false;

    if (!initialized) {
        brpc::SocketOptions options;
        options.remote_side = butil::EndPoint(butil::IP_ANY, 7777);
        if (brpc::Socket::Create(options, &sid) == 0) {
            brpc::Socket::Address(sid, &sock_ptr);
        }
        initialized = true;
    }

    return sock_ptr.get();
}

#endif // BRPC_TEST_FUZZING_FUZZ_COMMON_H
