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


#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/grpc.h"
#include "bthread/bthread.h"
#include "butil/time.h"
#include "grpc.pb.h"

namespace brpc {
namespace policy {
DECLARE_int32(h2_client_stream_window_size);
DECLARE_int32(h2_client_connection_window_size);
} // namespace policy
} // namespace brpc

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    if (GFLAGS_NAMESPACE::SetCommandLineOption("http_body_compress_threshold", "0").empty()) {
        std::cerr << "Fail to set -crash_on_fatal_log" << std::endl;
        return -1;
    }
    if (GFLAGS_NAMESPACE::SetCommandLineOption("crash_on_fatal_log", "true").empty()) {
        std::cerr << "Fail to set -crash_on_fatal_log" << std::endl;
        return -1;
    }
    return RUN_ALL_TESTS();
}

namespace {

const std::string g_prefix = "Hello, ";
const std::string g_req = "wyt";
const int64_t g_timeout_ms = 1000;
const std::string g_protocol = "h2:grpc";

class MyGrpcService : public ::test::GrpcService {
public:
    void Method(::google::protobuf::RpcController* cntl_base,
                const ::test::GrpcRequest* req,
                ::test::GrpcResponse* res,
                ::google::protobuf::Closure* done) {
        brpc::Controller* cntl =
                static_cast<brpc::Controller*>(cntl_base);
        brpc::ClosureGuard done_guard(done);

        EXPECT_EQ(g_req, req->message());
        if (req->gzip()) {
            cntl->set_response_compress_type(brpc::COMPRESS_TYPE_GZIP);
        }
        res->set_message(g_prefix + req->message());

        if (req->return_error()) {
            cntl->SetFailed(brpc::EINTERNAL, "%s", g_prefix.c_str());
            return;
        }
        if (req->has_timeout_us()) {
            if (req->timeout_us() < 0) {
                EXPECT_EQ(-1, cntl->deadline_us());
            } else {
                EXPECT_NEAR(cntl->deadline_us(),
                    butil::gettimeofday_us() + req->timeout_us(), 5000);
            }
        }
    }

