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
#include <atomic>
#include <unordered_map>
#include "brpc/server.h"

#include "brpc/controller.h"
#include "brpc/channel.h"
#include "brpc/callback.h"
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
        brpc::ClosureGuard done_guard(done);
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

struct BatchStreamFeedbackRaceState {
    brpc::StreamId server_first_stream_id{brpc::INVALID_STREAM_ID};
    brpc::StreamId server_extra_stream_id{brpc::INVALID_STREAM_ID};
    brpc::StreamId client_extra_stream_id{brpc::INVALID_STREAM_ID};

    std::atomic<int> server_first_write_rc{-1};
    std::atomic<int> server_second_write_rc{-1};
    std::atomic<bool> client_got_first_msg{false};
    std::atomic<bool> client_got_second_msg{false};
    std::atomic<bool> server_write_done{false};
    std::atomic<bool> rpc_done{false};
    std::atomic<int> client_closed_count{0};

    bthread_t server_send_tid{0};
    std::atomic<bool> server_send_started{false};
};

class BatchStreamClientHandler : public brpc::StreamInputHandler {
public:
    explicit BatchStreamClientHandler(BatchStreamFeedbackRaceState* state)
        : _state(state) {}

    int on_received_messages(brpc::StreamId id,
                             butil::IOBuf* const messages[],
                             size_t size) override {
        if (id != _state->client_extra_stream_id) {
            // This test only cares about extra stream in batch creation.
            return 0;
        }
        for (size_t i = 0; i < size; ++i) {
            const size_t len = messages[i]->length();
            messages[i]->clear();
            // First payload: 64 bytes. Second payload: 1 byte.
            if (len == 64) {
                _state->client_got_first_msg.store(true, std::memory_order_release);
            } else if (len == 1) {
                _state->client_got_second_msg.store(true, std::memory_order_release);
            }
        }
        return 0;
    }

    void on_idle_timeout(brpc::StreamId /*id*/) override {}

    void on_closed(brpc::StreamId /*id*/) override {
        _state->client_closed_count.fetch_add(1, std::memory_order_release);
    }

    void on_failed(brpc::StreamId /*id*/, int /*error_code*/, const std::string& /*error_text*/) override {}

private:
    BatchStreamFeedbackRaceState* _state;
};

static void* SendTwoMessagesOnServerExtraStream(void* arg) {
    auto* state = static_cast<BatchStreamFeedbackRaceState*>(arg);
    const brpc::StreamId sid = state->server_extra_stream_id;

    // Wait until server-side stream is connected.
    const int64_t connect_deadline_us = butil::gettimeofday_us() + 2 * 1000 * 1000L;
    bool connected = false;
    while (butil::gettimeofday_us() < connect_deadline_us) {
        brpc::SocketUniquePtr ptr;
        if (brpc::Socket::Address(sid, &ptr) == 0) {
            brpc::Stream* s = static_cast<brpc::Stream*>(ptr->conn());
            if (s->_host_socket != NULL && s->_connected) {
                connected = true;
                break;
            }
        }
        usleep(1000);
    }

    if (!connected) {
        state->server_first_write_rc.store(ETIMEDOUT, std::memory_order_relaxed);
        state->server_second_write_rc.store(ETIMEDOUT, std::memory_order_relaxed);
        state->server_write_done.store(true, std::memory_order_release);
        return NULL;
    }

    // 1) Send a payload exactly equal to max_buf_size(64).
    {
        std::string payload(64, 'a');
        butil::IOBuf out;
        out.append(payload);
        state->server_first_write_rc.store(brpc::StreamWrite(sid, out), std::memory_order_relaxed);
    }

    // 2) Then send another byte. This write should become writable only after
    // client sends FEEDBACK with consumed_size >= 64.
    const int64_t write_deadline_us = butil::gettimeofday_us() + 2 * 1000 * 1000L;
    int rc = -1;
    while (butil::gettimeofday_us() < write_deadline_us) {
        butil::IOBuf out;
        out.append("b", 1);
        rc = brpc::StreamWrite(sid, out);
        if (rc == 0) {
            break;
        }
        if (rc != EAGAIN) {
            break;
        }
        const timespec duetime = butil::milliseconds_from_now(100);
        (void)brpc::StreamWait(sid, &duetime);
    }
    state->server_second_write_rc.store(rc, std::memory_order_relaxed);
    state->server_write_done.store(true, std::memory_order_release);
    return NULL;
}

