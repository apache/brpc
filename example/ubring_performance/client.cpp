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

#include <stdlib.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>
#include <gflags/gflags.h>
#include "butil/atomicops.h"
#include "butil/fast_rand.h"
#include "butil/logging.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "bthread/bthread.h"
#include "bvar/latency_recorder.h"
#include "bvar/variable.h"
#include "test.pb.h"

#ifdef BRPC_WITH_UBRING

DEFINE_int32(thread_num, 0, "How many threads are used");
DEFINE_int32(queue_depth, 1, "How many requests can be pending in the queue");
DEFINE_int32(expected_qps, 0, "The expected QPS");
DEFINE_int32(max_thread_num, 16, "The max number of threads are used");
DEFINE_int32(attachment_size, -1, "Attachment size is used (in Bytes)");
DEFINE_bool(echo_attachment, false, "Select whether attachment should be echo");
DEFINE_string(connection_type, "single", "Connection type of the channel");
DEFINE_string(protocol, "baidu_std", "Protocol type.");
DEFINE_string(servers, "0.0.0.0:8002+0.0.0.0:8002", "IP Address of servers");
DEFINE_bool(use_ubring, false, "Use UBRING or not");
DEFINE_int32(rpc_timeout_ms, 5000, "RPC call timeout");
DEFINE_int32(test_seconds, 20, "Test running time");
DEFINE_int32(test_iterations, 0, "Test iterations");
DEFINE_int32(dummy_port, 8001, "Dummy server port number");

bvar::LatencyRecorder g_latency_recorder("client");
bvar::LatencyRecorder g_server_cpu_recorder("server_cpu");
bvar::LatencyRecorder g_client_cpu_recorder("client_cpu");
butil::atomic<uint64_t> g_last_time(0);
butil::atomic<uint64_t> g_total_bytes;
butil::atomic<uint64_t> g_total_cnt;
std::vector<std::string> g_servers;
int rr_index = 0;
std::atomic<bool> g_failed{false};
std::atomic<int64_t> g_next_request_ns{0};

static bool WaitForRequest(int64_t deadline) {
    int64_t next = butil::monotonic_time_ns();
    if (FLAGS_expected_qps > 0) {
        const int64_t interval = 1000000000L / FLAGS_expected_qps;
        int64_t previous = g_next_request_ns.load();
        do {
            next = std::max(previous, butil::monotonic_time_ns());
        } while (!g_next_request_ns.compare_exchange_weak(
            previous, next + interval));
    }
    while (!g_failed.load()) {
        const int64_t now = butil::monotonic_time_ns();
        if (now >= deadline) {
            return false;
        }
        if (now >= next) {
            return true;
        }
        bthread_usleep(std::min<int64_t>(
            (std::min(next, deadline) - now) / 1000 + 1, 10000));
    }
    return false;
}

class PerformanceTest {
public:
    PerformanceTest(int attachment_size, bool echo_attachment)
        : _addr(nullptr)
        , _channel(nullptr)
    {
        if (attachment_size > 0) {
            _addr = malloc(attachment_size);
            butil::fast_rand_bytes(_addr, attachment_size);
            _attachment.append(_addr, attachment_size);
        }
        _echo_attachment = echo_attachment;
    }

    ~PerformanceTest() {
        if (_addr) {
            free(_addr);
        }
        delete _channel;
    }

