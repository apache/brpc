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
#include <functional>
#include <string>
#include <vector>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#if BRPC_WITH_URMA
#include "bthread/bthread.h"
#include "butil/time.h"
#include "brpc/acceptor.h"
#include "brpc/channel.h"
#include "brpc/closure_guard.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/urma/urma_endpoint.h"
#include "brpc/urma/urma_helper.h"
#include "brpc/urma/urma_recovery_policy.h"
#include "brpc/urma_transport.h"
#include "echo.pb.h"

namespace brpc {
namespace urma {
DECLARE_int32(urma_buffer_count);
DECLARE_int32(urma_prepared_jetty_cnt);
DECLARE_int32(urma_client_handshake_version);
extern bool g_skip_urma_init;

class UrmaQosTestPeer {
public:
    static int Priority(UrmaEndpoint* endpoint) {
        return endpoint->_resource && endpoint->_resource->jetty
                   ? endpoint->_resource->jetty->jetty_cfg.jfs_cfg.priority
                   : -1;
    }
    static ssize_t FailCompletion(UrmaEndpoint* endpoint) {
        urma_cr_t cr{};
        cr.status = URMA_CR_RNR_RETRY_CNT_EXC_ERR;
        return endpoint->HandleCompletion(cr);
    }
};
}  // namespace urma
}  // namespace brpc

namespace {
using namespace brpc;

bool WaitUntil(const std::function<bool()>& ready) {
    const int64_t deadline = butil::monotonic_time_us() + 5000000;
    while (!ready()) {
        if (butil::monotonic_time_us() >= deadline) {
            return false;
        }
        bthread_usleep(1000);
    }
    return true;
}

class QosEcho : public test::EchoService {
    void Echo(google::protobuf::RpcController* base,
              const test::EchoRequest* request, test::EchoResponse* response,
              google::protobuf::Closure* done) override {
        ClosureGuard guard(done);
        auto* controller = static_cast<Controller*>(base);
        response->set_message(request->message());
        controller->response_attachment().append(
            controller->request_attachment());
    }
};

class UrmaQosChurnTest : public testing::TestWithParam<int> {
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
                std::strcmp(devices[0]->name, "mock_urma_device") == 0;
        urma_free_device_list(devices);
        if (status == URMA_SUCCESS) {
            urma_uninit();
        }
        if (_mock) {
            urma::g_skip_urma_init = false;
            urma::FLAGS_urma_prepared_jetty_cnt = 2;
            urma::GlobalUrmaInitializeOrDie();
        }
    }

    void SetUp() override {
        if (!_mock) {
            GTEST_SKIP() << "URMA churn tests use the link-time mock";
        }
        ASSERT_TRUE(urma::IsUrmaAvailable());
        _saved_mode = urma::GetUrmaRecoveryMode();
        urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::LEGACY);
        urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::AUTO);
        ASSERT_TRUE(urma::SetUrmaRecoveryCooldownMs(1000));
        urma::FLAGS_urma_client_handshake_version = GetParam();
        ASSERT_EQ(0, _server.AddService(&_service, SERVER_DOESNT_OWN_SERVICE));
        ServerOptions options;
        options.socket_mode = SOCKET_MODE_URMA;
        options.urma_qos.service_level = 2;
        ASSERT_EQ(0, _server.Start("127.0.0.1:0", &options));
        _options.socket_mode = SOCKET_MODE_URMA;
        _options.urma_qos.service_level = 4;
        _options.timeout_ms = 2000;
        _options.connect_timeout_ms = 2000;
        _options.max_retry = 0;
        _options.connection_type = "single";
    }

    void TearDown() override {
        if (_mock) {
            _server.Stop(0);
            _server.Join();
            EXPECT_TRUE(WaitUntil([this] {
                for (auto id : _client_ids) {
                    SocketUniquePtr socket;
                    if (Socket::AddressFailedAsWell(id, &socket) >= 0) {
                        return false;
                    }
                }
                return true;
            })) << "Client sockets retained after test completion";
            urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::LEGACY);
            urma::SetUrmaRecoveryMode(_saved_mode);
            urma::SetUrmaRecoveryCooldownMs(5000);
        }
    }

    void Echo(Channel* channel, int sequence) {
        Controller controller;
        test::EchoRequest request;
        test::EchoResponse response;
        request.set_message("QoS lifecycle " + std::to_string(sequence));
        std::string payload(65536, '\0');
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>((i + sequence) % 251);
        }
        controller.request_attachment().append(payload);
        test::EchoService_Stub(channel).Echo(&controller, &request, &response,
                                             nullptr);
        ASSERT_FALSE(controller.Failed()) << controller.ErrorText();
        EXPECT_EQ(request.message(), response.message());
        EXPECT_EQ(payload, controller.response_attachment().to_string());
    }

    void CheckConnection(Channel* channel, bool use_urma) {
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(channel->_server_id, &socket));
        EXPECT_EQ(4, socket->urma_qos().service_level);
        EXPECT_EQ(-1, socket->urma_qos().priority);
        auto* transport = static_cast<UrmaTransport*>(socket->_transport.get());
        EXPECT_EQ(use_urma ? UrmaTransport::URMA_ON : UrmaTransport::URMA_OFF,
                  transport->_urma_state.load());
        EXPECT_EQ(use_urma ? 10 : -1,
                  urma::UrmaQosTestPeer::Priority(transport->GetUrmaEp()));
        std::vector<SocketId> accepted;
        _server._am->ListConnections(&accepted);
        bool found = false;
        for (auto id : accepted) {
            SocketUniquePtr peer;
            if (Socket::Address(id, &peer) == 0 &&
                peer->remote_side() == socket->local_side()) {
                found = true;
                EXPECT_EQ(2, peer->urma_qos().service_level);
                auto* peer_transport =
                    static_cast<UrmaTransport*>(peer->_transport.get());
                const int expected_priority =
                    use_urma ? (urma::GetUrmaTpType() == URMA_RTP ? 0 : 6) : -1;
                EXPECT_EQ(expected_priority, urma::UrmaQosTestPeer::Priority(
                                                 peer_transport->GetUrmaEp()));
            }
        }
        EXPECT_TRUE(found);
    }

    static bool _mock;
    GFLAGS_NAMESPACE::FlagSaver _flags;
    urma::UrmaRecoveryMode _saved_mode = urma::UrmaRecoveryMode::LEGACY;
    QosEcho _service;
    Server _server;
    ChannelOptions _options;
    std::vector<SocketId> _client_ids;
};
bool UrmaQosChurnTest::_mock = false;

