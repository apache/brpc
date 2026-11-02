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

#include <cstring>
#include <string>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#if BRPC_WITH_URMA
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/urma/urma_endpoint.h"
#include "brpc/urma/urma_handshake.h"
#include "brpc/urma/urma_handshake.pb.h"
#include "brpc/urma/urma_helper.h"
#include "brpc/urma/urma_recovery_policy.h"
#include "echo.pb.h"

namespace brpc {
namespace urma {
DECLARE_string(urma_tp_type);
DECLARE_int32(urma_buffer_count);
DECLARE_int32(urma_client_handshake_version);
}  // namespace urma
}  // namespace brpc

namespace {

class PathEchoService : public test::EchoService {
    void Echo(google::protobuf::RpcController* base,
              const test::EchoRequest* request, test::EchoResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        auto* cntl = static_cast<brpc::Controller*>(base);
        response->set_message(request->message());
        cntl->response_attachment().append(cntl->request_attachment());
    }
};

class UrmaTransportTest : public ::testing::TestWithParam<int> {
protected:
    static void SetUpTestSuite() {
        urma_init_attr_t attr{};
        const auto status = urma_init(&attr);
        if (status != URMA_SUCCESS && status != URMA_EEXIST) {
            return;
        }
        int count = 0;
        auto** devices = urma_get_device_list(&count);
        _mock = devices && count > 0 &&
                strcmp(devices[0]->name, "mock_urma_device") == 0;
        urma_free_device_list(devices);
        if (status == URMA_SUCCESS) {
            urma_uninit();
        }
        if (_mock) {
            brpc::urma::FLAGS_urma_buffer_count = 4096;
            brpc::urma::GlobalUrmaInitializeOrDie();
        }
    }
    void SetUp() override {
        if (!_mock) {
            GTEST_SKIP() << "Path RPC tests use the link-time URMA mock";
        }
        ASSERT_TRUE(brpc::urma::IsUrmaAvailable());
        const bool rtp = brpc::urma::FLAGS_urma_tp_type == "rtp";
        ASSERT_EQ(rtp ? URMA_RTP : URMA_CTP, brpc::urma::GetUrmaTpType());
        ASSERT_EQ(rtp ? 0 : 6, brpc::urma::GetUrmaJettyPriority());
        brpc::urma::FLAGS_urma_client_handshake_version = GetParam();
        brpc::urma::SetUrmaRecoveryMode(brpc::urma::UrmaRecoveryMode::AUTO);
    }
    void TearDown() override {
        brpc::urma::SetUrmaRecoveryMode(brpc::urma::UrmaRecoveryMode::LEGACY);
    }
    static bool _mock;
};
bool UrmaTransportTest::_mock = false;

TEST_P(UrmaTransportTest, advertises_selected_path_in_both_hello_formats) {
    brpc::urma::UrmaEndpoint endpoint(nullptr);
    brpc::urma::v2_wire::HelloMessage hello;
    endpoint.FillLocalHelloV2(&hello);
    EXPECT_EQ(brpc::urma::GetUrmaTpType(), hello.tp_type);
    brpc::urma::UrmaHello protobuf_hello;
    endpoint.FillLocalHelloV3(&protobuf_hello);
    EXPECT_EQ(brpc::urma::GetUrmaTpType(), protobuf_hello.tp_type());
}

TEST_P(UrmaTransportTest, exchanges_zero_copy_payloads_and_receive_credits) {
    PathEchoService service;
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    brpc::ServerOptions so;
    so.socket_mode = brpc::SOCKET_MODE_URMA;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &so));
    brpc::Channel channel;
    brpc::ChannelOptions co;
    co.socket_mode = brpc::SOCKET_MODE_URMA;
    co.connection_type = "single";
    co.timeout_ms = 3000;
    co.max_retry = 0;
    const auto before = brpc::urma::GetUrmaRecoveryStatus();
    ASSERT_EQ(0, channel.Init(server.listen_address(), &co));
    for (int round = 0; round < 40; ++round) {
        const size_t size = round == 0 ? 0 : round == 1 ? 1024 * 1024 : 20000;
        const std::string payload(size, 'r');
        brpc::Controller cntl;
        test::EchoRequest request;
        test::EchoResponse response;
        request.set_message(payload);
        cntl.request_attachment().append(payload);
        test::EchoService_Stub(&channel).Echo(&cntl, &request, &response,
                                              nullptr);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        EXPECT_EQ(payload, response.message());
        EXPECT_EQ(payload, cntl.response_attachment().to_string());
    }
    EXPECT_EQ(before.negotiated_connections + 2,
              brpc::urma::GetUrmaRecoveryStatus().negotiated_connections);
    server.Stop(0);
    server.Join();
}

TEST_P(UrmaTransportTest,
       switches_new_connections_from_auto_to_tcp_only_and_back) {
    PathEchoService service;
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    brpc::ServerOptions so;
    so.socket_mode = brpc::SOCKET_MODE_URMA;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &so));
    brpc::Channel channel;
    brpc::ChannelOptions co;
    co.socket_mode = brpc::SOCKET_MODE_URMA;
    co.connection_type = "short";
    co.timeout_ms = 3000;
    co.max_retry = 0;
    ASSERT_EQ(0, channel.Init(server.listen_address(), &co));
    // Cumulative receive buffers exceed the pool capacity unless closed
    // short connections release their queues and buffers.
    const int requests_per_mode = 40;
    for (auto mode : {brpc::urma::UrmaRecoveryMode::AUTO,
                      brpc::urma::UrmaRecoveryMode::TCP_ONLY,
                      brpc::urma::UrmaRecoveryMode::AUTO}) {
        ASSERT_TRUE(brpc::urma::SetUrmaRecoveryMode(mode));
        const auto before = brpc::urma::GetUrmaRecoveryStatus();
        for (int i = 0; i < requests_per_mode; ++i) {
            brpc::Controller cntl;
            test::EchoRequest request;
            test::EchoResponse response;
            request.set_message("switch");
            test::EchoService_Stub(&channel).Echo(&cntl, &request, &response,
                                                  nullptr);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            EXPECT_EQ("switch", response.message());
        }
        const auto after = brpc::urma::GetUrmaRecoveryStatus();
        if (mode == brpc::urma::UrmaRecoveryMode::TCP_ONLY) {
            EXPECT_EQ(before.negotiated_connections,
                      after.negotiated_connections);
            EXPECT_EQ(before.tcp_only_admissions + requests_per_mode,
                      after.tcp_only_admissions);
        } else {
            EXPECT_EQ(before.negotiated_connections + 2 * requests_per_mode,
                      after.negotiated_connections);
            EXPECT_EQ(before.tcp_only_admissions, after.tcp_only_admissions);
        }
    }
    server.Stop(0);
    server.Join();
}

INSTANTIATE_TEST_SUITE_P(HandshakeVersions, UrmaTransportTest,
                         ::testing::Values(2, 3));

}  // namespace
#endif  // BRPC_WITH_URMA

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}
