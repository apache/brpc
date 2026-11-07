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
#include <sstream>
#include <vector>
#include <gtest/gtest.h>
#include <gflags/gflags.h>

#if BRPC_WITH_URMA
#include "butil/logging.h"
#include "butil/memory/scope_guard.h"
#include "brpc/global.h"
#include "brpc/acceptor.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/input_messenger.h"
#include "brpc/load_balancer.h"
#include "brpc/urma/urma_endpoint.h"
#include "brpc/urma/urma_helper.h"
#include "brpc/urma/urma_handshake.h"
#include "brpc/urma_transport.h"
#include "echo.pb.h"

using namespace brpc;

namespace brpc {
namespace urma {
DECLARE_int32(urma_client_handshake_version);
DECLARE_string(urma_tp_type);
extern bool g_skip_urma_init;
DECLARE_int32(urma_buffer_count);
DECLARE_int32(urma_prepared_jetty_cnt);

class UrmaQosTestPeer {
public:
    static int Allocate(UrmaEndpoint* ep) { return ep->AllocateResources(); }
    static int Priority(UrmaEndpoint* ep) {
        return ep->_resource->jetty->jetty_cfg.jfs_cfg.priority;
    }
    static ssize_t Complete(UrmaEndpoint* ep, const urma_cr_t& cr) {
        return ep->HandleCompletion(cr);
    }
    static int Import(UrmaEndpoint* ep, const ParsedHello& hello) {
        return ep->ImportPeer(hello);
    }
    static ParsedHello Hello(UrmaEndpoint* ep) {
        ParsedHello hello;
        ep->MakeLocalParsedHello(&hello);
        return hello;
    }
};

}  // namespace urma
}  // namespace brpc

TEST(UrmaHelperTest, explicit_priority_requires_ctp_capability) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[0].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[15].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[15].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[3].tp_type.bs.rtp = 1;
    EXPECT_TRUE(urma::IsUrmaPrioritySupported(attr, 0, URMA_CTP));
    EXPECT_TRUE(urma::IsUrmaPrioritySupported(attr, 15, URMA_CTP));
    for (int priority : {-2, -1, 3, 4, 16}) {
        EXPECT_FALSE(urma::IsUrmaPrioritySupported(attr, priority, URMA_CTP));
    }
}

TEST(UrmaHelperTest, automatic_priority_accepts_combined_capabilities) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[3].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[10].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[10].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[15].tp_type.bs.ctp = 1;
    EXPECT_EQ(3, urma::FindUrmaPriorityForTpType(attr, URMA_RTP));
    EXPECT_EQ(10, urma::FindUrmaPriorityForTpType(attr, URMA_CTP));
    EXPECT_TRUE(urma::IsUrmaPrioritySupported(attr, 10, URMA_CTP));
    EXPECT_EQ(-1, urma::FindUrmaPriorityForTpType(attr, URMA_UTP));
    EXPECT_EQ(-1, urma::FindUrmaPriorityForTpType(
                      attr, static_cast<urma_tp_type_t>(-1)));
    attr.dev_cap.priority_info[10].tp_type.bs.ctp = 0;
    EXPECT_EQ(15, urma::FindUrmaPriorityForTpType(attr, URMA_CTP));
    attr.dev_cap.priority_info[15].tp_type.bs.ctp = 0;
    EXPECT_EQ(-1, urma::FindUrmaPriorityForTpType(attr, URMA_CTP));
}

TEST(UrmaHelperTest, service_level_selects_ctp_priority_from_mapping) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[3].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[3].SL = 6;
    attr.dev_cap.priority_info[6].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[6].SL = 7;
    attr.dev_cap.priority_info[10].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[10].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[10].SL = 6;
    attr.dev_cap.priority_info[15].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[15].SL = 6;
    UrmaQosOptions qos;
    uint8_t priority = 0;
    qos.service_level = 6;
    ASSERT_TRUE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_CTP, 6, &priority));
    EXPECT_EQ(10, priority);
    qos.service_level = 7;
    ASSERT_TRUE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_CTP, 6, &priority));
    EXPECT_EQ(6, priority);
    qos.priority = 6;
    ASSERT_TRUE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_CTP, 6, &priority));
    EXPECT_EQ(6, priority);
    attr.dev_cap.priority_info[15].SL = 255;
    qos.priority = -1;
    qos.service_level = 255;
    ASSERT_TRUE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_CTP, 6, &priority));
    EXPECT_EQ(15, priority);
    qos.service_level = -1;
    attr = urma_device_attr_t{};
    ASSERT_TRUE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_CTP, 6, &priority));
    EXPECT_EQ(6, priority);
}

