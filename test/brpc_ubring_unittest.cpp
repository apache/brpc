// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to You under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>
#include <cstring>
#include <gflags/gflags.h>
#include <string>
#include "butil/macros.h"
#include "butil/sys_byteorder.h"
#include "brpc/socket.h"

#if BRPC_WITH_UBRING
#include <functional>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include "bthread/bthread.h"
#include "brpc/acceptor.h"
#include "brpc/adapter_transport.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/policy/baidu_rpc_meta.pb.h"
#include "brpc/policy/baidu_rpc_protocol.h"
#include "brpc/server.h"
#include "brpc/ubshm_transport.h"
#include "butil/fd_guard.h"
#include "butil/memory/scope_guard.h"
#include "butil/time.h"
#include "echo.pb.h"
#include "brpc/ubshm/common/common.h"
#include "brpc/handshake/ubshm_handshake.h"
#include "brpc/ubshm/ub_endpoint.h"
#include "brpc/ubshm/shm/shm_def.h"
#include "brpc/ubshm/shm/shm_mgr.h"
#include "brpc/ubshm/ub_ring_manager.h"
#include "brpc/ubshm/ub_ring.h"
#include "brpc/ubshm/ubr_msg.h"

namespace brpc {
namespace ubring {
DECLARE_int32(ub_disconnect_timeout_s);
DECLARE_int32(ub_connect_timeout_s);
DECLARE_int32(ub_hb_timer_interval_s);
DECLARE_int32(ub_event_queue_timer_interval_us);
DECLARE_int32(ub_flying_io_timeout_s);

extern bool g_skip_ub_init;
}  // namespace ubring
}  // namespace brpc

namespace {
struct __attribute__((packed)) HelloMessageLayout {
    uint16_t msg_len;
    uint16_t hello_ver;
    uint16_t impl_ver;
    uint64_t len;
    char shm_name[SHM_MAX_NAME_BUFF_LEN];
};
}

class HelloMessageTest : public ::testing::Test {
protected:
    void SetUp() override {
        memset(&msg, 0, sizeof(msg));
        buffer.resize(256, 0);
    }

    brpc::ubring::HelloMessage msg;
    std::string buffer;
};

TEST(HelloFormatExtensionTest, serialize_deserialize_roundtrip) {
    brpc::ubring::HelloFormatExtension extension = {
        brpc::ubring::HelloFormatExtension::WIRE_SIZE,
        brpc::ubring::UBR_DATA_FORMAT_LEGACY_64};
    char buffer[brpc::ubring::HelloFormatExtension::WIRE_SIZE] = {};

    extension.Serialize(buffer);

    brpc::ubring::HelloFormatExtension decoded = {};
    decoded.Deserialize(buffer);
    EXPECT_EQ(extension.extension_len, decoded.extension_len);
    EXPECT_EQ(extension.format_id, decoded.format_id);
}

TEST(HelloFormatExtensionTest, serialize_uses_network_byte_order) {
    brpc::ubring::HelloFormatExtension extension = {0x0102, 0x0304};
    char buffer[brpc::ubring::HelloFormatExtension::WIRE_SIZE] = {};
    const unsigned char expected[] = {0x01, 0x02, 0x03, 0x04};

    extension.Serialize(buffer);

    EXPECT_EQ(0, memcmp(expected, buffer, sizeof(expected)));
}

TEST(HelloFormatExtensionTest, deserialize_none_format) {
    const unsigned char buffer[] = {0x00, 0x04, 0x00, 0x00};
    brpc::ubring::HelloFormatExtension extension = {};

    extension.Deserialize(buffer);

    EXPECT_EQ(4, extension.extension_len);
    EXPECT_EQ(brpc::ubring::UBR_DATA_FORMAT_NONE, extension.format_id);
}

TEST(HelloFormatExtensionTest, deserialize_unknown_format) {
    const unsigned char buffer[] = {0x00, 0x04, 0x12, 0x34};
    brpc::ubring::HelloFormatExtension extension = {};

    extension.Deserialize(buffer);

    EXPECT_EQ(4, extension.extension_len);
    EXPECT_EQ(0x1234, extension.format_id);
}

TEST(UBShmHandshakeAdapterTest, rejects_unsupported_format_extension) {
    brpc::ubring::UBShmHandshakeAdapter adapter;
    std::string payload;
    ASSERT_EQ(brpc::handshake::STEP_OK,
              adapter.BuildExtension(true, &payload));
    EXPECT_EQ(std::string("\0\4\0\1", 4), payload);
    EXPECT_EQ(brpc::handshake::STEP_OK,
              adapter.ParseExtension(payload));

    payload[3] = 2;
    EXPECT_EQ(brpc::handshake::STEP_FALLBACK,
              adapter.ParseExtension(payload));
    payload[3] = 0;
    EXPECT_EQ(brpc::handshake::STEP_FALLBACK,
              adapter.ParseExtension(payload));
    payload[1] = 3;
    EXPECT_EQ(brpc::handshake::STEP_FALLBACK,
              adapter.ParseExtension(payload));
}

