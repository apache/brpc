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

// A client sending requests to the iouring echo server every interval_ms.

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <brpc/channel.h>
#if BRPC_WITH_IOURING
#include <brpc/iouring/iouring_helper.h>
#endif
#include "echo.pb.h"

DEFINE_bool(use_iouring, false,
            "Use io_uring transport for outbound connections. "
            "Requires the binary to be built with BRPC_WITH_IOURING.");
DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in src/brpc/options.proto");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:8000", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries (not including the first RPC)");
DEFINE_int32(interval_ms, 1000, "Milliseconds between consecutive requests");

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

#if BRPC_WITH_IOURING
    if (FLAGS_use_iouring) {
        // io_uring is not a distinct transport medium (unlike RDMA/UBRING);
        // it only changes how this process submits I/O for the same TCP fd.
        // Bringing up the global context here makes every plain-TCP Channel
        // created afterwards automatically pick it up (see
        // TcpTransport::Init()) -- no per-Channel socket_mode is needed.
        brpc::iouring::GlobalIouringInitializeOrDie();
        if (!brpc::iouring::InitPollingModeWithTag(/*tag=*/0)) {
            LOG(ERROR) << "Fail to init io_uring polling mode";
            return -1;
        }
    }
#else
    if (FLAGS_use_iouring) {
        LOG(ERROR) << "This binary was not compiled with io_uring support "
                      "(BRPC_WITH_IOURING is not set). "
                      "Rebuild with -DWITH_IOURING=ON.";
        return -1;
    }
#endif

    brpc::Channel channel;

    brpc::ChannelOptions options;
    options.protocol = FLAGS_protocol;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms;
    options.max_retry = FLAGS_max_retry;
    // No socket_mode to set here: io_uring is picked up automatically by
    // TcpTransport once the global context has been initialized above.
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    example::EchoService_Stub stub(&channel);

    int log_id = 0;
    while (!brpc::IsAskedToQuit()) {
        example::EchoRequest request;
        example::EchoResponse response;
        brpc::Controller cntl;

        request.set_message("hello world");
        cntl.set_log_id(log_id++);

        stub.Echo(&cntl, &request, &response, NULL);
        if (!cntl.Failed()) {
            LOG(INFO) << "Received response from " << cntl.remote_side()
                      << ": " << response.message()
                      << " latency=" << cntl.latency_us() << "us";
        } else {
            LOG(WARNING) << cntl.ErrorText();
        }
        usleep(FLAGS_interval_ms * 1000L);
    }

    LOG(INFO) << "EchoClient is going to quit";
    return 0;
}
