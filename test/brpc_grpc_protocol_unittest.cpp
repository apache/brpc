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
#include "butil/time.h"
#include "grpc.pb.h"

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    if (GFLAGS_NS::SetCommandLineOption("http_body_compress_threshold", "0").empty()) {
        std::cerr << "Fail to set -crash_on_fatal_log" << std::endl;
        return -1;
    }
    if (GFLAGS_NS::SetCommandLineOption("crash_on_fatal_log", "true").empty()) {
        std::cerr << "Fail to set -crash_on_fatal_log" << std::endl;
        return -1;
    }
    return RUN_ALL_TESTS();
}

namespace {

const std::string g_server_addr = "127.0.0.1:8011";
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

class GrpcTest : public ::testing::Test {
protected:
    GrpcTest() {
        EXPECT_EQ(0, _server.AddService(&_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
        EXPECT_EQ(0, _server.Start(g_server_addr.c_str(), NULL));
        brpc::ChannelOptions options;
        options.protocol = g_protocol;
        options.timeout_ms = g_timeout_ms;
        EXPECT_EQ(0, _channel.Init(g_server_addr.c_str(), "", &options));
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
        stub.Method(&cntl, &req, &res, NULL);
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
    stub.Method(&cntl, &req, &res, NULL);
    EXPECT_TRUE(cntl.Failed());
    EXPECT_EQ(cntl.ErrorCode(), brpc::EINTERNAL);
    EXPECT_TRUE(butil::StringPiece(cntl.ErrorText()).ends_with(butil::string_printf("%s", g_prefix.c_str())));
}

TEST_F(GrpcTest, RpcTimedOut) {
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = g_protocol;
    options.timeout_ms = g_timeout_ms;
    EXPECT_EQ(0, channel.Init(g_server_addr.c_str(), "", &options));

    test::GrpcRequest req;
    test::GrpcResponse res;
    brpc::Controller cntl;
    req.set_message(g_req);
    req.set_gzip(false);
    req.set_return_error(false);
    test::GrpcService_Stub stub(&_channel);
    stub.MethodTimeOut(&cntl, &req, &res, NULL);
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
    stub.MethodNotExist(&cntl, &req, &res, NULL);
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
        "", "-1"
    };

    // test all timeout format
    for (size_t i = 0; i < arraysize(timeouts); i = i + 2) {
        test::GrpcRequest req;
        test::GrpcResponse res;
        brpc::Controller cntl;
        req.set_message(g_req);
        req.set_gzip(false);
        req.set_return_error(false);
        req.set_timeout_us((int64_t)(strtol(timeouts[i+1], NULL, 10)));
        cntl.set_timeout_ms(-1);
        cntl.http_request().SetHeader("grpc-timeout", timeouts[i]);
        test::GrpcService_Stub stub(&_channel);
        stub.Method(&cntl, &req, &res, NULL);
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
        stub.Method(&cntl, &req, &res, NULL);
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
        stub.Method(&cntl, &req, &res, NULL);
        EXPECT_FALSE(cntl.Failed());
    }
}

} // namespace 