    int Init() {
        brpc::ChannelOptions options;
        options.socket_mode = FLAGS_use_ubring? brpc::SOCKET_MODE_UBRING : brpc::SOCKET_MODE_TCP;
        options.protocol = FLAGS_protocol;
        options.connection_type = FLAGS_connection_type;
        options.timeout_ms = FLAGS_rpc_timeout_ms;
        options.max_retry = 0;
        // TODO A bug exists when the connection_group parameter is used.
        // options.connection_group = std::to_string(reinterpret_cast<uintptr_t>(this));
        std::string server = g_servers[(rr_index++) % g_servers.size()];
        _channel = new brpc::Channel();
        if (_channel->Init(server.c_str(), &options) != 0) {
            LOG(ERROR) << "Fail to initialize channel";
            return -1;
        }
        
        // Warm up the connection before measuring requests.
        int retry = 3;
        while (retry > 0) {
            brpc::Controller cntl;
            test::PerfTestResponse response;
            test::PerfTestRequest request;
            request.set_echo_attachment(_echo_attachment);
            test::PerfTestService_Stub stub(_channel);
            stub.Test(&cntl, &request, &response, nullptr);
            if (!cntl.Failed()) {
                return 0;
            }
            LOG(WARNING) << "RPC call failed, retrying... (" << retry << " left): " << cntl.ErrorText();
            retry--;
            bthread_usleep(1000000);
        }
        LOG(ERROR) << "RPC call failed after multiple retries";
        return -1;
    }

    static void* RunTest(void* arg) {
        auto* self = static_cast<PerformanceTest*>(arg);
        const int64_t deadline = butil::monotonic_time_ns() +
            static_cast<int64_t>(FLAGS_test_seconds) * 1000000000L;
        uint64_t sent = 0;
        test::PerfTestService_Stub stub(self->_channel);
        while (!g_failed.load() &&
               (FLAGS_test_iterations == 0 ||
                sent < static_cast<uint64_t>(FLAGS_test_iterations))) {
            std::vector<brpc::Controller> cntls(FLAGS_queue_depth);
            std::vector<test::PerfTestRequest> requests(FLAGS_queue_depth);
            std::vector<test::PerfTestResponse> responses(FLAGS_queue_depth);
            int pending = 0;
            for (; pending < FLAGS_queue_depth; ++pending) {
                if ((FLAGS_test_iterations > 0 &&
                     sent >= static_cast<uint64_t>(FLAGS_test_iterations)) ||
                    !WaitForRequest(deadline)) {
                    break;
                }
                requests[pending].set_echo_attachment(self->_echo_attachment);
                cntls[pending].request_attachment().append(self->_attachment);
                stub.Test(&cntls[pending], &requests[pending],
                          &responses[pending], brpc::DoNothing());
                ++sent;
            }
            for (int i = 0; i < pending; ++i) {
                brpc::Join(cntls[i].call_id());
                if (cntls[i].Failed()) {
                    LOG(ERROR) << "RPC call failed: " << cntls[i].ErrorText();
                    g_failed.store(true);
                } else if (self->_echo_attachment &&
                           cntls[i].response_attachment() != self->_attachment) {
                    LOG(ERROR) << "Echo attachment does not match request";
                    g_failed.store(true);
                } else {
                    g_latency_recorder << cntls[i].latency_us();
                    g_total_bytes.fetch_add(self->_attachment.size(),
                                            butil::memory_order_relaxed);
                    g_total_cnt.fetch_add(1, butil::memory_order_relaxed);
                    if (!responses[i].cpu_usage().empty()) {
                        g_server_cpu_recorder <<
                            atof(responses[i].cpu_usage().c_str()) * 100;
                    }
                }
            }
            if (pending == 0) {
                break;
            }
            uint64_t last = g_last_time.load(butil::memory_order_relaxed);
            const uint64_t now = butil::gettimeofday_us();
            if (now > last && now - last > 100000 &&
                g_last_time.exchange(now, butil::memory_order_relaxed) == last) {
                g_client_cpu_recorder << atof(bvar::Variable::describe_exposed(
                    "process_cpu_usage").c_str()) * 100;
            }
        }
        return nullptr;
    }

private:
    void* _addr;
    brpc::Channel* _channel;
    butil::IOBuf _attachment;
    bool _echo_attachment;
};

