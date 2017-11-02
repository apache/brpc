// Copyright (c) 2014 Baidu, Inc.
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

DEFINE_int32(port, 8019, "TCP Port of this server");
DEFINE_int32(idle_timeout_s, -1, "Connection will be closed if there is no "
             "read/write operations during the last `idle_timeout_s'");
DEFINE_int32(max_concurrency, 0, "Limit of request processing in parallel");

using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::transport::TMemoryBuffer;

using namespace std;
using namespace example;

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

        boost::shared_ptr<TMemoryBuffer> buffer(new TMemoryBuffer());
        boost::shared_ptr<TBinaryProtocol> iprot(new TBinaryProtocol(buffer));
        EchoRequest t_request;

        size_t body_len  = request.head.body_len;
        uint8_t* thrfit_b = (uint8_t*)malloc(body_len);
        const size_t k = request.body.copy_to(thrfit_b, body_len);
        if ( k != body_len) {
            cntl->CloseConnection("Close connection due to copy thrift binary message error");
            return;

        }

        EchoService_Echo_args args;
        buffer->resetBuffer(thrfit_b, body_len);

        int32_t rseqid = 0;
        std::string fname;
        ::apache::thrift::protocol::TMessageType mtype;

        // deserilize thrift message
        iprot->readMessageBegin(fname, mtype, rseqid);

        args.read(iprot.get());
        iprot->readMessageEnd();
        iprot->getTransport()->readEnd();

        t_request = args.request;

        std::cout << "RPC funcname: " << fname << std::endl;
        std::cout << "request.data: " << t_request.data << std::endl;

        boost::shared_ptr<TMemoryBuffer> o_buffer(new TMemoryBuffer());
        boost::shared_ptr<TBinaryProtocol> oprot(new TBinaryProtocol(o_buffer));

        // Proc RPC
        EchoResponse t_response;
        t_response.data = t_request.data + " world";

        EchoService_Echo_result result;
        result.success = t_response;
        result.__isset.success = true;

        // serilize response
        oprot->writeMessageBegin("Echo", ::apache::thrift::protocol::T_REPLY, 0);
        result.write(oprot.get());
        oprot->writeMessageEnd();
        oprot->getTransport()->writeEnd();
        oprot->getTransport()->flush();

        butil::IOBuf buf;
        std::string s = o_buffer->getBufferAsString();

        buf.append(s);
        response->body = buf;

        printf("process in MyThriftProtocol\n");
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
