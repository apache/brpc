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
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "butil/config.h"
#include "butil/endpoint.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "echo.brpc.fb.h"

#if !BRPC_WITH_FLATBUFFERS
#error "benchmark_fb requires a FlatBuffers-enabled brpc library"
#endif

DEFINE_string(server, "", "Required IPv4 loopback endpoint printed by benchmark_fb_server");
DEFINE_string(connection_type, "single", "single, pooled, or short");
DEFINE_int32(request_count, 16, "Total requests across all threads, 1..1000");
DEFINE_int32(thread_num, 2, "Number of concurrent synchronous callers, 1..16");
DEFINE_int32(request_size, 64, "Bytes in the optional request string, 0..1048576");
DEFINE_int32(attachment_size, 0, "Bytes in the echoed attachment, 0..1048576");
DEFINE_int32(timeout_ms, 1500, "Per-RPC timeout, 1..5000 milliseconds");
DEFINE_int32(deadline_ms, 30000, "Overall client deadline, 1..120000 milliseconds");
DEFINE_bool(omit_message, false, "Omit the optional string rather than sending an empty string");
DEFINE_bool(corrupt_request, false, "Send an invalid root offset and require schema rejection");

namespace {
using brpc::flatbuffers::Message;
using brpc::flatbuffers::MessageBuilder;

std::string MakeBytes(size_t size, unsigned multiplier) {
    std::string bytes(size, '\0');
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<char>((i * multiplier) % 251);
    }
    return bytes;
}

Message MakeRequest(uint64_t request_id, const std::string& text) {
    MessageBuilder builder;
    const auto message = FLAGS_omit_message ?
        ::flatbuffers::Offset<::flatbuffers::String>() : builder.CreateString(text);
    builder.Finish(benchmark_fb::CreateRequest(builder, request_id, message));
    Message request = builder.ReleaseMessage();
    if (FLAGS_corrupt_request) {
        // Damage only the schema, not FRPC framing, so the server's generated
        // dispatcher must reject the request before invoking the service.
        memset(request.mutable_data(), 0xff, sizeof(::flatbuffers::uoffset_t));
    }
    return request;
}

bool CheckResponse(const Message& response, uint64_t request_id,
                   const std::string& text, const std::string& attachment,
                   const brpc::Controller& cntl) {
    if (!response.Verify<benchmark_fb::Response>()) {
        return false;
    }
    const auto* output = response.GetRoot<benchmark_fb::Response>();
    if (output->request_id() != request_id ||
        output->attachment_size() != attachment.size() ||
        cntl.response_attachment().to_string() != attachment) {
        return false;
    }
    if (FLAGS_omit_message) {
        return output->message() == nullptr;
    }
    return output->message() != nullptr && output->message()->str() == text;
}

bool ValidFlags(butil::EndPoint* endpoint) {
    return FLAGS_server.compare(0, 10, "127.0.0.1:") == 0 &&
        butil::str2endpoint(FLAGS_server.c_str(), endpoint) == 0 && endpoint->port > 0 &&
        (FLAGS_connection_type == "single" || FLAGS_connection_type == "pooled" ||
         FLAGS_connection_type == "short") &&
        FLAGS_request_count >= 1 && FLAGS_request_count <= 1000 &&
        FLAGS_thread_num >= 1 && FLAGS_thread_num <= 16 &&
        FLAGS_request_size >= 0 && FLAGS_request_size <= 1024 * 1024 &&
        FLAGS_attachment_size >= 0 && FLAGS_attachment_size <= 1024 * 1024 &&
        FLAGS_timeout_ms >= 1 && FLAGS_timeout_ms <= 5000 &&
        FLAGS_deadline_ms >= 1 && FLAGS_deadline_ms <= 120000;
}