TEST(UrmaHelperTest,
     service_level_rejects_unsupported_and_conflicting_mapping) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[3].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[3].SL = 3;
    attr.dev_cap.priority_info[6].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[6].SL = 2;
    attr.dev_cap.priority_info[10].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[10].SL = 4;
    UrmaQosOptions qos;
    uint8_t priority = 255;
    for (int sl : {-2, 0, 3, 6, 255, 256}) {
        qos.service_level = sl;
        EXPECT_FALSE(
            urma::ResolveUrmaJettyPriority(attr, qos, URMA_CTP, 6, &priority));
        EXPECT_EQ(EINVAL, errno);
        EXPECT_EQ(255, priority);
    }
    qos.priority = 6;
    qos.service_level = 4;
    EXPECT_FALSE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_CTP, 6, &priority));
    EXPECT_EQ(EINVAL, errno);
    EXPECT_EQ(255, priority);
#if !BRPC_WITH_GLOG
    qos.priority = -1;
    qos.service_level = 3;
    logging::StringSink log;
    logging::LogSink* old_sink = logging::SetLogSink(&log);
    const bool resolved =
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_CTP, 6, &priority);
    logging::SetLogSink(old_sink);
    EXPECT_FALSE(resolved);
    EXPECT_NE(std::string::npos, log.find("SL 3 has no CTP priority")) << log;
    EXPECT_NE(std::string::npos, log.find("[6:2,10:4]")) << log;
#endif
}

TEST(UrmaPathQosTest, automatic_and_explicit_paths_use_device_capabilities) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[0].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[0].SL = 2;
    urma_tp_type_t path = URMA_CTP;
    uint8_t priority = 15;
    ASSERT_TRUE(urma::SelectUrmaTransportPath(attr, "auto", &path, &priority));
    EXPECT_EQ(URMA_RTP, path);
    EXPECT_EQ(0, priority);
    attr.dev_cap.priority_info[6].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[6].tp_type.bs.rtp = 1;
    ASSERT_TRUE(urma::SelectUrmaTransportPath(attr, "auto", &path, &priority));
    EXPECT_EQ(URMA_CTP, path);
    EXPECT_EQ(6, priority);
    ASSERT_TRUE(urma::SelectUrmaTransportPath(attr, "rtp", &path, &priority));
    EXPECT_EQ(URMA_RTP, path);
    EXPECT_EQ(0, priority);
}

TEST(UrmaPathQosTest, unsupported_paths_preserve_outputs) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[0].tp_type.bs.rtp = 1;
    urma_tp_type_t path = URMA_UTP;
    uint8_t priority = 15;
    for (const char* request : {"ctp", "utp", "", "typo"}) {
        EXPECT_FALSE(
            urma::SelectUrmaTransportPath(attr, request, &path, &priority));
        EXPECT_EQ(URMA_UTP, path);
        EXPECT_EQ(15, priority);
    }
    EXPECT_FALSE(
        urma::SelectUrmaTransportPath(attr, "rtp", nullptr, &priority));
    EXPECT_FALSE(urma::SelectUrmaTransportPath(attr, "rtp", &path, nullptr));
    attr.dev_cap.priority_info[0].tp_type.value = 0;
    attr.dev_cap.priority_info[4].tp_type.bs.utp = 1;
    EXPECT_FALSE(urma::SelectUrmaTransportPath(attr, "auto", &path, &priority));
}