TEST_F(HelloMessageTest, serialize_deserialize_roundtrip) {
    msg.msg_len = 64;
    msg.hello_ver = 2;
    msg.impl_ver = 1;
    msg.len = 4 * 1024 * 1024;
    memcpy(msg.shm_name, "UBRING_test_C", 14);

    msg.Serialize(&buffer[0]);

    brpc::ubring::HelloMessage decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.Deserialize(&buffer[0]);

    EXPECT_EQ(msg.msg_len, decoded.msg_len);
    EXPECT_EQ(msg.hello_ver, decoded.hello_ver);
    EXPECT_EQ(msg.impl_ver, decoded.impl_ver);
    EXPECT_EQ(msg.len, decoded.len);
    EXPECT_EQ(0, memcmp(msg.shm_name, decoded.shm_name, SHM_MAX_NAME_BUFF_LEN));
}

TEST_F(HelloMessageTest, serialize_uses_network_byte_order) {
    msg.msg_len = 0x0102;
    msg.hello_ver = 0x0304;
    msg.impl_ver = 0x0506;
    msg.len = 0x0102030405060708ULL;
    memset(msg.shm_name, 0, SHM_MAX_NAME_BUFF_LEN);

    msg.Serialize(&buffer[0]);

    HelloMessageLayout* raw = reinterpret_cast<HelloMessageLayout*>(&buffer[0]);
    EXPECT_EQ(butil::HostToNet16(0x0102), raw->msg_len);
    EXPECT_EQ(butil::HostToNet16(0x0304), raw->hello_ver);
    EXPECT_EQ(butil::HostToNet16(0x0506), raw->impl_ver);
    EXPECT_EQ(butil::HostToNet64(0x0102030405060708ULL), raw->len);
}

TEST_F(HelloMessageTest, large_len_value) {
    msg.msg_len = 64;
    msg.hello_ver = 2;
    msg.impl_ver = 1;
    msg.len = 0xFFFFFFFFFFFFFFFFULL;
    memset(msg.shm_name, 0, SHM_MAX_NAME_BUFF_LEN);

    msg.Serialize(&buffer[0]);

    brpc::ubring::HelloMessage decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.Deserialize(&buffer[0]);

    EXPECT_EQ(0xFFFFFFFFFFFFFFFFULL, decoded.len);
}

TEST_F(HelloMessageTest, full_shm_name) {
    memset(msg.shm_name, 'A', SHM_MAX_NAME_BUFF_LEN);
    msg.msg_len = 64;
    msg.hello_ver = 2;
    msg.impl_ver = 1;
    msg.len = 0;
    msg.Serialize(&buffer[0]);

    brpc::ubring::HelloMessage decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.Deserialize(&buffer[0]);

    EXPECT_EQ(0, memcmp(msg.shm_name, decoded.shm_name, SHM_MAX_NAME_BUFF_LEN));
}

TEST_F(HelloMessageTest, toString_contains_fields) {
    msg.msg_len = 64;
    msg.hello_ver = 2;
    msg.impl_ver = 1;
    msg.len = 4194304;
    memcpy(msg.shm_name, "UBRING_test", 12);

    std::string s = msg.toString();
    EXPECT_NE(std::string::npos, s.find("msg_len=64"));
    EXPECT_NE(std::string::npos, s.find("hello_ver=2"));
    EXPECT_NE(std::string::npos, s.find("impl_ver=1"));
    EXPECT_NE(std::string::npos, s.find("UBRING_test"));
}

TEST(UBShmHandshakeAdapterTest, codec_uses_v3_wire_format) {
    brpc::ubring::UBShmHandshakeAdapter adapter;
    char shm_name[SHM_MAX_NAME_BUFF_LEN] = {0};
    memcpy(shm_name, "UBRING_test_C", 14);

    std::string payload;
    ASSERT_EQ(brpc::handshake::STEP_OK,
              adapter.BuildHello(true, 4 * 1024 * 1024,
                                 shm_name, &payload));
    brpc::ubring::HelloMessage decoded{};
    ASSERT_EQ(brpc::handshake::STEP_OK,
              adapter.ParseHello(payload, &decoded));
    EXPECT_EQ(64, decoded.msg_len);
    EXPECT_EQ(3, decoded.hello_ver);
    EXPECT_EQ(1, decoded.impl_ver);
    EXPECT_EQ(4 * 1024 * 1024, decoded.len);
    EXPECT_EQ(0, memcmp(shm_name, decoded.shm_name,
                        SHM_MAX_NAME_BUFF_LEN));

    std::string frame;
    ASSERT_EQ(brpc::handshake::FRAME_OK,
              brpc::handshake::FrameCodec::Encode(
                  adapter.HelloFrameSpec(), payload, &frame));
    ASSERT_EQ(64, frame.size());
    EXPECT_EQ("UB", frame.substr(0, 2));
}

