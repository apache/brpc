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
#include <vector>
#include <gflags/gflags.h>
#include "butil/atomicops.h"
#include "butil/fast_rand.h"
#include "butil/logging.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "bthread/bthread.h"
#include "bvar/latency_recorder.h"
#include "bvar/variable.h"
#include "test.pb.h"

#ifdef BRPC_WITH_RDMA

DEFINE_int32(thread_num, 0, "How many threads are used");
DEFINE_int32(queue_depth, 1, "How many requests can be pending in the queue");
DEFINE_int32(expected_qps, 0, "The expected QPS");
DEFINE_int32(max_thread_num, 16, "The max number of threads are used");
DEFINE_int32(attachment_size, -1, "Attachment size is used (in Bytes)");
DEFINE_bool(echo_attachment, false, "Select whether attachment should be echo");
DEFINE_string(connection_type, "single", "Connection type of the channel");
DEFINE_string(protocol, "baidu_std", "Protocol type.");
DEFINE_string(servers, "0.0.0.0:8002+0.0.0.0:8002", "IP Address of servers");
DEFINE_bool(use_rdma, true, "Use RDMA or not");
DEFINE_int32(rpc_timeout_ms, 2000, "RPC call timeout");
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
volatile bool g_stop = false;

butil::atomic<int64_t> g_token(10000);

static void* GenerateToken(void* arg) {
    int64_t start_time = butil::monotonic_time_ns();
    int64_t accumulative_token = g_token.load(butil::memory_order_relaxed);
    while (!g_stop) {
        bthread_usleep(100000);
        int64_t now = butil::monotonic_time_ns();
        if (accumulative_token * 1000000000 / (now - start_time) < FLAGS_expected_qps) {
            int64_t delta = FLAGS_expected_qps * (now - start_time) / 1000000000 - accumulative_token;
            g_token.fetch_add(delta, butil::memory_order_relaxed);
            accumulative_token += delta;
        }
    }
    return NULL;
}

class PerformanceTest {
public:
    PerformanceTest(int attachment_size, bool echo_attachment)
        : _addr(NULL)
        , _channel(NULL)
        , _start_time(0)
        , _iterations(0)
        , _stop(false)
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

    inline bool IsStop() { return _stop; }

    int Init() {
        brpc::ChannelOptions options;
        options.use_rdma = FLAGS_use_rdma;
        options.protocol = FLAGS_protocol;
        options.connection_type = FLAGS_connection_type;
        options.timeout_ms = FLAGS_rpc_timeout_ms;
        options.max_retry = 0;
        std::string server = g_servers[(rr_index++) % g_servers.size()];
        _channel = new brpc::Channel();
        if (_channel->Init(server.c_str(), &options) != 0) {
            LOG(ERROR) << "Fail to initialize channel";
            return -1;
        }
        brpc::Controller cntl;
        test::PerfTestResponse response;
        test::PerfTestRequest request;
        request.set_echo_attachment(_echo_attachment);
        test::PerfTestService_Stub stub(_channel);
        stub.Test(&cntl, &request, &response, NULL);
        if (cntl.Failed()) {
            LOG(ERROR) << "RPC call failed: " << cntl.ErrorText();
            return -1;
        }
        return 0;
    }

    struct RespClosure {
        brpc::Controller* cntl;
        test::PerfTestResponse* resp;
        PerformanceTest* test;
    };

    void SendRequest() {
        if (FLAGS_expected_qps > 0) {
            while (g_token.load(butil::memory_order_relaxed) <= 0) {
                bthread_usleep(10);
            }
            g_token.fetch_sub(1, butil::memory_order_relaxed);
        }
        RespClosure* closure = new RespClosure;
        test::PerfTestRequest request;
        closure->resp = new test::PerfTestResponse();
        closure->cntl = new brpc::Controller();
        request.set_echo_attachment(_echo_attachment);
        closure->cntl->request_attachment().append(_attachment);
        closure->test = this;
        google::protobuf::Closure* done = brpc::NewCallback(&HandleResponse, closure);
        test::PerfTestService_Stub stub(_channel);
        stub.Test(closure->cntl, &request, closure->resp, done);
    }

