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
#include "brpc/ubshm/common/common.h"
#include "brpc/ubshm/ub_endpoint.h"
#include "brpc/ubshm/shm/shm_def.h"
#include "brpc/ubshm/shm/shm_mgr.h"
#include "brpc/ubshm/ub_ring_manager.h"

namespace brpc {
namespace ubring {
DECLARE_int32(ub_disconnect_timeout_s);
DECLARE_int32(ub_connect_timeout_s);
DECLARE_int32(ub_hb_timer_interval_s);
DECLARE_int32(ub_event_queue_timer_interval_us);
DECLARE_int32(ub_event_queue_timer_interval_max_us);
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
        {"ub_event_queue_timer_interval_max_us", "_us", "microseconds", "10000"},
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
    EXPECT_EQ(10000, brpc::ubring::FLAGS_ub_event_queue_timer_interval_max_us);
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

#else

TEST(UbringDisabledTest, skip) {
    SUCCEED() << "BRPC_WITH_UBRING is not enabled, skip.";
}

#endif  // BRPC_WITH_UBRING
