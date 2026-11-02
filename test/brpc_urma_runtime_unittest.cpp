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
#include <sys/socket.h>
#include <unistd.h>
#include <limits>
#include <gtest/gtest.h>
#include <gflags/gflags.h>

#if BRPC_WITH_URMA
#include "butil/atomicops.h"
#include "butil/sys_byteorder.h"
#include "urma_api.h"
#include "brpc/urma/urma_handshake.h"
#include "brpc/urma/urma_handshake.pb.h"
#include "brpc/urma/urma_helper.h"
#include "urma_types.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/input_messenger.h"
#include "brpc/urma/urma_endpoint.h"
#include "brpc/urma/urma_recovery_policy.h"
#include "butil/fd_guard.h"
#include "bvar/variable.h"
#include "echo.pb.h"

using namespace brpc;

namespace brpc {
namespace urma {

DECLARE_int32(urma_client_handshake_version);
extern bool g_skip_urma_init;
extern butil::atomic<bool> g_urma_available;

}  // namespace urma
}  // namespace brpc

TEST(UrmaRuntimeHelperTest, combined_priority_and_invalid_transport_type) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[3].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[6].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[6].tp_type.bs.rtp = 1;
    EXPECT_EQ(6, urma::FindUrmaPriorityForTpType(attr, URMA_CTP));
    EXPECT_EQ(-1, urma::FindUrmaPriorityForTpType(
                      attr, static_cast<urma_tp_type_t>(255)));
}

TEST(UrmaRuntimeHelperTest,
     auto_path_prefers_ctp_and_selects_rtp_on_rtp_only_devices) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[0].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[0].SL = 2;
    urma_tp_type_t path = URMA_CTP;
    uint8_t priority = 6;
    ASSERT_TRUE(urma::SelectUrmaTransportPath(attr, "auto", &path, &priority));
    EXPECT_EQ(URMA_RTP, path);
    EXPECT_EQ(0, priority);
    attr.dev_cap.priority_info[7].tp_type.bs.ctp = 1;
    ASSERT_TRUE(urma::SelectUrmaTransportPath(attr, "auto", &path, &priority));
    EXPECT_EQ(URMA_CTP, path);
    EXPECT_EQ(7, priority);
}

TEST(UrmaRuntimeHelperTest,
     explicit_path_uses_matching_priority_and_combined_capabilities) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[2].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[2].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[6].tp_type.bs.ctp = 1;
    urma_tp_type_t path = URMA_UTP;
    uint8_t priority = 15;
    for (const auto& requested : {"rtp", "ctp"}) {
        ASSERT_TRUE(
            urma::SelectUrmaTransportPath(attr, requested, &path, &priority));
        EXPECT_EQ(std::string(requested) == "rtp" ? URMA_RTP : URMA_CTP, path);
        EXPECT_EQ(2, priority);
    }
}

TEST(UrmaRuntimeHelperTest,
     rejects_unsupported_paths_in_a_reported_capability_table) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[0].tp_type.bs.rtp = 1;
    urma_tp_type_t path = URMA_UTP;
    uint8_t priority = 15;
    EXPECT_FALSE(urma::SelectUrmaTransportPath(attr, "ctp", &path, &priority));
    EXPECT_EQ(URMA_UTP, path);
    EXPECT_EQ(15, priority);
    attr.dev_cap.priority_info[0].tp_type.value = 0;
    attr.dev_cap.priority_info[4].tp_type.bs.utp = 1;
    EXPECT_FALSE(urma::SelectUrmaTransportPath(attr, "auto", &path, &priority));
    EXPECT_FALSE(urma::SelectUrmaTransportPath(attr, "rtp", &path, &priority));
}

TEST(UrmaRuntimeHelperTest,
     providers_without_path_capabilities_keep_compatibility_priorities) {
    urma_device_attr_t attr{};
    urma_tp_type_t path = URMA_UTP;
    uint8_t priority = 15;
    for (const auto& requested : {"auto", "ctp", "rtp"}) {
        ASSERT_TRUE(
            urma::SelectUrmaTransportPath(attr, requested, &path, &priority));
        const bool rtp = std::string(requested) == "rtp";
        EXPECT_EQ(rtp ? URMA_RTP : URMA_CTP, path);
        EXPECT_EQ(rtp ? 0 : 6, priority);
    }
}