TEST(UrmaPathQosTest, empty_capability_table_uses_compatibility_defaults) {
    urma_device_attr_t attr{};
    urma_tp_type_t path = URMA_UTP;
    uint8_t priority = 15;
    for (const char* request : {"auto", "ctp", "rtp"}) {
        ASSERT_TRUE(
            urma::SelectUrmaTransportPath(attr, request, &path, &priority));
        const bool rtp = std::string(request) == "rtp";
        EXPECT_EQ(rtp ? URMA_RTP : URMA_CTP, path);
        EXPECT_EQ(rtp ? 0 : 6, priority);
        UrmaQosOptions qos;
        uint8_t resolved = 15;
        ASSERT_TRUE(urma::ResolveUrmaJettyPriority(attr, qos, path, priority,
                                                   &resolved));
        EXPECT_EQ(priority, resolved);
        qos.service_level = 2;
        EXPECT_FALSE(urma::ResolveUrmaJettyPriority(attr, qos, path, priority,
                                                    &resolved));
    }
}

TEST(UrmaPathQosTest, service_level_resolution_is_scoped_to_the_selected_path) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[0].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[0].SL = 2;
    attr.dev_cap.priority_info[4].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[4].SL = 4;
    attr.dev_cap.priority_info[6].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[6].SL = 2;
    attr.dev_cap.priority_info[10].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[10].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[10].SL = 4;
    UrmaQosOptions qos;
    qos.service_level = 2;
    uint8_t priority = 15;
    ASSERT_TRUE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_RTP, 0, &priority));
    EXPECT_EQ(0, priority);
    ASSERT_TRUE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_CTP, 6, &priority));
    EXPECT_EQ(6, priority);
    qos.service_level = 4;
    ASSERT_TRUE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_RTP, 0, &priority));
    EXPECT_EQ(4, priority);
    qos.priority = 10;
    for (auto path : {URMA_RTP, URMA_CTP}) {
        ASSERT_TRUE(
            urma::ResolveUrmaJettyPriority(attr, qos, path, 0, &priority));
        EXPECT_EQ(10, priority);
    }
}

TEST(UrmaPathQosTest, rejects_cross_path_priorities_and_conflicting_sl) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[0].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[0].SL = 2;
    attr.dev_cap.priority_info[6].tp_type.bs.ctp = 1;
    attr.dev_cap.priority_info[6].SL = 6;
    UrmaQosOptions qos;
    uint8_t priority = 15;
    qos.service_level = 6;
    EXPECT_FALSE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_RTP, 0, &priority));
    qos.priority = 6;
    EXPECT_FALSE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_RTP, 0, &priority));
    qos.priority = 0;
    EXPECT_FALSE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_CTP, 6, &priority));
    EXPECT_FALSE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_RTP, 0, &priority));
    EXPECT_EQ(15, priority);
    EXPECT_EQ(EINVAL, errno);
}

TEST(UrmaPathQosTest, rejects_invalid_resolution_arguments) {
    urma_device_attr_t attr{};
    UrmaQosOptions qos;
    uint8_t priority = 15;
    EXPECT_FALSE(
        urma::ResolveUrmaJettyPriority(attr, qos, URMA_RTP, 0, nullptr));
    EXPECT_FALSE(urma::ResolveUrmaJettyPriority(qos, nullptr));
    for (auto path : {URMA_UTP, static_cast<urma_tp_type_t>(255)}) {
        EXPECT_FALSE(
            urma::ResolveUrmaJettyPriority(attr, qos, path, 0, &priority));
        EXPECT_FALSE(urma::IsUrmaPrioritySupported(attr, 0, path));
    }
    EXPECT_EQ(15, priority);
    EXPECT_EQ(EINVAL, errno);
}

class UrmaQosTest : public ::testing::Test {
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
        urma::g_skip_urma_init = false;
        urma::FLAGS_urma_buffer_count = 1024;
        urma::FLAGS_urma_prepared_jetty_cnt = 2;
        urma::GlobalUrmaInitializeOrDie();
    }
    void SetUp() override {
        if (!_mock) {
            GTEST_SKIP() << "URMA QoS integration tests use the link-time mock";
        }
        ASSERT_TRUE(urma::IsUrmaAvailable());
        ASSERT_NE(nullptr, urma::GetUrmaContext());
    }
    static bool _mock;
    static urma::UrmaEndpoint* Endpoint(SocketUniquePtr& socket) {
        return static_cast<UrmaTransport*>(socket->_transport.get())
            ->GetUrmaEp();
    }
    static int Sl2Priority() {
        return urma::GetUrmaTpType() == URMA_RTP ? 0 : 6;
    }
};
bool UrmaQosTest::_mock = false;

