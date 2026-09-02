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

#include "brpc/policy/rtmp_protocol.h"
#include "brpc/rtmp.h"
#include "brpc/server.h"
#include "butil/iobuf.h"
#include "fuzz_common.h"

namespace {

constexpr size_t kMinInputLength = 5;
constexpr size_t kMaxInputLength = 4096;

class FuzzRtmpService : public brpc::RtmpService {
public:
    brpc::RtmpServerStream* NewStream(const brpc::RtmpConnectRequest&) override {
        return nullptr;
    }
};

brpc::Server* get_fuzz_server() {
    static brpc::Server* started = []() -> brpc::Server* {
        static FuzzRtmpService rtmp_service;
        static brpc::Server server;
        brpc::ServerOptions options;
        options.rtmp_service = &rtmp_service;
        options.has_builtin_services = false;
        options.num_threads = 0;
        if (server.Start(0, &options) != 0) {
            // Fall back to a context with no service
            LOG(ERROR) << "Fail to start fuzz RTMP server, "
                          "server-side command handlers will be skipped";
            return nullptr;
        }

        // Stop again straight away. _options -- and so rtmp_service -- is not
        // touched by Stop()/Join()
        server.Stop(0);
        server.Join();
        return &server;
    }();
    return started;
}

}  // namespace

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < kMinInputLength || size > kMaxInputLength){
        return 0;
    }

    brpc::Socket* sock = get_fuzz_socket();
    if (sock == nullptr) {
        return 0;
    }
    brpc::Server* server = get_fuzz_server();

    butil::IOBuf buf;
    buf.append(data, size);

    sock->reset_parsing_context(new brpc::policy::RtmpContext(nullptr, server));
    brpc::policy::ParseRtmpMessage(&buf, sock, false, server);
    return 0;
}
