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
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(max_concurrency, 0, "Limit of request processing in parallel");

class EchoServiceHandler : virtual public example::EchoServiceIf {
public:
    EchoServiceHandler() {}

    void Echo(example::EchoResponse& res, const example::EchoRequest& req) {
        // Process request, just attach a simple string.
        res.data = req.data + " (processed by handler)";
        return;
    }

};

static std::atomic<int> g_counter(0);

// Adapt your own thrift-based protocol to use brpc 
class MyThriftProtocol : public brpc::ThriftService {
public:
    explicit MyThriftProtocol(EchoServiceHandler* handler) : _handler(handler) { }

    void ProcessThriftFramedRequest(const brpc::Server&,
                              brpc::Controller* cntl,
                              brpc::ThriftFramedMessage* request,
                              brpc::ThriftFramedMessage* response,
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

        example::EchoRequest* req = request->Cast<example::EchoRequest>();
        example::EchoResponse* res = response->Cast<example::EchoResponse>();

        if (g_counter++ % 2 == 0) {
            if (!_handler) {
                cntl->CloseConnection("Close connection due to no valid handler");
                LOG(ERROR) << "No valid handler";
                return;
            }
            _handler->Echo(*res, *req);
        } else {
            res->data = req->data + " (processed directly)";
        }
    }

private:
    EchoServiceHandler* _handler;

};

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Server server;
    brpc::ServerOptions options;

    EchoServiceHandler thrift_service_handler;
    options.thrift_service = new MyThriftProtocol(&thrift_service_handler);
    options.idle_timeout_sec = FLAGS_idle_timeout_s;
    options.max_concurrency = FLAGS_max_concurrency;

    // Start the server.
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start EchoServer";
        return -1;
    }

    // Wait until Ctrl-C is pressed, then Stop() and Join() the server.
    server.RunUntilAskedToQuit();
    return 0;
}
