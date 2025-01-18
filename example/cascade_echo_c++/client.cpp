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

// A client sending requests to server which will send the request to itself
// again according to the field `depth'

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <bthread/bthread.h>
#include <brpc/channel.h>
#include <brpc/server.h>
#include "echo.pb.h"
#include <bvar/bvar.h>
#include <butil/fast_rand.h>

DEFINE_int32(thread_num, 2, "Number of threads to send requests");
DEFINE_bool(use_bthread, false, "Use bthread to send requests");
DEFINE_string(attachment, "foo", "Carry this along with requests");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:8000", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in src/brpc/options.proto");
DEFINE_int32(depth, 0, "number of loop calls");
// Don't send too frequently in this example
DEFINE_int32(sleep_ms, 1000, "milliseconds to sleep after each RPC");
DEFINE_int32(dummy_port, -1, "Launch dummy server at this port");

bvar::LatencyRecorder g_latency_recorder("client");

void* sender(void* arg) {
    brpc::Channel* chan = (brpc::Channel*)arg;
    // Normally, you should not call a Channel directly, but instead construct
    // a stub Service wrapping it. stub can be shared by all threads as well.
    example::EchoService_Stub stub(chan);

    // Send a request and wait for the response every 1 second.
    while (!brpc::IsAskedToQuit()) {
        // We will receive response synchronously, safe to put variables
        // on stack.
        example::EchoRequest request;
        example::EchoResponse response;
        brpc::Controller cntl;

        request.set_message("hello world");
        if (FLAGS_depth > 0) {
            request.set_depth(FLAGS_depth);
        }

        // Set request_id to be a random string
        cntl.set_request_id(butil::fast_rand_printable(9));

        // Set attachment which is wired to network directly instead of 
        // being serialized into protobuf messages.
        cntl.request_attachment().append(FLAGS_attachment);

        // Because `done'(last parameter) is NULL, this function waits until
        // the response comes back or error occurs(including timedout).
        stub.Echo(&cntl, &request, &response, NULL);
        if (cntl.Failed()) {
            //LOG_EVERY_SECOND(WARNING) << "Fail to send EchoRequest, " << cntl.ErrorText();
        } else {
            g_latency_recorder << cntl.latency_us();
        }
        if (FLAGS_sleep_ms != 0) {
            bthread_usleep(FLAGS_sleep_ms * 1000L);
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::SetUsageMessage("Send EchoRequest to server every second");
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = FLAGS_protocol;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    
    // Initialize the channel, NULL means using default options. 
    // options, see `brpc/channel.h'.
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    std::vector<bthread_t> bids;
    std::vector<pthread_t> pids;
    if (!FLAGS_use_bthread) {
        pids.resize(FLAGS_thread_num);
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            if (pthread_create(&pids[i], NULL, sender, &channel) != 0) {
                LOG(ERROR) << "Fail to create pthread";
                return -1;
            }
        }
    } else {
        bids.resize(FLAGS_thread_num);
        for (int i = 0; i < FLAGS_thread_num; ++i) {
            if (bthread_start_background(
                    &bids[i], NULL, sender, &channel) != 0) {
                LOG(ERROR) << "Fail to create bthread";
                return -1;
            }
        }
    }

    if (FLAGS_dummy_port >= 0) {
        brpc::StartDummyServerAt(FLAGS_dummy_port);
    }

    while (!brpc::IsAskedToQuit()) {
        sleep(1);
        LOG(INFO) << "Sending EchoRequest at qps=" << g_latency_recorder.qps(1)
                  << " latency=" << g_latency_recorder.latency(1);
    }

    LOG(INFO) << "EchoClient is going to quit";
    for (int i = 0; i < FLAGS_thread_num; ++i) {
        if (!FLAGS_use_bthread) {
            pthread_join(pids[i], NULL);
        } else {
            bthread_join(bids[i], NULL);
        }
    }
    return 0;
}