TEST_F(UrmaQosTest, service_level_requires_capability_on_the_selected_path) {
    ChannelOptions options;
    options.socket_mode = SOCKET_MODE_URMA;
    options.urma_qos.service_level = 3;
    Channel channel;
    if (urma::GetUrmaTpType() == URMA_CTP) {
        EXPECT_EQ(-1, channel.Init("127.0.0.1:8123", &options));
    } else {
        ASSERT_EQ(0, channel.Init("127.0.0.1:8123", &options));
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(channel._server_id, &socket));
        ASSERT_EQ(0, urma::UrmaQosTestPeer::Allocate(Endpoint(socket)));
        EXPECT_EQ(3, urma::UrmaQosTestPeer::Priority(Endpoint(socket)));
        EXPECT_EQ(3, urma::GetUrmaServiceLevelForPriority(3));
    }
}

TEST_F(UrmaQosTest,
       advertises_selected_path_and_rejects_mismatch_before_import) {
    SocketOptions options;
    options.socket_mode = SOCKET_MODE_URMA;
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));
    auto* endpoint = Endpoint(socket);
    auto hello = urma::UrmaQosTestPeer::Hello(endpoint);
    EXPECT_EQ(static_cast<uint8_t>(urma::GetUrmaTpType()), hello.tp_type);
    for (auto path :
         {URMA_CTP, URMA_RTP, URMA_UTP, static_cast<urma_tp_type_t>(255)}) {
        if (path == urma::GetUrmaTpType()) {
            continue;
        }
        hello.tp_type = static_cast<uint8_t>(path);
        EXPECT_EQ(-1, urma::UrmaQosTestPeer::Import(endpoint, hello));
        EXPECT_EQ(EPROTO, errno);
    }
    socket->SetFailed();
}

TEST_F(UrmaQosTest, rejects_invalid_and_unsupported_priority) {
    butil::EndPoint address;
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:8123", &address));
    for (int priority : {-2, 16, 3, 4, 9}) {
        if (priority == 3 && urma::GetUrmaTpType() == URMA_RTP) {
            continue;
        }
        ChannelOptions co;
        co.socket_mode = SOCKET_MODE_URMA;
        co.urma_qos.priority = priority;
        Channel channel;
        EXPECT_EQ(-1, channel.Init(address, &co));
        ServerOptions so;
        so.socket_mode = SOCKET_MODE_URMA;
        so.urma_qos = co.urma_qos;
        Server server;
        EXPECT_EQ(-1, server.Start("127.0.0.1:0", &so));
        if (!co.urma_qos.IsValid()) {
            SocketOptions options;
            options.socket_mode = SOCKET_MODE_URMA;
            options.urma_qos = co.urma_qos;
            SocketId sid;
            EXPECT_EQ(-1, Socket::Create(options, &sid));
            EXPECT_EQ(EINVAL, errno);
        } else {
            SocketOptions options;
            options.socket_mode = SOCKET_MODE_URMA;
            options.urma_qos = co.urma_qos;
            SocketId sid;
            ASSERT_EQ(0, Socket::Create(options, &sid));
            SocketUniquePtr socket;
            ASSERT_EQ(0, Socket::Address(sid, &socket));
            EXPECT_EQ(-1, urma::UrmaQosTestPeer::Allocate(Endpoint(socket)));
            EXPECT_EQ(EINVAL, errno);
            socket->SetFailed();
        }
    }
}

