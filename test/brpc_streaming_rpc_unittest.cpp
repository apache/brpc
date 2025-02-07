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

// brpc - A framework to host and access services throughout Baidu.

// Date: 2015/10/22 16:28:44

#include <gtest/gtest.h>
#include "brpc/server.h"

#include "brpc/controller.h"
#include "brpc/channel.h"
#include "brpc/socket.h"
#include "brpc/stream_impl.h"
#include "brpc/policy/streaming_rpc_protocol.h"
#include "echo.pb.h"

class AfterAcceptStream {
public:
    virtual void action(brpc::StreamId) = 0;
};

class MyServiceWithStream : public test::EchoService {
public:
    MyServiceWithStream(const brpc::StreamOptions& options) 
        : _options(options)
        , _after_accept_stream(NULL)
    {}
    MyServiceWithStream(const brpc::StreamOptions& options,
                        AfterAcceptStream* after_accept_stream) 
        : _options(options)
        , _after_accept_stream(after_accept_stream)
    {}
    MyServiceWithStream()
        : _options()
        , _after_accept_stream(NULL)
    {}

    void Echo(::google::protobuf::RpcController* controller,
                       const ::test::EchoRequest* request,
                       ::test::EchoResponse* response,
                       ::google::protobuf::Closure* done) {
        brpc::ClosureGuard done_gurad(done);
        response->set_message(request->message());
        brpc::Controller* cntl = (brpc::Controller*)controller;
        brpc::StreamId response_stream;
        ASSERT_EQ(0, StreamAccept(&response_stream, *cntl, &_options));
        LOG(INFO) << "Created response_stream=" << response_stream;
        if (_after_accept_stream) {
            _after_accept_stream->action(response_stream);
        }
    }
private:
    brpc::StreamOptions _options;
    AfterAcceptStream* _after_accept_stream;
};

class StreamingRpcTest : public testing::Test {
protected:
    void SetUp() { request.set_message("hello world"); }
    void TearDown() {}

    test::EchoRequest request;
    test::EchoResponse response;
};

TEST_F(StreamingRpcTest, sanity) {
    brpc::Server server;
    MyServiceWithStream service;
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));
    brpc::Controller cntl;
    brpc::StreamId request_stream;
    ASSERT_EQ(0, StreamCreate(&request_stream, cntl, NULL));
    brpc::ScopedStream stream_guard(request_stream);
    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText() << " request_stream=" << request_stream;
    usleep(10);
    brpc::StreamClose(request_stream);
    server.Stop(0);
    server.Join();
}

struct HandlerControl {
    HandlerControl()
        : block(false)
    {}
    bool block;
};

class OrderedInputHandler : public brpc::StreamInputHandler {
public:
    explicit OrderedInputHandler(HandlerControl *cntl = NULL)
        : _expected_next_value(0)
        , _failed(false)
        , _stopped(false)
        , _idle_times(0)
        , _cntl(cntl)
    {}

    int on_received_messages(brpc::StreamId /*id*/,
                             butil::IOBuf *const messages[],
                             size_t size) override {
        if (_cntl && _cntl->block) {
            while (_cntl->block) {
                usleep(100);
            }
        }
        for (size_t i = 0; i < size; ++i) {
            CHECK(messages[i]->length() == sizeof(int));
            int network = 0;
            messages[i]->cutn(&network, sizeof(int));
            EXPECT_EQ((int)ntohl(network), _expected_next_value++);
        }
        return 0;
    }

    void on_idle_timeout(brpc::StreamId /*id*/) override {
        ++_idle_times;
    }

    void on_closed(brpc::StreamId /*id*/) override {
        ASSERT_FALSE(_stopped);
        _stopped = true;
    }

    void on_failed(brpc::StreamId id, int error_code,
                   const std::string& /*error_text*/) override {
        ASSERT_FALSE(_failed);
        ASSERT_NE(0, error_code);
        _failed = true;
    }

    bool failed() const { return _failed; }
    bool stopped() const { return _stopped; }
    int idle_times() const { return _idle_times; }
private:
    int _expected_next_value;
    bool _failed;
    bool _stopped;
    int _idle_times;
    HandlerControl* _cntl;
};