TEST_P(UrmaQosChurnTest, short_connections_release_resources) {
    _options.connection_type = "short";
    Channel channel;
    ASSERT_EQ(0, channel.Init(_server.listen_address(), &_options));
    _client_ids.push_back(channel._server_id);
    for (int i = 0; i < 64; ++i) {
        ASSERT_NO_FATAL_FAILURE(Echo(&channel, i));
        ASSERT_TRUE(WaitUntil([this] {
            return _server._am->ConnectionCount() == 0;
        })) << "Accepted short connection retained after RPC "
            << i;
    }
}

TEST_P(UrmaQosChurnTest, cooldown_and_fresh_connections_preserve_business_qos) {
    Channel original;
    ASSERT_EQ(0, original.Init(_server.listen_address(), &_options));
    _client_ids.push_back(original._server_id);
    ASSERT_NO_FATAL_FAILURE(Echo(&original, 1));
    ASSERT_NO_FATAL_FAILURE(CheckConnection(&original, true));
    const auto baseline = urma::GetUrmaRecoveryStatus();
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(original._server_id, &socket));
    auto* transport = static_cast<UrmaTransport*>(socket->_transport.get());
    EXPECT_EQ(-1,
              urma::UrmaQosTestPeer::FailCompletion(transport->GetUrmaEp()));
    socket->SetFailed(EIO, "Injected completion error");
    socket.reset();

    _options.connection_group = "qos-cooldown";
    Channel tcp;
    ASSERT_EQ(0, tcp.Init(_server.listen_address(), &_options));
    _client_ids.push_back(tcp._server_id);
    ASSERT_NO_FATAL_FAILURE(Echo(&tcp, 2));
    ASSERT_NO_FATAL_FAILURE(CheckConnection(&tcp, false));
    EXPECT_GT(urma::GetUrmaRecoveryStatus().peer_cooldown_admissions,
              baseline.peer_cooldown_admissions);
    ASSERT_TRUE(WaitUntil(
        [] { return urma::GetUrmaRecoveryStatus().cooled_peers == 0; }));

    _options.connection_group = "qos-recovered";
    Channel recovered;
    ASSERT_EQ(0, recovered.Init(_server.listen_address(), &_options));
    _client_ids.push_back(recovered._server_id);
    ASSERT_NO_FATAL_FAILURE(Echo(&recovered, 3));
    ASSERT_NO_FATAL_FAILURE(CheckConnection(&recovered, true));
    const auto after = urma::GetUrmaRecoveryStatus();
    const size_t reason = static_cast<size_t>(urma::UrmaFailureReason::RNR);
    EXPECT_EQ(baseline.failures[reason] + 1, after.failures[reason]);
    EXPECT_GT(after.negotiated_connections, baseline.negotiated_connections);
}

INSTANTIATE_TEST_SUITE_P(HandshakeVersions, UrmaQosChurnTest,
                         testing::Values(2, 3));
}  // namespace
#endif  // BRPC_WITH_URMA

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
#if BRPC_WITH_URMA
    brpc::urma::FLAGS_urma_buffer_count = 4096;
#endif
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}