    void MethodTimeOut(::google::protobuf::RpcController* cntl_base,
              const ::test::GrpcRequest* req,
              ::test::GrpcResponse* res,
              ::google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        bthread_usleep(2000000 /*2s*/);
        res->set_message(g_prefix + req->message());
        return;
    }
};

class WindowGrpcService : public ::test::GrpcService {
public:
    void Method(::google::protobuf::RpcController*,
                const ::test::GrpcRequest* req,
                ::test::GrpcResponse* res,
                ::google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        const std::string& msg = req->message();
        const std::string prefix = "resp_size:";
        if (msg.compare(0, prefix.size(), prefix) == 0) {
            size_t n = static_cast<size_t>(strtoull(msg.c_str() + prefix.size(), nullptr, 10));
            char fill = msg.back();
            if (fill >= '0' && fill <= '9') {
                fill = 'r';
            }
            res->set_message(std::string(n, fill));
        } else {
            res->set_message(msg);
        }
    }
};

class GrpcTest : public ::testing::Test {
protected:
    GrpcTest() {
        EXPECT_EQ(0, _server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
        EXPECT_EQ(0, _server.Start(0, nullptr));
        brpc::ChannelOptions options;
        options.protocol = g_protocol;
        options.timeout_ms = g_timeout_ms;
        EXPECT_EQ(0, _channel.Init(_server.listen_address(), &options));
    }

    virtual ~GrpcTest() {};
    virtual void SetUp() {};
    virtual void TearDown() {};

    void CallMethod(bool req_gzip, bool res_gzip) {
        test::GrpcRequest req;
        test::GrpcResponse res;
        brpc::Controller cntl;
        if (req_gzip) {
            cntl.set_request_compress_type(brpc::COMPRESS_TYPE_GZIP);
        }
        req.set_message(g_req);
        req.set_gzip(res_gzip);
        req.set_return_error(false);

        test::GrpcService_Stub stub(&_channel);
        stub.Method(&cntl, &req, &res, nullptr);
        EXPECT_FALSE(cntl.Failed()) << cntl.ErrorCode() << ": " << cntl.ErrorText();
        EXPECT_EQ(res.message(), g_prefix + g_req);
    }

    brpc::Server _server;
    MyGrpcService _svc;
    brpc::Channel _channel;
};

TEST_F(GrpcTest, percent_encode) {
    std::string out;
    std::string s1("abcdefg !@#$^&*()/");
    std::string s1_out("abcdefg%20%21%40%23%24%5e%26%2a%28%29%2f");
    brpc::PercentEncode(s1, &out);
    EXPECT_TRUE(out == s1_out) << s1_out << " vs " << out;

    char s2_buf[] = "\0\0%\33\35 brpc";
    std::string s2(s2_buf, sizeof(s2_buf) - 1);
    std::string s2_expected_out("%00%00%25%1b%1d%20brpc");
    brpc::PercentEncode(s2, &out);
    EXPECT_TRUE(out == s2_expected_out) << s2_expected_out << " vs " << out;
}

TEST_F(GrpcTest, percent_decode) {
    std::string out;
    std::string s1("abcdefg%20%21%40%23%24%5e%26%2a%28%29%2f");
    std::string s1_out("abcdefg !@#$^&*()/");
    brpc::PercentDecode(s1, &out);
    EXPECT_TRUE(out == s1_out) << s1_out << " vs " << out;

    std::string s2("%00%00%1b%1d%20brpc");
    char s2_expected_out_buf[] = "\0\0\33\35 brpc";
    std::string s2_expected_out(s2_expected_out_buf, sizeof(s2_expected_out_buf) - 1);
    brpc::PercentDecode(s2, &out);
    EXPECT_TRUE(out == s2_expected_out) << s2_expected_out << " vs " << out;
}

TEST_F(GrpcTest, sanity) {
    for (int i = 0; i < 2; ++i) { // if req use gzip or not
        for (int j = 0; j < 2; ++j) { // if res use gzip or not
            CallMethod(i, j);
        }
    }
}

TEST_F(GrpcTest, return_error) {
    test::GrpcRequest req;
    test::GrpcResponse res;
    brpc::Controller cntl;
    req.set_message(g_req);
    req.set_gzip(false);
    req.set_return_error(true);
    test::GrpcService_Stub stub(&_channel);
    stub.Method(&cntl, &req, &res, nullptr);
    EXPECT_TRUE(cntl.Failed());
    EXPECT_EQ(cntl.ErrorCode(), brpc::EINTERNAL);
    EXPECT_TRUE(butil::StringPiece(cntl.ErrorText()).ends_with(butil::string_printf("%s", g_prefix.c_str())));
}

TEST_F(GrpcTest, RpcTimedOut) {
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = g_protocol;
    options.timeout_ms = g_timeout_ms;
    EXPECT_EQ(0, channel.Init(_server.listen_address(), &options));

    test::GrpcRequest req;
    test::GrpcResponse res;
    brpc::Controller cntl;
    req.set_message(g_req);
    req.set_gzip(false);
    req.set_return_error(false);
    test::GrpcService_Stub stub(&_channel);
    stub.MethodTimeOut(&cntl, &req, &res, nullptr);
    EXPECT_TRUE(cntl.Failed());
    EXPECT_EQ(cntl.ErrorCode(), brpc::ERPCTIMEDOUT);
}

TEST_F(GrpcTest, MethodNotExist) {
    test::GrpcRequest req;
    test::GrpcResponse res;
    brpc::Controller cntl;
    req.set_message(g_req);
    req.set_gzip(false);
    req.set_return_error(false);
    test::GrpcService_Stub stub(&_channel);
    stub.MethodNotExist(&cntl, &req, &res, nullptr);
    EXPECT_TRUE(cntl.Failed());
    EXPECT_EQ(cntl.ErrorCode(), brpc::EINTERNAL);
    ASSERT_TRUE(butil::StringPiece(cntl.ErrorText()).ends_with("Method MethodNotExist() not implemented."));
}

TEST_F(GrpcTest, GrpcTimeOut) {
    const char* timeouts[] = {
        // valid case
        "2H", "7200000000",
        "3M", "180000000",
        "+1S", "1000000",
        "4m", "4000",
        "5u", "5",
        "6n", "1",
        // invalid case
        "30A", "-1",
        "123ASH", "-1",
        "HHHH", "-1",
        "112", "-1",
        "H999m", "-1",
        "", "-1",
        // out of range: more digits than the gRPC wire format allows, or
        // negative. The large ones overflowed the conversion to microseconds.
        "100000000S", "-1",
        "18446744073710S", "-1",
        "2562047789H", "-1",
        "9223372036854775807u", "-1",
        "9223372036854775807n", "-1",
        "-1S", "-1"
    };

    // test all timeout format
    for (size_t i = 0; i < arraysize(timeouts); i = i + 2) {
        test::GrpcRequest req;
        test::GrpcResponse res;
        brpc::Controller cntl;
        req.set_message(g_req);
        req.set_gzip(false);
        req.set_return_error(false);
        req.set_timeout_us((int64_t)(strtol(timeouts[i+1], nullptr, 10)));
        cntl.set_timeout_ms(-1);
        cntl.http_request().SetHeader("grpc-timeout", timeouts[i]);
        test::GrpcService_Stub stub(&_channel);
        stub.Method(&cntl, &req, &res, nullptr);
        EXPECT_FALSE(cntl.Failed());
    }

    // test timeout by using timeout_ms in cntl
    {
        test::GrpcRequest req;
        test::GrpcResponse res;
        brpc::Controller cntl;
        req.set_message(g_req);
        req.set_gzip(false);
        req.set_return_error(false);
        req.set_timeout_us(9876000);
        cntl.set_timeout_ms(9876);
        test::GrpcService_Stub stub(&_channel);
        stub.Method(&cntl, &req, &res, nullptr);
        EXPECT_FALSE(cntl.Failed());
    }

    // test timeout by using timeout_ms in channel
    {
        test::GrpcRequest req;
        test::GrpcResponse res;
        brpc::Controller cntl;
        req.set_message(g_req);
        req.set_gzip(false);
        req.set_return_error(false);
        req.set_timeout_us(g_timeout_ms * 1000);
        test::GrpcService_Stub stub(&_channel);
        stub.Method(&cntl, &req, &res, nullptr);
        EXPECT_FALSE(cntl.Failed());
    }
}

TEST(GrpcProtocol, client_sends_large_request_with_small_remote_window) {
    WindowGrpcService service;
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    brpc::ServerOptions server_options;
    server_options.h2_settings.stream_window_size = 32;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &server_options));

