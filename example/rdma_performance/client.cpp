// Copyright (c) 2014 baidu-rpc authors.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A performance test.

#include <stdlib.h>
#include <unistd.h>
#include <vector>
#include <butil/atomicops.h>
#include <butil/fast_rand.h>
#include <butil/logging.h>
#include <brpc/channel.h>
#include <brpc/rdma/rdma_helper.h>
#include <bthread/bthread.h>
#include <bvar/latency_recorder.h>
#include <gflags/gflags.h>
#include "test.pb.h"

DEFINE_int32(thread_num, 0, "How many threads are used");
DEFINE_int32(max_thread_num, 16, "The max number of threads are used");
DEFINE_int32(attachment_size, -1, "Attachment size is used (in KB)");
DEFINE_bool(specify_attachment_addr, false, "Specify the address of attachment");
DEFINE_bool(echo_attachment, false, "Select whether attachment should be echo");
DEFINE_string(protocol, "baidu_std", "Protocol type.");
DEFINE_string(servers, "0.0.0.0:8002", "IP Address of server");
DEFINE_bool(use_rdma, true, "Use RDMA or not");
DEFINE_int32(rpc_timeout_ms, -1, "RPC call timeout");
DEFINE_int32(test_seconds, 20, "Test running time");
DEFINE_int32(test_iterations, 0, "Test iterations");

bvar::LatencyRecorder g_latency_recorder("client");
bvar::LatencyRecorder g_cpu_recorder("server_cpu");
butil::atomic<uint64_t> g_total_bytes;
std::vector<std::string> g_servers;

class PerformanceTest {
public:
    PerformanceTest(int attachment_size, bool echo_attachment)
        : _addr(NULL) {
        if (attachment_size == 0) {
            attachment_size = 1;
        } else {
            attachment_size *= 1024;
        }

        _addr = malloc(attachment_size);
        butil::fast_rand_bytes(_addr, attachment_size);
        if (FLAGS_specify_attachment_addr) {
            brpc::rdma::RegisterMemoryForRdma(_addr, attachment_size);
            _attachment.append_zerocopy(_addr, attachment_size, NULL);
        } else {
            _attachment.append(_addr, attachment_size);
        }
        _echo_attachment = echo_attachment;
    }

    ~PerformanceTest() {
        if (_addr) {
            if (FLAGS_specify_attachment_addr) {
                brpc::rdma::DeregisterMemoryForRdma(_addr);
            }
            free(_addr);
        }
    }

    int Init() {
        brpc::ChannelOptions options;
        options.use_rdma = FLAGS_use_rdma;
        options.protocol = FLAGS_protocol;
        options.connection_type = "pooled";
        options.timeout_ms = FLAGS_rpc_timeout_ms;
        options.max_retry = 0;
        std::string server = g_servers[butil::fast_rand() % g_servers.size()];
        if (_channel.Init(server.c_str(), &options) != 0) {
            LOG(ERROR) << "Fail to initialize channel";
            return -1;
        }
        test::PerfTestService_Stub stub(&_channel);
        brpc::Controller cntl;
        test::PerfTestResponse response;
        test::PerfTestRequest request;
        request.set_echo_attachment(_echo_attachment);
        cntl.request_attachment().append(_attachment);
        stub.Test(&cntl, &request, &response, NULL);
        if (cntl.Failed()) {
            LOG(ERROR) << "RPC call failed: " << cntl.ErrorText();
            return -1;
        }
        return 0;
    }