int RunClient(const butil::EndPoint& endpoint) {
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "fb_rpc";
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms;
    options.connect_timeout_ms = std::min(FLAGS_timeout_ms, 1000);
    options.max_retry = 0;
    if (channel.Init(endpoint, &options) != 0) {
        std::cerr << "Cannot initialize FlatBuffers channel\n";
        return 1;
    }
    // Generated stub methods route through Channel::FBCallMethod.
    benchmark_fb::BenchmarkService::Stub stub(&channel);
    const std::string text = MakeBytes(FLAGS_omit_message ? 0 : FLAGS_request_size, 31);
    const std::string attachment = MakeBytes(FLAGS_attachment_size, 17);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(FLAGS_deadline_ms);
    std::atomic<int> next{0};
    std::atomic<int> successes{0};
    std::atomic<int> expected_rejections{0};
    std::atomic<bool> failed{false};
    std::mutex error_mutex;
    auto fail = [&](const std::string& message) {
        if (!failed.exchange(true)) {
            std::lock_guard<std::mutex> lock(error_mutex);
            std::cerr << message << '\n';
        }
    };
    auto worker = [&] {
        try {
            while (!failed.load()) {
                const int index = next.fetch_add(1);
                if (index >= FLAGS_request_count) {
                    break;
                }
                const int64_t remaining = std::chrono::duration_cast<
                    std::chrono::milliseconds>(deadline -
                    std::chrono::steady_clock::now()).count();
                if (remaining <= 0) {
                    fail("Overall client deadline exceeded");
                    break;
                }
                const uint64_t request_id = static_cast<uint64_t>(index) + 1;
                const Message request = MakeRequest(request_id, text);
                Message response;
                brpc::Controller cntl;
                cntl.set_timeout_ms(std::min<int64_t>(FLAGS_timeout_ms, remaining));
                cntl.request_attachment().append(attachment);
                stub.Echo(&cntl, &request, &response, nullptr);
                if (FLAGS_corrupt_request) {
                    // The generated generic RpcController::SetFailed(string)
                    // maps to -1. Timeouts and connection errors do not count.
                    if (!cntl.Failed() || cntl.ErrorCode() != -1 ||
                        response.size() != 0 || !cntl.response_attachment().empty()) {
                        fail("Expected server schema rejection; error=" + cntl.ErrorText());
                        break;
                    }
                    ++expected_rejections;
                } else if (cntl.Failed()) {
                    fail("RPC failed: " + cntl.ErrorText());
                    break;
                } else if (!CheckResponse(response, request_id, text, attachment, cntl)) {
                    fail("Response schema, fields, optional string or attachment mismatch");
                    break;
                } else {
                    ++successes;
                }
            }
        } catch (const std::exception& error) {
            fail(std::string("Client worker failed: ") + error.what());
        }
    };
    std::vector<std::thread> workers;
    workers.reserve(FLAGS_thread_num);
    try {
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            workers.emplace_back(worker);
        }
    } catch (const std::exception& error) {
        fail(std::string("Cannot start client worker: ") + error.what());
    }
    for (auto& thread : workers) {
        thread.join();
    }
    const int completed = successes.load() + expected_rejections.load();
    if (completed != FLAGS_request_count) {
        fail("Not all requested RPCs completed");
    }
    std::cout << "{\"completed\":" << completed
              << ",\"successes\":" << successes.load()
              << ",\"expected_rejections\":" << expected_rejections.load()
              << ",\"failures\":" << (failed.load() ? 1 : 0) << "}\n";
    return failed.load() ? 1 : 0;
}
}  // namespace

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    butil::EndPoint endpoint;
    if (!ValidFlags(&endpoint)) {
        std::cerr << "Invalid flags; --server=127.0.0.1:PORT is required. "
                     "See --help and README.md for bounded parameter ranges.\n";
        return 2;
    }
    try {
        return RunClient(endpoint);
    } catch (const std::exception& error) {
        std::cerr << "Client failed: " << error.what() << '\n';
        return 1;
    }
}
