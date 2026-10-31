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
#include <sys/resource.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "butil/config.h"
#include "butil/endpoint.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "bthread/bthread.h"
#include "echo.brpc.fb.h"
#include "echo_pb.pb.h"

#if !BRPC_WITH_FLATBUFFERS
#error "benchmark_fb requires a FlatBuffers-enabled brpc library"
#endif

DEFINE_string(server, "", "IPv4 endpoint printed by benchmark_fb_server; "
              "a non-loopback address also needs --allow_remote");
DEFINE_bool(allow_remote, false, "Allow a non-loopback --server for cross-host benchmarks");
DEFINE_string(protocol, "fb_rpc", "fb_rpc, or baidu_std for the protobuf control service");
DEFINE_string(connection_type, "single", "single, pooled, or short");
DEFINE_int32(request_count, 16, "Count mode: total requests across all threads, 1..1000");
DEFINE_int32(duration_s, 0, "0 selects count mode; 1..3600 runs a timed benchmark");
DEFINE_int32(warmup_s, 0, "Timed mode: unmeasured warm-up seconds, 0..600");
DEFINE_int32(thread_num, 2, "Concurrent synchronous callers: 1..16, or 1..1024 when timed");
DEFINE_int32(request_size, 64, "Bytes in the optional request string, 0..1048576");
DEFINE_int32(attachment_size, 0, "Bytes in the echoed attachment, 0..1048576");
DEFINE_int32(timeout_ms, 1500, "Per-RPC timeout, 1..5000 milliseconds");
DEFINE_int32(deadline_ms, 30000, "Count mode: overall client deadline, 1..120000 milliseconds");
DEFINE_bool(omit_message, false, "Omit the optional string rather than sending an empty string");
DEFINE_bool(use_bthread, false, "Run callers as bthreads instead of pthreads");
DEFINE_bool(corrupt_request, false, "fb_rpc only: send an invalid root offset and "
            "require schema rejection");

namespace {
using brpc::flatbuffers::Message;
using brpc::flatbuffers::MessageBuilder;
using Clock = std::chrono::steady_clock;

// Result of one RPC: kSuccess, kRejected (expected schema rejection) or kFailed.
enum CallResult { kSuccess, kRejected, kFailed };
using CallFn = std::function<CallResult(uint64_t, int64_t, std::string*)>;

std::string MakeBytes(size_t size, unsigned multiplier) {
    std::string bytes(size, '\0');
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<char>((i * multiplier) % 251);
    }
    return bytes;
}

Message MakeRequest(uint64_t request_id, const std::string& text) {
    MessageBuilder builder(text.size() + 256);
    const auto message = FLAGS_omit_message ?
        ::flatbuffers::Offset<::flatbuffers::String>() :
        builder.CreateString(text.data(), text.size());
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
    return output->message() != nullptr && output->message()->size() == text.size() &&
        memcmp(output->message()->data(), text.data(), text.size()) == 0;
}

bool CheckPbResponse(const benchmark_fb_pb::Response& response, uint64_t request_id,
                     const std::string& text, const std::string& attachment,
                     const brpc::Controller& cntl) {
    if (response.request_id() != request_id ||
        response.attachment_size() != attachment.size() ||
        cntl.response_attachment().to_string() != attachment) {
        return false;
    }
    if (FLAGS_omit_message) {
        return !response.has_message();
    }
    return response.has_message() && response.message() == text;
}

bool IsLoopback(const std::string& server) {
    return server.compare(0, 10, "127.0.0.1:") == 0;
}

