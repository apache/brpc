// Copyright (c) 2017 Baidu, Inc.
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

// A client sending thrift requests to server every 1 second.

#include <gflags/gflags.h>

#include <butil/logging.h>
#include <butil/time.h>
#include <butil/strings/string_piece.h>
#include <brpc/channel.h>
#include <brpc/thrift_binary_message.h>
#include <bvar/bvar.h>

#include "thrift/transport/TBufferTransports.h"
#include "thrift/protocol/TBinaryProtocol.h"

#include "gen-cpp/EchoService.h"
#include "gen-cpp/echo_types.h"

#include "thrift_utils.h"

bvar::LatencyRecorder g_latency_recorder("client");

DEFINE_string(server, "0.0.0.0:8019", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);
    
    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;
    
    // Initialize the channel, NULL means using default options. 
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_THRIFT;
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    // Send a request and wait for the response every 1 second.
    int log_id = 0;
    while (!brpc::IsAskedToQuit()) {
        brpc::ThriftBinaryMessage request;
        brpc::ThriftBinaryMessage response;
        brpc::Controller cntl;

        // Thrift Req
        example::EchoRequest thrift_request;
        thrift_request.data = "hello";

        std::string function_name = "Echo";
        int32_t seqid = 0;
        
        if (!serilize_thrift_server_message<example::EchoService_Echo_pargs>(thrift_request, function_name, seqid, &request)) {
            LOG(ERROR) << "serilize_thrift_server_message error!";
            continue;
        }

        cntl.set_log_id(log_id ++);  // set by user

        // Because `done'(last parameter) is NULL, this function waits until
        // the response comes back or error occurs(including timedout).
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);

        if (cntl.Failed()) {
            LOG(ERROR) << "Fail to send thrift request, " << cntl.ErrorText();
            sleep(1); // Remove this sleep in production code.
        } else {
            g_latency_recorder << cntl.latency_us();
        }

        example::EchoResponse thrift_response;
        if (!deserilize_thrift_server_message<example::EchoService_Echo_presult>(response, &function_name, &seqid, &thrift_response)) {
            LOG(ERROR) << "deserilize_thrift_server_message error!";
            continue;
        }

        LOG(INFO) << "Thrift function_name: " << function_name
            << "Thrift Res data: " << thrift_response.data;

        LOG_EVERY_SECOND(INFO)
            << "Sending thrift requests at qps=" << g_latency_recorder.qps(1)
            << " latency=" << g_latency_recorder.latency(1);
        
        sleep(1);
    }

    LOG(INFO) << "EchoClient is going to quit";
    return 0;
}
