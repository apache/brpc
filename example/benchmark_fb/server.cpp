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
#include <chrono>
#include <iostream>
#include <thread>
#include "butil/config.h"
#include "butil/endpoint.h"
#include "brpc/closure_guard.h"
#include "brpc/controller.h"
#include "brpc/errno.pb.h"
#include "brpc/server.h"
#include "echo.brpc.fb.h"
#include "echo_pb.pb.h"

#if !BRPC_WITH_FLATBUFFERS
#error "benchmark_fb requires a FlatBuffers-enabled brpc library"
#endif

DEFINE_string(listen_addr, "127.0.0.1:0", "IPv4 address; port 0 selects a free port. "
              "A non-loopback address also needs --allow_remote");
DEFINE_bool(allow_remote, false, "Allow a non-loopback --listen_addr for cross-host benchmarks");
DEFINE_int32(duration_s, 60, "Exit after 1..86400 seconds, or earlier on SIGINT/SIGTERM");
DEFINE_int32(max_concurrency, 32, "Maximum concurrent requests, 1..4096");

namespace {
const size_t kMaxBytes = 1024 * 1024;

// Reserve the echoed string plus table overhead up front so a large reply
// does not grow the builder by repeated reallocate-and-copy.
size_t BuilderSize(const benchmark_fb::Request* input) {
    const size_t text = input->message() ? input->message()->size() : 0;
    return text + 256;
}

class BenchmarkServiceImpl : public benchmark_fb::BenchmarkService {
public:
    void Echo(google::protobuf::RpcController* controller,
              const brpc::flatbuffers::Message* request,
              brpc::flatbuffers::Message* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        if (!controller) {
            return;
        }
        auto* cntl = static_cast<brpc::Controller*>(controller);
        // The generated dispatcher also verifies the schema. Keep this guard
        // for callers invoking the typed service method directly.
        if (!request || !response || !request->Verify<benchmark_fb::Request>()) {
            cntl->SetFailed(brpc::EREQUEST, "Invalid FlatBuffers Request");
            return;
        }
        const auto* input = request->GetRoot<benchmark_fb::Request>();
        if ((input->message() && input->message()->size() > kMaxBytes) ||
            cntl->request_attachment().size() > kMaxBytes) {
            cntl->SetFailed(brpc::EREQUEST, "Example payload limit is 1 MiB per field");
            return;
        }
        brpc::flatbuffers::MessageBuilder builder(BuilderSize(input));
        // Preserve absent versus empty strings, including embedded zero bytes.
        const auto text = input->message() ?
            builder.CreateString(input->message()->data(), input->message()->size()) :
            ::flatbuffers::Offset<::flatbuffers::String>();
        builder.Finish(benchmark_fb::CreateResponse(
            builder, input->request_id(), text, cntl->request_attachment().size()));
        *response = builder.ReleaseMessage();
        cntl->response_attachment().append(cntl->request_attachment());
    }
};

// Protobuf control service over baidu_std with the same echo semantics.
class PbBenchmarkServiceImpl : public benchmark_fb_pb::BenchmarkService {
public:
    void Echo(google::protobuf::RpcController* controller,
              const benchmark_fb_pb::Request* request,
              benchmark_fb_pb::Response* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        auto* cntl = static_cast<brpc::Controller*>(controller);
        if (request->message().size() > kMaxBytes ||
            cntl->request_attachment().size() > kMaxBytes) {
            cntl->SetFailed(brpc::EREQUEST, "Example payload limit is 1 MiB per field");
            return;
        }
        response->set_request_id(request->request_id());
        if (request->has_message()) {
            response->set_message(request->message());
        }
        response->set_attachment_size(cntl->request_attachment().size());
        cntl->response_attachment().append(cntl->request_attachment());
    }
};
}  // namespace

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    butil::EndPoint endpoint;
    if ((FLAGS_listen_addr.compare(0, 10, "127.0.0.1:") != 0 && !FLAGS_allow_remote) ||
        butil::str2endpoint(FLAGS_listen_addr.c_str(), &endpoint) != 0 ||
        FLAGS_duration_s < 1 || FLAGS_duration_s > 86400 ||
        FLAGS_max_concurrency < 1 || FLAGS_max_concurrency > 4096) {
        std::cerr << "Use --listen_addr=127.0.0.1:PORT (or --allow_remote), "
                     "--duration_s=1..86400 and --max_concurrency=1..4096\n";
        return 2;
    }
    // brpc defaults SIGTERM to immediate termination. This example always
    // enables graceful shutdown, including the smoke's owned-process cleanup.
    if (GFLAGS_NAMESPACE::SetCommandLineOption(
            "graceful_quit_on_sigterm", "true").empty()) {
        std::cerr << "Cannot enable graceful SIGTERM handling\n";
        return 1;
    }
    BenchmarkServiceImpl service;
    brpc::Server server;
    if (server.AddFlatBuffersService(&service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        std::cerr << "Cannot register FlatBuffers service\n";
        return 1;
    }
    PbBenchmarkServiceImpl pb_service;
    if (server.AddService(&pb_service, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        std::cerr << "Cannot register protobuf control service\n";
        return 1;
    }
    brpc::ServerOptions options;
    options.has_builtin_services = false;
    options.enabled_protocols = "fb_rpc baidu_std";
    options.max_concurrency = FLAGS_max_concurrency;
    if (server.Start(endpoint, &options) != 0) {
        std::cerr << "Cannot start FlatBuffers server\n";
        return 1;
    }
    // Install brpc's termination handlers before publishing readiness.
    brpc::IsAskedToQuit();
    std::cout << "BRPC_FB_READY " << server.listen_address() << std::endl;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(FLAGS_duration_s);
    while (!brpc::IsAskedToQuit() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const int stop_result = server.Stop(0);
    const int join_result = server.Join();
    return stop_result == 0 && join_result == 0 ? 0 : 1;
}