bool Test(int thread_num, int attachment_size) {
    std::cout << "[Threads: " << thread_num
        << ", Depth: " << FLAGS_queue_depth
        << ", Attachment: " << attachment_size << "B"
        << ", UBRING: " << (FLAGS_use_ubring ? "yes" : "no")
        << ", Echo: " << (FLAGS_echo_attachment ? "yes]" : "no]")
        << std::endl;
    g_total_bytes.store(0, butil::memory_order_relaxed);
    g_total_cnt.store(0, butil::memory_order_relaxed);
    std::vector<PerformanceTest*> tests;
    for (int k = 0; k < thread_num; ++k) {
        PerformanceTest* t = new PerformanceTest(attachment_size, FLAGS_echo_attachment);
        if (t->Init() < 0) {
            exit(1);
        }
        tests.push_back(t);
    }
    uint64_t start_time = butil::gettimeofday_us();
    g_next_request_ns.store(butil::monotonic_time_ns());
    std::vector<bthread_t> tid(thread_num);
    for (int k = 0; k < thread_num; ++k) {
        CHECK_EQ(0, bthread_start_background(
            &tid[k], &BTHREAD_ATTR_NORMAL, PerformanceTest::RunTest, tests[k]));
    }
    for (int k = 0; k < thread_num; ++k) {
        bthread_join(tid[k], nullptr);
    }
    uint64_t end_time = butil::gettimeofday_us();
    double throughput = g_total_bytes / 1.048576 / (end_time - start_time);
    if (FLAGS_test_iterations == 0) {
        std::cout << "Avg-Latency: " << g_latency_recorder.latency(10)
            << ", 90th-Latency: " << g_latency_recorder.latency_percentile(0.9)
            << ", 99th-Latency: " << g_latency_recorder.latency_percentile(0.99)
            << ", 99.9th-Latency: " << g_latency_recorder.latency_percentile(0.999)
            << ", Throughput: " << throughput << "MB/s"
            << ", QPS: " << (g_total_cnt.load(butil::memory_order_relaxed) *
                              1000000.0 / (end_time - start_time))
            << ", Server CPU-utilization: " << g_server_cpu_recorder.latency(10) << "%"
            << ", Client CPU-utilization: " << g_client_cpu_recorder.latency(10) << "%"
            << std::endl;
    } else {
        std::cout << " Throughput: " << throughput << "MB/s" << std::endl;
    }
    for (auto* test : tests) {
        delete test;
    }
    return !g_failed.load();
}

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_queue_depth <= 0 || FLAGS_test_seconds <= 0 ||
        FLAGS_expected_qps < 0 || FLAGS_test_iterations < 0) {
        LOG(ERROR) << "queue_depth and test_seconds must be positive; "
                      "expected_qps and test_iterations must be nonnegative";
        return 1;
    }
    brpc::StartDummyServerAt(FLAGS_dummy_port);

    std::string::size_type pos1 = 0;
    std::string::size_type pos2 = FLAGS_servers.find('+');
    while (pos2 != std::string::npos) {
        g_servers.push_back(FLAGS_servers.substr(pos1, pos2 - pos1));
        pos1 = pos2 + 1;
        pos2 = FLAGS_servers.find('+', pos1);
    }
    g_servers.push_back(FLAGS_servers.substr(pos1));

    if (FLAGS_thread_num > 0 && FLAGS_attachment_size >= 0) {
        if (!Test(FLAGS_thread_num, FLAGS_attachment_size)) {
            return 1;
        }
    } else if (FLAGS_thread_num <= 0 && FLAGS_attachment_size >= 0) {
        for (int i = 1; i <= FLAGS_max_thread_num; i *= 2) {
            if (!Test(i, FLAGS_attachment_size)) {
                return 1;
            }
        }
    } else if (FLAGS_thread_num > 0 && FLAGS_attachment_size < 0) {
        for (int i = 1; i <= 1024; i *= 4) {
            if (!Test(FLAGS_thread_num, i)) {
                return 1;
            }
        }
    } else {
        for (int j = 1; j <= 1024; j *= 4) {
            for (int i = 1; i <= FLAGS_max_thread_num; i *= 2) {
                if (!Test(i, j)) {
                    return 1;
                }
            }
        }
    }

    return 0;
}

#else

int main(int argc, char* argv[]) {
    LOG(ERROR) << " brpc is not compiled with ubring. To enable it, please refer to the ubring documentation";
    return 1;
}

#endif
