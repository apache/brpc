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

// A client sending requests to server every 1 second.

#include <gflags/gflags.h>

#include <butil/logging.h>
#include <butil/time.h>
#include <butil/strings/string_piece.h>
#include <brpc/channel.h>
#include <brpc/nshead_message.h>
#include <bvar/bvar.h>

bvar::LatencyRecorder g_latency_recorder("client");

DEFINE_string(server, "0.0.0.0:8010", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    
    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;
    
    // Initialize the channel, NULL means using default options. 
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_NSHEAD;
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    // Send a request and wait for the response every 1 second.
    int log_id = 0;
    while (!brpc::IsAskedToQuit()) {
        brpc::NsheadMessage request;
        brpc::NsheadMessage response;
        brpc::Controller cntl;

        // Append message to `request'
        request.body.append("hello world");

        cntl.set_log_id(log_id ++);  // set by user

        // Because `done'(last parameter) is NULL, this function waits until
        // the response comes back or error occurs(including timedout).
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        if (cntl.Failed()) {
            LOG(ERROR) << "Fail to send nshead request, " << cntl.ErrorText();
            sleep(1); // Remove this sleep in production code.
        } else {
            g_latency_recorder << cntl.latency_us();
        }
        LOG_EVERY_SECOND(INFO)
            << "Sending nshead requests at qps=" << g_latency_recorder.qps(1)
            << " latency=" << g_latency_recorder.latency(1);
    }

    LOG(INFO) << "EchoClient is going to quit";
    return 0;
}
