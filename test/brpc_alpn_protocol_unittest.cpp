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

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "gflags/gflags.h"

#include "echo.pb.h"
#include "brpc/channel.h"
#include "brpc/server.h"
#include "butil/fd_guard.h"
#include "butil/endpoint.h"

DEFINE_string(listen_addr, "0.0.0.0:8011", "Server listen address.");

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    google::ParseCommandLineFlags(&argc, &argv, true);    
    return RUN_ALL_TESTS();
}

namespace {

class EchoServerImpl : public test::EchoService {
public:
    virtual void Echo(google::protobuf::RpcController* controller,
                        const ::test::EchoRequest* request,
                        test::EchoResponse* response,
                        google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        response->set_message(request->message());

        brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
        LOG(NOTICE) << "protocol:" << cntl->request_protocol();
  }
};

class ALPNTest : public testing::Test {
public:
    ALPNTest() = default;
    virtual ~ALPNTest() = default;

    virtual void SetUp() override { 
        // Start brpc server with SSL
        brpc::ServerOptions server_options;
        auto&& ssl_options = server_options.mutable_ssl_options();
        ssl_options->default_cert.certificate = "cert1.crt";
        ssl_options->default_cert.private_key = "cert1.key";
        ssl_options->alpns = "http, h2, baidu_std";

        EXPECT_EQ(0, _server.AddService(&_echo_server_impl,
                                        brpc::SERVER_DOESNT_OWN_SERVICE));
        EXPECT_EQ(0, _server.Start(FLAGS_listen_addr.data(), &server_options));
    }
    
    virtual void TearDown() override {
        _server.Stop(0);
        _server.Join();
    }

    std::string HandshakeWithServer(std::vector<std::string> alpns) {
        // Init client ssl ctx and set alpn.
        brpc::ChannelSSLOptions options;
        SSL_CTX* ssl_ctx = brpc::CreateClientSSLContext(options);
        EXPECT_NE(nullptr, ssl_ctx);

        std::string raw_alpn;
        for (auto&& alpn : alpns) {
            raw_alpn.append(brpc::ALPNProtocolToString(brpc::AdaptiveProtocolType(alpn)));
        }
        SSL_CTX_set_alpn_protos(ssl_ctx, 
                reinterpret_cast<const unsigned char*>(raw_alpn.data()), raw_alpn.size());
    
        // TCP connect.
        butil::EndPoint endpoint;
        butil::str2endpoint(FLAGS_listen_addr.data(), &endpoint);

        int cli_fd = butil::tcp_connect(endpoint, nullptr);
        butil::fd_guard guard(cli_fd);
        EXPECT_NE(0, cli_fd);

        // SSL handshake.
        SSL* ssl = brpc::CreateSSLSession(ssl_ctx, 0, cli_fd, false);
        EXPECT_NE(nullptr, ssl);
        EXPECT_EQ(1, SSL_do_handshake(ssl)); 

        // Get handshake result.
        const unsigned char* select_alpn = nullptr;
        unsigned int len = 0;
        SSL_get0_alpn_selected(ssl, &select_alpn, &len);
        return std::string(reinterpret_cast<const char*>(select_alpn), len);
    }

private:
    brpc::Server _server;
    EchoServerImpl _echo_server_impl;
};

TEST_F(ALPNTest, Server) {
    // Server alpn support h2 http baidu_std, test the following case:
    // 1. Client provides 1 protocol which is in the list supported by the server.
    // 2. Server select protocol according to priority.
    // 3. Server does not support the protocol provided by the client.

    EXPECT_EQ("baidu_std", ALPNTest::HandshakeWithServer({"baidu_std"}));
    EXPECT_EQ("h2", ALPNTest::HandshakeWithServer({"baidu_std", "h2"}));
    EXPECT_EQ("", ALPNTest::HandshakeWithServer({"nshead"}));
}

} // namespace