bool ValidFlags(butil::EndPoint* endpoint) {
    const bool timed = FLAGS_duration_s > 0;
    return (IsLoopback(FLAGS_server) || FLAGS_allow_remote) &&
        butil::str2endpoint(FLAGS_server.c_str(), endpoint) == 0 && endpoint->port > 0 &&
        (FLAGS_protocol == "fb_rpc" || FLAGS_protocol == "baidu_std") &&
        !(FLAGS_corrupt_request && (FLAGS_protocol != "fb_rpc" || timed)) &&
        (FLAGS_connection_type == "single" || FLAGS_connection_type == "pooled" ||
         FLAGS_connection_type == "short") &&
        FLAGS_request_count >= 1 && FLAGS_request_count <= 1000 &&
        FLAGS_duration_s >= 0 && FLAGS_duration_s <= 3600 &&
        FLAGS_warmup_s >= 0 && FLAGS_warmup_s <= 600 && (timed || FLAGS_warmup_s == 0) &&
        FLAGS_thread_num >= 1 && FLAGS_thread_num <= (timed ? 1024 : 16) &&
        FLAGS_request_size >= 0 && FLAGS_request_size <= 1024 * 1024 &&
        FLAGS_attachment_size >= 0 && FLAGS_attachment_size <= 1024 * 1024 &&
        FLAGS_timeout_ms >= 1 && FLAGS_timeout_ms <= 5000 &&
        FLAGS_deadline_ms >= 1 && FLAGS_deadline_ms <= 120000;
}

struct Shared {
    std::atomic<bool> failed{false};
    std::mutex error_mutex;
    void Fail(const std::string& message) {
        if (!failed.exchange(true)) {
            std::lock_guard<std::mutex> lock(error_mutex);
            std::cerr << message << '\n';
        }
    }
};

// Builds one synchronous RPC for the selected protocol. Both protocols carry
// the same request string and attachment and verify every echoed byte.
CallFn MakeCall(brpc::Channel* channel, const std::string& text,
                const std::string& attachment) {
    if (FLAGS_protocol == "baidu_std") {
        auto stub = std::make_shared<benchmark_fb_pb::BenchmarkService_Stub>(channel);
        return [stub, &text, &attachment](uint64_t request_id, int64_t timeout_ms,
                                          std::string* error) {
            benchmark_fb_pb::Request request;
            request.set_request_id(request_id);
            if (!FLAGS_omit_message) {
                request.set_message(text);
            }
            benchmark_fb_pb::Response response;
            brpc::Controller cntl;
            cntl.set_timeout_ms(timeout_ms);
            cntl.request_attachment().append(attachment);
            stub->Echo(&cntl, &request, &response, nullptr);
            if (cntl.Failed()) {
                *error = "RPC failed: " + cntl.ErrorText();
                return kFailed;
            }
            if (!CheckPbResponse(response, request_id, text, attachment, cntl)) {
                *error = "Response fields, optional string or attachment mismatch";
                return kFailed;
            }
            return kSuccess;
        };
    }
    // Generated stub methods route through Channel::FBCallMethod.
    auto stub = std::make_shared<benchmark_fb::BenchmarkService::Stub>(channel);
    return [stub, &text, &attachment](uint64_t request_id, int64_t timeout_ms,
                                      std::string* error) {
        const Message request = MakeRequest(request_id, text);
        Message response;
        brpc::Controller cntl;
        cntl.set_timeout_ms(timeout_ms);
        cntl.request_attachment().append(attachment);
        stub->Echo(&cntl, &request, &response, nullptr);
        if (FLAGS_corrupt_request) {
            // The generated generic RpcController::SetFailed(string)
            // maps to -1. Timeouts and connection errors do not count.
            if (!cntl.Failed() || cntl.ErrorCode() != -1 ||
                response.size() != 0 || !cntl.response_attachment().empty()) {
                *error = "Expected server schema rejection; error=" + cntl.ErrorText();
                return kFailed;
            }
            return kRejected;
        }
        if (cntl.Failed()) {
            *error = "RPC failed: " + cntl.ErrorText();
            return kFailed;
        }
        if (!CheckResponse(response, request_id, text, attachment, cntl)) {
            *error = "Response schema, fields, optional string or attachment mismatch";
            return kFailed;
        }
        return kSuccess;
    };
}

struct BthreadArg {
    Shared* shared;
    const std::function<void(int)>* body;
    int index;
};

void* BthreadEntry(void* raw) {
    std::unique_ptr<BthreadArg> arg(static_cast<BthreadArg*>(raw));
    try {
        (*arg->body)(arg->index);
    } catch (const std::exception& error) {
        arg->shared->Fail(std::string("Client worker failed: ") + error.what());
    }
    return nullptr;
}

