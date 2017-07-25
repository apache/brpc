// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved
//
// If you have any problem, contact us:
//   Baidu Hi : group 1296497 
//   Email    : pbrpc@baidu.com
//   Wiki     : http://wiki.baidu.com/display/RPC

// A client sending requests to ubrpc server every 1 second.
// This client can access the server in public/baidu-rpc-ub/example/echo_c++_compack_ubrpc as well.

#include <gflags/gflags.h>

#include <base/logging.h>
#include <base/time.h>
#include <brpc/channel.h>
#include "echo.pb.h"

DEFINE_string(server, "0.0.0.0:8500", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");
DEFINE_bool(multi_args, false,
            "The ubrpc to be accessed has more than one request and response");

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);
    
    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;
    
    // Initialize the channel, NULL means using default options. 
    brpc::ChannelOptions options;
    options.protocol = "ubrpc_compack";
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    example::EchoService_Stub stub(&channel);
    example::EchoRequest request;
    example::EchoResponse response;
    example::MultiRequests multi_requests;
    example::MultiResponses multi_responses;
    brpc::Controller cntl;
    
    // Send a request and wait for the response every 1 second.
    int log_id = 0;
    while (!brpc::IsAskedToQuit()) {
        // Reset before reuse.
        cntl.Reset();
        
        if (!FLAGS_multi_args) {
            request.Clear();
            response.Clear();
            request.set_message("hello world");
            for (int i = (log_id % 7); i > 0; --i) {
                example::Object* obj = request.add_objects();
                obj->set_id(log_id);
                if (log_id % 2 == 0) {
                    obj->set_value(log_id);
                }
                if (log_id % 3 == 0) {
                    obj->set_note("foo");
                }
                if (log_id % 5 == 0) {
                    for (int j = (log_id % 3); j > 0; --j) {
                        example::Parameter* param = obj->add_params();
                        if (log_id % 2 == 0) {
                            param->set_x(log_id);
                        }
                        if (log_id % 3 == 0) {
                            param->set_y("bar");
                        }
                        if (log_id % 5 == 0) {
                            param->set_z(log_id);
                        }
                    }
                }
            }
        } else {
            multi_requests.Clear();
            multi_responses.Clear();
            multi_requests.mutable_req1()->set_message("hello");
            multi_requests.mutable_req2()->set_message("world");
            cntl.set_idl_names(brpc::idl_multi_req_multi_res);
        }
        cntl.set_log_id(log_id ++);  // set by user

        // Because `done'(last parameter) is NULL, this function waits until
        // the response comes back or error occurs(including timedout).
        if (!FLAGS_multi_args) {
            // [idl] void Echo(EchoRequest req, out EchoResponse res);
            stub.Echo(&cntl, &request, &response, NULL);
        } else {
            // [idl] uint32_t EchoWithMultiArgs(EchoRequest req1, EchoRequest req2, 
            //                                     out EchoResponse res1, out EchoResponse res2);
            stub.EchoWithMultiArgs(&cntl, &multi_requests, &multi_responses, NULL);
        }
        if (!cntl.Failed()) {
            LOG(INFO) << "Received response from " << cntl.remote_side()
                       << ": " << noflush;
            if (!FLAGS_multi_args) {
                LOG(INFO) << response.message() << noflush;
            } else {
                LOG(INFO) << "res1=" << multi_responses.res1().message()
                          << " res2=" << multi_responses.res2().message()
                          << " result=" << cntl.idl_result()
                          << noflush;
            }
            LOG(INFO) << " latency=" << cntl.latency_us() << "us";
        } else {
            LOG(ERROR) << "Fail to send request, " << cntl.ErrorText();
        }
        sleep(1);
    }

    LOG(INFO) << "EchoClient is going to quit";
    return 0;
}