TEST(UBShmHandshakeAdapterTest, short_name_is_zero_padded) {
    brpc::ubring::UBShmHandshakeAdapter adapter;
    char short_name[SHM_MAX_NAME_BUFF_LEN];
    memset(short_name, 0x5a, sizeof(short_name));
    short_name[0] = 'x';
    short_name[1] = '\0';

    std::string payload;
    ASSERT_EQ(brpc::handshake::STEP_OK,
              adapter.BuildHello(true, 4096, short_name, &payload));

    brpc::ubring::HelloMessage decoded{};
    ASSERT_EQ(brpc::handshake::STEP_OK,
              adapter.ParseHello(payload, &decoded));
    EXPECT_EQ('x', decoded.shm_name[0]);
    for (size_t i = 1; i < SHM_MAX_NAME_BUFF_LEN; ++i) {
        EXPECT_EQ('\0', decoded.shm_name[i]) << "index=" << i;
    }
}

TEST(UBShmHandshakeAdapterTest, client_hello_advertises_allocated_shm_name) {
    brpc::ubring::SHM local_shm{};
    local_shm.len = 4096;
    strcpy(local_shm.name, "UBRING_127.0.0.1:8000_C");
    brpc::ubring::UBShmHandshakeAdapter adapter;
    adapter.ConfigureClientHello(local_shm);
    std::string payload;
    ASSERT_EQ(brpc::handshake::STEP_OK, adapter.BuildHello(true, &payload));
    brpc::ubring::HelloMessage decoded{};
    ASSERT_EQ(brpc::handshake::STEP_OK, adapter.ParseHello(payload, &decoded));
    EXPECT_EQ(local_shm.len, decoded.len);
    EXPECT_STREQ(local_shm.name, decoded.shm_name);
}

TEST(UBShmHandshakeAdapterTest, rejects_unterminated_remote_name) {
    brpc::ubring::HelloMessage message{};
    message.msg_len = 64;
    message.hello_ver = 3;
    message.impl_ver = 1;
    message.len = 4096;
    memset(message.shm_name, 'A', SHM_MAX_NAME_BUFF_LEN);

    std::string payload(
        sizeof(HelloMessageLayout), '\0');
    message.Serialize(&payload[0]);

    brpc::ubring::UBShmHandshakeAdapter adapter;
    brpc::ubring::HelloMessage decoded{};
    errno = 0;
    EXPECT_EQ(brpc::handshake::STEP_ERROR,
              adapter.ParseHello(payload, &decoded));
    EXPECT_EQ(EPROTO, errno);
}

TEST(UBShmHandshakeAdapterTest, disabled_hello_requests_tcp_fallback) {
    brpc::ubring::UBShmHandshakeAdapter adapter;
    std::string payload;
    ASSERT_EQ(brpc::handshake::STEP_OK,
              adapter.BuildHello(false, 0, NULL, &payload));
    brpc::ubring::HelloMessage decoded{};
    EXPECT_EQ(brpc::handshake::STEP_FALLBACK,
              adapter.ParseHello(payload, &decoded));
    EXPECT_EQ(64, decoded.msg_len);
}

TEST(UBRingConfigurationTest, time_flags_include_units_and_expected_defaults) {
    struct TimeFlagExpectation {
        const char* name;
        const char* suffix;
        const char* unit;
        const char* default_value;
    };
    const TimeFlagExpectation expected_flags[] = {
        {"ub_disconnect_timeout_s", "_s", "seconds", "5"},
        {"ub_connect_timeout_s", "_s", "seconds", "1"},
        {"ub_hb_timer_interval_s", "_s", "seconds", "5"},
        {"ub_event_queue_timer_interval_us", "_us", "microseconds", "100"},
        {"ub_flying_io_timeout_s", "_s", "seconds", "5"},
    };

    for (const auto& expected : expected_flags) {
        GFLAGS_NAMESPACE::CommandLineFlagInfo info;
        ASSERT_TRUE(GFLAGS_NAMESPACE::GetCommandLineFlagInfo(
            expected.name, &info)) << expected.name;
        const std::string flag_name(expected.name);
        const std::string suffix(expected.suffix);
        ASSERT_GE(flag_name.size(), suffix.size());
        EXPECT_EQ(flag_name.size() - suffix.size(), flag_name.rfind(suffix));
        EXPECT_NE(std::string::npos, info.description.find(expected.unit));
        EXPECT_EQ(std::string(expected.default_value), info.default_value);
    }

    EXPECT_EQ(5, brpc::ubring::FLAGS_ub_disconnect_timeout_s);
    EXPECT_EQ(1, brpc::ubring::FLAGS_ub_connect_timeout_s);
    EXPECT_EQ(5, brpc::ubring::FLAGS_ub_hb_timer_interval_s);
    EXPECT_EQ(100, brpc::ubring::FLAGS_ub_event_queue_timer_interval_us);
    EXPECT_EQ(5, brpc::ubring::FLAGS_ub_flying_io_timeout_s);
    EXPECT_EQ(100U * USEC_TO_NSEC,
              static_cast<uint32_t>(
                  brpc::ubring::FLAGS_ub_event_queue_timer_interval_us) *
                  USEC_TO_NSEC);
}