TEST_F(StreamingRpcTest, received_in_order) {
    OrderedInputHandler handler;
    brpc::StreamOptions opt;
    opt.handler = &handler;
    opt.messages_in_batch = 100;
    brpc::Server server;
    MyServiceWithStream service(opt);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));
    brpc::Controller cntl;
    brpc::StreamId request_stream;
    brpc::StreamOptions request_stream_options;
    request_stream_options.max_buf_size = 0;
    ASSERT_EQ(0, StreamCreate(&request_stream, cntl, &request_stream_options));
    brpc::ScopedStream stream_guard(request_stream);
    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText() << " request_stream=" << request_stream;
    const int N = 10000;
    for (int i = 0; i < N; ++i) {
        int network = htonl(i);
        butil::IOBuf out;
        out.append(&network, sizeof(network));
        ASSERT_EQ(0, brpc::StreamWrite(request_stream, out)) << "i=" << i;
    }
    ASSERT_EQ(0, brpc::StreamClose(request_stream));
    server.Stop(0);
    server.Join();
    while (!handler.stopped()) {
        usleep(100);
    }
    ASSERT_FALSE(handler.failed());
    ASSERT_EQ(0, handler.idle_times());
    ASSERT_EQ(N, handler._expected_next_value);
}

void on_writable(brpc::StreamId, void* arg, int error_code) {
    std::pair<bool, int>* p = (std::pair<bool, int>*)arg;
    p->first = true;
    p->second = error_code;
    LOG(INFO) << "error_code=" << error_code;
}

TEST_F(StreamingRpcTest, block) {
    HandlerControl hc;
    hc.block = true;
    OrderedInputHandler handler(&hc);
    brpc::StreamOptions opt;
    opt.handler = &handler;
    const int N = 10000;
    opt.max_buf_size = sizeof(uint32_t) *  N;
    brpc::Server server;
    MyServiceWithStream service(opt);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));
    brpc::Controller cntl;
    brpc::StreamId request_stream;
    brpc::ScopedStream stream_guard(request_stream);
    brpc::StreamOptions request_stream_options;
    request_stream_options.max_buf_size = sizeof(uint32_t) * N;
    ASSERT_EQ(0, StreamCreate(&request_stream, cntl, &request_stream_options));
    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText() << " request_stream="
                                << request_stream;
    for (int i = 0; i < N; ++i) {
        int network = htonl(i);
        butil::IOBuf out;
        out.append(&network, sizeof(network));
        ASSERT_EQ(0, brpc::StreamWrite(request_stream, out)) << "i=" << i;
    }
    // sync wait
    int dummy = 102030123;
    butil::IOBuf out;
    out.append(&dummy, sizeof(dummy));
    ASSERT_EQ(EAGAIN, brpc::StreamWrite(request_stream, out));
    hc.block = false;
    ASSERT_EQ(0, brpc::StreamWait(request_stream, NULL));
    // wait flushing all the pending messages
    while (handler._expected_next_value != N) {
        usleep(100);
    }
    // block hanlder again to test async wait
    hc.block = true;
    // async wait
    for (int i = N; i < N + N; ++i) {
        int network = htonl(i);
        butil::IOBuf out;
        out.append(&network, sizeof(network));
        ASSERT_EQ(0, brpc::StreamWrite(request_stream, out)) << "i=" << i;
    }
    out.clear();
    out.append(&dummy, sizeof(dummy));
    ASSERT_EQ(EAGAIN, brpc::StreamWrite(request_stream, out));
    hc.block = false;
    std::pair<bool, int> p = std::make_pair(false, 0);
    usleep(10);
    brpc::StreamWait(request_stream, NULL, on_writable, &p);
    while (!p.first) {
        usleep(100);
    }
    ASSERT_EQ(0, p.second);

    // wait flushing all the pending messages
    while (handler._expected_next_value != N + N) {
        usleep(100);
    }
    usleep(1000);

    LOG(INFO) << "Starting block";
    hc.block = true;
    for (int i = N + N; i < N + N + N; ++i) {
        int network = htonl(i);
        butil::IOBuf out;
        out.append(&network, sizeof(network));
        ASSERT_EQ(0, brpc::StreamWrite(request_stream, out)) << "i=" << i - N - N;
    }
    out.clear();
    out.append(&dummy, sizeof(dummy));
    ASSERT_EQ(EAGAIN, brpc::StreamWrite(request_stream, out));
    timespec duetime = butil::microseconds_from_now(1);
    p.first = false;
    LOG(INFO) << "Start wait";
    brpc::StreamWait(request_stream, &duetime, on_writable, &p);
    while (!p.first) {
        usleep(100);
    }
    ASSERT_TRUE(p.first);
    EXPECT_EQ(ETIMEDOUT, p.second);
    hc.block = false;
    ASSERT_EQ(0, brpc::StreamClose(request_stream));
    while (!handler.stopped()) {
        usleep(100);
    }

    ASSERT_FALSE(handler.failed());
    ASSERT_EQ(0, handler.idle_times());
    ASSERT_EQ(N + N + N, handler._expected_next_value);
}

