// Copyright (c) 2016 Baidu, Inc.
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

// A server to receive EchoRequest and send back EchoResponse.

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <brpc/thrift_service.h>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>

#include "gen-cpp/EchoService.h"
#include "gen-cpp/echo_types.h"

DEFINE_int32(port, 8019, "TCP Port of this server");
DEFINE_int32(port2, 8018, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(max_concurrency, 0, "Limit of request processing in parallel");

class EchoServiceHandler : virtual public example::EchoServiceIf {
public:
    EchoServiceHandler() {}

    void Echo(example::EchoResponse& res, const example::EchoRequest& req) {
        // Process request, just attach a simple string.
        res.data = req.data + " world";
        LOG(INFO) << "Echo req.data: " << req.data;
        return;
    }

};

// Adapt your own thrift-based protocol to use brpc 
class MyThriftProtocol : public brpc::ThriftService {
public:
    MyThriftProtocol(EchoServiceHandler* handler) : _handler(handler) { }

    void ProcessThriftFramedRequest(const brpc::Server&,
                              brpc::Controller* cntl,
                              brpc::ThriftMessage* request,
                              brpc::ThriftMessage* response,
                              brpc::ThriftClosure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        if (cntl->Failed()) {
            // NOTE: You can send back a response containing error information
            // back to client instead of closing the connection.
            cntl->CloseConnection("Close connection due to previous error");
            return;
        }

        example::EchoRequest* req = request->cast<example::EchoRequest>();
        example::EchoResponse* res = response->cast<example::EchoResponse>();

        // process with req and res
        if (_handler) {
            _handler->Echo(*res, *req);
        } else {
            cntl->CloseConnection("Close connection due to no valid handler");
            LOG(ERROR) << "Fail to process thrift request due to no valid handler";
            return;
        }

        LOG(INFO) << "success to process thrift request in brpc with handler";

    }

private:
    EchoServiceHandler* _handler;

};

// Adapt your own thrift-based protocol to use brpc 
class MyThriftProtocolPbManner : public brpc::ThriftService {
public:
    void ProcessThriftFramedRequest(const brpc::Server&,
                              brpc::Controller* cntl,
                              brpc::ThriftMessage* request,
                              brpc::ThriftMessage* response,
                              brpc::ThriftClosure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        if (cntl->Failed()) {
            // NOTE: You can send back a response containing error information
            // back to client instead of closing the connection.
            cntl->CloseConnection("Close connection due to previous error");
            return;
        }

        example::EchoRequest* req = request->cast<example::EchoRequest>();
        example::EchoResponse* res = response->cast<example::EchoResponse>();

        // process with req and res
        res->data = req->data + " world another!";

        LOG(INFO) << "success to process thrift request in brpc with pb manner";

    }

};


int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Server server;
    brpc::ServerOptions options;

    auto thrift_service_handler = new EchoServiceHandler();

    options.thrift_service = new MyThriftProtocol(thrift_service_handler);
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    options.max_concurrency = FLAGS_max_concurrency;

    // Start the server.
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    brpc::Server server2;
    brpc::ServerOptions options2;
    options2.thrift_service = new MyThriftProtocolPbManner;
    options2.idle_timeout_sec = FLAGS_idle_timeout_s;
    options2.max_concurrency = FLAGS_max_concurrency;

    // Start the server2.
    if (server2.Start(FLAGS_port2, &options2) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