TEST(UrmaRuntimeHelperTest, rejects_invalid_path_configuration) {
    urma_device_attr_t attr{};
    urma_tp_type_t path = URMA_CTP;
    uint8_t priority = 6;
    for (const auto& requested : {"", "utp", "RM", "typo"}) {
        EXPECT_FALSE(
            urma::SelectUrmaTransportPath(attr, requested, &path, &priority));
    }
    EXPECT_FALSE(
        urma::SelectUrmaTransportPath(attr, "auto", nullptr, &priority));
    EXPECT_FALSE(urma::SelectUrmaTransportPath(attr, "auto", &path, nullptr));
}

TEST(UrmaRuntimeHelperTest,
     local_sge_initializes_optional_fields_and_accepts_segment_edges) {
    urma_target_seg_t segment{};
    segment.urma_ctx = urma::GetUrmaContext();
    segment.seg.ubva.va = 0x1000;
    segment.seg.len = 8192;
    urma_sge_t sge;
    memset(&sge, 0xff, sizeof(sge));
    ASSERT_TRUE(urma::InitUrmaLocalSge(&sge, 0x1000, 8192, &segment));
    EXPECT_EQ(0x1000u, sge.addr);
    EXPECT_EQ(8192u, sge.len);
    EXPECT_EQ(&segment, sge.tseg);
    EXPECT_EQ(nullptr, sge.user_tseg);
    ASSERT_TRUE(urma::InitUrmaLocalSge(&sge, 0x2fff, 1, &segment));
    EXPECT_EQ(0x2fffu, sge.addr);
    EXPECT_EQ(1u, sge.len);
}

TEST(UrmaRuntimeHelperTest, local_sge_rejects_out_of_range_and_wrong_context) {
    urma_target_seg_t segment{};
    segment.urma_ctx = urma::GetUrmaContext();
    segment.seg.ubva.va = 0x1000;
    segment.seg.len = 8192;
    urma_sge_t sge{};
    for (auto values : {std::pair<uint64_t, size_t>(0xfff, 1),
                        {0x3000, 1},
                        {0x2fff, 2},
                        {0x1000, 0},
                        {0x1000, static_cast<size_t>(UINT32_MAX) + 1}}) {
        EXPECT_FALSE(urma::InitUrmaLocalSge(&sge, values.first, values.second,
                                            &segment));
        EXPECT_EQ(ERDMAMEM, errno);
    }
    EXPECT_FALSE(urma::InitUrmaLocalSge(&sge, 0x1000, 1, nullptr));
    segment.urma_ctx = reinterpret_cast<urma_context_t*>(1);
    EXPECT_FALSE(urma::InitUrmaLocalSge(&sge, 0x1000, 1, &segment));
    segment.urma_ctx = urma::GetUrmaContext();
    segment.seg.ubva.va = UINT64_MAX - 7;
    segment.seg.len = 16;
    EXPECT_FALSE(urma::InitUrmaLocalSge(&sge, UINT64_MAX - 3, 2, &segment));
}

static urma_target_seg_t g_copy_test_segment;
static int g_copy_test_releases;

static urma_target_seg_t* ResolveCopyTestSegment(void* address) {
    g_copy_test_segment = urma_target_seg_t{};
    g_copy_test_segment.urma_ctx = urma::GetUrmaContext();
    g_copy_test_segment.seg.ubva.va = reinterpret_cast<uint64_t>(address);
    g_copy_test_segment.seg.len = urma::GetUrmaRecvBlockSize();
    return &g_copy_test_segment;
}

static urma_target_seg_t* RejectCopyTestSegment(void*) { return nullptr; }

static void DeleteCopyTestData(void* data) {
    ++g_copy_test_releases;
    delete[] static_cast<char*>(data);
}

