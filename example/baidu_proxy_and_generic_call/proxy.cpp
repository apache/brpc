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
// todo
// A proxy to receive EchoRequest and send back EchoResponse.

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/strings/string_number_conversions.h>
#include <brpc/server.h>
#include <brpc/controller.h>
#include <brpc/channel.h>
#include <json2pb/pb_to_json.h>

DEFINE_int32(port, 8000, "TCP Port of this server");
DEFINE_string(listen_addr, "", "Server listen address, may be IPV4/IPV6/UDS."
            " If this is set, the flag port will be ignored");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server_address, "0.0.0.0:8001", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");
DEFINE_int32(interval_ms, 1000, "Milliseconds between consecutive requests");

// Your implementation of example::EchoService
// Notice that implementing brpc::Describable grants the ability to put
// additional information in /status.
namespace example {
class BaiduMasterServiceImpl : public brpc::BaiduMasterService {
public:
    void ProcessRpcRequest(brpc::Controller* cntl,
                           const brpc::SerializedRequest* request,
                           brpc::SerializedResponse* response,
                           ::google::protobuf::Closure* done) override {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        // A Channel represents a communication line to a Server. Notice that
        // Channel is thread-safe and can be shared by all threads in your program.
        brpc::Channel channel;

        // Initialize the channel, NULL means using default options.
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_BAIDU_STD;
        options.connection_type = FLAGS_connection_type;
        options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
        options.max_retry = FLAGS_max_retry;
        if (channel.Init(FLAGS_server_address.c_str(),
            FLAGS_load_balancer.c_str(), &options) != 0) {
            LOG(ERROR) << "Fail to initialize channel";
            (*cntl->response_user_fields())["x-bd-proxy-error-code"] =
                butil::IntToString(brpc::EINTERNAL);
            (*cntl->response_user_fields())["x-bd-proxy-error-text"] =
                "Fail to initialize channel";
            return;
        }

        LOG(INFO) << "Received request[log_id=" << cntl->log_id()
                  << "] from " << cntl->remote_side()
                  << " to " << cntl->local_side()
                  << ", serialized request size=" << request->serialized_data().size()
                  << ", request compress type=" << cntl->request_compress_type()
                  << " (attached=" << cntl->request_attachment() << ")";

        brpc::Controller call_cntl;
        call_cntl.set_log_id(cntl->log_id());
        call_cntl.request_attachment().swap(cntl->request_attachment());
        call_cntl.set_request_compress_type(cntl->request_compress_type());
        call_cntl.reset_sampled_request(cntl->release_sampled_request());
        // It is ok to use request and response for sync rpc.
        channel.CallMethod(NULL, &call_cntl, request, response, NULL);
        (*cntl->response_user_fields())["x-bd-proxy-error-code"] =
            butil::IntToString(call_cntl.ErrorCode());
        if (call_cntl.Failed()) {
            (*cntl->response_user_fields())["x-bd-proxy-error-text"] =
                call_cntl.ErrorText();
            LOG(ERROR) << "Fail to call service=" << call_cntl.sampled_request()->meta.service_name()
                       << ", method=" << call_cntl.sampled_request()->meta.method_name()
                       << ", error_code=" << call_cntl.ErrorCode()
                       << ", error_text=" << call_cntl.ErrorCode();
            return;
        } else {
            LOG(INFO) << "Received response from " << call_cntl.remote_side()
                      << " to " << call_cntl.local_side()
                      << ", serialized response size=" << response->serialized_data().size()
                      << ", response compress type=" << call_cntl.response_compress_type()
                      << ", attached=" << call_cntl.response_attachment()
                      << ", latency=" << call_cntl.latency_us() << "us";
        }
        cntl->response_attachment().swap(call_cntl.response_attachment());
        cntl->set_response_compress_type(call_cntl.response_compress_type());
    }
};
}  // namespace example

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    // Generally you only need one Server.
    brpc::Server server;

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
    // Add the baidu master service into server.
    // Notice new operator, because server will delete it in dtor of Server.
    options.baidu_master_service = new example::BaiduMasterServiceImpl();
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    if (server.Start(point, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