    brpc::Channel channel;
    brpc::ChannelOptions channel_options;
    channel_options.protocol = g_protocol;
    channel_options.timeout_ms = 10000;
    ASSERT_EQ(0, channel.Init(server.listen_address(), &channel_options));
    test::GrpcService_Stub stub(&channel);

    // Establish the H2 connection and receive the server SETTINGS first.
    {
        test::GrpcRequest request;
        test::GrpcResponse response;
        brpc::Controller cntl;
        request.set_message("warmup");
        request.set_gzip(false);
        request.set_return_error(false);
        stub.Method(&cntl, &request, &response, nullptr);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(request.message(), response.message());
    }

    test::GrpcRequest request;
    test::GrpcResponse response;
    brpc::Controller cntl;
    request.set_message(std::string(128 * 1024, 'x'));
    request.set_gzip(false);
    request.set_return_error(false);
    stub.Method(&cntl, &request, &response, nullptr);
    EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ(request.message(), response.message());
}

TEST(GrpcProtocol, server_sends_large_response_with_small_remote_window) {
    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    // Simulate a gRPC client with a tiny stream window (256 bytes) and
    // RFC 7540 default 64 KiB connection window receiving a 512 KiB response
    // to a tiny request (like GetClusterOverallStatus).
    brpc::policy::FLAGS_h2_client_stream_window_size = 256;
    brpc::policy::FLAGS_h2_client_connection_window_size = 65535;

    WindowGrpcService service;
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start("127.0.0.1:0", nullptr));

    brpc::Channel channel;
    brpc::ChannelOptions channel_options;
    channel_options.protocol = g_protocol;
    channel_options.timeout_ms = 10000;
    ASSERT_EQ(0, channel.Init(server.listen_address(), &channel_options));
    test::GrpcService_Stub stub(&channel);

    // Warmup RPC so Server receives Client's SETTINGS (stream_window_size=256)
    {
        test::GrpcRequest request;
        test::GrpcResponse response;
        brpc::Controller cntl;
        request.set_message("warmup");
        request.set_gzip(false);
        request.set_return_error(false);
        stub.Method(&cntl, &request, &response, nullptr);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ("warmup", response.message());
    }

    // Request a 512 KiB response with a tiny request
    const size_t resp_size = 512 * 1024;
    test::GrpcRequest request;
    test::GrpcResponse response;
    brpc::Controller cntl;
    request.set_message(butil::string_printf("resp_size:%zu:A", resp_size));
    request.set_gzip(false);
    request.set_return_error(false);
    stub.Method(&cntl, &request, &response, nullptr);
    EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ(std::string(resp_size, 'A'), response.message());
}

