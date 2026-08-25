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


#ifdef BRPC_WITH_GDR
#include <cuda.h>
#include <cuda_runtime.h>
#include <memory>
#endif

#include <gflags/gflags.h>
#include "butil/atomicops.h"
#include "butil/logging.h"
#include "butil/time.h"
#include "brpc/server.h"
#include "bvar/variable.h"
#include "test.pb.h"

#ifdef BRPC_WITH_RDMA

DEFINE_int32(port, 8002, "TCP Port of this server");
DEFINE_bool(use_rdma, true, "Use RDMA or not");
DEFINE_bool(use_gdr, false, "Use GDR or not");
#ifdef BRPC_WITH_GDR
DEFINE_bool(consume_on_gpu, false,
            "Demonstrate how the app consumes the received attachment in GPU "
            "memory when GDR is enabled: copy it into an app-owned GPU buffer "
            "for CUDA kernels (only meaningful with --use_gdr)");
#endif

butil::atomic<uint64_t> g_last_time(0);

namespace test {
class PerfTestServiceImpl : public PerfTestService {
public:
    PerfTestServiceImpl() {}
    ~PerfTestServiceImpl() {}

    void Test(google::protobuf::RpcController* cntl_base,
              const PerfTestRequest* request,
              PerfTestResponse* response,
              google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        uint64_t last = g_last_time.load(butil::memory_order_relaxed);
        uint64_t now = butil::monotonic_time_us();
        if (now > last && now - last > 100000) {
            if (g_last_time.exchange(now, butil::memory_order_relaxed) == last) {
                response->set_cpu_usage(bvar::Variable::describe_exposed("process_cpu_usage"));
            } else {
                response->set_cpu_usage("");
            }
        } else {
            response->set_cpu_usage("");
        }
        if (request->echo_attachment()) {
            brpc::Controller* cntl =
                static_cast<brpc::Controller*>(cntl_base);
#ifdef BRPC_WITH_GDR
            if (FLAGS_use_gdr && FLAGS_consume_on_gpu) {
                // Copy the received attachment to GPU memory owned by the
                // application, so the app can consume it with CUDA kernels.
                // The data is consumed locally and never sent back, so no
                // RDMA memory region registration is needed.
                // Pick the copy direction from the actual source memory.
                // Even with --use_gdr, the server still accepts plain TCP
                // (or non-GDR RDMA) connections: the GDR capability is
                // per-connection, negotiated at RDMA handshake, while a
                // single server serves all connection kinds. A TCP client
                // (or an RDMA client falling back to non-GDR, e.g. when its
                // RdmaEndpoint/GPU pool failed to initialize) delivers its
                // attachment in host memory, while a GDR client delivers it
                // in GPU memory. Passing a host pointer to a device-to-device
                // copy is undefined behavior per the CUDA documentation, so
                // host sources are staged through a host buffer and copied
                // host-to-device instead.
                const butil::IOBuf& att = cntl->request_attachment();
                const size_t size = att.size();
                if (size > 0) {
                    void* gpu_addr = nullptr;
                    if (cudaMalloc(&gpu_addr, size) != cudaSuccess) {
                        LOG(ERROR) << "Fail to alloc gpu memory for attachment";
                    } else if (att.is_gpu_memory()) {
                        if (att.copy_from_gpu(gpu_addr, size, 0,
                                              /*to_gpu=*/true) != size) {
                            LOG(ERROR) << "Fail to copy attachment to gpu memory";
                            cudaFree(gpu_addr);
                            return;
                        }
                    } else {
                        std::unique_ptr<char[]> host_buf(new (std::nothrow) char[size]);
                        if (!host_buf) {
                            LOG(ERROR) << "Fail to alloc staging buffer";
                            cudaFree(gpu_addr);
                            return;
                        }
                        if (att.copy_to(host_buf.get(), size) != size ||
                            cudaMemcpy(gpu_addr, host_buf.get(), size,
                                       cudaMemcpyHostToDevice) != cudaSuccess) {
                            LOG(ERROR) << "Fail to copy attachment to gpu memory";
                            cudaFree(gpu_addr);
                            return;
                        }
                    }
                    // gpu_addr now holds the attachment in GPU memory;
                    // the app consumes it directly (e.g. feeds a CUDA
                    // kernel) and releases it when done.
                    cudaFree(gpu_addr);
                }
            } else if (request->echo_attachment())
#endif
            {
                cntl->response_attachment().append(cntl->request_attachment());
            }
        }
    }
};
}

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_use_gdr && !FLAGS_use_rdma) {
        LOG(ERROR) << "--use_gdr requires --use_rdma to be enabled";
        return -1;
    }

    brpc::Server server;
    test::PerfTestServiceImpl perf_test_service_impl;

    if (server.AddService(&perf_test_service_impl, 
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }
    g_last_time.store(0, butil::memory_order_relaxed);

    brpc::ServerOptions options;
    options.socket_mode = FLAGS_use_rdma? brpc::SOCKET_MODE_RDMA : brpc::SOCKET_MODE_TCP;
#ifdef BRPC_WITH_GDR
    if (FLAGS_use_gdr) {
        options.socket_mode = brpc::SOCKET_MODE_GDR;
    }
#endif

    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    server.RunUntilAskedToQuit();
    return 0;
}

#else


int main(int argc, char* argv[]) {
    LOG(ERROR) << " brpc is not compiled with rdma. To enable it, please refer to https://github.com/apache/brpc/blob/master/docs/en/rdma.md";
    return 0;
}

#endif