TEST(UrmaRuntimeHelperTest,
     copied_send_coalesces_host_fragments_and_retains_aligned_payload) {
    g_copy_test_releases = 0;
    butil::IOBuf buffer;
    for (const char* text : {"first", "second"}) {
        const size_t length = strlen(text);
        char* data = new char[length];
        memcpy(data, text, length);
        ASSERT_EQ(0, buffer.append_user_data(data, length, DeleteCopyTestData));
    }
    ASSERT_EQ(2u, buffer.backing_block_num());
    urma_sge_t sge{};
    ASSERT_TRUE(
        urma::CopyUrmaSendBuffer(&buffer, &sge, ResolveCopyTestSegment));
    EXPECT_EQ(2, g_copy_test_releases);
    EXPECT_EQ("firstsecond", buffer.to_string());
    ASSERT_EQ(1u, buffer.backing_block_num());
    EXPECT_EQ(0u, sge.addr % urma::URMA_SEND_BUFFER_ALIGNMENT);
    EXPECT_EQ(reinterpret_cast<uint64_t>(buffer.backing_block(0).data()),
              sge.addr);
    EXPECT_EQ(buffer.size(), sge.len);
    EXPECT_EQ(&g_copy_test_segment, sge.tseg);
    EXPECT_EQ(nullptr, sge.user_tseg);
    EXPECT_EQ("firstsecond",
              std::string(reinterpret_cast<const char*>(sge.addr), sge.len));

    buffer.clear();
    const std::string large(
        urma::GetUrmaRecvBlockSize() - (urma::URMA_SEND_BUFFER_ALIGNMENT - 1),
        'x');
    buffer.append(large);
    ASSERT_TRUE(
        urma::CopyUrmaSendBuffer(&buffer, &sge, ResolveCopyTestSegment));
    EXPECT_EQ(0u, sge.addr % urma::URMA_SEND_BUFFER_ALIGNMENT);
    EXPECT_EQ(large, buffer.to_string());
    EXPECT_EQ(large.size(), sge.len);
}

TEST(UrmaRuntimeHelperTest, copied_send_failure_preserves_source_payload) {
    butil::IOBuf buffer;
    buffer.append("unchanged");
    urma_sge_t sge{};
    EXPECT_FALSE(
        urma::CopyUrmaSendBuffer(&buffer, &sge, RejectCopyTestSegment));
    EXPECT_EQ(ERDMAMEM, errno);
    EXPECT_EQ("unchanged", buffer.to_string());
    const std::string large(urma::GetUrmaRecvBlockSize(), 'y');
    buffer.clear();
    buffer.append(large);
    EXPECT_FALSE(
        urma::CopyUrmaSendBuffer(&buffer, &sge, ResolveCopyTestSegment));
    EXPECT_EQ(EMSGSIZE, errno);
    EXPECT_EQ(large, buffer.to_string());
}

class UrmaRuntimeMockTest : public ::testing::Test {
protected:
    void SetUp() override {
        urma::g_skip_urma_init = false;

        urma_init_attr_t init_attr{};
        const urma_status_t status = urma_init(&init_attr);
        ASSERT_TRUE(status == URMA_SUCCESS || status == URMA_EEXIST);
        _owns_urma_init = status == URMA_SUCCESS;

        int num_devices = 0;
        urma_device_t** devices = urma_get_device_list(&num_devices);
        ASSERT_NE(nullptr, devices);
        ASSERT_GT(num_devices, 0);
        const bool using_mock =
            strcmp(devices[0]->name, "mock_urma_device") == 0;
        urma_free_device_list(devices);
        if (!using_mock) {
            if (_owns_urma_init) {
                EXPECT_EQ(URMA_SUCCESS, urma_uninit());
                _owns_urma_init = false;
            }
            GTEST_SKIP()
                << "UrmaRuntimeMockTest requires the URMA link-time mock";
        }
    }

    void TearDown() override {
        if (_owns_urma_init) {
            EXPECT_EQ(URMA_SUCCESS, urma_uninit());
        }
        urma::g_skip_urma_init = true;
        urma::g_urma_available.store(true, butil::memory_order_relaxed);
    }

private:
    bool _owns_urma_init{false};
};