class MyServiceWithBatchStream : public test::EchoService {
public:
    MyServiceWithBatchStream(const brpc::StreamOptions& options,
                             BatchStreamFeedbackRaceState* state)
        : _options(options), _state(state) {}

    void Echo(::google::protobuf::RpcController* controller,
              const ::test::EchoRequest* request,
              ::test::EchoResponse* response,
              ::google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        response->set_message(request->message());
        brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);

        brpc::StreamIds response_streams;
        ASSERT_EQ(0, brpc::StreamAccept(response_streams, *cntl, &_options));
        ASSERT_EQ(2u, response_streams.size());
        _state->server_first_stream_id = response_streams[0];
        _state->server_extra_stream_id = response_streams[1];

        bthread_t tid;
        ASSERT_EQ(0, bthread_start_background(
                         &tid, &BTHREAD_ATTR_NORMAL,
                         SendTwoMessagesOnServerExtraStream, _state));
        _state->server_send_tid = tid;
        _state->server_send_started.store(true, std::memory_order_release);
    }

private:
    brpc::StreamOptions _options;
    BatchStreamFeedbackRaceState* _state;
};

static void SetAtomicTrue(std::atomic<bool>* f) {
    f->store(true, std::memory_order_release);
}

template <typename Pred>
static bool WaitForTrue(Pred pred, int timeout_ms) {
    const int64_t deadline_us = butil::gettimeofday_us() + (int64_t)timeout_ms * 1000L;
    while (!pred() && butil::gettimeofday_us() < deadline_us) {
        usleep(1000);
    }
    return pred();
}

static bool WaitForTrue(const std::atomic<bool>& f, int timeout_ms) {
    return WaitForTrue([&f]() { return f.load(std::memory_order_acquire); }, timeout_ms);
}

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