TEST_F(UrmaQosTest, rejects_invalid_unsupported_and_conflicting_service_level) {
    struct RequestedQos {
        int priority;
        int sl;
    };
    for (const auto& requested : {RequestedQos{-1, -2},
                                  {-1, 256},
                                  {-1, 0},
                                  {-1, 3},
                                  {-1, 6},
                                  {-1, 255},
                                  {6, 4},
                                  {3, 3},
                                  {4, 3}}) {
        if (requested.sl == 3 && requested.priority != 4 &&
            urma::GetUrmaTpType() == URMA_RTP) {
            continue;
        }
        ChannelOptions co;
        co.socket_mode = SOCKET_MODE_URMA;
        co.urma_qos.priority = requested.priority;
        co.urma_qos.service_level = requested.sl;
        Channel channel;
        EXPECT_EQ(-1, channel.Init("127.0.0.1:8123", &co));
        ServerOptions so;
        so.socket_mode = SOCKET_MODE_URMA;
        so.urma_qos = co.urma_qos;
        Server server;
        EXPECT_EQ(-1, server.Start("127.0.0.1:0", &so));
        SocketOptions options;
        options.socket_mode = SOCKET_MODE_URMA;
        options.urma_qos = co.urma_qos;
        SocketId sid;
        if (!co.urma_qos.IsValid()) {
            EXPECT_EQ(-1, Socket::Create(options, &sid));
            EXPECT_EQ(EINVAL, errno);
        } else {
            ASSERT_EQ(0, Socket::Create(options, &sid));
            SocketUniquePtr socket;
            ASSERT_EQ(0, Socket::Address(sid, &socket));
            EXPECT_EQ(-1, urma::UrmaQosTestPeer::Allocate(Endpoint(socket)));
            EXPECT_EQ(EINVAL, errno);
            socket->SetFailed();
        }
    }
}

TEST_F(UrmaQosTest,
       channels_isolate_priority_and_inherit_to_child_connections) {
    ChannelOptions co;
    co.socket_mode = SOCKET_MODE_URMA;
    Channel automatic, low, same, high, named, tcp, tcp_with_qos;
    ASSERT_EQ(0, automatic.Init("127.0.0.1:8123", &co));
    co.urma_qos.priority = 6;
    ASSERT_EQ(0, low.Init("127.0.0.1:8123", &co));
    ASSERT_EQ(0, same.Init("127.0.0.1:8123", &co));
    ASSERT_EQ(0, named.Init("list://127.0.0.1:8123", "rr", &co));
    co.urma_qos.priority = 10;
    ASSERT_EQ(0, high.Init("127.0.0.1:8123", &co));
    EXPECT_EQ(low._server_id, same._server_id);
    EXPECT_NE(low._server_id, high._server_id);
    EXPECT_NE(low._server_id, automatic._server_id);
    co.socket_mode = SOCKET_MODE_TCP;
    ASSERT_EQ(0, tcp_with_qos.Init("127.0.0.1:8123", &co));
    ASSERT_EQ(0, tcp.Init("127.0.0.1:8123", nullptr));
    EXPECT_EQ(tcp._server_id, tcp_with_qos._server_id);
    EXPECT_NE(tcp._server_id, automatic._server_id);
    SocketUniquePtr main, pooled, short_socket, selected;
    ASSERT_EQ(0, Socket::Address(low._server_id, &main));
    ASSERT_EQ(0, main->GetPooledSocket(&pooled));
    ASSERT_EQ(0, main->GetShortSocket(&short_socket));
    LoadBalancer::SelectIn input{};
    LoadBalancer::SelectOut output(&selected);
    ASSERT_EQ(0, named._lb->SelectServer(input, &output));
    for (auto* socket :
         {main.get(), pooled.get(), short_socket.get(), selected.get()}) {
        EXPECT_EQ(6, socket->urma_qos().priority);
    }
    EXPECT_EQ(main->id(), selected->id());
    EXPECT_EQ(0, pooled->ReturnToPool());
    short_socket->SetFailed();
}

