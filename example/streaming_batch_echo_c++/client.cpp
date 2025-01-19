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

// A client sending requests to server in batch every 1 second.

#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/channel.h>
#include <brpc/stream.h>
#include "echo.pb.h"

DEFINE_bool(send_attachment, true, "Carry attachment along with requests");
DEFINE_string(connection_type, "", "Connection type. Available values: single, pooled, short");
DEFINE_string(server, "0.0.0.0:8001", "IP Address of server");
DEFINE_int32(timeout_ms, 100, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 

class StreamClientReceiver : public brpc::StreamInputHandler {
public:
    virtual int on_received_messages(brpc::StreamId id,
                                     butil::IOBuf *const messages[],
                                     size_t size) {
        std::ostringstream os;
        for (size_t i = 0; i < size; ++i) {
            os << "msg[" << i << "]=" << *messages[i];
        }
        LOG(INFO) << "Received from Stream=" << id << ": " << os.str();
        return 0;
    }
    virtual void on_idle_timeout(brpc::StreamId id) {
        LOG(INFO) << "Stream=" << id << " has no data transmission for a while";
    }
    virtual void on_closed(brpc::StreamId id) {
        LOG(INFO) << "Stream=" << id << " is closed";
    }

    virtual void on_finished(brpc::StreamId id, int32_t finish_code) {
        LOG(INFO) << "Stream=" << id << " is finished, code " << finish_code;
    }
};

int main(int argc, char* argv[]) {
    // Parse gflags. We recommend you to use gflags as well.
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    // A Channel represents a communication line to a Server. Notice that 
    // Channel is thread-safe and can be shared by all threads in your program.
    brpc::Channel channel;
        
    // Initialize the channel, NULL means using default options. 
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_BAIDU_STD;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = FLAGS_timeout_ms/*milliseconds*/;
    options.max_retry = FLAGS_max_retry;
    if (channel.Init(FLAGS_server.c_str(), NULL) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    // Normally, you should not call a Channel directly, but instead construct
    // a stub Service wrapping it. stub can be shared by all threads as well.
    example::EchoService_Stub stub(&channel);
    StreamClientReceiver receiver;
    brpc::Controller cntl;
    brpc::StreamIds streams;
    brpc::StreamOptions stream_options;
    stream_options.handler = &receiver;
    if (brpc::StreamCreate(streams, 3, cntl, &stream_options) != 0) {
        LOG(ERROR) << "Fail to create stream";
        return -1;
    }
    for(size_t i = 0; i < streams.size(); ++i) {
        LOG(INFO) << "Created Stream=" << streams[i];
    }
    example::EchoRequest request;
    example::EchoResponse response;
    request.set_message("I'm a RPC to connect stream");
    stub.Echo(&cntl, &request, &response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to connect stream, " << cntl.ErrorText();
        return -1;
    }
    
    while (!brpc::IsAskedToQuit()) {
        butil::IOBuf msg1;
        msg1.append("abcdefghijklmnopqrstuvwxyz");
        CHECK_EQ(0, brpc::StreamWrite(streams[0], msg1));
        butil::IOBuf msg2;
        msg2.append("0123456789");
        CHECK_EQ(0, brpc::StreamWrite(streams[1], msg2));
        sleep(1);
        butil::IOBuf msg3;
        msg3.append("hello world");
        CHECK_EQ(0, brpc::StreamWrite(streams[2], msg3));
        sleep(1);
    }

    CHECK_EQ(0, brpc::StreamClose(streams[0]));
    CHECK_EQ(0, brpc::StreamClose(streams[1]));
    CHECK_EQ(0, brpc::StreamClose(streams[2]));
    LOG(INFO) << "EchoClient is going to quit";
    return 0;
}
