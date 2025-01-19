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

// A client to send 2 requests to server and accept the first returned response.

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <brpc/channel.h>
#include "echo.pb.h"

DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in src/brpc/options.proto");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:8000", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");

// A special done for canceling another RPC.
class CancelRPC : public google::protobuf::Closure {
public:
    explicit CancelRPC(brpc::CallId rpc_id) : _rpc_id(rpc_id) {}
    
    void Run() {
        brpc::StartCancel(_rpc_id);
    }
    
private:
    brpc::CallId _rpc_id;
};

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    
    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;

    // Initialize the channel, NULL means using default options. 
    brpc::ChannelOptions options;
    options.protocol = FLAGS_protocol;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    // Normally, you should not call a Channel directly, but instead construct
    // a stub Service wrapping it. stub can be shared by all threads as well.
    example::EchoService_Stub stub(&channel);

    // Send a request and wait for the response every 1 second.
    int log_id = 0;
    while (!brpc::IsAskedToQuit()) {
        example::EchoRequest request1;
        example::EchoResponse response1;
        brpc::Controller cntl1;

        example::EchoRequest request2;
        example::EchoResponse response2;
        brpc::Controller cntl2;

        request1.set_message("hello1");
        request2.set_message("hello2");
        
        cntl1.set_log_id(log_id ++);  // set by user
        cntl2.set_log_id(log_id ++);

        const brpc::CallId id1 = cntl1.call_id();
        const brpc::CallId id2 = cntl2.call_id();
        CancelRPC done1(id2);
        CancelRPC done2(id1);
        
        butil::Timer tm;
        tm.start();
        // Send 2 async calls and join them. They will cancel each other in
        // their done which is run before the RPC being Join()-ed. Canceling
        // a finished RPC has no effect. 
        // For example:
        //  Time       RPC1                      RPC2
        //   1     response1 comes back. 
        //   2     running done1.
        //   3     cancel RPC2   
        //   4                              running done2 (NOTE: done also runs)
        //   5                              cancel RPC1 (no effect)
        stub.Echo(&cntl1, &request1, &response1, &done1);
        stub.Echo(&cntl2, &request2, &response2, &done2);
        brpc::Join(id1);
        brpc::Join(id2);
        tm.stop();
        if (cntl1.Failed() && cntl2.Failed()) {
            LOG(WARNING) << "Both failed. rpc1:" << cntl1.ErrorText()
                         << ", rpc2: " << cntl2.ErrorText();
        } else if (!cntl1.Failed()) {
            LOG(INFO) << "Received `" << response1.message() << "' from rpc1="
                      << id1.value << '@' << cntl1.remote_side()
                      << " latency=" << tm.u_elapsed() << "us"
                      << " rpc1_latency=" << cntl1.latency_us() << "us"
                      << " rpc2_latency=" << cntl2.latency_us() << "us";
        } else {
            LOG(INFO) << "Received `" << response2.message() << "' from rpc2="
                      << id2.value << '@' << cntl2.remote_side()
                      << " latency=" << tm.u_elapsed() << "us"
                      << " rpc1_latency=" << cntl1.latency_us() << "us"
                      << " rpc2_latency=" << cntl2.latency_us() << "us";
        }
        sleep(1);
    }

    LOG(INFO) << "EchoClient is going to quit";
    return 0;
}
