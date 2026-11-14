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

#include <gflags/gflags.h>

#include "butil/atomicops.h"
#include "butil/logging.h"
#include "butil/time.h"
#include "brpc/closure_guard.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "bvar/variable.h"
#include "test.pb.h"

#if BRPC_WITH_URMA

DEFINE_int32(port, 8003, "TCP Port of this server");
DEFINE_bool(use_urma, true, "Use URMA transport (true) or TCP (false)");
DEFINE_int32(urma_priority, -1,
             "Outbound URMA priority: -1 automatic, or [0,15]");
DEFINE_int32(urma_sl, -1, "Outbound URMA SL: -1 unspecified, or [0,255]");

butil::atomic<uint64_t> g_last_time(0);

namespace test {
class PerfTestServiceImpl : public PerfTestService {
public:
    void Test(google::protobuf::RpcController* cntl_base,
              const PerfTestRequest* request,
              PerfTestResponse* response,
              google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        const uint64_t last =
            g_last_time.load(butil::memory_order_relaxed);
        const uint64_t now = butil::monotonic_time_us();
        if (now > last && now - last > 100000) {
            if (g_last_time.exchange(now, butil::memory_order_relaxed) == last) {
                response->set_cpu_usage(
                    bvar::Variable::describe_exposed("process_cpu_usage"));
            } else {
                response->set_cpu_usage("");
            }
        } else {
            response->set_cpu_usage("");
        }
        if (request->echo_attachment()) {
            brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
            cntl->response_attachment().append(cntl->request_attachment());
        }
    }
};
}  // namespace test

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    brpc::Server server;
    test::PerfTestServiceImpl service;

    if (server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add PerfTestService";
        return -1;
    }

    brpc::ServerOptions options;
    options.urma_qos.priority = FLAGS_urma_priority;
    options.urma_qos.service_level = FLAGS_urma_sl;
    options.socket_mode = FLAGS_use_urma ? brpc::SOCKET_MODE_URMA
                                          : brpc::SOCKET_MODE_TCP;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start server";
        return -1;
    }
    LOG(INFO) << "URMA performance server started on port " << FLAGS_port
              << " (use_urma=" << FLAGS_use_urma
              << ", urma_priority=" << FLAGS_urma_priority
              << ", urma_sl=" << FLAGS_urma_sl << ")";
    server.RunUntilAskedToQuit();
    return 0;
}

#else

#include <cstdio>
int main() {
    printf("This example requires brpc built with -DWITH_URMA=ON.\n");
    return 0;
}

#endif  // BRPC_WITH_URMA