namespace brpc {
namespace urma {

class UrmaRecoveryTestPeer {
public:
    static int ImportPeer(UrmaEndpoint* ep, const ParsedHello& peer) {
        return ep->ImportPeer(peer);
    }
    static void Establish(UrmaEndpoint* ep) {
        GlobalUrmaRecoveryPolicy().AllowNewConnection("", &ep->_recovery_epoch);
        ep->_state = UrmaEndpoint::ESTABLISHED;
    }
    static ssize_t Complete(UrmaEndpoint* ep, urma_cr_status_t status) {
        urma_cr_t cr{};
        cr.status = status;
        return ep->HandleCompletion(cr);
    }
    static void Report(UrmaEndpoint* ep, UrmaFailureReason reason) {
        ep->ReportFailure(reason);
    }
    static void PollFailure(UrmaEndpoint* ep) {
        UrmaResource resource;
        urma_jfc_t invalid_jfc{};
        resource.jfc = &invalid_jfc;
        ep->_resource = &resource;
        SocketOptions options;
        options.user = ep;
        SocketId sid;
        if (Socket::Create(options, &sid) == 0) {
            SocketUniquePtr carrier;
            if (Socket::Address(sid, &carrier) == 0) {
                const bool polling = FLAGS_urma_use_polling;
                FLAGS_urma_use_polling = true;
                UrmaEndpoint::PollCq(carrier.get());
                FLAGS_urma_use_polling = polling;
                carrier->SetFailed();
            }
        }
        ep->_resource = nullptr;
        resource.jfc = nullptr;
    }
    static bool Backpressure(UrmaEndpoint* ep) {
        UrmaResource resource;
        urma_jetty_t jetty{};
        urma_target_jetty_t remote{};
        resource.jetty = &jetty;
        resource.remote_jetty = &remote;
        ep->_resource = &resource;
        ep->_sq_window_size = 0;
        ep->_remote_rq_window_size = 1;
        butil::IOBuf data;
        data.append("payload");
        butil::IOBuf* buf = &data;
        const ssize_t rc = ep->CutFromIOBufList(&buf, 1);
        const bool ok = rc == -1 && errno == EAGAIN && data.size() == 7;
        ep->_resource = nullptr;
        resource.jetty = nullptr;
        resource.remote_jetty = nullptr;
        return ok;
    }
    static int FailedAck(UrmaEndpoint* ep) {
        // A Jetty without an imported target fails provider submission.
        UrmaResource resource;
        urma_jetty_t jetty{};
        urma_target_jetty_t remote{};
        resource.jetty = &jetty;
        resource.remote_jetty = &remote;
        ep->_resource = &resource;
        ep->_sq_imm_window_size = 1;
        ep->_remote_window_capacity = 2;
        ep->_new_rq_wrs = 2;
        urma_cr_t cr{};
        cr.status = URMA_CR_SUCCESS;
        cr.user_ctx = 0;
        const int rc = ep->HandleCompletion(cr);
        ep->_resource = nullptr;
        resource.jetty = nullptr;
        resource.remote_jetty = nullptr;
        return rc;
    }
};

}  // namespace urma
}  // namespace brpc

class UrmaRecoveryTest : public UrmaRuntimeMockTest {
protected:
    void SetUp() override {
        UrmaRuntimeMockTest::SetUp();
        if (IsSkipped() || HasFatalFailure()) {
            return;
        }
        urma::g_skip_urma_init = true;
        urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::LEGACY);
        urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::AUTO);
    }
    void TearDown() override {
        urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::LEGACY);
        UrmaRuntimeMockTest::TearDown();
    }
    void CreateSocket(SocketUniquePtr* socket) {
        SocketOptions opt;
        butil::str2endpoint("127.0.0.1:8123", &opt.remote_side);
        opt.user = get_client_side_messenger();
        SocketId sid;
        ASSERT_EQ(0, Socket::Create(opt, &sid));
        ASSERT_EQ(0, Socket::Address(sid, socket));
    }
};

TEST_F(UrmaRecoveryTest, recovery_mode_and_metrics) {
    ASSERT_TRUE(urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::TCP_ONLY));
    EXPECT_FALSE(urma::GlobalUrmaRecoveryPolicy().AllowNewConnection("peer"));
    EXPECT_FALSE(urma::SetUrmaRecoveryCooldownMs(0));
    EXPECT_TRUE(urma::SetUrmaRecoveryCooldownMs(5000));
    EXPECT_NE(std::string::npos,
              bvar::Variable::describe_exposed("urma_recovery_status")
                  .find("tcp_only"));
    EXPECT_FALSE(bvar::Variable::describe_exposed(
                     "urma_recovery_tcp_fallback_admissions")
                     .empty());
}

