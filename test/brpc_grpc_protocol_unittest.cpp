// Copyright (c) 2018 Bilibili, Inc.
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

// Authors: Jiashun Zhu(zhujiashun@bilibili.com)

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/channel.h"
#include "brpc/grpc.h"
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
const int64_t g_timeout_ms = 3000;
const std::string g_protocol = "grpc";

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
        if (req->has_gzip() && req->gzip()) {
            cntl->set_response_compress_type(brpc::COMPRESS_TYPE_GZIP);
        }
        res->set_message(g_prefix + req->message());

        if (req->has_error_code()) {
            const int error_code = req->error_code();
            cntl->set_grpc_error_code((brpc::GrpcStatus)error_code,
                        butil::string_printf("%s%d", g_prefix.c_str(), error_code));
            return;
        }
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

        test::GrpcService_Stub stub(&_channel);
        stub.Method(&cntl, &req, &res, NULL);
        EXPECT_FALSE(cntl.Failed()) << cntl.ErrorCode() << ": " << cntl.ErrorText();
        EXPECT_EQ(res.message(), g_prefix + g_req);
        EXPECT_EQ(brpc::GRPC_OK, cntl.grpc_status());
    }

    brpc::Server _server;
    MyGrpcService _svc;
    brpc::Channel _channel;
};

TEST_F(GrpcTest, percent_encode) {
    std::string out;
    std::string s1("abcdefg !@#$^&*()/");
    brpc::percent_encode(s1, &out);
    EXPECT_TRUE(out == s1) << s1 << " vs " << out;

    char s2_buf[] = "\0\0%\33\35 brpc";
    std::string s2(s2_buf, sizeof(s2_buf) - 1);
    std::string s2_expected_out("%00%00%25%1b%1d brpc");
    brpc::percent_encode(s2, &out);
    EXPECT_TRUE(out == s2_expected_out) << s2_expected_out << " vs " << out;
}

TEST_F(GrpcTest, percent_decode) {
    std::string out;
    std::string s1("abcdefg !@#$^&*()/");
    brpc::percent_decode(s1, &out);
    EXPECT_TRUE(out == s1) << s1 << " vs " << out;

    std::string s2("%00%00%1b%1d brpc");
    char s2_expected_out_buf[] = "\0\0\33\35 brpc";
    std::string s2_expected_out(s2_expected_out_buf, sizeof(s2_expected_out_buf) - 1);
    brpc::percent_decode(s2, &out);
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
    // GRPC_OK(0) is skipped
    for (int i = 1; i < (int)brpc::GRPC_MAX; ++i) {
        test::GrpcRequest req;
        test::GrpcResponse res;
        brpc::Controller cntl;
        req.set_message(g_req);
        req.set_error_code(i);
        test::GrpcService_Stub stub(&_channel);
        stub.Method(&cntl, &req, &res, NULL);
        EXPECT_TRUE(cntl.Failed());
        EXPECT_EQ((int)cntl.grpc_status(), i);
        EXPECT_EQ(cntl.grpc_message(), butil::string_printf("%s%d", g_prefix.c_str(), i));
    }
}

} // namespace 
