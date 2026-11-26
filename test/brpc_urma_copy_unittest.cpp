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
#include <gtest/gtest.h>
#include <gflags/gflags.h>

#if BRPC_WITH_URMA
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/urma/urma_helper.h"
#include "brpc/urma/urma_recovery_policy.h"
#include "echo.pb.h"

namespace brpc {
namespace urma {
DECLARE_bool(urma_copy_send);
DECLARE_int32(urma_buffer_count);
DECLARE_int32(urma_prepared_jetty_cnt);
DECLARE_int32(urma_max_sge);
DECLARE_int32(urma_client_handshake_version);
}  // namespace urma
}  // namespace brpc

namespace {

class CopiedEchoService : public test::EchoService {
    void Echo(google::protobuf::RpcController* base,
              const test::EchoRequest* request, test::EchoResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        auto* cntl = static_cast<brpc::Controller*>(base);
        response->set_message(request->message());
        cntl->response_attachment().append(cntl->request_attachment());
    }
};

class UrmaCopiedRpcTest : public ::testing::TestWithParam<int> {
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
        if (!_mock) {
            return;
        }
        brpc::urma::FLAGS_urma_copy_send = true;
        brpc::urma::FLAGS_urma_buffer_count = 2048;
        brpc::urma::FLAGS_urma_prepared_jetty_cnt = 0;
        brpc::urma::FLAGS_urma_max_sge = 1;
        brpc::urma::GlobalUrmaInitializeOrDie();
    }
    void SetUp() override {
        if (!_mock) {
            GTEST_SKIP() << "Copied URMA RPC tests use the link-time mock";
        }
        ASSERT_TRUE(brpc::urma::IsUrmaAvailable());
        ASSERT_NE(nullptr, brpc::urma::GetUrmaContext());
        brpc::urma::FLAGS_urma_client_handshake_version = GetParam();
    }
    static bool _mock;
};
bool UrmaCopiedRpcTest::_mock = false;

TEST_P(UrmaCopiedRpcTest,
       copies_payload_and_attachment_across_wrs_and_credit_returns) {
    CopiedEchoService service;
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
    // Exceed the send window to exercise receive-credit ACKs as well as data WRs.
    for (int round = 0; round < 40; ++round) {
        const size_t size = round == 0 ? 0 : 20000;
        const std::string payload(size, 'v');
        brpc::Controller cntl;
        test::EchoRequest request;
        test::EchoResponse response;
        request.set_message(payload);
        cntl.request_attachment().append(payload);
        test::EchoService_Stub(&channel).Echo(&cntl, &request, &response,
                                              nullptr);
        EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
        EXPECT_EQ(payload, response.message());
        EXPECT_EQ(payload, cntl.response_attachment().to_string());
    }
    EXPECT_EQ(before.negotiated_connections + 2,
              brpc::urma::GetUrmaRecoveryStatus().negotiated_connections);
    server.Stop(0);
    server.Join();
}

INSTANTIATE_TEST_SUITE_P(HandshakeVersions, UrmaCopiedRpcTest,
                         ::testing::Values(2, 3));

}  // namespace
#endif  // BRPC_WITH_URMA

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}
