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

// A server to receive EchoRequest and send back EchoResponse.

#include <brpc/channel.h>
#include <brpc/server.h>
#include <butil/logging.h>
#include <gflags/gflags.h>
#include <json2pb/pb_to_json.h>
#include "echo.pb.h"

DEFINE_bool(echo_attachment, true, "Echo attachment as well");
DEFINE_int32(port, 8000, "TCP Port of this server");
DEFINE_string(listen_addr, "",
              "Server listen address, may be IPV4/IPV6/UDS."
              " If this is set, the flag port will be ignored");
DEFINE_int32(idle_timeout_s, -1,
             "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_string(server, "0.0.0.0:8001", "IP Address of server");
DEFINE_string(protocol, "baidu_std", "Protocol type. Defined in src/brpc/options.proto");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");
// Your implementation of example::EchoService
// Notice that implementing brpc::Describable grants the ability to put
// additional information in /status.
namespace example {

static const bthread_attr_t BTHREAD_ATTR_NORMAL_WITH_SPAN = {
    BTHREAD_STACKTYPE_NORMAL, BTHREAD_INHERIT_SPAN, NULL, BTHREAD_TAG_INVALID};

void* RunThreadFunc(void*) {
    TRACEPRINTF("RunThreadFunc %lu", bthread_self());
    // brpc::FLAGS_enable_rpcz = true;
    // A Channel represents a communication line to a Server. Notice that
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;
    // Initialize the channel, NULL means using default options.
    brpc::ChannelOptions options;
    options.protocol = FLAGS_protocol;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms /*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), "", &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return nullptr;
    }
    example::EchoService_Stub stub(&channel);
    // We will receive response synchronously, safe to put variables
    // on stack.
    example::EchoRequest request;
    example::EchoResponse response;
    brpc::Controller cntl;
    request.set_message("hello world");

    // Because `done'(last parameter) is NULL, this function waits until
    // the response comes back or error occurs(including timedout).
    stub.Echo(&cntl, &request, &response, NULL);
    if (!cntl.Failed()) {
        LOG(INFO) << "Received response from " << cntl.remote_side() << " to " << cntl.local_side()
                  << ": " << response.message() << " (attached=" << cntl.response_attachment()
                  << ")"
                  << " latency=" << cntl.latency_us() << "us";
    } else {
        LOG(WARNING) << cntl.ErrorText();
    }

    return nullptr;
}

class EchoServiceImpl : public EchoService {
public:
    EchoServiceImpl() {}
    virtual ~EchoServiceImpl() {}
    virtual void Echo(google::protobuf::RpcController* cntl_base, const EchoRequest* request,
                      EchoResponse* response, google::protobuf::Closure* done) {
        bthread_list_t list;
        bthread_list_init(&list, 0, 0);
        for (int i = 0; i < 2; ++i) {
            bthread_t tid;
            bthread_start_background(&tid, &BTHREAD_ATTR_NORMAL_WITH_SPAN, RunThreadFunc, nullptr);
            bthread_list_add(&list, tid);
        }
        bthread_list_join(&list);

        TRACEPRINTF("Handle request");

        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);

        // The purpose of following logs is to help you to understand
        // how clients interact with servers more intuitively. You should
        // remove these logs in performance-sensitive servers.
        LOG(INFO) << "Received request[log_id=" << cntl->log_id() << "] from "
                  << cntl->remote_side() << " to " << cntl->local_side() << ": "
                  << request->message() << " (attached=" << cntl->request_attachment() << ")";

        // Fill response.
        response->set_message(request->message());

        // You can compress the response by setting Controller, but be aware
        // that compression may be costly, evaluate before turning on.
        // cntl->set_response_compress_type(brpc::COMPRESS_TYPE_GZIP);

        if (FLAGS_echo_attachment) {
            // Set attachment which is wired to network directly instead of
            // being serialized into protobuf messages.
            cntl->response_attachment().append(cntl->request_attachment());
        }
    }
};
}  // namespace example

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    // Generally you only need one Server.
    brpc::Server server;

    // Instance of your service.
    example::EchoServiceImpl echo_service_impl;

    // Add the service into server. Notice the second parameter, because the
    // service is put on stack, we don't want server to delete it, otherwise
    // use brpc::SERVER_OWNS_SERVICE.
    if (server.AddService(&echo_service_impl, brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    butil::EndPoint point;
    if (!FLAGS_listen_addr.empty()) {
        if (butil::str2endpoint(FLAGS_listen_addr.c_str(), &point) < 0) {
            LOG(ERROR) << "Invalid listen address:" << FLAGS_listen_addr;
            return -1;
        }
    } else {
        point = butil::EndPoint(butil::IP_ANY, FLAGS_port);
    }
    // Start the server.
    brpc::ServerOptions options;
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    if (server.Start(point, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
