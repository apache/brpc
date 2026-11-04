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

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#if BRPC_WITH_URMA
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "brpc/channel.h"
#include "brpc/closure_guard.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/urma/urma_helper.h"
#include "brpc/urma/urma_recovery_policy.h"
#include "bthread/bthread.h"
#include "butil/time.h"
#include "butil/logging.h"
#include "echo.pb.h"

namespace brpc {
namespace urma {
DECLARE_int32(urma_buffer_count);
DECLARE_int32(urma_client_handshake_version);
}  // namespace urma
}  // namespace brpc

namespace {

class RemoteFailureEcho : public test::EchoService {
public:
    std::string marker;
    std::atomic<bool> fail_next_response{false};

    void Echo(google::protobuf::RpcController* base,
              const test::EchoRequest* request, test::EchoResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard guard(done);
        auto* cntl = static_cast<brpc::Controller*>(base);
        response->set_message(request->message());
        cntl->response_attachment().append(cntl->request_attachment());
        // The request has arrived; the next data SEND belongs to the server.
        if (fail_next_response.exchange(false)) {
            FILE* file = fopen(marker.c_str(), "w");
            CHECK(file != nullptr);
            CHECK_EQ(0, fclose(file));
        }
    }
};

class UrmaRemoteRecoveryTest : public testing::Test {
protected:
    void SetUp() override {
        const char* preload = std::getenv("LD_PRELOAD");
        if (!preload ||
            !std::strstr(preload, "brpc_urma_send_failure_injector")) {
            GTEST_SKIP() << "Run through CTest to preload the SEND injector";
        }
        urma_init_attr_t attr{};
        const auto status = urma_init(&attr);
        ASSERT_TRUE(status == URMA_SUCCESS || status == URMA_EEXIST);
        int count = 0;
        auto** devices = urma_get_device_list(&count);
        const bool mock =
            devices && count > 0 &&
            std::strcmp(devices[0]->name, "mock_urma_device") == 0;
        urma_free_device_list(devices);
        if (status == URMA_SUCCESS) {
            ASSERT_EQ(URMA_SUCCESS, urma_uninit());
        }
        if (!mock) {
            GTEST_SKIP() << "Remote failure tests use the URMA mock";
        }
        brpc::urma::FLAGS_urma_buffer_count = 4096;
        brpc::urma::GlobalUrmaInitializeOrDie();
        ASSERT_TRUE(brpc::urma::IsUrmaAvailable());
        ASSERT_TRUE(brpc::urma::SetUrmaRecoveryCooldownMs(2000));
        ASSERT_TRUE(brpc::urma::SetUrmaRecoveryMode(
            brpc::urma::UrmaRecoveryMode::AUTO));
        char marker[] = "/tmp/brpc-urma-remote-XXXXXX";
        const int fd = mkstemp(marker);
        ASSERT_GE(fd, 0);
        ASSERT_EQ(0, close(fd));
        _service.marker = marker;
        ASSERT_EQ(0, unlink(marker));
        ASSERT_EQ(0, setenv("BRPC_URMA_FAIL_ONCE_FILE", marker, 1));
        ASSERT_EQ(
            0, _server.AddService(&_service, brpc::SERVER_DOESNT_OWN_SERVICE));
        brpc::ServerOptions options;
        options.socket_mode = brpc::SOCKET_MODE_URMA;
        ASSERT_EQ(0, _server.Start("127.0.0.1:0", &options));
    }

    void TearDown() override {
        _server.Stop(0);
        _server.Join();
        if (!_service.marker.empty()) {
            unlink(_service.marker.c_str());
            unsetenv("BRPC_URMA_FAIL_ONCE_FILE");
        }
        brpc::urma::SetUrmaRecoveryMode(brpc::urma::UrmaRecoveryMode::LEGACY);
    }

    bool Echo(brpc::Channel* channel) {
        brpc::Controller cntl;
        test::EchoRequest request;
        test::EchoResponse response;
        request.set_message("remote failure recovery");
        const std::string payload(65536, 'r');
        cntl.request_attachment().append(payload);
        test::EchoService_Stub(channel).Echo(&cntl, &request, &response,
                                             nullptr);
        if (cntl.Failed()) {
            return false;
        }
        EXPECT_EQ(request.message(), response.message());
        EXPECT_EQ(payload, cntl.response_attachment().to_string());
        return true;
    }

    template <typename Predicate>
    bool WaitUntil(Predicate predicate) {
        const int64_t deadline = butil::monotonic_time_us() + 3000000;
        while (!predicate()) {
            if (butil::monotonic_time_us() >= deadline) {
                return false;
            }
            bthread_usleep(1000);
        }
        return true;
    }

    RemoteFailureEcho _service;
    brpc::Server _server;
};

TEST_F(UrmaRemoteRecoveryTest, server_send_failure_cools_client_and_recovers) {
    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.socket_mode = brpc::SOCKET_MODE_URMA;
    options.connection_type = "short";
    options.timeout_ms = 1000;
    options.connect_timeout_ms = 1000;
    options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(_server.listen_address(), &options));
    const auto transport =
        static_cast<size_t>(brpc::urma::UrmaFailureReason::TRANSPORT_ERROR);
    const auto send_post =
        static_cast<size_t>(brpc::urma::UrmaFailureReason::SEND_POST);
    const auto before = brpc::urma::GetUrmaRecoveryStatus();
    // Normal client-initiated short closes must not quarantine the server.
    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(Echo(&channel));
    }
    EXPECT_EQ(0u, brpc::urma::GetUrmaRecoveryStatus().cooled_peers);
    EXPECT_EQ(before.failures[transport],
              brpc::urma::GetUrmaRecoveryStatus().failures[transport]);

    _service.fail_next_response = true;
    EXPECT_FALSE(Echo(&channel));
    ASSERT_EQ(before.failures[send_post] + 1,
              brpc::urma::GetUrmaRecoveryStatus().failures[send_post]);
    ASSERT_TRUE(WaitUntil(
        [] { return brpc::urma::GetUrmaRecoveryStatus().cooled_peers == 1; }));
    const auto cooling = brpc::urma::GetUrmaRecoveryStatus();
    EXPECT_EQ(before.failures[transport] + 1, cooling.failures[transport]);
    ASSERT_TRUE(Echo(&channel));
    const auto tcp = brpc::urma::GetUrmaRecoveryStatus();
    EXPECT_EQ(cooling.negotiated_connections, tcp.negotiated_connections);
    EXPECT_GT(tcp.peer_cooldown_admissions, cooling.peer_cooldown_admissions);

    ASSERT_TRUE(WaitUntil(
        [] { return brpc::urma::GetUrmaRecoveryStatus().cooled_peers == 0; }));
    ASSERT_TRUE(Echo(&channel));
    const auto recovered = brpc::urma::GetUrmaRecoveryStatus();
    EXPECT_GT(recovered.negotiated_connections, tcp.negotiated_connections);
    EXPECT_EQ(tcp.peer_cooldown_admissions, recovered.peer_cooldown_admissions);
    EXPECT_EQ(before.failures[send_post] + 1, recovered.failures[send_post]);
    EXPECT_EQ(before.failures[transport] + 1, recovered.failures[transport]);
}

}  // namespace
#endif  // BRPC_WITH_URMA

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}