TEST_F(StreamingRpcTest, auto_close_if_host_socket_closed) {
    HandlerControl hc;
    hc.block = true;
    OrderedInputHandler handler(&hc);
    brpc::StreamOptions opt;
    opt.handler = &handler;
    const int N = 10000;
    opt.max_buf_size = sizeof(uint32_t) *  N;
    brpc::Server server;
    MyServiceWithStream service(opt);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));
    brpc::Controller cntl;
    brpc::StreamId request_stream;
    brpc::StreamOptions request_stream_options;
    request_stream_options.max_buf_size = sizeof(uint32_t) * N;
    ASSERT_EQ(0, StreamCreate(&request_stream, cntl, &request_stream_options));
    brpc::ScopedStream stream_guard(request_stream);
    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText() << " request_stream=" << request_stream;

    {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(request_stream, &ptr));
        brpc::Stream* s = (brpc::Stream*)ptr->conn();
        ASSERT_TRUE(s->_host_socket != NULL);
        s->_host_socket->SetFailed();
    }

    usleep(100);
    butil::IOBuf out;
    out.append("test");
    ASSERT_EQ(EINVAL, brpc::StreamWrite(request_stream, out));
    while (!handler.stopped()) {
        usleep(100);
    }
    ASSERT_TRUE(handler.failed());
    ASSERT_EQ(0, handler.idle_times());
    ASSERT_EQ(0, handler._expected_next_value);
}

TEST_F(StreamingRpcTest, failed_when_rst) {
    OrderedInputHandler handler;
    brpc::StreamOptions opt;
    opt.handler = &handler;
    opt.messages_in_batch = 100;
    brpc::Server server;
    MyServiceWithStream service(opt);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));
    brpc::Controller cntl;
    brpc::StreamId request_stream;
    brpc::StreamOptions request_stream_options;
    request_stream_options.max_buf_size = 0;
    ASSERT_EQ(0, StreamCreate(&request_stream, cntl, &request_stream_options));
    brpc::ScopedStream stream_guard(request_stream);
    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText() << " request_stream=" << request_stream;
    const int N = 10000;
    for (int i = 0; i < N; ++i) {
        int network = htonl(i);
        butil::IOBuf out;
        out.append(&network, sizeof(network));
        ASSERT_EQ(0, brpc::StreamWrite(request_stream, out)) << "i=" << i;
    }

    usleep(1000 * 10);
    {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, brpc::Socket::Address(request_stream, &ptr));
        brpc::Stream* s = (brpc::Stream*)ptr->conn();
        ASSERT_TRUE(s->_host_socket != NULL);
        brpc::policy::SendStreamRst(s->_host_socket,
                                    s->_remote_settings.stream_id());
    }
    // ASSERT_EQ(0, brpc::StreamClose(request_stream));
    server.Stop(0);
    server.Join();
    while (!handler.stopped() && !handler.failed()) {
        usleep(100);
    }
    ASSERT_TRUE(handler.failed());
    ASSERT_EQ(0, handler.idle_times());
    ASSERT_EQ(N, handler._expected_next_value);
}