void RunWorkers(Shared* shared, const std::function<void(int)>& body) {
    if (FLAGS_use_bthread) {
        std::vector<bthread_t> ids(FLAGS_thread_num, INVALID_BTHREAD);
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            auto* arg = new BthreadArg{shared, &body, i};
            if (bthread_start_background(&ids[i], nullptr, BthreadEntry, arg) != 0) {
                delete arg;
                ids[i] = INVALID_BTHREAD;
                shared->Fail("Cannot start bthread worker");
            }
        }
        for (bthread_t id : ids) {
            if (id != INVALID_BTHREAD) {
                bthread_join(id, nullptr);
            }
        }
        return;
    }
    std::vector<std::thread> workers;
    workers.reserve(FLAGS_thread_num);
    try {
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            workers.emplace_back([shared, &body, i] {
                try {
                    body(i);
                } catch (const std::exception& error) {
                    shared->Fail(std::string("Client worker failed: ") + error.what());
                }
            });
        }
    } catch (const std::exception& error) {
        shared->Fail(std::string("Cannot start client worker: ") + error.what());
    }
    for (auto& thread : workers) {
        thread.join();
    }
}

// Bounded functional mode used by smoke.py.
int RunCount(const CallFn& call) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(FLAGS_deadline_ms);
    std::atomic<int> next{0};
    std::atomic<int> successes{0};
    std::atomic<int> expected_rejections{0};
    Shared shared;
    RunWorkers(&shared, [&](int) {
        while (!shared.failed.load()) {
            const int index = next.fetch_add(1);
            if (index >= FLAGS_request_count) {
                break;
            }
            const int64_t remaining = std::chrono::duration_cast<
                std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (remaining <= 0) {
                shared.Fail("Overall client deadline exceeded");
                break;
            }
            std::string error;
            const CallResult result = call(static_cast<uint64_t>(index) + 1,
                std::min<int64_t>(FLAGS_timeout_ms, remaining), &error);
            if (result == kFailed) {
                shared.Fail(error);
                break;
            }
            ++(result == kRejected ? expected_rejections : successes);
        }
    });
    const int completed = successes.load() + expected_rejections.load();
    if (completed != FLAGS_request_count) {
        shared.Fail("Not all requested RPCs completed");
    }
    std::cout << "{\"completed\":" << completed
              << ",\"successes\":" << successes.load()
              << ",\"expected_rejections\":" << expected_rejections.load()
              << ",\"failures\":" << (shared.failed.load() ? 1 : 0) << "}\n";
    return shared.failed.load() ? 1 : 0;
}