TEST_F(UrmaQosTest, sub_socket_qos_preserves_common_connection_options) {
    brpc::GlobalInitializeOrDie();
    SocketOptions options;
    options.socket_mode = SOCKET_MODE_URMA;
    options.urma_qos.priority = 10;
    options.urma_qos.service_level = 4;
    options.keepalive_options.emplace();
    options.keepalive_options->keepalive_idle_s = 11;
    options.keepalive_options->keepalive_interval_s = 12;
    options.keepalive_options->keepalive_count = 13;
    options.tcp_user_timeout_ms = 1400;
    options.bthread_tag = bthread_self_tag() + 1;
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    BRPC_SCOPE_EXIT { Socket::SetFailed(id); };
    SocketUniquePtr parent;
    ASSERT_EQ(0, Socket::Address(id, &parent));
    options.keepalive_options->keepalive_idle_s = 99;
    for (int pooled = 0; pooled < 2; ++pooled) {
        SCOPED_TRACE(pooled);
        SocketUniquePtr child;
        ASSERT_EQ(0, pooled ? parent->GetPooledSocket(&child)
                            : parent->GetShortSocket(&child));
        BRPC_SCOPE_EXIT { child->SetFailed(); };
        EXPECT_EQ(SOCKET_MODE_URMA, child->socket_mode());
        EXPECT_EQ(10, child->urma_qos().priority);
        EXPECT_EQ(4, child->urma_qos().service_level);
        ASSERT_TRUE(child->_keepalive_options.has_value());
        EXPECT_EQ(11, child->_keepalive_options->keepalive_idle_s);
        EXPECT_EQ(12, child->_keepalive_options->keepalive_interval_s);
        EXPECT_EQ(13, child->_keepalive_options->keepalive_count);
        EXPECT_EQ(1400, child->_tcp_user_timeout_ms);
        EXPECT_EQ(options.bthread_tag, child->_io_event.bthread_tag());
    }
}

TEST_F(UrmaQosTest, service_level_isolates_and_inherits_connections) {
    ChannelOptions co;
    co.socket_mode = SOCKET_MODE_URMA;
    co.urma_qos.service_level = 2;
    Channel low, same, high, named, by_priority, tcp, tcp_with_qos;
    ASSERT_EQ(0, low.Init("127.0.0.1:8123", &co));
    ASSERT_EQ(0, same.Init("127.0.0.1:8123", &co));
    ASSERT_EQ(0, named.Init("list://127.0.0.1:8123", "rr", &co));
    co.urma_qos.service_level = 4;
    ASSERT_EQ(0, high.Init("127.0.0.1:8123", &co));
    co.urma_qos.service_level = -1;
    co.urma_qos.priority = 6;
    ASSERT_EQ(0, by_priority.Init("127.0.0.1:8123", &co));
    EXPECT_EQ(low._server_id, same._server_id);
    EXPECT_NE(low._server_id, high._server_id);
    EXPECT_NE(low._server_id, by_priority._server_id);
    SocketUniquePtr main, pooled, short_socket, selected;
    ASSERT_EQ(0, Socket::Address(low._server_id, &main));
    ASSERT_EQ(0, main->GetPooledSocket(&pooled));
    ASSERT_EQ(0, main->GetShortSocket(&short_socket));
    LoadBalancer::SelectIn input{};
    LoadBalancer::SelectOut output(&selected);
    ASSERT_EQ(0, named._lb->SelectServer(input, &output));
    for (auto* socket :
         {main.get(), pooled.get(), short_socket.get(), selected.get()}) {
        EXPECT_EQ(-1, socket->urma_qos().priority);
        EXPECT_EQ(2, socket->urma_qos().service_level);
    }
    EXPECT_EQ(main->id(), selected->id());
    EXPECT_EQ(0, pooled->ReturnToPool());
    short_socket->SetFailed();
    co.socket_mode = SOCKET_MODE_TCP;
    co.urma_qos.service_level = 256;
    ASSERT_EQ(0, tcp_with_qos.Init("127.0.0.1:8123", &co));
    ASSERT_EQ(0, tcp.Init("127.0.0.1:8123", nullptr));
    EXPECT_EQ(tcp._server_id, tcp_with_qos._server_id);
}

TEST_F(UrmaQosTest, prepared_jettys_are_used_only_for_matching_priority) {
    urma::UrmaEndpoint::GlobalRelease();
    ASSERT_EQ(0, urma::UrmaEndpoint::GlobalInitialize());
    // SL 4 resolves to priority 10 and must leave the default Jettys available.
    struct RequestedQos {
        int priority;
        int sl;
        int effective;
    };
    for (const auto& qos : {RequestedQos{-1, 4, 10},
                            {-1, 2, Sl2Priority()},
                            {6, 2, 6},
                            {10, -1, 10},
                            {-1, -1, urma::GetUrmaJettyPriority()},
                            {10, 4, 10}}) {
        SocketOptions options;
        options.socket_mode = SOCKET_MODE_URMA;
        options.urma_qos.priority = qos.priority;
        options.urma_qos.service_level = qos.sl;
        SocketId sid;
        ASSERT_EQ(0, Socket::Create(options, &sid));
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(sid, &socket));
        auto* ep = Endpoint(socket);
        ASSERT_EQ(0, urma::UrmaQosTestPeer::Allocate(ep));
        EXPECT_EQ(qos.effective, urma::UrmaQosTestPeer::Priority(ep));
        std::ostringstream debug;
        ep->DebugInfo(debug);
        EXPECT_NE(std::string::npos,
                  debug.str().find("urma_effective_priority="));
        EXPECT_NE(std::string::npos,
                  debug.str().find("urma_sl=" + std::to_string(qos.sl)));
        EXPECT_NE(std::string::npos,
                  debug.str().find(
                      "urma_effective_sl=" +
                      std::to_string(urma::GetUrmaServiceLevelForPriority(
                          qos.effective))));
        socket->SetFailed();
    }
}

