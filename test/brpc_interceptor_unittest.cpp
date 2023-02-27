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
#include "brpc/nshead_service.h"
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
const std::string NSHEAD_EXP_RESPONSE = "error";

class EchoServiceImpl : public ::test::EchoService {
public:
    EchoServiceImpl() = default;
    ~EchoServiceImpl() override = default;
    void Echo(google::protobuf::RpcController* cntl_base,
              const ::test::EchoRequest* request,
              ::test::EchoResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        ASSERT_EQ(EXP_REQUEST, request->message());
        response->set_message(EXP_RESPONSE);
    }
};

// Adapt your own nshead-based protocol to use brpc
class MyNsheadProtocol : public brpc::NsheadService {
public:
    void ProcessNsheadRequest(const brpc::Server&,
                              brpc::Controller* cntl,
                              const brpc::NsheadMessage& request,
                              brpc::NsheadMessage* response,
                              brpc::NsheadClosure* done) {
        // This object helps you to call done->Run() in RAII style. If you need
        // to process the request asynchronously, pass done_guard.release().
        brpc::ClosureGuard done_guard(done);

        response->head = request.head;
        if (cntl->Failed()) {
            ASSERT_TRUE(cntl->Failed());
            ASSERT_EQ(EREJECT, cntl->ErrorCode());
            response->body.append(NSHEAD_EXP_RESPONSE);
            return;
        }
        response->body.append(EXP_RESPONSE);
    }
};

class MyInterceptor : public brpc::Interceptor {
public:
    MyInterceptor() = default;

    ~MyInterceptor() override = default;

    bool Accept(const brpc::Controller* controller,
                int& error_code,
                std::string& error_txt) const override {
        if (g_index % 2 == 0) {
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
        options.nshead_service = new MyNsheadProtocol;
        options.server_owns_interceptor = true;
        EXPECT_EQ(0, _server.Start(port, &options));
    }

    ~InterceptorTest() override = default;

    static void CallMethod(test::EchoService_Stub& stub,
                           ::test::EchoRequest& req,
                           ::test::EchoResponse& res) {
        for (g_index = 0; g_index < 1000; ++g_index) {
            brpc::Controller cntl;
            stub.Echo(&cntl, &req, &res, NULL);
            if (g_index % 2 == 0) {
                ASSERT_TRUE(cntl.Failed());
                ASSERT_EQ(EREJECT, cntl.ErrorCode());
            } else {
                ASSERT_FALSE(cntl.Failed());
                EXPECT_EQ(EXP_RESPONSE, res.message()) << cntl.ErrorText();
            }
        }
    }

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
        CallMethod(stub, req, res);
    }

    // PROTOCOL_HTTP
    {
        brpc::Channel channel;
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_HTTP;
        ASSERT_EQ(0, channel.Init("localhost", port, &options));
        test::EchoService_Stub stub(&channel);
        // Set the x-bd-error-code header of http response to brpc error code.
        brpc::policy::FLAGS_use_http_error_code = true;
        CallMethod(stub, req, res);
    }

    // PROTOCOL_HULU_PBRPC
    {
        brpc::Channel channel;
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_HULU_PBRPC;
        ASSERT_EQ(0, channel.Init("localhost", port, &options));
        test::EchoService_Stub stub(&channel);
        CallMethod(stub, req, res);
    }

    // PROTOCOL_SOFA_PBRPC
    {
        brpc::Channel channel;
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_SOFA_PBRPC;
        ASSERT_EQ(0, channel.Init("localhost", port, &options));
        test::EchoService_Stub stub(&channel);
        CallMethod(stub, req, res);
    }

    // PROTOCOL_NSHEAD
    {
        brpc::Channel channel;
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_NSHEAD;
        ASSERT_EQ(0, channel.Init("localhost", port, &options));
        brpc::NsheadMessage request;
        for (g_index = 0; g_index < 1000; ++g_index) {
            brpc::Controller cntl;
            brpc::NsheadMessage response;
            channel.CallMethod(NULL, &cntl, &request, &response, NULL);
            if (g_index % 2 == 0) {
                ASSERT_EQ(NSHEAD_EXP_RESPONSE, response.body.to_string());
            } else {
                ASSERT_EQ(EXP_RESPONSE, response.body.to_string());
            }
        }
    }
}