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
#include "brpc/policy/sofa_pbrpc_protocol.h"
#include "brpc/channel.h"
#include "brpc/server.h"
#include "brpc/interceptor.h"
#include "echo.pb.h"

namespace brpc {
namespace policy {
DECLARE_bool(use_http_error_code);
}
}

int main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

const int EREJECT = 4000;
int g_index = 0;
const int port = 8613;
const std::string EXP_REQUEST = "hello";
const std::string EXP_RESPONSE = "world";

class EchoServiceImpl : public ::test::EchoService {
public:
    EchoServiceImpl() = default;
    ~EchoServiceImpl() override = default;
    virtual void Echo(google::protobuf::RpcController* cntl_base,
                      const ::test::EchoRequest* request,
                      ::test::EchoResponse* response,
                      google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        EXPECT_EQ(EXP_REQUEST, request->message());
        response->set_message(EXP_RESPONSE);
    }
};

class MyInterceptor : public brpc::Interceptor {
public:
    MyInterceptor() = default;

    ~MyInterceptor() override = default;

    bool Accept(const brpc::Controller* controller,
                int& error_code,
                std::string& error_txt) const override {
        if (g_index == 0) {
            // Reject request
            error_code = EREJECT;
            error_txt = "reject g_index=0";
            return false;
        }

        return true;
    }
};

class InterceptorTest : public ::testing::Test {
public:
    InterceptorTest() {
        EXPECT_EQ(0, _server.AddService(&_echo_svc,
                                        brpc::SERVER_DOESNT_OWN_SERVICE));
        brpc::ServerOptions options;
        options.interceptor = new MyInterceptor;
        options.server_owns_interceptor = true;
        EXPECT_EQ(0, _server.Start(port, &options));
    }

    ~InterceptorTest() override = default;

private:
    brpc::Server _server;
    EchoServiceImpl _echo_svc;
};

TEST_F(InterceptorTest, sanity) {
    ::test::EchoRequest req;
    ::test::EchoResponse res;
    req.set_message(EXP_REQUEST);

    // PROTOCOL_BAIDU_STD
    {
        brpc::Channel channel;
        brpc::ChannelOptions options;
        ASSERT_EQ(0, channel.Init("localhost", port, &options));
        test::EchoService_Stub stub(&channel);

        // First request will be rejected.
        brpc::Controller cntl;
        stub.Echo(&cntl, &req, &res, NULL);
        ASSERT_TRUE(cntl.Failed());
        ASSERT_EQ(EREJECT, cntl.ErrorCode());

        ++g_index;
        cntl.Reset();
        stub.Echo(&cntl, &req, &res, NULL);
        ASSERT_FALSE(cntl.Failed());
        EXPECT_EQ(EXP_RESPONSE, res.message()) << cntl.ErrorText();
    }

    g_index = 0;
    // PROTOCOL_HTTP
    {
        brpc::Channel channel;
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_HTTP;
        ASSERT_EQ(0, channel.Init("localhost", port, &options));
        test::EchoService_Stub stub(&channel);

        // First request will be rejected.
        // Set the x-bd-error-code header of http response to brpc error code.
        brpc::policy::FLAGS_use_http_error_code = true;
        brpc::Controller cntl;
        stub.Echo(&cntl, &req, &res, NULL);
        ASSERT_TRUE(cntl.Failed());
        ASSERT_EQ(EREJECT, cntl.ErrorCode());

        ++g_index;
        cntl.Reset();
        stub.Echo(&cntl, &req, &res, NULL);
        ASSERT_FALSE(cntl.Failed());
        ASSERT_EQ(EXP_RESPONSE, res.message()) << cntl.ErrorText();
    }

    g_index = 0;
    // PROTOCOL_HULU_PBRPC
    {
        brpc::Channel channel;
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_HULU_PBRPC;
        ASSERT_EQ(0, channel.Init("localhost", port, &options));
        test::EchoService_Stub stub(&channel);

        // First request will be rejected.
        brpc::Controller cntl;
        stub.Echo(&cntl, &req, &res, NULL);
        ASSERT_TRUE(cntl.Failed());
        ASSERT_EQ(EREJECT, cntl.ErrorCode());

        ++g_index;
        cntl.Reset();
        stub.Echo(&cntl, &req, &res, NULL);
        ASSERT_FALSE(cntl.Failed());
        ASSERT_EQ(EXP_RESPONSE, res.message()) << cntl.ErrorText();
    }

    g_index = 0;
    // PROTOCOL_SOFA_PBRPC
    {
        brpc::Channel channel;
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_SOFA_PBRPC;
        ASSERT_EQ(0, channel.Init("localhost", port, &options));
        test::EchoService_Stub stub(&channel);

        // First request will be rejected.
        brpc::Controller cntl;
        stub.Echo(&cntl, &req, &res, NULL);
        ASSERT_TRUE(cntl.Failed());
        ASSERT_EQ(EREJECT, cntl.ErrorCode());

        ++g_index;
        cntl.Reset();
        stub.Echo(&cntl, &req, &res, NULL);
        ASSERT_FALSE(cntl.Failed());
        ASSERT_EQ(EXP_RESPONSE, res.message()) << cntl.ErrorText();
    }
}