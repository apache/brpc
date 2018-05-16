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

// A benchmark to test max throughput.

#include <bthread/bthread.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <brpc/channel.h>
#include <gflags/gflags.h>
#include "echo.pb.h"

DEFINE_int32(thread_num, 1, "Number of threads to send requests");
DEFINE_int32(attachment_size, 0, "Carry so many kilobyte attachment along with requests");
DEFINE_string(connection_type, "pooled", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:8002", "IP Address of server");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");
DEFINE_int32(dummy_port, -1, "Launch dummy server at this port");
DEFINE_int32(iterations, 300, "Loop times");

std::string g_attachment;

static void* sender(void* arg) {
    example::EchoService_Stub stub(static_cast<google::protobuf::RpcChannel*>(arg));

    int log_id = 0;
    int loop = FLAGS_iterations;
    while (!brpc::IsAskedToQuit() && loop > 0) {
        example::EchoRequest request;
        example::EchoResponse response;
        brpc::Controller cntl;
        request.set_message("");

        cntl.set_log_id(log_id++);
        cntl.request_attachment().append(g_attachment);
        stub.Echo(&cntl, &request, &response, NULL);
        if (cntl.Failed()) {
            CHECK(brpc::IsAskedToQuit() || !FLAGS_dont_fail)
                << "error=" << cntl.ErrorText() << " latency=" << cntl.latency_us();
            bthread_usleep(50000);
        }
        loop--;
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Channel channel[FLAGS_thread_num];
    
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.connection_type = FLAGS_connection_type;
    options.connect_timeout_ms = std::min(FLAGS_timeout_ms / 2, 100);
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        if (channel[i].Init(FLAGS_server.c_str(), &options) != 0) {
            LOG(ERROR) << "Fail to initialize channel";
            return -1;
        }
    }

    if (FLAGS_attachment_size > 0) {
        g_attachment.resize(FLAGS_attachment_size * 1024, 'a');
    }

    if (FLAGS_dummy_port >= 0) {
        brpc::StartDummyServerAt(FLAGS_dummy_port);
    }

    std::vector<bthread_t> tids;
    tids.resize(FLAGS_thread_num);

    uint64_t start_time = butil::gettimeofday_us();
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        if (pthread_create(&tids[i], NULL, sender, &channel[i]) != 0) {
            LOG(ERROR) << "Fail to create pthread";
            return -1;
        }
    }
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        pthread_join(tids[i], NULL);
    }
    uint64_t end_time = butil::gettimeofday_us();

    double throughput = 1024.0 * FLAGS_thread_num * FLAGS_attachment_size
                        * FLAGS_iterations / (end_time - start_time);
    LOG(INFO)
        << "loops=" << FLAGS_iterations << " "
        << "thread_num=" << FLAGS_thread_num << " "
        << "attachment_size=" << FLAGS_attachment_size << "KB "
        << "throughput=" << throughput << "MB/s";

    return 0;
}