double CpuSeconds() {
    rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
        (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
}

// Closed-loop benchmark: every caller keeps one RPC in flight for
// warmup_s + duration_s and records per-call latency in the measured window.
int RunTimed(const CallFn& call) {
    Shared shared;
    std::vector<std::vector<uint32_t>> latencies(FLAGS_thread_num);
    std::atomic<uint64_t> next_id{1};
    std::atomic<bool> measuring{false};
    std::atomic<bool> stop{false};
    std::atomic<int64_t> failures{0};
    std::thread timer([&] {
        std::this_thread::sleep_for(std::chrono::seconds(FLAGS_warmup_s));
        measuring = true;
        std::this_thread::sleep_for(std::chrono::seconds(FLAGS_duration_s));
        stop = true;
    });
    // Wait for the warm-up boundary on this thread to sample CPU and time.
    auto measure_begin = Clock::now();
    double cpu_begin = 0;
    std::thread sampler([&] {
        while (!measuring.load() && !stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        measure_begin = Clock::now();
        cpu_begin = CpuSeconds();
    });
    RunWorkers(&shared, [&](int index) {
        std::vector<uint32_t>& samples = latencies[index];
        samples.reserve(1 << 16);
        while (!stop.load(std::memory_order_relaxed)) {
            const bool measured = measuring.load(std::memory_order_relaxed);
            const auto begin = Clock::now();
            std::string error;
            const CallResult result = call(next_id.fetch_add(1, std::memory_order_relaxed),
                                           FLAGS_timeout_ms, &error);
            const auto end = Clock::now();
            if (result != kSuccess) {
                if (failures.fetch_add(1) == 0) {
                    shared.Fail(error);
                }
                continue;
            }
            if (measured && !stop.load(std::memory_order_relaxed)) {
                samples.push_back(static_cast<uint32_t>(std::min<int64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        end - begin).count(), UINT32_MAX)));
            }
        }
    });
    const auto measure_end = Clock::now();
    const double cpu_end = CpuSeconds();
    timer.join();
    sampler.join();
    std::vector<uint32_t> all;
    for (const auto& samples : latencies) {
        all.insert(all.end(), samples.begin(), samples.end());
    }
    std::sort(all.begin(), all.end());
    const double seconds = std::chrono::duration<double>(measure_end - measure_begin).count();
    auto percentile = [&all](double p) -> uint32_t {
        if (all.empty()) {
            return 0;
        }
        const size_t rank = static_cast<size_t>(p * (all.size() - 1) + 0.5);
        return all[std::min(rank, all.size() - 1)];
    };
    double sum = 0;
    for (uint32_t value : all) {
        sum += value;
    }
    const double qps = seconds > 0 ? all.size() / seconds : 0;
    const double payload = static_cast<double>(FLAGS_omit_message ? 0 : FLAGS_request_size) +
        FLAGS_attachment_size;
    std::cout << std::fixed << std::setprecision(1)
              << "{\"mode\":\"timed\",\"protocol\":\"" << FLAGS_protocol
              << "\",\"connection_type\":\"" << FLAGS_connection_type
              << "\",\"use_bthread\":" << (FLAGS_use_bthread ? "true" : "false")
              << ",\"thread_num\":" << FLAGS_thread_num
              << ",\"request_size\":" << FLAGS_request_size
              << ",\"attachment_size\":" << FLAGS_attachment_size
              << ",\"seconds\":" << seconds
              << ",\"requests\":" << all.size()
              << ",\"failures\":" << failures.load()
              << ",\"qps\":" << qps
              << ",\"payload_mib_per_s\":" << qps * payload / (1024.0 * 1024.0)
              << ",\"avg_us\":" << (all.empty() ? 0 : sum / all.size())
              << ",\"p50_us\":" << percentile(0.50)
              << ",\"p90_us\":" << percentile(0.90)
              << ",\"p99_us\":" << percentile(0.99)
              << ",\"p999_us\":" << percentile(0.999)
              << ",\"max_us\":" << (all.empty() ? 0 : all.back())
              << ",\"client_cpu_cores\":" << std::setprecision(3)
              << (seconds > 0 ? (cpu_end - cpu_begin) / seconds : 0)
              << "}\n";
    return failures.load() == 0 && !all.empty() ? 0 : 1;
}

int RunClient(const butil::EndPoint& endpoint) {
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = FLAGS_protocol;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms;
    options.connect_timeout_ms = std::min(FLAGS_timeout_ms, 1000);
    options.max_retry = 0;
    if (channel.Init(endpoint, &options) != 0) {
        std::cerr << "Cannot initialize " << FLAGS_protocol << " channel\n";
        return 1;
    }
    const std::string text = MakeBytes(FLAGS_omit_message ? 0 : FLAGS_request_size, 31);
    const std::string attachment = MakeBytes(FLAGS_attachment_size, 17);
    const CallFn call = MakeCall(&channel, text, attachment);
    return FLAGS_duration_s > 0 ? RunTimed(call) : RunCount(call);
}
}  // namespace

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    butil::EndPoint endpoint;
    if (!ValidFlags(&endpoint)) {
        std::cerr << "Invalid flags; --server=127.0.0.1:PORT is required unless "
                     "--allow_remote is set. See --help and README.md for bounded "
                     "parameter ranges.\n";
        return 2;
    }
    try {
        return RunClient(endpoint);
    } catch (const std::exception& error) {
        std::cerr << "Client failed: " << error.what() << '\n';
        return 1;
    }
}