struct ConcurrentGrpcCallArg {
    test::GrpcService_Stub* stub;
    size_t resp_size;
    char fill;
    bool ok;
};

static void* RunConcurrentGrpcCall(void* void_arg) {
    ConcurrentGrpcCallArg* arg = static_cast<ConcurrentGrpcCallArg*>(void_arg);
    test::GrpcRequest request;
    test::GrpcResponse response;
    brpc::Controller cntl;
    request.set_message(butil::string_printf("resp_size:%zu:%c", arg->resp_size, arg->fill));
    request.set_gzip(false);
    request.set_return_error(false);
    arg->stub->Method(&cntl, &request, &response, nullptr);
    arg->ok = !cntl.Failed() && (response.message() == std::string(arg->resp_size, arg->fill));
    if (cntl.Failed()) {
        LOG(ERROR) << "Concurrent gRPC failed: " << cntl.ErrorText();
    }
    return nullptr;
}

TEST(GrpcProtocol, concurrent_large_responses_with_small_remote_window) {
    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    // 8 concurrent streams each returning 128 KiB (1 MiB total) over a single
    // HTTP/2 connection with 4 KiB stream window and 64 KiB connection window.
    brpc::policy::FLAGS_h2_client_stream_window_size = 4096;
    brpc::policy::FLAGS_h2_client_connection_window_size = 65535;

    WindowGrpcService service;
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    ASSERT_EQ(0, server.Start("127.0.0.1:0", nullptr));

    brpc::Channel channel;
    brpc::ChannelOptions channel_options;
    channel_options.protocol = g_protocol;
    channel_options.timeout_ms = 15000;
    ASSERT_EQ(0, channel.Init(server.listen_address(), &channel_options));
    test::GrpcService_Stub stub(&channel);

    const int kNumStreams = 8;
    bthread_t tids[kNumStreams];
    ConcurrentGrpcCallArg args[kNumStreams];
    for (int i = 0; i < kNumStreams; ++i) {
        args[i].stub = &stub;
        args[i].resp_size = 128 * 1024;
        args[i].fill = static_cast<char>('A' + i);
        args[i].ok = false;
        ASSERT_EQ(0, bthread_start_background(&tids[i], nullptr, RunConcurrentGrpcCall, &args[i]));
    }
    for (int i = 0; i < kNumStreams; ++i) {
        bthread_join(tids[i], nullptr);
        EXPECT_TRUE(args[i].ok) << "Stream " << i << " failed";
    }
}

} // namespace 