TEST_F(StreamingRpcTest, idle_timeout) {
    HandlerControl hc;
    hc.block = true;
    OrderedInputHandler handler(&hc);
    brpc::StreamOptions opt;
    opt.handler = &handler;
    opt.idle_timeout_ms = 2;
    const int N = 10000;
    opt.max_buf_size = sizeof(uint32_t) *  N;
    brpc::Server server;
    MyServiceWithStream service(opt);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));
    brpc::Controller cntl;
    brpc::StreamId request_stream;
    brpc::StreamOptions request_stream_options;
    request_stream_options.max_buf_size = sizeof(uint32_t) * N;
    ASSERT_EQ(0, StreamCreate(&request_stream, cntl, &request_stream_options));
    brpc::ScopedStream stream_guard(request_stream);
    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText() << " request_stream=" << request_stream;
    usleep(10 * 1000 + 800);
    ASSERT_EQ(0, brpc::StreamClose(request_stream));
    while (!handler.stopped()) {
        usleep(100);
    }
    ASSERT_FALSE(handler.failed());
//    ASSERT_TRUE(handler.idle_times() >= 4 && handler.idle_times() <= 6)
//               << handler.idle_times();
    ASSERT_EQ(0, handler._expected_next_value);
}

class PingPongHandler : public brpc::StreamInputHandler {
public:
    explicit PingPongHandler()
        : _expected_next_value(0)
        , _failed(false)
        , _stopped(false)
        , _idle_times(0)
    {
    }
    int on_received_messages(brpc::StreamId id,
                             butil::IOBuf *const messages[],
                             size_t size) override {
        if (size != 1) {
            _failed = true;
            return 0;
        }
        for (size_t i = 0; i < size; ++i) {
            CHECK(messages[i]->length() == sizeof(int));
            int network = 0;
            messages[i]->cutn(&network, sizeof(int));
            if ((int)ntohl(network) != _expected_next_value) {
                _failed = true;
            }
            int send_back = ntohl(network) + 1;
            _expected_next_value = send_back + 1;
            butil::IOBuf out;
            network = htonl(send_back);
            out.append(&network, sizeof(network));
            // don't care the return value
            brpc::StreamWrite(id, out);
        }
        return 0;
    }

    void on_idle_timeout(brpc::StreamId /*id*/) override {
        ++_idle_times;
    }

    void on_closed(brpc::StreamId /*id*/) override {
        ASSERT_FALSE(_stopped);
        _stopped = true;
    }


    void on_failed(brpc::StreamId id, int error_code,
                   const std::string& /*error_text*/) override {
        ASSERT_FALSE(_failed);
        ASSERT_NE(0, error_code);
        _failed = true;
    }

    bool failed() const { return _failed; }
    bool stopped() const { return _stopped; }
    int idle_times() const { return _idle_times; }
private:
    int _expected_next_value;
    bool _failed;
    bool _stopped;
    int _idle_times;
};

TEST_F(StreamingRpcTest, ping_pong) {
    PingPongHandler resh;
    brpc::StreamOptions opt;
    opt.handler = &resh;
    const int N = 10000;
    opt.max_buf_size = sizeof(uint32_t) *  N;
    brpc::Server server;
    MyServiceWithStream service(opt);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));
    brpc::Controller cntl;
    brpc::StreamId request_stream;
    brpc::StreamOptions request_stream_options;
    PingPongHandler reqh;
    reqh._expected_next_value = 1;
    request_stream_options.handler = &reqh;
    request_stream_options.max_buf_size = sizeof(uint32_t) * N;
    ASSERT_EQ(0, StreamCreate(&request_stream, cntl, &request_stream_options));
    brpc::ScopedStream stream_guard(request_stream);
    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText() << " request_stream=" << request_stream;
    int send = 0;
    butil::IOBuf out;
    out.append(&send, sizeof(send));
    ASSERT_EQ(0, brpc::StreamWrite(request_stream, out));
    usleep(10 * 1000);
    ASSERT_EQ(0, brpc::StreamClose(request_stream));
    while (!resh.stopped() || !reqh.stopped()) {
        usleep(100);
    }
    ASSERT_FALSE(resh.failed());
    ASSERT_FALSE(reqh.failed());
    ASSERT_EQ(0, resh.idle_times());
    ASSERT_EQ(0, reqh.idle_times());
}

