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
#include <butil/thrift_utils.h>
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
        res.data = req.data + " world";
        LOG(INFO) << "Echo req.data: " << req.data;
        return;
    }

};

// Adapt your own thrift-based protocol to use brpc 
class MyThriftProtocol : public brpc::ThriftFramedService {
public:
    void ProcessThriftBinaryRequest(const brpc::Server&,
                              brpc::Controller* cntl,
                              const brpc::ThriftBinaryMessage& request,
                              brpc::ThriftBinaryMessage* response, 
                              brpc::ThriftFramedClosure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        if (cntl->Failed()) {
            // NOTE: You can send back a response containing error information
            // back to client instead of closing the connection.
            cntl->CloseConnection("Close connection due to previous error");
            return;
        }

        // Just an example, you don't need to new the processor each time.
        boost::shared_ptr<EchoServiceHandler> service_hander(new EchoServiceHandler());
        boost::shared_ptr<example::EchoServiceProcessor> processor(
            new example::EchoServiceProcessor(service_hander));
        if (brpc_thrift_server_helper(request, response, processor)) {
            LOG(INFO) << "success to process thrift request in brpc";
        } else {
            LOG(INFO) << "failed to process thrift request in brpc";
        }

    }

};

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Server server;
    brpc::ServerOptions options;
    options.thrift_service = new MyThriftProtocol;
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