    static void* RunTest(void* arg) {
        PerformanceTest* test = (PerformanceTest*)arg;
        test::PerfTestService_Stub stub(&test->_channel);
        uint64_t start_time = butil::gettimeofday_us();
        uint32_t iterations = FLAGS_test_iterations;
        while (true) {
            brpc::Controller cntl;
            test::PerfTestResponse response;
            test::PerfTestRequest request;

            request.set_echo_attachment(test->_echo_attachment);
            cntl.request_attachment().append(test->_attachment);
            stub.Test(&cntl, &request, &response, NULL);

            if (cntl.Failed()) {
                LOG(ERROR) << "RPC call failed: " << cntl.ErrorText();
                break;
            }

            if (FLAGS_echo_attachment) {
                CHECK(test->_attachment == cntl.response_attachment());
            }

            g_latency_recorder << cntl.latency_us();
            if (response.cpu_usage().size() > 0) {
                g_cpu_recorder << atof(response.cpu_usage().c_str()) * 100;
            }
            g_total_bytes.fetch_add(test->_attachment.size(),
                                    butil::memory_order_relaxed);
            if (iterations == 0 && FLAGS_test_iterations > 0) {
                break;
            }
            --iterations;
            if (butil::gettimeofday_us() - start_time > FLAGS_test_seconds * 1000000u) {
                break;
            }
        }

        return NULL;
    }

private:
    void* _addr;
    butil::IOBuf _attachment;
    bool _echo_attachment;
    brpc::Channel _channel;
};

void Test(int thread_num, int attachment_size) {
    std::cout << "[Threads: " << thread_num
        << ", Attachment: " << attachment_size << "KB"
        << ", Echo: " << (FLAGS_echo_attachment ? "yes]" : "no]") << std::flush;
    g_total_bytes.store(0, butil::memory_order_relaxed);
    std::vector<PerformanceTest*> tests;
    for (int k = 0; k < thread_num; ++k) {
        PerformanceTest* t = new PerformanceTest(attachment_size, FLAGS_echo_attachment);
        if (t->Init() < 0) {
            exit(1);
        }
        tests.push_back(t);
    }
    uint64_t start_time = butil::gettimeofday_us();
    bthread_t tid[thread_num];
    for (int k = 0; k < thread_num; ++k) {
        bthread_start_background(&tid[k], &BTHREAD_ATTR_NORMAL,
                PerformanceTest::RunTest, tests[k]);
    }
    for (int k = 0; k < thread_num; ++k) {
        bthread_join(tid[k], NULL);
    }
    uint64_t end_time = butil::gettimeofday_us();
    double throughput = g_total_bytes / 1.048576 / (end_time - start_time);
    if (FLAGS_test_iterations == 0) {
        std::cout << " Avg-Latency: " << g_latency_recorder.latency(10)
            << ", 90th-Latency: " << g_latency_recorder.latency_percentile(0.9)
            << ", 99th-Latency: " << g_latency_recorder.latency_percentile(0.99)
            << ", 99.9th-Latency: " << g_latency_recorder.latency_percentile(0.999)
            << ", Throughput: " << throughput << "MB/s"
            << ", CPU-utilization: " << g_cpu_recorder.latency(10) << "\%" << std::endl;
    } else {
        std::cout << " Throughput: " << throughput << "MB/s" << std::endl;
    }
    for (int k = 0; k < thread_num; ++k) {
        delete tests[k];
    }
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    // Initialize RDMA environment in advance.
    if (FLAGS_use_rdma) {
        brpc::rdma::GlobalRdmaInitializeOrDie();
    }

    bthread_setconcurrency(sysconf(_SC_NPROCESSORS_ONLN));

    std::string::size_type pos1 = 0;
    std::string::size_type pos2 = FLAGS_servers.find('+');
    while (pos2 != std::string::npos) {
        g_servers.push_back(FLAGS_servers.substr(pos1, pos2 - pos1));
        pos1 = pos2 + 1;
        pos2 = FLAGS_servers.find('+', pos1);
    }
    g_servers.push_back(FLAGS_servers.substr(pos1));

    if (FLAGS_thread_num > 0 && FLAGS_attachment_size >= 0) {
        Test(FLAGS_thread_num, FLAGS_attachment_size);
    } else if (FLAGS_thread_num <= 0 && FLAGS_attachment_size >= 0) {
        for (int i = 1; i <= FLAGS_max_thread_num; i *= 2) {
            Test(i, FLAGS_attachment_size);
        }
    } else if (FLAGS_thread_num > 0 && FLAGS_attachment_size < 0) {
        for (int i = 1; i <= 1024; i *= 4) {
            Test(FLAGS_thread_num, i);
        }
    } else {
        for (int j = 1; j <= 1024; j *= 4) {
            for (int i = 1; i <= FLAGS_max_thread_num; i *= 2) {
                Test(i, j);
            }
        }
    }

    return 0;
}

