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

#include "thrift/transport/TBufferTransports.h"
#include "thrift/protocol/TBinaryProtocol.h"

#include "gen-cpp/EchoService.h"
#include "gen-cpp/echo_types.h"

#include "thrift_utils.h"

DEFINE_int32(port, 8019, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(max_concurrency, 0, "Limit of request processing in parallel");

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

        example::EchoRequest thrift_request;
		std::string function_name;
		int32_t seqid;
		
		// 
		if (!serilize_thrift_client_message<example::EchoService_Echo_args>(request, 
			&thrift_request, &function_name, &seqid)) {
			cntl->CloseConnection("Close connection due to serilize thrift client reuqest error!");
			LOG(ERROR) << "serilize thrift client reuqest error!";
            return;
		}

		LOG(INFO) << "RPC funcname: " << function_name
			<< "thrift request data: " << thrift_request.data;

		example::EchoResponse thrift_response;
		// Proc RPC , just append a simple string
        thrift_response.data = thrift_request.data + " world";

		if (!deserilize_thrift_client_message<example::EchoService_Echo_result>(thrift_response,
			function_name, seqid, response)) {
			cntl->CloseConnection("Close connection due to deserilize thrift client response error!");
			LOG(ERROR) << "deserilize thrift client response error!";
            return;
		}

        LOG(INFO) << "success process thrift request in brpc";
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