TEST_F(UrmaQosTest, access_error_preserves_priority_and_reports_direction) {
    for (int priority : {-1, 10}) {
        SocketOptions options;
        options.socket_mode = SOCKET_MODE_URMA;
        options.urma_qos.priority = priority;
        options.urma_qos.service_level = priority == -1 ? 2 : 4;
        SocketId sid;
        ASSERT_EQ(0, Socket::Create(options, &sid));
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(sid, &socket));
        auto* ep = Endpoint(socket);
        ASSERT_EQ(0, urma::UrmaQosTestPeer::Allocate(ep));
        const int effective = priority == -1 ? Sl2Priority() : priority;
        for (bool receive : {false, true}) {
            urma_cr_t cr{};
            cr.status = URMA_CR_LOC_ACCESS_ERR;
            cr.flag.bs.s_r = receive;
            cr.local_id = 42;
#if !BRPC_WITH_GLOG
            logging::StringSink log;
            logging::LogSink* old_sink = logging::SetLogSink(&log);
#endif
            const ssize_t result = urma::UrmaQosTestPeer::Complete(ep, cr);
            const int saved_errno = errno;
#if !BRPC_WITH_GLOG
            logging::SetLogSink(old_sink);
            EXPECT_NE(std::string::npos, log.find("status=4")) << log;
            EXPECT_NE(std::string::npos,
                      log.find(receive ? "direction=recv" : "direction=send"))
                << log;
            EXPECT_NE(std::string::npos, log.find("local_id=42")) << log;
            EXPECT_NE(std::string::npos,
                      log.find("urma_priority=" + std::to_string(priority)))
                << log;
            EXPECT_NE(std::string::npos, log.find("urma_effective_priority=" +
                                                  std::to_string(effective)))
                << log;
            EXPECT_NE(std::string::npos,
                      log.find("urma_sl=" +
                               std::to_string(options.urma_qos.service_level)))
                << log;
            EXPECT_NE(std::string::npos,
                      log.find("urma_effective_sl=" +
                               std::to_string(options.urma_qos.service_level)))
                << log;
#endif
            EXPECT_EQ(-1, result);
            EXPECT_EQ(EIO, saved_errno);
            EXPECT_EQ(effective, urma::UrmaQosTestPeer::Priority(ep));
        }
        socket->SetFailed();
    }
}

class UrmaQosEcho : public ::test::EchoService {
    void Echo(google::protobuf::RpcController*,
              const ::test::EchoRequest* request,
              ::test::EchoResponse* response,
              google::protobuf::Closure* done) override {
        response->set_message(request->message());
        done->Run();
    }
};

