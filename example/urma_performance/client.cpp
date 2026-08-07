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

#include <cstdint>
#include <vector>

#include <gflags/gflags.h>

#include "butil/atomicops.h"
#include "butil/fast_rand.h"
#include "butil/logging.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "bthread/bthread.h"
#include "bvar/latency_recorder.h"
#include "bvar/variable.h"
#include "test.pb.h"

#if BRPC_WITH_URMA

DEFINE_string(server, "127.0.0.1:8003", "IP Port of urma performance server");
DEFINE_int32(thread_num, 0, "How many threads are used");
DEFINE_int32(queue_depth, 1, "How many requests can be pending in the queue");
DEFINE_int32(expected_qps, 0, "The expected QPS");
DEFINE_int32(max_thread_num, 16, "The max number of threads are used");
DEFINE_int32(attachment_size, -1, "Attachment size is used (in Bytes)");
DEFINE_int32(rpc_timeout_ms, 5000, "Timeout for each RPC in milliseconds");
DEFINE_bool(echo_attachment, false, "Select whether attachment should be echo");
DEFINE_bool(use_urma, true, "Use URMA transport (true) or TCP (false)");

bvar::LatencyRecorder g_latency("client");
bvar::Adder<int64_t> g_error_count("client_error_count");

static void* worker(void* arg) {
    test::PerfTestService_Stub* stub =
        static_cast<test::PerfTestService_Stub*>(arg);
    int qps = FLAGS_expected_qps;
    while (!brpc::IsAskedToQuit()) {
        butil::FastRandSeed seed;
        butil::init_fast_rand_seed(&seed);
        std::vector<brpc::Controller> cntls(FLAGS_queue_depth);
        std::vector<test::PerfTestRequest> reqs(FLAGS_queue_depth);
        std::vector<test::PerfTestResponse> resps(FLAGS_queue_depth);
        std::vector<brpc::CallId> ids(FLAGS_queue_depth);
        for (int i = 0; i < FLAGS_queue_depth; ++i) {
            cntls[i].set_log_id(butil::fast_rand(&seed) & 0x7fffffff);
            reqs[i].set_echo_attachment(FLAGS_echo_attachment);
            if (FLAGS_attachment_size >= 0) {
                cntls[i].request_attachment().resize(FLAGS_attachment_size, 'a');
            }
            ids[i] = cntls[i].call_id();
            stub->Test(&cntls[i], &reqs[i], &resps[i], brpc::DoNothing());
        }
        for (int i = 0; i < FLAGS_queue_depth; ++i) {
            brpc::Join(ids[i]);
            if (cntls[i].Failed()) {
                g_error_count << 1;
                LOG_EVERY_SECOND(WARNING)
                    << "RPC failed: " << cntls[i].ErrorText();
            } else {
                g_latency << cntls[i].latency_us();
            }
        }
        if (qps > 0) {
            usleep(FLAGS_queue_depth * 1000000 / qps);
        }
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    brpc::ChannelOptions options;
    options.socket_mode = FLAGS_use_urma ? brpc::SOCKET_MODE_URMA
                                          : brpc::SOCKET_MODE_TCP;
    options.connect_timeout_ms = FLAGS_rpc_timeout_ms;
    options.timeout_ms = FLAGS_rpc_timeout_ms;
    options.max_retry = 0;
    brpc::Channel channel;
    if (channel.Init(FLAGS_server.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to init channel to " << FLAGS_server;
        return -1;
    }
    test::PerfTestService_Stub stub(&channel);

    // Complete one RPC before starting all workers. This makes handshake and
    // data-path failures visible instead of looking like a hung benchmark.
    brpc::Controller warmup_cntl;
    warmup_cntl.set_timeout_ms(FLAGS_rpc_timeout_ms);
    test::PerfTestRequest warmup_req;
    test::PerfTestResponse warmup_resp;
    warmup_req.set_echo_attachment(false);
    stub.Test(&warmup_cntl, &warmup_req, &warmup_resp, nullptr);
    if (warmup_cntl.Failed()) {
        LOG(ERROR) << "Warm-up RPC failed after timeout_ms="
                   << FLAGS_rpc_timeout_ms << ": "
                   << warmup_cntl.ErrorText();
        return -1;
    }
    LOG(INFO) << "Warm-up RPC to " << FLAGS_server
              << " succeeded, latency=" << warmup_cntl.latency_us() << "us";

    int thread_num = FLAGS_thread_num;
    if (thread_num == 0) {
        thread_num = FLAGS_max_thread_num;
    }
    if (thread_num <= 0 || FLAGS_queue_depth <= 0) {
        LOG(ERROR) << "thread_num and queue_depth must be positive";
        return -1;
    }
    std::vector<bthread_t> tids(thread_num);
    for (int i = 0; i < thread_num; ++i) {
        bthread_start_background(&tids[i], nullptr, worker, &stub);
    }
    LOG(INFO) << "URMA performance client started (server=" << FLAGS_server
              << ", use_urma=" << FLAGS_use_urma
              << ", threads=" << thread_num
              << ", rpc_timeout_ms=" << FLAGS_rpc_timeout_ms << ")";
    while (!brpc::IsAskedToQuit()) {
        sleep(1);
        LOG(INFO) << "qps=" << g_latency.qps(1)
                  << " latency=" << g_latency.latency(1) << "us"
                  << " errors=" << g_error_count.get_value();
    }
    for (int i = 0; i < thread_num; ++i) {
        bthread_join(tids[i], nullptr);
    }
    return 0;
}

#else

#include <cstdio>
int main() {
    printf("This example requires brpc built with -DWITH_URMA=ON.\n");
    return 0;
}

#endif  // BRPC_WITH_URMA