TEST_F(UrmaRecoveryTest, rejects_mismatched_path_before_importing_resources) {
    urma::UrmaEndpoint endpoint(nullptr);
    urma::ParsedHello peer;
    peer.tp_type = urma::GetUrmaTpType() == URMA_CTP ? URMA_RTP : URMA_CTP;
    EXPECT_EQ(-1, urma::UrmaRecoveryTestPeer::ImportPeer(&endpoint, peer));
    EXPECT_EQ(EPROTO, errno);
    peer.tp_type = URMA_UTP;
    EXPECT_EQ(-1, urma::UrmaRecoveryTestPeer::ImportPeer(&endpoint, peer));
    EXPECT_EQ(EPROTO, errno);
}

TEST_F(UrmaRecoveryTest, completion_errors_cool_peer_once) {
    for (auto status : {URMA_CR_ACK_TIMEOUT_ERR, URMA_CR_RNR_RETRY_CNT_EXC_ERR,
                        URMA_CR_LOC_ACCESS_ERR, URMA_CR_REM_OPERATION_ERR}) {
        urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::LEGACY);
        urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::AUTO);
        SocketUniquePtr socket;
        ASSERT_NO_FATAL_FAILURE(CreateSocket(&socket));
        urma::UrmaEndpoint ep(socket.get());
        urma::UrmaRecoveryTestPeer::Establish(&ep);
        const auto before = urma::GetUrmaRecoveryStatus();
        EXPECT_EQ(-1, urma::UrmaRecoveryTestPeer::Complete(&ep, status));
        const auto reason = status == URMA_CR_ACK_TIMEOUT_ERR
                                ? urma::UrmaFailureReason::RETRY_EXHAUSTED
                            : status == URMA_CR_RNR_RETRY_CNT_EXC_ERR
                                ? urma::UrmaFailureReason::RNR
                            : status == URMA_CR_LOC_ACCESS_ERR
                                ? urma::UrmaFailureReason::ACCESS_ERROR
                                : urma::UrmaFailureReason::TRANSPORT_ERROR;
        EXPECT_EQ(before.failures[static_cast<size_t>(reason)] + 1,
                  urma::GetUrmaRecoveryStatus()
                      .failures[static_cast<size_t>(reason)]);
        EXPECT_EQ(status == URMA_CR_LOC_ACCESS_ERR,
                  urma::GlobalUrmaRecoveryPolicy().AllowNewConnection(
                      "127.0.0.1:8123"));
        urma::UrmaRecoveryTestPeer::Report(&ep,
                                           urma::UrmaFailureReason::CQ_POLL);
        EXPECT_EQ(before.failures[static_cast<size_t>(
                      urma::UrmaFailureReason::CQ_POLL)],
                  urma::GetUrmaRecoveryStatus().failures[static_cast<size_t>(
                      urma::UrmaFailureReason::CQ_POLL)]);
        socket->SetFailed();
    }
}

TEST_F(UrmaRecoveryTest, flush_backpressure_and_old_epoch_do_not_cool_peer) {
    SocketUniquePtr socket;
    ASSERT_NO_FATAL_FAILURE(CreateSocket(&socket));
    urma::UrmaEndpoint ep(socket.get());
    urma::UrmaRecoveryTestPeer::Establish(&ep);
    EXPECT_TRUE(urma::UrmaRecoveryTestPeer::Backpressure(&ep));
    EXPECT_EQ(-1,
              urma::UrmaRecoveryTestPeer::Complete(&ep, URMA_CR_WR_FLUSH_ERR));
    EXPECT_TRUE(
        urma::GlobalUrmaRecoveryPolicy().AllowNewConnection("127.0.0.1:8123"));
    urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::TCP_ONLY);
    urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::AUTO);
    urma::UrmaRecoveryTestPeer::Report(&ep, urma::UrmaFailureReason::CQ_POLL);
    EXPECT_TRUE(
        urma::GlobalUrmaRecoveryPolicy().AllowNewConnection("127.0.0.1:8123"));
    socket->SetFailed();
}