class SendNAfterAcceptStream : public AfterAcceptStream {
public:
    explicit SendNAfterAcceptStream(int n)
        : _n(n) {}
    void action(brpc::StreamId s) {
        for (int i = 0; i < _n; ++i) {
            int network = htonl(i);
            butil::IOBuf out;
            out.append(&network, sizeof(network));
            ASSERT_EQ(0, brpc::StreamWrite(s, out)) << "i=" << i;
        }
    }
private:
    int _n;
};

TEST_F(StreamingRpcTest, server_send_data_before_run_done) {
    const int N = 10000;
    SendNAfterAcceptStream after_accept(N);
    brpc::StreamOptions opt;
    opt.max_buf_size = -1;
    brpc::Server server;
    MyServiceWithStream service(opt, &after_accept);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));
    OrderedInputHandler handler;
    brpc::StreamOptions request_stream_options;
    request_stream_options.handler = &handler;
    brpc::StreamId request_stream;
    brpc::Controller cntl;
    ASSERT_EQ(0, StreamCreate(&request_stream, cntl, &request_stream_options));
    brpc::ScopedStream stream_guard(request_stream);
    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText() << " request_stream=" << request_stream;
    // wait flushing all the pending messages
    while (handler._expected_next_value != N) {
        usleep(100);
    }
    ASSERT_EQ(0, brpc::StreamClose(request_stream));
    while (!handler.stopped()) {
        usleep(100);
    }
    ASSERT_FALSE(handler.failed());
    ASSERT_EQ(0, handler.idle_times());
}

TEST_F(StreamingRpcTest, segment_stream_data_automatically) {
    GFLAGS_NAMESPACE::SetCommandLineOption("stream_write_max_segment_size", "1");
    OrderedInputHandler handler;
    brpc::StreamOptions opt;
    opt.handler = &handler;
    opt.messages_in_batch = 100;
    brpc::Server server;
    MyServiceWithStream service(opt);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));
    brpc::Controller cntl;
    brpc::StreamId request_stream;
    brpc::StreamOptions request_stream_options;
    ASSERT_EQ(0, StreamCreate(&request_stream, cntl, &request_stream_options));
    brpc::ScopedStream stream_guard(request_stream);
    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText() << " request_stream=" << request_stream;
    const int N = 1000;
    for (int i = 0; i < N; ++i) {
        int network = htonl(i);
        butil::IOBuf out;
        out.append(&network, sizeof(network));
        ASSERT_EQ(0, brpc::StreamWrite(request_stream, out)) << "i=" << i;
    }

    brpc::SocketUniquePtr host_socket_ptr;
    {
      brpc::SocketUniquePtr ptr;
      ASSERT_EQ(0, brpc::Socket::Address(request_stream, &ptr));
      brpc::Stream *s = (brpc::Stream *)ptr->conn();
      ASSERT_TRUE(s->_host_socket != NULL);
      s->_host_socket->ReAddress(&host_socket_ptr);
    }

    ASSERT_EQ(0, brpc::StreamClose(request_stream));
    server.Stop(0);
    server.Join();
    while (!handler.stopped()) {
        usleep(100);
    }
    const int64_t now_ms = butil::cpuwide_time_ms();
    host_socket_ptr->UpdateStatsEverySecond(now_ms);
    brpc::SocketStat stat;
    host_socket_ptr->GetStat(&stat);
    ASSERT_LT(N * sizeof(N), stat.out_num_messages_m);
    ASSERT_FALSE(handler.failed());
    ASSERT_EQ(0, handler.idle_times());
    ASSERT_EQ(N, handler._expected_next_value);
    GFLAGS_NAMESPACE::SetCommandLineOption("stream_write_max_segment_size", "536870912");
}