namespace brpc {
namespace ubring {
class UBShmEndpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        _saved_skip = g_skip_ub_init;
        g_skip_ub_init = false;
        ShmMgrInit();
        UBRingManager::UbrMgrInit();
        UBShmEndpoint::GlobalInitialize();

        brpc::SocketOptions options;
        ASSERT_EQ(0, brpc::Socket::Create(options, &_socket_id));
        brpc::SocketUniquePtr s;
        ASSERT_EQ(0, brpc::Socket::Address(_socket_id, &s));
        _socket = s.get();
        _ep = new UBShmEndpoint(_socket);
    }

    void TearDown() override {
        delete _ep;
        brpc::SocketUniquePtr s;
        if (brpc::Socket::Address(_socket_id, &s) == 0) {
            s->SetFailed();
        }
        UBShmEndpoint::GlobalRelease();
        UBRingManager::UbrMgrFini();
        ShmMgrFini();
        g_skip_ub_init = _saved_skip;
    }

    brpc::SocketId _socket_id = brpc::INVALID_SOCKET_ID;
    brpc::Socket* _socket = nullptr;
    UBShmEndpoint* _ep = nullptr;
    bool _saved_skip = false;
};
}  // namespace ubring
}  // namespace brpc

using brpc::ubring::UBShmEndpointTest;

TEST_F(UBShmEndpointTest, construct_initial_state) {
    ASSERT_NE(nullptr, _ep);
    EXPECT_EQ(brpc::ubring::UBR_DATA_FORMAT_NONE,
              _ep->negotiated_data_format());
}

TEST_F(UBShmEndpointTest, reset_clears_negotiated_data_format) {
    _ep->SetNegotiatedDataFormat(brpc::ubring::UBR_DATA_FORMAT_LEGACY_64);

    _ep->Reset();

    EXPECT_EQ(brpc::ubring::UBR_DATA_FORMAT_NONE,
              _ep->negotiated_data_format());
}

TEST_F(UBShmEndpointTest, allocate_client_resources_real_shm) {
    brpc::ubring::SHM local_trx_shm =
        {nullptr, 4 * 1024 * 1024, 0, {0}, (uint32_t)_socket->fd()};
    int ret = _ep->AllocateClientResources(&local_trx_shm, "UBRING_ut_client");
    EXPECT_EQ(0, ret);
}

TEST_F(UBShmEndpointTest, reset_cleans_up_resources) {
    brpc::ubring::SHM local_trx_shm =
        {nullptr, 4 * 1024 * 1024, 0, {0}, (uint32_t)_socket->fd()};
    _ep->AllocateClientResources(&local_trx_shm, "UBRING_ut_reset");
    _ep->Reset();
}

TEST_F(UBShmEndpointTest, reset_is_idempotent) {
    _ep->Reset();
    _ep->Reset();
}

