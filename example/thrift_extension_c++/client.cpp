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

// A client sending requests to server every 1 second.

#include <gflags/gflags.h>

#include <butil/logging.h>
#include <butil/time.h>
#include <butil/strings/string_piece.h>
#include <brpc/channel.h>
#include <brpc/thrift_binary_message.h>
#include <bvar/bvar.h>

#include "thrift/transport/TBufferTransports.h"
#include "thrift/protocol/TBinaryProtocol.h"

#include "gen-cpp/EchoService.h"
#include "gen-cpp/echo_types.h"

bvar::LatencyRecorder g_latency_recorder("client");

using apache::thrift::protocol::TBinaryProtocol;
using apache::thrift::transport::TMemoryBuffer;

using namespace std;
using namespace example;

DEFINE_string(server, "0.0.0.0:8019", "IP Address of server");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    google::ParseCommandLineFlags(&argc, &argv, true);
    
    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;
    
    // Initialize the channel, NULL means using default options. 
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_THRIFT;
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    // Send a request and wait for the response every 1 second.
    int log_id = 0;
    while (!brpc::IsAskedToQuit()) {
        brpc::ThriftBinaryMessage request;
        brpc::ThriftBinaryMessage response;
        brpc::Controller cntl;

        // Append message to `request'

        boost::shared_ptr<TMemoryBuffer> o_buffer(new TMemoryBuffer());
        boost::shared_ptr<TBinaryProtocol> oprot(new TBinaryProtocol(o_buffer));


        // Construct request
        EchoRequest t_request;
        t_request.data = "hello";

        // Serialize thrift request to binary request
        oprot->writeMessageBegin("Echo", ::apache::thrift::protocol::T_CALL, 0);

        EchoService_Echo_pargs args;
        args.request = &t_request;
        args.write(oprot.get());

        oprot->writeMessageEnd();
        oprot->getTransport()->writeEnd();
        oprot->getTransport()->flush();

        butil::IOBuf buf;
        std::string s = o_buffer->getBufferAsString();

        buf.append(s);
        request.body = buf;

        cntl.set_log_id(log_id ++);  // set by user

        // Because `done'(last parameter) is NULL, this function waits until
        // the response comes back or error occurs(including timedout).
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);

        if (cntl.Failed()) {
            LOG(ERROR) << "Fail to send thrift request, " << cntl.ErrorText();
            sleep(1); // Remove this sleep in production code.
        } else {
            g_latency_recorder << cntl.latency_us();
        }


        // Parse/Desrialize binary response to thrift response
        boost::shared_ptr<TMemoryBuffer> buffer(new TMemoryBuffer());
        boost::shared_ptr<TBinaryProtocol> iprot(new TBinaryProtocol(buffer));

        size_t body_len  = response.head.body_len;
        uint8_t* thrfit_b = (uint8_t*)malloc(body_len);
        const size_t k = response.body.copy_to(thrfit_b, body_len);
        if ( k != body_len) {
            std::cout << "copy_to error!" << std::endl;
            printf("k: %lu, body_len: %lu\n", k, body_len);
            return -1;

        }

        buffer->resetBuffer(thrfit_b, body_len);

        int32_t rseqid = 0;
        std::string fname;          // Thrift function name
        ::apache::thrift::protocol::TMessageType mtype;

        try {
            iprot->readMessageBegin(fname, mtype, rseqid);
            if (mtype == ::apache::thrift::protocol::T_EXCEPTION) {
                ::apache::thrift::TApplicationException x;
                x.read(iprot.get());
                iprot->readMessageEnd();
                iprot->getTransport()->readEnd();
                throw x;
            }
            if (mtype != ::apache::thrift::protocol::T_REPLY) {
                iprot->skip(::apache::thrift::protocol::T_STRUCT);
                iprot->readMessageEnd();
                iprot->getTransport()->readEnd();
            }
            if (fname.compare("Echo") != 0) {
                iprot->skip(::apache::thrift::protocol::T_STRUCT);
                iprot->readMessageEnd();
                iprot->getTransport()->readEnd();
            }

            EchoResponse t_response;
            EchoService_Echo_presult result;
            result.success = &t_response;
            result.read(iprot.get());
            iprot->readMessageEnd();
            iprot->getTransport()->readEnd();

            if (!result.__isset.success) {
                // _return pointer has now been filled
                std::cout << "result.success not set!" << std::endl;
                return -1;
            }

            std::cout << "response: " << t_response.data << std::endl;
        
        } catch (...) {

            std::cout << "Thrift Exception!" << std::endl;
        }


        LOG_EVERY_SECOND(INFO)
            << "Sending thrift requests at qps=" << g_latency_recorder.qps(1)
            << " latency=" << g_latency_recorder.latency(1);
    }

    LOG(INFO) << "EchoClient is going to quit";
    return 0;
}