TEST_F(StreamingRpcTest, batch_create_stream_feedback_race) {
    BatchStreamFeedbackRaceState state;
    BatchStreamClientHandler client_handler(&state);

    brpc::StreamOptions server_stream_opt;
    // Make server-side sender sensitive to FEEDBACK quickly.
    server_stream_opt.max_buf_size = 16;

    brpc::Server server;
    MyServiceWithBatchStream service(server_stream_opt, &state);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));

    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));

    brpc::Controller cntl;
    brpc::StreamIds request_streams;
    brpc::StreamOptions client_stream_opt;
    client_stream_opt.handler = &client_handler;
    client_stream_opt.max_buf_size = 0;
    ASSERT_EQ(0, brpc::StreamCreate(request_streams, 2, cntl, &client_stream_opt));
    ASSERT_EQ(2u, request_streams.size());
    state.client_extra_stream_id = request_streams[1];

    // Block SetConnected() on the extra stream to enlarge the race window.
    brpc::SocketUniquePtr client_extra_ptr;
    ASSERT_EQ(0, brpc::Socket::Address(state.client_extra_stream_id, &client_extra_ptr));
    brpc::Stream* client_extra_stream = static_cast<brpc::Stream*>(client_extra_ptr->conn());
    bthread_mutex_lock(&client_extra_stream->_connect_mutex);
    struct UnlockGuard {
        bthread_mutex_t* m;
        ~UnlockGuard() {
            if (m) {
                bthread_mutex_unlock(m);
            }
        }
    } unlock_guard{&client_extra_stream->_connect_mutex};

    BRPC_SCOPE_EXIT {
        if (state.server_extra_stream_id != brpc::INVALID_STREAM_ID) {
            brpc::StreamClose(state.server_extra_stream_id);
        }
        if (state.server_first_stream_id != brpc::INVALID_STREAM_ID) {
            brpc::StreamClose(state.server_first_stream_id);
        }
        for (auto sid : request_streams) {
            brpc::StreamClose(sid);
        }

        if (state.server_send_tid) {
            bthread_join(state.server_send_tid, NULL);
        }
        server.Stop(0);
        server.Join();

        // Release the SocketUniquePtr held above so the fake socket can be
        // recycled. Otherwise BeforeRecycle / on_closed for the extra stream
        // is deferred until `client_extra_ptr` destructs at scope exit, which
        // happens *after* `client_handler` and `state` are destroyed -> UAF
        // inside Stream::Consume on Linux.
        client_extra_ptr.reset();

        // on_closed() runs asynchronously on each client stream's consumer
        // bthread. Wait for both before letting handler/state go out of
        // scope, otherwise Stream::Consume will dereference freed memory.
        int expected_closed = request_streams.size();
        WaitForTrue([&state, expected_closed]() {
            return state.client_closed_count.load(std::memory_order_acquire)
                   >= expected_closed;
        }, 2000);
    };

    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, brpc::NewCallback(SetAtomicTrue, &state.rpc_done));

    // Wait until client consumes the first 64B payload on extra stream.
    ASSERT_TRUE(WaitForTrue(state.client_got_first_msg, 2000));

    // Unblock SetConnected(); the fix in PR 3215 should send the first FEEDBACK
    // with consumed_size=64 here, making server-side stream writable again.
    bthread_mutex_unlock(&client_extra_stream->_connect_mutex);
    unlock_guard.m = NULL;

    ASSERT_TRUE(WaitForTrue(state.rpc_done, 2000));
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();

    // Wait for server-side send thread to be started.
    ASSERT_TRUE(WaitForTrue(state.server_send_started, 2000));

    ASSERT_TRUE(WaitForTrue(state.server_write_done, 2000));
    ASSERT_EQ(0, state.server_first_write_rc.load(std::memory_order_relaxed));
    ASSERT_EQ(0, state.server_second_write_rc.load(std::memory_order_relaxed));
    ASSERT_TRUE(WaitForTrue(state.client_got_second_msg, 2000));
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
    // wait flushing all the pending messages
    while (handler._expected_next_value != N) {
        usleep(100);
    }
    // block hanlder again to test async wait
    hc.block = true;
    // async wait
    for (int i = N; i < N + N; ++i) {
        ASSERT_EQ(0, brpc::StreamWait(request_stream, NULL));
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

    while (handler._expected_next_value != N) {
        usleep(100);
    }
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
    int on_received_messages(brpc::StreamId id,
                             butil::IOBuf *const messages[],
                             size_t size) override {
        if (size != 1) {
            LOG(INFO) << "size=" << size;
            _error = true;
            return 0;
        }
        for (size_t i = 0; i < size; ++i) {
            CHECK(messages[i]->length() == sizeof(int));
            int network = 0;
            messages[i]->cutn(&network, sizeof(int));
            if ((int)ntohl(network) != _expected_next_value) {
                _error = true;
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

    bool error() const { return _error; }
    bool failed() const { return _failed; }
    bool stopped() const { return _stopped; }
    int idle_times() const { return _idle_times; }
private:
    int _expected_next_value{0};
    bool _error{false};
    bool _failed{false};
    bool _stopped{false};
    int _idle_times{0};
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
    ASSERT_FALSE(resh.error());
    ASSERT_FALSE(reqh.error());
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

TEST_F(StreamingRpcTest, create_request_stream_twice_on_same_controller_returns_error) {
    brpc::Controller cntl;

    brpc::StreamId first_stream = brpc::INVALID_STREAM_ID;
    ASSERT_EQ(0, brpc::StreamCreate(&first_stream, cntl, NULL));
    brpc::ScopedStream stream_guard(first_stream);

    brpc::StreamId second_stream = brpc::INVALID_STREAM_ID;
    ASSERT_EQ(-1, brpc::StreamCreate(&second_stream, cntl, NULL));
    ASSERT_EQ(brpc::INVALID_STREAM_ID, second_stream);
}

// Handler that tracks per-stream reception so a single handler instance can be
// shared by all streams created in one batch (StreamCreate uses the same
// StreamOptions, thus the same handler, for every stream).
class MultiStreamHandler : public brpc::StreamInputHandler {
public:
    int on_received_messages(brpc::StreamId id,
                             butil::IOBuf* const messages[],
                             size_t size) override {
        std::unique_lock<bthread::Mutex> lck(_mu);
        for (size_t i = 0; i < size; ++i) {
            if (messages[i]->length() != sizeof(int)) {
                _error = true;
                continue;
            }
            int network = 0;
            messages[i]->cutn(&network, sizeof(int));
            const int value = (int)ntohl(network);
            // Each stream should receive a strictly increasing sequence 0..N-1.
            if (value != _expected[id]) {
                _error = true;
            }
            ++_expected[id];
            ++_received[id];
        }
        return 0;
    }

    void on_idle_timeout(brpc::StreamId /*id*/) override {}

    void on_closed(brpc::StreamId id) override {
        std::unique_lock<bthread::Mutex> lck(_mu);
        ++_closed[id];
    }

    void on_failed(brpc::StreamId /*id*/, int /*error_code*/,
                   const std::string& /*error_text*/) override {
        std::unique_lock<bthread::Mutex> lck(_mu);
        _failed = true;
    }

    size_t received(brpc::StreamId id) {
        std::unique_lock<bthread::Mutex> lck(_mu);
        return _received[id];
    }
    size_t closed(brpc::StreamId id) {
        std::unique_lock<bthread::Mutex> lck(_mu);
        return _closed[id];
    }
    size_t total_received() {
        std::unique_lock<bthread::Mutex> lck(_mu);
        size_t sum = 0;
        for (const auto& kv : _received) {
            sum += kv.second;
        }
        return sum;
    }
    size_t num_streams() {
        std::unique_lock<bthread::Mutex> lck(_mu);
        return _received.size();
    }
    bool error() {
        std::unique_lock<bthread::Mutex> lck(_mu);
        return _error;
    }
    bool failed() {
        std::unique_lock<bthread::Mutex> lck(_mu);
        return _failed;
    }

private:
    bthread::Mutex _mu;
    std::unordered_map<brpc::StreamId, int> _expected;
    std::unordered_map<brpc::StreamId, size_t> _received;
    std::unordered_map<brpc::StreamId, size_t> _closed;
    bool _error{false};
    bool _failed{false};
};

// Server that accepts a batch of streams and sends N ordered ints on every
// accepted stream, including the extra streams.
class MyServiceWithExtraStream : public test::EchoService {
public:
    MyServiceWithExtraStream(const brpc::StreamOptions& options,
                             size_t stream_count, int n)
        : _options(options), _stream_count(stream_count), _n(n) {}

    void Echo(::google::protobuf::RpcController* controller,
              const ::test::EchoRequest* request,
              ::test::EchoResponse* response,
              ::google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
        response->set_message(request->message());

        brpc::StreamIds response_streams;
        ASSERT_EQ(0, brpc::StreamAccept(response_streams, *cntl, &_options));
        ASSERT_EQ(_stream_count, response_streams.size());

        // Send ordered ints on each accepted stream.
        for (size_t s = 0; s < response_streams.size(); ++s) {
            for (int i = 0; i < _n; ++i) {
                int network = htonl(i);
                butil::IOBuf out;
                out.append(&network, sizeof(network));
                ASSERT_EQ(0, brpc::StreamWrite(response_streams[s], out))
                    << "stream_index=" << s << " i=" << i;
            }
        }
    }

private:
    brpc::StreamOptions _options;
    size_t _stream_count;
    int _n;
};

TEST_F(StreamingRpcTest, batch_create_extra_stream) {
    const size_t STREAM_COUNT = 3;  // 1 first stream + 2 extra streams
    const int N = 1000;

    MultiStreamHandler handler;

    brpc::StreamOptions server_stream_opt;
    server_stream_opt.max_buf_size = -1;

    brpc::Server server;
    MyServiceWithExtraStream service(server_stream_opt, STREAM_COUNT, N);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));

    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));

    brpc::Controller cntl;
    brpc::StreamOptions client_stream_opt;
    client_stream_opt.handler = &handler;
    brpc::StreamIds request_streams;
    ASSERT_EQ(0, brpc::StreamCreate(request_streams, STREAM_COUNT, cntl,
                                    &client_stream_opt));
    ASSERT_EQ(STREAM_COUNT, request_streams.size());

    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();

    // Every stream, including the extra streams, must end up connected with a valid host_socket.
    for (size_t i = 0; i < request_streams.size(); ++i) {
        const brpc::StreamId sid = request_streams[i];
        ASSERT_TRUE(WaitForTrue([sid]() {
            brpc::SocketUniquePtr ptr;
            if (brpc::Socket::Address(sid, &ptr) != 0) {
                return false;
            }
            brpc::Stream* s = static_cast<brpc::Stream*>(ptr->conn());
            return s->_host_socket != NULL &&
                   s->_connected.load(butil::memory_order_acquire);
        }, 5000)) << "stream_index=" << i;
    }

    // Every stream (first + extra) should receive all N ordered messages.
    for (size_t i = 0; i < request_streams.size(); ++i) {
        const brpc::StreamId sid = request_streams[i];
        ASSERT_TRUE(WaitForTrue([&handler, sid, N]() {
            return handler.received(sid) >= N;
        }, 5000))
            << "stream_index=" << i << " received=" << handler.received(sid);
        ASSERT_EQ(N, handler.received(sid)) << "stream_index=" << i;
    }
    ASSERT_FALSE(handler.error());
    ASSERT_FALSE(handler.failed());

    for (auto sid : request_streams) {
        brpc::StreamClose(sid);
    }
    server.Stop(0);
    server.Join();

    // Wait for on_closed() of every stream so the handler outlives the
    // asynchronous consumer bthreads.
    for (size_t i = 0; i < request_streams.size(); ++i) {
        const brpc::StreamId sid = request_streams[i];
        ASSERT_TRUE(WaitForTrue([&handler, sid]() {
            return handler.closed(sid) >= 1;
        }, 2000));
    }
}