namespace {
bool WaitForUBCondition(const std::function<bool()>& condition) {
    const int64_t deadline = butil::gettimeofday_us() + 5000000;
    while (!condition()) {
        if (butil::gettimeofday_us() >= deadline) {
            return false;
        }
        bthread_usleep(1000);
    }
    return true;
}

bool TransferUBTestBytes(int fd, void* bytes, size_t size, bool sending) {
    size_t offset = 0;
    while (offset < size) {
        const ssize_t n = sending
            ? send(fd, static_cast<char*>(bytes) + offset, size - offset,
                   MSG_NOSIGNAL)
            : recv(fd, static_cast<char*>(bytes) + offset, size - offset, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        offset += n;
    }
    return true;
}

class UBFallbackEchoService : public test::EchoService {
public:
    void Echo(google::protobuf::RpcController*, const test::EchoRequest* request,
              test::EchoResponse* response, google::protobuf::Closure* done)
        override {
        response->set_message(request->message());
        done->Run();
    }
};

void SendAndCheckUBFallbackRPC(int fd, bool include_ack, uint64_t id) {
    test::EchoRequest request;
    request.set_message("UBSHM fallback RPC");
    brpc::Controller cntl;
    butil::IOBuf body;
    brpc::policy::SerializeRpcRequest(&body, &cntl, &request);
    butil::IOBuf packet;
    brpc::policy::PackRpcRequest(&packet, nullptr, id,
        test::EchoService::descriptor()->FindMethodByName("Echo"),
        &cntl, body, nullptr);
    ASSERT_FALSE(cntl.Failed());
    std::string bytes(include_ack ? 4 : 0, '\0');
    bytes += packet.to_string();
    ASSERT_TRUE(TransferUBTestBytes(fd, &bytes[0], bytes.size(), true));
    uint8_t header[12];
    ASSERT_TRUE(TransferUBTestBytes(fd, header, sizeof(header), false));
    ASSERT_EQ(0, memcmp(header, "PRPC", 4));
    uint32_t total_size;
    uint32_t meta_size;
    memcpy(&total_size, header + 4, sizeof(total_size));
    memcpy(&meta_size, header + 8, sizeof(meta_size));
    total_size = butil::NetToHost32(total_size);
    meta_size = butil::NetToHost32(meta_size);
    ASSERT_GT(total_size, 0u);
    ASSERT_LT(total_size, 65536u);
    ASSERT_LE(meta_size, total_size);
    std::string response(total_size, '\0');
    ASSERT_TRUE(TransferUBTestBytes(fd, &response[0], response.size(), false));
    brpc::policy::RpcMeta meta;
    ASSERT_TRUE(meta.ParseFromArray(response.data(), meta_size));
    ASSERT_EQ(id, meta.correlation_id());
    ASSERT_EQ(0, meta.response().error_code());
    test::EchoResponse echo;
    ASSERT_TRUE(echo.ParseFromArray(response.data() + meta_size,
                                   total_size - meta_size));
    ASSERT_EQ(request.message(), echo.message());
}
}  // namespace

TEST_F(UBShmEndpointTest, failed_server_allocation_cleans_resources_and_serves_tcp) {
    UBFallbackEchoService service;
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    brpc::ServerOptions options;
    options.socket_mode = brpc::SOCKET_MODE_UBRING;
    options.enabled_protocols = "baidu_std";
    options.internal_port = -1;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &options));

    // The pin is destroyed before Server on any assertion failure.
    brpc::SocketUniquePtr socket;
    butil::fd_guard peer(::socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_GE(peer, 0);
    timeval timeout = {5, 0};
    ASSERT_EQ(0, setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO,
                           &timeout, sizeof(timeout)));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(server.listen_address().port);
    ASSERT_EQ(0, connect(peer, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
    ASSERT_TRUE(WaitForUBCondition([&] {
        std::vector<brpc::SocketId> ids;
        server._am->ListConnections(&ids);
        return !ids.empty() && brpc::Socket::Address(ids[0], &socket) == 0;
    }));

    // A valid Hello references absent remote SHM. Allocation creates a ring
    // and poller socket before mapping fails, exercising partial cleanup.
    std::string missing_name = "UBRING_missing_" + std::to_string(getpid());
    butil::fd_guard absent(shm_open(missing_name.c_str(), O_RDONLY, 0));
    ASSERT_LT(absent, 0);
    ASSERT_EQ(ENOENT, errno);
    brpc::ubring::UBShmHandshakeAdapter protocol;
    protocol.ConfigureClientHello(4 * 1024 * 1024, missing_name.c_str());
    std::string payload;
    ASSERT_EQ(brpc::handshake::STEP_OK, protocol.BuildHello(true, &payload));
    std::string hello;
    ASSERT_EQ(brpc::handshake::FRAME_OK, brpc::handshake::FrameCodec::Encode(
        protocol.HelloFrameSpec(), payload, &hello));
    ASSERT_TRUE(TransferUBTestBytes(peer, &hello[0], hello.size(), true));
    char reply[64];
    ASSERT_TRUE(TransferUBTestBytes(peer, reply, sizeof(reply), false));
    brpc::ubring::HelloMessage disabled{};
    disabled.Deserialize(reply + 2);
    ASSERT_EQ(0u, disabled.len);

    ASSERT_NO_FATAL_FAILURE(SendAndCheckUBFallbackRPC(peer, true, 101));
    auto* adapter = brpc::AdapterTransport::Get(socket.get());
    ASSERT_TRUE(WaitForUBCondition([&] {
        return adapter->handshake_phase() == brpc::handshake::FALLBACK_TCP;
    }));
    auto* transport = brpc::UBShmTransport::Get(socket.get());
    EXPECT_FALSE(transport->UpgradeActive());
    EXPECT_EQ(nullptr, transport->GetUBShmEp()->_ub_ring);
    EXPECT_EQ(brpc::INVALID_SOCKET_ID, transport->GetUBShmEp()->_poller_sid);
    EXPECT_EQ(brpc::ubring::UBR_DATA_FORMAT_NONE,
              transport->GetUBShmEp()->negotiated_data_format());
    ASSERT_NO_FATAL_FAILURE(SendAndCheckUBFallbackRPC(peer, false, 102));
    EXPECT_EQ(brpc::handshake::FALLBACK_TCP, adapter->handshake_phase());
    EXPECT_FALSE(socket->Failed());
    peer.reset(-1);
    socket.reset();
    server.Stop(0);
    server.Join();
}

TEST_F(UBShmEndpointTest, malformed_hello_releases_allocated_ring_and_poller) {
    brpc::SocketOptions options;
    options.socket_mode = brpc::SOCKET_MODE_UBRING;
    brpc::SocketId id;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    brpc::SocketUniquePtr socket;
    ASSERT_EQ(0, brpc::Socket::Address(id, &socket));
    BRPC_SCOPE_EXIT { socket->SetFailed(); };
    auto* transport = brpc::UBShmTransport::Get(socket.get());
    brpc::ubring::SHM local{};
    local.len = 4 * 1024 * 1024;
    const std::string name = "cleanup_" + std::to_string(getpid());
    ASSERT_EQ(0, transport->PrepareUpgradeResources(&local, name.c_str()));
    auto* ep = transport->GetUBShmEp();
    ASSERT_NE(nullptr, ep->_ub_ring);
    const brpc::SocketId poller = ep->_poller_sid;
    ASSERT_NE(brpc::INVALID_SOCKET_ID, poller);

    brpc::ubring::HelloMessage hello{};
    hello.msg_len = 64;
    hello.hello_ver = 3;
    hello.impl_ver = 1;
    hello.len = 4 * 1024 * 1024;
    memset(hello.shm_name, 'x', sizeof(hello.shm_name));
    char frame[64] = {'U', 'B'};
    hello.Serialize(frame + 2);
    butil::IOBuf source;
    source.append(frame, sizeof(frame));
    EXPECT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG,
        brpc::handshake::GetUBShmServerHandshakeAdapter()
            ->ExecuteServerHandshake(&source, socket.get()).error());
    EXPECT_EQ(brpc::handshake::FAILED,
              brpc::AdapterTransport::Get(socket.get())->handshake_phase());
    EXPECT_FALSE(transport->UpgradeActive());
    EXPECT_EQ(nullptr, ep->_ub_ring);
    EXPECT_EQ(brpc::INVALID_SOCKET_ID, ep->_poller_sid);
    brpc::SocketUniquePtr old_poller;
    EXPECT_NE(0, brpc::Socket::Address(poller, &old_poller));
    butil::fd_guard removed(shm_open(local.name, O_RDONLY, 0));
    EXPECT_LT(removed, 0);
    EXPECT_EQ(ENOENT, errno);
}