TEST_F(UrmaQosTest, request_and_response_priorities_apply_across_restart) {
    GFLAGS_NAMESPACE::FlagSaver saved_flags;
    UrmaQosEcho service;
    Server server;
    ASSERT_EQ(0, server.AddService(&service, SERVER_DOESNT_OWN_SERVICE));
    for (int response_priority : {6, 10}) {
        urma::FLAGS_urma_client_handshake_version =
            response_priority == 6 ? 2 : 3;
        ServerOptions so;
        so.socket_mode = SOCKET_MODE_URMA;
        so.urma_qos.priority = response_priority;
        ASSERT_EQ(0, server.Start("127.0.0.1:0", &so));
        ChannelOptions co;
        co.socket_mode = SOCKET_MODE_URMA;
        co.urma_qos.priority = response_priority == 6 ? 10 : 6;
        co.timeout_ms = 2000;
        co.max_retry = 0;
        co.connection_type = "single";
        Channel channel;
        ASSERT_EQ(0, channel.Init(server.listen_address(), &co));
        Controller cntl;
        ::test::EchoRequest request;
        ::test::EchoResponse response;
        request.set_message("URMA QoS");
        ::test::EchoService_Stub(&channel).Echo(&cntl, &request, &response,
                                                nullptr);
        EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
        EXPECT_EQ(request.message(), response.message());
        SocketUniquePtr client;
        ASSERT_EQ(0, Socket::Address(channel._server_id, &client));
        EXPECT_EQ(co.urma_qos.priority,
                  urma::UrmaQosTestPeer::Priority(Endpoint(client)));
        std::vector<SocketId> sockets;
        server._am->ListConnections(&sockets);
        ASSERT_EQ(1u, sockets.size());
        SocketUniquePtr accepted;
        ASSERT_EQ(0, Socket::Address(sockets[0], &accepted));
        EXPECT_EQ(response_priority, accepted->urma_qos().priority);
        EXPECT_EQ(response_priority,
                  urma::UrmaQosTestPeer::Priority(Endpoint(accepted)));
        accepted.reset();
        client.reset();
        server.Stop(0);
        server.Join();
    }
}

TEST_F(UrmaQosTest, request_and_response_service_levels_apply_across_restart) {
    GFLAGS_NAMESPACE::FlagSaver saved_flags;
    UrmaQosEcho service;
    Server server;
    ASSERT_EQ(0, server.AddService(&service, SERVER_DOESNT_OWN_SERVICE));
    for (int response_sl : {2, 4}) {
        urma::FLAGS_urma_client_handshake_version = response_sl == 2 ? 2 : 3;
        ServerOptions so;
        so.socket_mode = SOCKET_MODE_URMA;
        so.urma_qos.service_level = response_sl;
        ASSERT_EQ(0, server.Start("127.0.0.1:0", &so));
        ChannelOptions co;
        co.socket_mode = SOCKET_MODE_URMA;
        co.urma_qos.service_level = response_sl == 2 ? 4 : 2;
        co.timeout_ms = 2000;
        co.max_retry = 0;
        co.connection_type = "single";
        Channel channel;
        ASSERT_EQ(0, channel.Init(server.listen_address(), &co));
        Controller cntl;
        ::test::EchoRequest request;
        ::test::EchoResponse response;
        request.set_message("URMA SL mapping");
        ::test::EchoService_Stub(&channel).Echo(&cntl, &request, &response,
                                                nullptr);
        EXPECT_FALSE(cntl.Failed()) << cntl.ErrorText();
        EXPECT_EQ(request.message(), response.message());
        SocketUniquePtr client;
        ASSERT_EQ(0, Socket::Address(channel._server_id, &client));
        EXPECT_EQ(-1, client->urma_qos().priority);
        EXPECT_EQ(co.urma_qos.service_level, client->urma_qos().service_level);
        EXPECT_EQ(response_sl == 2 ? 10 : Sl2Priority(),
                  urma::UrmaQosTestPeer::Priority(Endpoint(client)));
        std::vector<SocketId> sockets;
        server._am->ListConnections(&sockets);
        ASSERT_EQ(1u, sockets.size());
        SocketUniquePtr accepted;
        ASSERT_EQ(0, Socket::Address(sockets[0], &accepted));
        EXPECT_EQ(-1, accepted->urma_qos().priority);
        EXPECT_EQ(response_sl, accepted->urma_qos().service_level);
        EXPECT_EQ(response_sl == 2 ? Sl2Priority() : 10,
                  urma::UrmaQosTestPeer::Priority(Endpoint(accepted)));
        std::ostringstream debug;
        Endpoint(accepted)->DebugInfo(debug);
        EXPECT_NE(std::string::npos,
                  debug.str().find("urma_effective_sl=" +
                                   std::to_string(response_sl)));
        accepted.reset();
        client.reset();
        server.Stop(0);
        server.Join();
    }
}

#endif  // BRPC_WITH_URMA

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}