    static void HandleResponse(RespClosure* closure) {
        std::unique_ptr<brpc::Controller> cntl_guard(closure->cntl);
        std::unique_ptr<test::PerfTestResponse> response_guard(closure->resp);
        if (closure->cntl->Failed()) {
            LOG(ERROR) << "RPC call failed: " << closure->cntl->ErrorText();
            closure->test->_stop = true;
            return;
        }

        g_latency_recorder << closure->cntl->latency_us();
        if (closure->resp->cpu_usage().size() > 0) {
            g_server_cpu_recorder << atof(closure->resp->cpu_usage().c_str()) * 100;
        }
        g_total_bytes.fetch_add(closure->cntl->request_attachment().size(), butil::memory_order_relaxed);
        g_total_cnt.fetch_add(1, butil::memory_order_relaxed);

        cntl_guard.reset(NULL);
        response_guard.reset(NULL);

        if (closure->test->_iterations == 0 && FLAGS_test_iterations > 0) {
            closure->test->_stop = true;
            return;
        }
        --closure->test->_iterations;
        uint64_t last = g_last_time.load(butil::memory_order_relaxed);
        uint64_t now = butil::gettimeofday_us();
        if (now > last && now - last > 100000) {
            if (g_last_time.exchange(now, butil::memory_order_relaxed) == last) {
                g_client_cpu_recorder << 
                    atof(bvar::Variable::describe_exposed("process_cpu_usage").c_str()) * 100;
            }
        }
        if (now - closure->test->_start_time > FLAGS_test_seconds * 1000000u) {
            closure->test->_stop = true;
            return;
        }
        closure->test->SendRequest();
    }

    static void* RunTest(void* arg) {
        PerformanceTest* test = (PerformanceTest*)arg;
        test->_start_time = butil::gettimeofday_us();
        test->_iterations = FLAGS_test_iterations;
        
        for (int i = 0; i < FLAGS_queue_depth; ++i) {
            test->SendRequest();
        }

        return NULL;
    }

private:
    void* _addr;
    brpc::Channel* _channel;
    uint64_t _start_time;
    uint32_t _iterations;
    volatile bool _stop;
    butil::IOBuf _attachment;
    bool _echo_attachment;
};

static void* DeleteTest(void* arg) {
    PerformanceTest* test = (PerformanceTest*)arg;
    delete test;
    return NULL;
}

void Test(int thread_num, int attachment_size) {
    std::cout << "[Threads: " << thread_num
        << ", Depth: " << FLAGS_queue_depth
        << ", Attachment: " << attachment_size << "B"
        << ", RDMA: " << (FLAGS_use_rdma ? "yes" : "no")
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
    bthread_t tid[thread_num];
    if (FLAGS_expected_qps > 0) {
        bthread_t tid;
        bthread_start_background(&tid, &BTHREAD_ATTR_NORMAL, GenerateToken, NULL);
    }
    for (int k = 0; k < thread_num; ++k) {
        bthread_start_background(&tid[k], &BTHREAD_ATTR_NORMAL,
                PerformanceTest::RunTest, tests[k]);
    }
    for (int k = 0; k < thread_num; ++k) {
        while (!tests[k]->IsStop()) {
            bthread_usleep(10000);
        }
    }
    uint64_t end_time = butil::gettimeofday_us();
    double throughput = g_total_bytes / 1.048576 / (end_time - start_time);
    if (FLAGS_test_iterations == 0) {
        std::cout << "Avg-Latency: " << g_latency_recorder.latency(10)
            << ", 90th-Latency: " << g_latency_recorder.latency_percentile(0.9)
            << ", 99th-Latency: " << g_latency_recorder.latency_percentile(0.99)
            << ", 99.9th-Latency: " << g_latency_recorder.latency_percentile(0.999)
            << ", Throughput: " << throughput << "MB/s"
            << ", QPS: " << (g_total_cnt.load(butil::memory_order_relaxed) * 1000 / (end_time - start_time)) << "k"
            << ", Server CPU-utilization: " << g_server_cpu_recorder.latency(10) << "\%"
            << ", Client CPU-utilization: " << g_client_cpu_recorder.latency(10) << "\%"
            << std::endl;
    } else {
        std::cout << " Throughput: " << throughput << "MB/s" << std::endl;
    }
    g_stop = true;
    for (int k = 0; k < thread_num; ++k) {
        bthread_start_background(&tid[k], &BTHREAD_ATTR_NORMAL, DeleteTest, tests[k]);
    }
}

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    // Initialize RDMA environment in advance.
    if (FLAGS_use_rdma) {
        brpc::rdma::GlobalRdmaInitializeOrDie();
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

#else

int main(int argc, char* argv[]) {
    LOG(ERROR) << " brpc is not compiled with rdma. To enable it, please refer to https://github.com/apache/brpc/blob/master/docs/en/rdma.md";
    return 0;
}

#endif