TEST_F(UBShmEndpointTest, shared_memory_rpc_waits_for_final_tcp_ack) {
    UBFallbackEchoService service;
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    brpc::ServerOptions options;
    options.socket_mode = brpc::SOCKET_MODE_UBRING;
    options.enabled_protocols = "baidu_std";
    options.internal_port = -1;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &options));

    brpc::SocketUniquePtr server_socket;
    brpc::SocketUniquePtr client_socket;
    butil::fd_guard peer(::socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_GE(peer, 0);
    timeval timeout = {5, 0};
    ASSERT_EQ(0, setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO,
                           &timeout, sizeof(timeout)));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(server.listen_address().port);
    ASSERT_EQ(0, connect(peer, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
    ASSERT_TRUE(WaitForUBCondition([&] {
        std::vector<brpc::SocketId> ids;
        server._am->ListConnections(&ids);
        return !ids.empty() && brpc::Socket::Address(ids[0], &server_socket) == 0;
    }));

    brpc::SocketOptions client_options;
    client_options.socket_mode = brpc::SOCKET_MODE_UBRING;
    brpc::SocketId client_id;
    ASSERT_EQ(0, brpc::Socket::Create(client_options, &client_id));
    ASSERT_EQ(0, brpc::Socket::Address(client_id, &client_socket));
    BRPC_SCOPE_EXIT { client_socket->SetFailed(); };
    auto* client_transport = brpc::UBShmTransport::Get(client_socket.get());
    brpc::ubring::SHM local{};
    local.len = 4 * 1024 * 1024;
    local.fd = peer;
    const std::string name = "ack_race_" + std::to_string(getpid());
    ASSERT_EQ(0, client_transport->PrepareUpgradeResources(&local, name.c_str()));
    brpc::ubring::UBShmHandshakeAdapter protocol;
    protocol.ConfigureClientHello(local);
    std::string payload;
    ASSERT_EQ(brpc::handshake::STEP_OK, protocol.BuildHello(true, &payload));
    std::string hello;
    ASSERT_EQ(brpc::handshake::FRAME_OK, brpc::handshake::FrameCodec::Encode(
        protocol.HelloFrameSpec(), payload, &hello));
    ASSERT_TRUE(TransferUBTestBytes(peer, &hello[0], hello.size(), true));
    char reply[64];
    ASSERT_TRUE(TransferUBTestBytes(peer, reply, sizeof(reply), false));
    brpc::ubring::HelloMessage server_hello{};
    server_hello.Deserialize(reply + 2);
    ASSERT_GT(server_hello.len, 0u);
    std::string extension_payload;
    ASSERT_EQ(brpc::handshake::STEP_OK,
              protocol.BuildExtension(true, &extension_payload));
    std::string extension;
    ASSERT_EQ(brpc::handshake::FRAME_OK, brpc::handshake::FrameCodec::Encode(
        protocol.ExtensionFrameSpec(), extension_payload, &extension));
    ASSERT_TRUE(TransferUBTestBytes(peer, &extension[0], extension.size(), true));
    char extension_reply[brpc::ubring::HelloFormatExtension::WIRE_SIZE];
    ASSERT_TRUE(TransferUBTestBytes(peer, extension_reply,
                                   sizeof(extension_reply), false));
    auto* adapter = brpc::AdapterTransport::Get(server_socket.get());
    ASSERT_TRUE(WaitForUBCondition([&] {
        return adapter->handshake_phase() == brpc::handshake::ACK_WAIT;
    }));
    ASSERT_EQ(0, client_transport->NegotiateUpgradeResources(&local, name.c_str()));

    test::EchoRequest request;
    request.set_message("RPC queued before the final ACK");
    brpc::Controller cntl;
    butil::IOBuf body;
    brpc::policy::SerializeRpcRequest(&body, &cntl, &request);
    butil::IOBuf packet;
    brpc::policy::PackRpcRequest(&packet, nullptr, 201,
        test::EchoService::descriptor()->FindMethodByName("Echo"),
        &cntl, body, nullptr);
    ASSERT_FALSE(cntl.Failed());
    const std::string bytes = packet.to_string();
    iovec data{const_cast<char*>(bytes.data()), bytes.size()};
    auto* client_ring = client_transport->GetUBShmEp()->_ub_ring;
    ASSERT_EQ(static_cast<ssize_t>(bytes.size()), client_ring->UbrTrxWritev(&data, 1));

    auto* server_ep = brpc::UBShmTransport::Get(server_socket.get())->GetUBShmEp();
    EXPECT_FALSE(server_ep->_receive_events_started.load(butil::memory_order_acquire));
    // Invoke a would-be early poll synchronously while ACK is deliberately
    // withheld. It must not feed PRPC bytes into the TCP handshake context.
    ASSERT_NE(nullptr, server_socket->parsing_context());
    brpc::ubring::UBShmEndpoint::PollIn(server_ep, EPOLLIN);
    EXPECT_EQ(brpc::handshake::ACK_WAIT, adapter->handshake_phase());
    EXPECT_FALSE(server_socket->Failed());

    uint32_t ack = butil::HostToNet32(1);
    ASSERT_TRUE(TransferUBTestBytes(peer, &ack, sizeof(ack), true));
    // No second SHM write: registering level-triggered receive polling must
    // pick up the request that was already queued before the ACK.
    butil::IOPortal response;
    ASSERT_TRUE(WaitForUBCondition([&] {
        const ssize_t n = response.append_from_reader(client_ring, 65536);
        if (n < 0 && errno != EAGAIN && errno != EINTR) {
            return true;
        }
        if (response.size() < 12) {
            return false;
        }
        uint8_t header[12];
        response.copy_to(header, sizeof(header));
        uint32_t total_size;
        memcpy(&total_size, header + 4, sizeof(total_size));
        return response.size() >= 12 + butil::NetToHost32(total_size);
    }));
    const std::string received = response.to_string();
    ASSERT_GE(received.size(), 12u);
    ASSERT_EQ(0, memcmp(received.data(), "PRPC", 4));
    uint32_t meta_size;
    memcpy(&meta_size, received.data() + 8, sizeof(meta_size));
    meta_size = butil::NetToHost32(meta_size);
    ASSERT_LE(meta_size, received.size() - 12);
    brpc::policy::RpcMeta meta;
    ASSERT_TRUE(meta.ParseFromArray(received.data() + 12, meta_size));
    EXPECT_EQ(201u, meta.correlation_id());
    EXPECT_EQ(0, meta.response().error_code());
    test::EchoResponse echo;
    ASSERT_TRUE(echo.ParseFromArray(received.data() + 12 + meta_size,
                                   received.size() - 12 - meta_size));
    EXPECT_EQ(request.message(), echo.message());
    EXPECT_EQ(brpc::handshake::ESTABLISHED, adapter->handshake_phase());
    EXPECT_TRUE(server_ep->_receive_events_started.load(butil::memory_order_acquire));
    client_transport->DeactivateUpgrade();
    peer.reset(-1);
    server_socket.reset();
    server.Stop(0);
    server.Join();
}

TEST_F(UBShmEndpointTest, upgraded_client_receives_shared_memory_rpc_responses) {
    UBFallbackEchoService service;
    brpc::Server server;
    ASSERT_EQ(0, server.AddService(&service, brpc::SERVER_DOESNT_OWN_SERVICE));
    brpc::ServerOptions server_options;
    server_options.socket_mode = brpc::SOCKET_MODE_UBRING;
    server_options.enabled_protocols = "baidu_std";
    server_options.internal_port = -1;
    ASSERT_EQ(0, server.Start("127.0.0.1:0", &server_options));
    {
        brpc::Channel channel;
        brpc::ChannelOptions options;
        options.socket_mode = brpc::SOCKET_MODE_UBRING;
        options.timeout_ms = 5000;
        ASSERT_EQ(0, channel.Init(server.listen_address(), &options));
        test::EchoService_Stub stub(&channel);
        for (int i = 0; i < 2; ++i) {
            brpc::Controller cntl;
            test::EchoRequest request;
            request.set_message(std::string(1024, 'a' + i));
            test::EchoResponse response;
            stub.Echo(&cntl, &request, &response, nullptr);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            EXPECT_EQ(request.message(), response.message());
        }
        std::vector<brpc::SocketId> ids;
        server._am->ListConnections(&ids);
        ASSERT_EQ(1u, ids.size());
        brpc::SocketUniquePtr socket;
        ASSERT_EQ(0, brpc::Socket::Address(ids[0], &socket));
        EXPECT_EQ(brpc::handshake::ESTABLISHED,
                  brpc::AdapterTransport::Get(socket.get())->handshake_phase());
        EXPECT_TRUE(brpc::UBShmTransport::Get(socket.get())->UpgradeActive());
    }
    server.Stop(0);
    server.Join();
}

// The receive paths (UbrTrxRecvBlockMode / StartReadv) read `msg_len' and
// `cur_index' out of a chunk header the remote peer writes into the ring, then
// copy `msg_len - cur_index' bytes from the 60-byte `payload.inner'. A peer
// that writes msg_len > 60, or cur_index > msg_len (which underflows the
// uint8_t subtraction), makes that copy over-read the payload into adjacent
// shared memory. IsRecvChunkHeaderValid is the guard both paths now apply.
TEST(UBRingRecvChunkHeaderTest, reject_out_of_range_len_and_index) {
    using brpc::ubring::UBRing;
    // Legitimate values a well-formed peer produces: full payload, partial
    // consume, and the fully-consumed boundary.
    EXPECT_TRUE(UBRing::IsRecvChunkHeaderValid(UBR_MSG_PAYLOAD_LEN, 0));
    EXPECT_TRUE(UBRing::IsRecvChunkHeaderValid(10, 5));
    EXPECT_TRUE(UBRing::IsRecvChunkHeaderValid(0, 0));
    EXPECT_TRUE(UBRing::IsRecvChunkHeaderValid(UBR_MSG_PAYLOAD_LEN,
                                               UBR_MSG_PAYLOAD_LEN));
    // msg_len past the payload capacity -> over-read source.
    EXPECT_FALSE(UBRing::IsRecvChunkHeaderValid(UBR_MSG_PAYLOAD_LEN + 1, 0));
    EXPECT_FALSE(UBRing::IsRecvChunkHeaderValid(255, 0));
    // cur_index past msg_len -> `msg_len - cur_index' underflows to a large
    // uint8_t.
    EXPECT_FALSE(UBRing::IsRecvChunkHeaderValid(0, 1));
    EXPECT_FALSE(UBRing::IsRecvChunkHeaderValid(10, 20));
}

// A crafted chunk laid out exactly like one in the ring: the guard rejects it
// so the recv loop never reaches the over-reading memcpy.
TEST(UBRingRecvChunkHeaderTest, crafted_chunk_is_rejected) {
    brpc::ubring::UbrMsgFormat chunk;
    memset(&chunk, 0xAB, sizeof(chunk));
    chunk.header[UBR_MSG_LEN_INDEX] = 255;  // peer claims 255 bytes in a 60-byte payload
    chunk.header[UBR_MSG_CUR_INDEX] = 0;
    EXPECT_FALSE(brpc::ubring::UBRing::IsRecvChunkHeaderValid(
        chunk.header[UBR_MSG_LEN_INDEX], chunk.header[UBR_MSG_CUR_INDEX]));

    chunk.header[UBR_MSG_LEN_INDEX] = UBR_MSG_PAYLOAD_LEN;
    chunk.header[UBR_MSG_CUR_INDEX] = 0;
    EXPECT_TRUE(brpc::ubring::UBRing::IsRecvChunkHeaderValid(
        chunk.header[UBR_MSG_LEN_INDEX], chunk.header[UBR_MSG_CUR_INDEX]));
}

#else

TEST(UbringDisabledTest, skip) {
    SUCCEED() << "BRPC_WITH_UBRING is not enabled, skip.";
}

#endif  // BRPC_WITH_UBRING