// Regression test for extra streams that only carry *upstream* data: the server
// accepts the batch of streams but never sends anything downstream, so the
// client's extra streams can NOT obtain their host_socket via Stream::OnReceived.
// This forces Controller::HandleStreamConnection to set the extra streams'
// host_socket itself. If it (incorrectly) sets host_socket on the first stream
// instead (s->SetHostSocket, which is a no-op because the first stream's
// host_socket was already set while receiving the RPC response),
// Stream::SetConnected() CHECK-fails on every extra stream here.
TEST_F(StreamingRpcTest, batch_create_extra_stream_upstream_only) {
    const size_t STREAM_COUNT = 3;  // 1 first stream + 2 extra streams
    const int N = 1000;

    MultiStreamHandler server_handler;

    brpc::StreamOptions server_stream_opt;
    server_stream_opt.handler = &server_handler;  // receive upstream data
    server_stream_opt.max_buf_size = 0;

    brpc::Server server;
    // n = 0: server never sends downstream data on any accepted stream.
    MyServiceWithExtraStream service(server_stream_opt, STREAM_COUNT, 0);
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start(9007, NULL));

    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1:9007", NULL));

    brpc::Controller cntl;
    // No downstream handler is needed on the client side.
    brpc::StreamOptions client_stream_opt;
    client_stream_opt.max_buf_size = -1;
    brpc::StreamIds request_streams;
    ASSERT_EQ(0, brpc::StreamCreate(request_streams, STREAM_COUNT, cntl,
                                    &client_stream_opt));
    ASSERT_EQ(STREAM_COUNT, request_streams.size());

    test::EchoService_Stub stub(&channel);
    stub.Echo(&cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();

    // Without any downstream data, extra streams have no chance to get their
    // host_socket set via OnReceived. They must still become connected, which
    // only holds if HandleStreamConnection sets host_socket on the extra
    // streams themselves.
    for (size_t i = 0; i < request_streams.size(); ++i) {
        const brpc::StreamId sid = request_streams[i];
        ASSERT_TRUE(WaitForTrue([sid]() {
            brpc::SocketUniquePtr ptr;
            if (brpc::Socket::Address(sid, &ptr) != 0) {
                return false;
            }
            brpc::Stream* s = static_cast<brpc::Stream*>(ptr->conn());
            return s->_host_socket != NULL &&
                   s->_connected.load(butil::memory_order_acquire);
        }, 5000)) << "stream_index=" << i;
    }

    // Push ordered ints upstream on every stream (first + extra).
    for (size_t i = 0; i < request_streams.size(); ++i) {
        for (int j = 0; j < N; ++j) {
            int network = htonl(j);
            butil::IOBuf out;
            out.append(&network, sizeof(network));
            ASSERT_EQ(0, brpc::StreamWrite(request_streams[i], out))
                << "stream_index=" << i << " j=" << j;
        }
    }

    // The server must receive all N ordered messages on each of the accepted
    // streams (kStreamCount streams in total). Server-side stream ids are not
    // known here, so verify the aggregate.
    const size_t TOTAL = STREAM_COUNT * N;
    ASSERT_TRUE(WaitForTrue([&server_handler, TOTAL]() {
        return server_handler.total_received() >= TOTAL;
    }, 5000)) << "total_received=" << server_handler.total_received();
    ASSERT_EQ(TOTAL, server_handler.total_received());
    ASSERT_EQ(STREAM_COUNT, server_handler.num_streams());
    ASSERT_FALSE(server_handler.error());
    ASSERT_FALSE(server_handler.failed());

    for (auto sid : request_streams) {
        brpc::StreamClose(sid);
    }
    server.Stop(0);
    server.Join();
}