TEST_F(UrmaRecoveryTest, ack_post_failure_propagates) {
    SocketUniquePtr socket;
    ASSERT_NO_FATAL_FAILURE(CreateSocket(&socket));
    urma::UrmaEndpoint ep(socket.get());
    urma::UrmaRecoveryTestPeer::Establish(&ep);
    EXPECT_EQ(-1, urma::UrmaRecoveryTestPeer::FailedAck(&ep));
    EXPECT_FALSE(
        urma::GlobalUrmaRecoveryPolicy().AllowNewConnection("127.0.0.1:8123"));
    socket->SetFailed();
}

TEST_F(UrmaRecoveryTest, jfc_poll_failure_fails_socket_and_cools_peer) {
    SocketUniquePtr socket;
    ASSERT_NO_FATAL_FAILURE(CreateSocket(&socket));
    urma::UrmaEndpoint ep(socket.get());
    urma::UrmaRecoveryTestPeer::Establish(&ep);
    const auto before = urma::GetUrmaRecoveryStatus();
    urma::UrmaRecoveryTestPeer::PollFailure(&ep);
    EXPECT_TRUE(socket->Failed());
    EXPECT_FALSE(
        urma::GlobalUrmaRecoveryPolicy().AllowNewConnection("127.0.0.1:8123"));
    EXPECT_EQ(
        before.failures[static_cast<size_t>(urma::UrmaFailureReason::CQ_POLL)] +
            1,
        urma::GetUrmaRecoveryStatus()
            .failures[static_cast<size_t>(urma::UrmaFailureReason::CQ_POLL)]);
}

class UrmaRecoveryEcho : public ::test::EchoService {
    void Echo(google::protobuf::RpcController*,
              const ::test::EchoRequest* request,
              ::test::EchoResponse* response,
              google::protobuf::Closure* done) override {
        response->set_message(request->message());
        done->Run();
    }
};

TEST_F(UrmaRecoveryTest, tcp_only_client_completes_rpc_on_urma_server) {
    urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::TCP_ONLY);
    UrmaRecoveryEcho service;
    Server server;
    ASSERT_EQ(0, server.AddService(&service, SERVER_DOESNT_OWN_SERVICE));
    ServerOptions so;
    so.socket_mode = SOCKET_MODE_URMA;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &so));
    Channel channel;
    ChannelOptions co;
    co.socket_mode = SOCKET_MODE_URMA;
    co.connection_type = "short";
    co.timeout_ms = 2000;
    ASSERT_EQ(0, channel.Init(server.listen_address(), &co));
    ::test::EchoService_Stub stub(&channel);
    Controller cntl;
    ::test::EchoRequest request;
    ::test::EchoResponse response;
    request.set_message("recovery over TCP");
    stub.Echo(&cntl, &request, &response, nullptr);
    EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ(request.message(), response.message());
    urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::AUTO);
    SocketOptions failed_options;
    failed_options.remote_side = server.listen_address();
    failed_options.user = get_client_side_messenger();
    SocketId failed_sid;
    ASSERT_EQ(0, Socket::Create(failed_options, &failed_sid));
    SocketUniquePtr failed_socket;
    ASSERT_EQ(0, Socket::Address(failed_sid, &failed_socket));
    {
        urma::UrmaEndpoint failed_ep(failed_socket.get());
        urma::UrmaRecoveryTestPeer::Establish(&failed_ep);
        EXPECT_EQ(-1, urma::UrmaRecoveryTestPeer::Complete(
                          &failed_ep, URMA_CR_RNR_RETRY_CNT_EXC_ERR));
    }
    failed_socket->SetFailed();
    const auto before = urma::GetUrmaRecoveryStatus();
    cntl.Reset();
    response.Clear();
    stub.Echo(&cntl, &request, &response, nullptr);
    EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ(request.message(), response.message());
    EXPECT_EQ(before.peer_cooldown_admissions + 1,
              urma::GetUrmaRecoveryStatus().peer_cooldown_admissions);
    server.Stop(0);
    server.Join();
}

