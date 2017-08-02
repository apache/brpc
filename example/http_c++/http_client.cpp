// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// If you have any problem, contact us:
//   Baidu Hi : group 1296497 
//   Email    : pbrpc@baidu.com
//   Wiki     : http://wiki.baidu.com/display/RPC/baidu-rpc

// - Access pb services via HTTP
//   ./http_client http://db-rpc-dev00.db01.baidu.com:8765/EchoService/Echo -d '{"message":"hello"}'
// - Access builtin services
//   ./http_client http://db-rpc-dev00.db01.baidu.com:8765/vars/rpc_server*
// - Access www.baidu.com
//   ./http_client www.baidu.com

#include <gflags/gflags.h>
#include <base/logging.h>
#include <brpc/channel.h>

DEFINE_string(d, "", "POST this data to the http server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 1000, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_string(protocol, "http", "http or h2c");

namespace brpc {
DECLARE_bool(http_verbose);
}

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);

    if (argc != 2) {
        LOG(ERROR) << "Usage: ./http_client \"www.baidu.com\"";
        return -1;
    }
    char* url = argv[1];
    
    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = FLAGS_protocol;
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;

    // Initialize the channel, NULL means using default options. 
    // options, see `brpc/channel.h'.
    if (channel.Init(url, FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    // We will receive response synchronously, safe to put variables
    // on stack.
    brpc::Controller cntl;

    cntl.http_request().uri() = url;
    if (!FLAGS_d.empty()) {
        cntl.http_request().set_method(brpc::HTTP_METHOD_POST);
        cntl.request_attachment().append(FLAGS_d);
    }

    // Because `done'(last parameter) is NULL, this function waits until
    // the response comes back or error occurs(including timedout).
    channel.CallMethod(NULL, &cntl, NULL, NULL, NULL);
    if (cntl.Failed()) {
        std::cerr << cntl.ErrorText() << std::endl;
        return -1;
    }
    // If -http_verbose is on, baidu-rpc already prints the response to stderr.
    if (!brpc::FLAGS_http_verbose) {
        std::cout << cntl.response_attachment() << std::endl;
    }
    return 0;
}