TEST_F(UrmaRecoveryTest, server_declines_v2_and_v3_without_allocating_jetty) {
    urma::SetUrmaRecoveryMode(urma::UrmaRecoveryMode::TCP_ONLY);
    Server server;
    ServerOptions so;
    so.socket_mode = SOCKET_MODE_URMA;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &so));
    for (int version : {2, 3}) {
        butil::fd_guard fd(
            butil::tcp_connect(server.listen_address(), nullptr));
        ASSERT_GE(fd, 0);
        timeval timeout{2, 0};
        ASSERT_EQ(0, setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                sizeof(timeout)));
        urma::v2_wire::HelloMessage hello;
        hello.msg_len = urma::v2_wire::HELLO_PACKET_LEN;
        hello.hello_ver = urma::v2_wire::HELLO_V2_VERSION;
        hello.impl_ver = urma::v2_wire::IMPL_V2_VERSION;
        hello.buffer_size = 8192;
        hello.recv_buffer_cnt = 127;
        hello.jetty_id = 42;
        memset(hello.eid, 1, 16);
        memset(hello.seg_eid, 1, 16);
        hello.tp_type = URMA_CTP;
        hello.seg_va = 0x100000;
        hello.seg_len = 1024 * 1024;
        if (version == 2) {
            uint8_t packet[urma::v2_wire::HELLO_PACKET_LEN];
            memcpy(packet, "URMA", 4);
            hello.Serialize(packet + 4);
            ASSERT_EQ(sizeof(packet),
                      static_cast<size_t>(write(fd, packet, sizeof(packet))));
            uint8_t reply[sizeof(packet)];
            ASSERT_EQ(sizeof(reply),
                      static_cast<size_t>(
                          recv(fd, reply, sizeof(reply), MSG_WAITALL)));
            EXPECT_EQ(0, memcmp(reply, "URMA", 4));
            urma::v2_wire::HelloMessage rejected;
            rejected.Deserialize(reply + 4);
            EXPECT_EQ(0, rejected.hello_ver);
            EXPECT_EQ(0u, rejected.jetty_id);
        } else {
            urma::UrmaHello msg;
            msg.set_buffer_size(8192);
            msg.set_recv_buffer_cnt(127);
            msg.set_jetty_id(42);
            msg.set_uasid(0);
            msg.set_seg_uasid(0);
            msg.set_seg_token_id(0);
            msg.set_eid(hello.eid, 16);
            msg.set_seg_eid(hello.seg_eid, 16);
            msg.set_tp_type(URMA_CTP);
            msg.set_seg_va(hello.seg_va);
            msg.set_seg_len(hello.seg_len);
            const std::string body = msg.SerializeAsString();
            uint32_t size = butil::HostToNet32(body.size());
            std::string packet("URM3", 4);
            packet.append(reinterpret_cast<const char*>(&size), 4);
            packet.append(body);
            ASSERT_EQ(packet.size(), static_cast<size_t>(write(
                                         fd, packet.data(), packet.size())));
            char header[8];
            ASSERT_EQ(8, recv(fd, header, 8, MSG_WAITALL));
            EXPECT_EQ(0, memcmp(header, "URM3", 4));
            memcpy(&size, header + 4, 4);
            size = butil::NetToHost32(size);
            ASSERT_LE(size, 4096u);
            std::string reply(size, '\0');
            ASSERT_EQ(size, static_cast<uint32_t>(
                                recv(fd, &reply[0], size, MSG_WAITALL)));
            urma::UrmaHello rejected;
            ASSERT_TRUE(rejected.ParseFromString(reply));
            EXPECT_EQ(0u, rejected.jetty_id());
        }
        const std::string request =
            "GET /version HTTP/1.1\r\nHost: localhost\r\n\r\n";
        ASSERT_EQ(request.size(), static_cast<size_t>(write(fd, request.data(),
                                                            request.size())));
        char response[4096];
        const ssize_t nr = recv(fd, response, sizeof(response), 0);
        ASSERT_GT(nr, 0);
        EXPECT_NE(std::string::npos, std::string(response, nr).find(" 200 "));
    }
    server.Stop(0);
    server.Join();
}

#endif  // BRPC_WITH_URMA

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
#if BRPC_WITH_URMA
    urma::g_skip_urma_init = true;
    urma::g_urma_available.store(true, butil::memory_order_relaxed);
#endif
    return RUN_ALL_TESTS();
}
