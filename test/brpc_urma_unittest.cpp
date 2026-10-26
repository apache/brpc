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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>

#if BRPC_WITH_URMA
#include "butil/atomicops.h"
#include "butil/fd_guard.h"
#include "brpc/input_messenger.h"
#include "butil/sys_byteorder.h"
#include "urma_api.h"
#include "brpc/handshake/urma_handshake.h"
#include "brpc/handshake/handshake_frame.h"
#include "brpc/handshake/handshake_io.h"
#include "brpc/urma/urma_handshake.pb.h"
#include "brpc/urma/urma_helper.h"
#include "brpc/urma_transport.h"
#include "urma_types.h"
#include "echo.pb.h"
#include "brpc/closure_guard.h"
#include "brpc/adapter_transport.h"
#include "brpc/acceptor.h"
#include "brpc/policy/transport_handshake_protocol.h"
#include "brpc/urma/urma_stream_trace.h"
#include "brpc/global.h"

using namespace brpc;

namespace brpc {
namespace urma {

DECLARE_int32(urma_client_handshake_version);
DECLARE_bool(urma_trace_stream);
extern bool g_skip_urma_init;
extern butil::atomic<bool> g_urma_available;

}  // namespace urma
}  // namespace brpc

// ---------------------------------------------------------------------------
// v2 binary HelloMessage: serialize + deserialize round-trips.
// ---------------------------------------------------------------------------
TEST(UrmaHandshakeTest, v2_serialize_deserialize_roundtrip) {
    urma::v2_wire::HelloMessage m;
    m.msg_len = urma::v2_wire::HELLO_PACKET_LEN;
    m.hello_ver = urma::v2_wire::HELLO_V2_VERSION;
    m.impl_ver = urma::v2_wire::IMPL_V2_VERSION;
    m.buffer_size = 8192;
    m.recv_buffer_cnt = 127;
    m.jetty_id = 0x12345678;
    for (int i = 0; i < 16; ++i) {
        m.eid[i] = static_cast<uint8_t>(i + 1);
    }
    m.uasid = 0xdeadbeef;
    m.tp_type = 1;  // URMA_CTP
    for (int i = 0; i < 16; ++i) {
        m.seg_eid[i] = static_cast<uint8_t>(16 - i);
    }
    m.seg_uasid = 0xcafebabe;
    m.seg_va = 0x1122334455667788ULL;
    m.seg_len = 1ULL << 20;
    m.seg_token_id = 0x42424242;

    uint8_t buf[urma::v2_wire::HELLO_BODY_LEN];
    m.Serialize(buf);

    urma::v2_wire::HelloMessage m2;
    m2.Deserialize(buf);
    EXPECT_EQ(m.msg_len, m2.msg_len);
    EXPECT_EQ(m.hello_ver, m2.hello_ver);
    EXPECT_EQ(m.impl_ver, m2.impl_ver);
    EXPECT_EQ(m.buffer_size, m2.buffer_size);
    EXPECT_EQ(m.recv_buffer_cnt, m2.recv_buffer_cnt);
    EXPECT_EQ(m.jetty_id, m2.jetty_id);
    EXPECT_EQ(0, memcmp(m.eid, m2.eid, 16));
    EXPECT_EQ(m.uasid, m2.uasid);
    EXPECT_EQ(m.tp_type, m2.tp_type);
    EXPECT_EQ(0, memcmp(m.seg_eid, m2.seg_eid, 16));
    EXPECT_EQ(m.seg_uasid, m2.seg_uasid);
    EXPECT_EQ(m.seg_va, m2.seg_va);
    EXPECT_EQ(m.seg_len, m2.seg_len);
    EXPECT_EQ(m.seg_token_id, m2.seg_token_id);
}

// ---------------------------------------------------------------------------
// v2 packet on the wire: "URMA" magic + body.
// ---------------------------------------------------------------------------
TEST(UrmaHandshakeTest, v2_packet_magic_is_urma) {
    EXPECT_EQ(4u, urma::v2_wire::MAGIC_STR_LEN);
    char magic[4] = {'U', 'R', 'M', 'A'};
    EXPECT_EQ(0, memcmp(magic, "URMA", 4));
    EXPECT_EQ(4u + 82u, urma::v2_wire::HELLO_PACKET_LEN);
}

// ---------------------------------------------------------------------------
// v3 protobuf UrmaHello: serialize + parse round-trips.
// ---------------------------------------------------------------------------
TEST(UrmaHandshakeTest, v3_protobuf_roundtrip) {
    urma::UrmaHello msg;
    msg.set_buffer_size(8192);
    msg.set_recv_buffer_cnt(127);
    msg.set_jetty_id(0x12345678);
    uint8_t eid[16];
    for (int i = 0; i < 16; ++i) {
        eid[i] = static_cast<uint8_t>(i + 1);
    }
    msg.set_eid(eid, 16);
    msg.set_uasid(0xdeadbeef);
    msg.set_tp_type(1);
    uint8_t seg_eid[16];
    for (int i = 0; i < 16; ++i) {
        seg_eid[i] = static_cast<uint8_t>(16 - i);
    }
    msg.set_seg_eid(seg_eid, 16);
    msg.set_seg_uasid(0xcafebabe);
    msg.set_seg_va(0x1122334455667788ULL);
    msg.set_seg_len(1ULL << 20);
    msg.set_seg_token_id(0x42424242);

    std::string body;
    ASSERT_TRUE(msg.SerializeToString(&body));
    urma::UrmaHello msg2;
    ASSERT_TRUE(msg2.ParseFromString(body));
    EXPECT_EQ(msg.buffer_size(), msg2.buffer_size());
    EXPECT_EQ(msg.recv_buffer_cnt(), msg2.recv_buffer_cnt());
    EXPECT_EQ(msg.jetty_id(), msg2.jetty_id());
    EXPECT_EQ(16, msg2.eid().size());
    EXPECT_EQ(0, memcmp(msg.eid().data(), msg2.eid().data(), 16));
    EXPECT_EQ(msg.uasid(), msg2.uasid());
    EXPECT_EQ(msg.tp_type(), msg2.tp_type());
    EXPECT_EQ(16, msg2.seg_eid().size());
    EXPECT_EQ(msg.seg_uasid(), msg2.seg_uasid());
    EXPECT_EQ(msg.seg_va(), msg2.seg_va());
    EXPECT_EQ(msg.seg_len(), msg2.seg_len());
    EXPECT_EQ(msg.seg_token_id(), msg2.seg_token_id());
}

TEST(UrmaHandshakeTest, common_adapter_parses_v2_and_v3_hello) {
    urma::v2_wire::HelloMessage v2{};
    v2.msg_len = urma::v2_wire::HELLO_PACKET_LEN;
    v2.hello_ver = urma::v2_wire::HELLO_V2_VERSION;
    v2.impl_ver = urma::v2_wire::IMPL_V2_VERSION;
    v2.buffer_size = 8192;
    v2.recv_buffer_cnt = 127;
    v2.jetty_id = 7;
    v2.tp_type = URMA_CTP;
    v2.seg_va = 0x1000;
    v2.seg_len = 8192;
    uint8_t v2_body[urma::v2_wire::HELLO_BODY_LEN];
    v2.Serialize(v2_body);
    const std::string v2_payload(
        reinterpret_cast<const char*>(v2_body + sizeof(uint16_t)),
        sizeof(v2_body) - sizeof(uint16_t));

    urma::UrmaHandshakeAdapter v2_adapter(nullptr, 2);
    ASSERT_EQ(handshake::STEP_OK, v2_adapter.ParseHello(v2_payload));
    EXPECT_EQ(7u, v2_adapter.remote().jetty_id);
    EXPECT_EQ(urma::v2_wire::HELLO_PACKET_LEN,
              v2_adapter.HelloFrameSpec().min_frame_len);

    urma::UrmaHello v3;
    v3.set_buffer_size(8192);
    v3.set_recv_buffer_cnt(127);
    v3.set_jetty_id(9);
    v3.set_eid(std::string(16, '\1'));
    v3.set_uasid(1);
    v3.set_tp_type(URMA_CTP);
    v3.set_seg_eid(std::string(16, '\2'));
    v3.set_seg_uasid(2);
    v3.set_seg_va(0x2000);
    v3.set_seg_len(8192);
    v3.set_seg_token_id(3);
    std::string v3_payload;
    ASSERT_TRUE(v3.SerializeToString(&v3_payload));

    urma::UrmaHandshakeAdapter v3_adapter(nullptr, 3);
    ASSERT_EQ(handshake::STEP_OK, v3_adapter.ParseHello(v3_payload));
    EXPECT_EQ(9u, v3_adapter.remote().jetty_id);
    EXPECT_EQ(handshake::FrameSpec::U32_BODY_LENGTH,
              v3_adapter.HelloFrameSpec().length_encoding);
}

TEST(UrmaHandshakeTest, common_adapter_rejects_oversized_v3_tp_type) {
    urma::UrmaHello message;
    message.set_buffer_size(8192);
    message.set_recv_buffer_cnt(127);
    message.set_jetty_id(9);
    message.set_eid(std::string(16, '\1'));
    message.set_uasid(1);
    message.set_tp_type(256);
    message.set_seg_eid(std::string(16, '\2'));
    message.set_seg_uasid(2);
    message.set_seg_va(0x2000);
    message.set_seg_len(8192);
    message.set_seg_token_id(3);
    std::string payload;
    ASSERT_TRUE(message.SerializeToString(&payload));

    urma::UrmaHandshakeAdapter adapter(nullptr, 3);
    EXPECT_EQ(handshake::STEP_FALLBACK, adapter.ParseHello(payload));
}

TEST(UrmaHandshakeTest, common_frames_preserve_fragmented_and_coalesced_data) {
    for (int version = 2; version <= 3; ++version) {
        urma::UrmaHandshakeAdapter adapter(nullptr, version);
        std::string hello;
        ASSERT_EQ(handshake::STEP_OK, adapter.BuildHello(false, &hello));
        std::string frame;
        ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
            adapter.HelloFrameSpec(), hello, &frame));

        butil::IOBuf source;
        source.append(frame.data(), 3);
        handshake::IOBufHandshakeInput input(&source);
        std::string parsed;
        EXPECT_EQ(handshake::FRAME_NEED_MORE,
                  handshake::FrameCodec::ParseBufferedFrame(
                      &input, adapter.HelloFrameSpec(), &parsed));
        EXPECT_EQ(3u, source.size());

        source.append(frame.data() + 3, frame.size() - 3);
        source.append("RPC", 3);
        ASSERT_EQ(handshake::FRAME_OK,
                  handshake::FrameCodec::ParseBufferedFrame(
                      &input, adapter.HelloFrameSpec(), &parsed));
        EXPECT_EQ(hello, parsed);
        EXPECT_EQ(3u, source.size());
        EXPECT_EQ("RPC", source.to_string());
    }
}

TEST(UrmaTransportTest, inactive_data_path_fails_without_tcp_delegate) {
    UrmaTransport transport;
    butil::IOBuf data;
    butil::IOBuf* list[] = {&data};
    errno = 0;
    EXPECT_EQ(-1, transport.CutFromIOBuf(&data));
    EXPECT_EQ(ENOTCONN, errno);
    errno = 0;
    EXPECT_EQ(-1, transport.CutFromIOBufList(list, 1));
    EXPECT_EQ(ENOTCONN, errno);

    butil::atomic<int> epollout_butex(0);
    const timespec duetime = {};
    errno = 0;
    EXPECT_EQ(1, transport.WaitEpollOut(&epollout_butex, false, duetime));
    EXPECT_EQ(ENOTCONN, errno);
}

class CapturingUrmaHandshakeIO : public handshake::HandshakeIO {
public:
    int ReadExact(void*, size_t) override { return -1; }
    int WriteAll(const void* data, size_t len) override {
        output.append(static_cast<const char*>(data), len);
        return 0;
    }
    int PushBack(const void*, size_t) override { return -1; }

    std::string output;
};

class FailingUrmaResources : public handshake::HandshakeTransport {
public:
    FailingUrmaResources() : version(0), fallback(false), failed(false) {}
    void OnProtocolSelected(handshake::HandshakeProtocol* protocol) override {
        version = protocol->ProtocolVersion();
    }
    handshake::StepResult PrepareResources() override {
        return handshake::STEP_FALLBACK;
    }
    handshake::StepResult NegotiateResources() override {
        return handshake::STEP_ERROR;
    }
    void OnEstablished() override { ADD_FAILURE() << "Unexpected upgrade"; }
    void OnFallback() override { fallback = true; }
    void OnFailed() override { failed = true; }

    int version;
    bool fallback;
    bool failed;
};

TEST(UrmaHandshakeTest, common_server_fallback_preserves_coalesced_rpc) {
    urma::v2_wire::HelloMessage hello{};
    hello.hello_ver = urma::v2_wire::HELLO_V2_VERSION;
    hello.impl_ver = urma::v2_wire::IMPL_V2_VERSION;
    hello.buffer_size = 8192;
    hello.recv_buffer_cnt = 127;
    hello.jetty_id = 7;
    hello.tp_type = URMA_CTP;
    hello.seg_va = 0x1000;
    hello.seg_len = 8192;
    uint8_t body[urma::v2_wire::HELLO_BODY_LEN];
    hello.Serialize(body);
    const std::string payload(
        reinterpret_cast<const char*>(body + sizeof(uint16_t)),
        sizeof(body) - sizeof(uint16_t));
    urma::UrmaHandshakeAdapter protocol(nullptr, 2);
    std::string frame;
    ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
        protocol.HelloFrameSpec(), payload, &frame));

    butil::IOBuf source;
    source.append(frame);
    source.append("RPC", 3);
    handshake::IOBufHandshakeInput input(&source);
    CapturingUrmaHandshakeIO io;
    handshake::HandshakeSession session;
    session.SetIOForTest(&io);
    FailingUrmaResources transport;
    std::vector<handshake::HandshakeProtocol*> protocols(1, &protocol);

    EXPECT_EQ(handshake::STEP_FALLBACK,
              session.RunServer(protocols, &input, &transport, false));
    EXPECT_EQ(handshake::FALLBACK_TCP, session.phase(butil::memory_order_acquire));
    EXPECT_EQ(2, transport.version);
    EXPECT_TRUE(transport.fallback);
    EXPECT_FALSE(transport.failed);
    EXPECT_EQ("RPC", source.to_string());
    EXPECT_EQ(urma::v2_wire::HELLO_PACKET_LEN, io.output.size());
    EXPECT_EQ("URMA", io.output.substr(0, 4));
}

// Exercise the real URMA field codec and common server driver without device
// allocation. Only the local Hello and resource provider are test doubles.
class BufferedUrmaTestProtocol : public urma::UrmaHandshakeAdapter {
public:
    explicit BufferedUrmaTestProtocol(int version)
        : urma::UrmaHandshakeAdapter(nullptr, version) {
        if (version == 2) {
            urma::v2_wire::HelloMessage hello{};
            hello.msg_len = urma::v2_wire::HELLO_PACKET_LEN;
            hello.hello_ver = urma::v2_wire::HELLO_V2_VERSION;
            hello.impl_ver = urma::v2_wire::IMPL_V2_VERSION;
            hello.buffer_size = 8192;
            hello.recv_buffer_cnt = 127;
            hello.jetty_id = 7;
            hello.tp_type = URMA_CTP;
            hello.seg_va = 0x1000;
            hello.seg_len = 8192;
            uint8_t body[urma::v2_wire::HELLO_BODY_LEN];
            hello.Serialize(body);
            local_payload.assign(
                reinterpret_cast<const char*>(body + sizeof(uint16_t)),
                sizeof(body) - sizeof(uint16_t));
        } else {
            urma::UrmaHello hello;
            hello.set_buffer_size(8192);
            hello.set_recv_buffer_cnt(127);
            hello.set_jetty_id(7);
            hello.set_tp_type(URMA_CTP);
            hello.set_eid(std::string(16, '\0'));
            hello.set_uasid(0);
            hello.set_seg_eid(std::string(16, '\0'));
            hello.set_seg_uasid(0);
            hello.set_seg_token_id(0);
            hello.set_seg_va(0x1000);
            hello.set_seg_len(8192);
            hello.SerializeToString(&local_payload);
        }
    }
    handshake::StepResult BuildHello(bool enabled, std::string* payload) override {
        if (!enabled) {
            return urma::UrmaHandshakeAdapter::BuildHello(false, payload);
        }
        *payload = local_payload;
        return handshake::STEP_OK;
    }
    std::string local_payload;
};

class TrackedUrmaTestResources : public handshake::HandshakeTransport {
public:
    butil::IOBuf* source = nullptr;
    bool allocated = false;
    bool active = false;
    int cleanup_count = 0;
    handshake::StepResult PrepareResources() override {
        allocated = true;
        return handshake::STEP_OK;
    }
    handshake::StepResult NegotiateResources() override {
        return handshake::STEP_OK;
    }
    handshake::StepResult ValidateEstablished() override {
        return source->empty() ? handshake::STEP_OK : handshake::STEP_ERROR;
    }
    void OnEstablished() override { active = true; }
    void OnFallback() override { Cleanup(); }
    void OnFailed() override { Cleanup(); }
private:
    void Cleanup() {
        allocated = false;
        active = false;
        ++cleanup_count;
    }
};

class ScriptedUrmaHandshakeIO : public CapturingUrmaHandshakeIO {
public:
    int ReadExact(void* data, size_t len) override {
        if (input.size() < len) {
            return -1;
        }
        memcpy(data, input.data(), len);
        input.erase(0, len);
        return 0;
    }
    std::string input;
};

class RejectingUrmaImport : public TrackedUrmaTestResources {
public:
    handshake::StepResult NegotiateResources() override {
        return handshake::STEP_FALLBACK;
    }
};

class RejectingUrmaPreparation : public TrackedUrmaTestResources {
public:
    handshake::StepResult PrepareResources() override {
        allocated = true;
        return handshake::STEP_FALLBACK;
    }
};

TEST(UrmaHandshakeTest, server_resource_failure_preserves_legacy_rpc_without_ack) {
    for (int version : {2, 3}) {
        for (bool reject_prepare : {false, true}) {
            BufferedUrmaTestProtocol protocol(version);
            std::string hello;
            ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
                protocol.HelloFrameSpec(), protocol.local_payload, &hello));
            butil::IOBuf source;
            source.append(hello);
            source.append("PRPC", 4);
            handshake::IOBufHandshakeInput input(&source);
            handshake::HandshakeSession session;
            CapturingUrmaHandshakeIO io;
            session.SetIOForTest(&io);
            RejectingUrmaPreparation prepare;
            RejectingUrmaImport negotiate;
            TrackedUrmaTestResources* resources = reject_prepare
                ? static_cast<TrackedUrmaTestResources*>(&prepare) : &negotiate;
            std::vector<handshake::HandshakeProtocol*> protocols(1, &protocol);
            EXPECT_EQ(handshake::STEP_FALLBACK,
                      session.RunServer(protocols, &input, resources, false));
            EXPECT_EQ(handshake::FALLBACK_TCP,
                      session.phase(butil::memory_order_acquire));
            EXPECT_EQ("PRPC", source.to_string());
            EXPECT_FALSE(resources->allocated);
            EXPECT_EQ(1, resources->cleanup_count);
            std::string disabled;
            ASSERT_EQ(handshake::STEP_OK, protocol.BuildHello(false, &disabled));
            std::string expected;
            ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
                protocol.HelloFrameSpec(), disabled, &expected));
            EXPECT_EQ(expected, io.output);
        }
    }
}

TEST(UrmaHandshakeTest, disabled_server_hello_does_not_send_ack) {
    for (int version : {2, 3}) {
        BufferedUrmaTestProtocol protocol(version);
        std::string disabled;
        ASSERT_EQ(handshake::STEP_OK, protocol.BuildHello(false, &disabled));
        ScriptedUrmaHandshakeIO io;
        ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
            protocol.HelloFrameSpec(), disabled, &io.input));
        std::string expected;
        ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
            protocol.HelloFrameSpec(), protocol.local_payload, &expected));
        handshake::HandshakeSession session;
        session.SetIOForTest(&io);
        TrackedUrmaTestResources resources;
        EXPECT_EQ(handshake::STEP_FALLBACK,
                  session.RunClient(&protocol, &resources));
        EXPECT_EQ(expected, io.output);  // No ACK before ordinary RPC bytes.
        EXPECT_FALSE(resources.allocated);
        EXPECT_EQ(1, resources.cleanup_count);
        EXPECT_EQ(handshake::FALLBACK_TCP,
                  session.phase(butil::memory_order_acquire));
    }
}

TEST(UrmaHandshakeTest, client_import_failure_still_sends_disabled_ack) {
    for (int version : {2, 3}) {
        BufferedUrmaTestProtocol protocol(version);
        ScriptedUrmaHandshakeIO io;
        ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
            protocol.HelloFrameSpec(), protocol.local_payload, &io.input));
        const std::string expected = io.input + std::string(4, '\0');
        handshake::HandshakeSession session;
        session.SetIOForTest(&io);
        RejectingUrmaImport resources;
        EXPECT_EQ(handshake::STEP_FALLBACK,
                  session.RunClient(&protocol, &resources));
        EXPECT_EQ(expected, io.output);
        EXPECT_FALSE(resources.allocated);
        EXPECT_EQ(1, resources.cleanup_count);
    }
}

class BufferedUrmaTestAdapter : public handshake::StandardHandshakeAdapter {
public:
    explicit BufferedUrmaTestAdapter(int version) : protocol(version) {
        session.SetIOForTest(&io);
    }
    BufferedUrmaTestProtocol protocol;
    TrackedUrmaTestResources resources;
    CapturingUrmaHandshakeIO io;
    mutable handshake::HandshakeSession session;
protected:
    handshake::StepResult RunServerStep(butil::IOBuf* source, Socket*) override {
        resources.source = source;
        handshake::IOBufHandshakeInput input(source);
        std::vector<handshake::HandshakeProtocol*> protocols(1, &protocol);
        return session.RunServer(protocols, &input, &resources, false);
    }
    handshake::HandshakeSession* GetSession(Socket*) const override {
        return &session;
    }
};

class UrmaCommonServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        int fds[2];
        ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
        peer_fd.reset(fds[1]);
        SocketOptions options;
        options.fd = fds[0];
        SocketId id;
        ASSERT_EQ(0, Socket::Create(options, &id));
        ASSERT_EQ(0, Socket::Address(id, &socket));
    }
    void TearDown() override {
        if (socket) {
            socket->reset_parsing_context(nullptr);
            socket->SetFailed();
        }
    }
    SocketUniquePtr socket;
    butil::fd_guard peer_fd;
};

TEST_F(UrmaCommonServerTest, reclaims_send_buffer_before_publishing_sq_credit) {
    urma::UrmaEndpoint endpoint(socket.get());
    const uint16_t capacity = 8;
    endpoint._sq_size = capacity + urma::UrmaEndpoint::RESERVED_WR_NUM;
    endpoint._local_window_capacity = capacity;
    endpoint._sbuf.resize(capacity);
    endpoint._sq_window_size.store(0, butil::memory_order_relaxed);
    int reclaimed = 0;
    urma_cr_t completion{};
    completion.status = URMA_CR_SUCCESS;
    completion.flag.bs.s_r = 0;
    completion.user_ctx = 1;
    for (int round = 0; round < 4 * capacity; ++round) {
        const uint16_t slot = round % capacity;
        ASSERT_EQ(slot, endpoint._sq_sent);
        ASSERT_EQ(0, endpoint._sbuf[slot].append_user_data(
            new char('x'), 1, [&](void* data) {
                // Deterministically inspect the publication boundary from
                // inside clear(), before the slot can be reused by a writer.
                EXPECT_EQ(0u, endpoint._sq_window_size.load(
                                  butil::memory_order_acquire));
                ++reclaimed;
                delete static_cast<char*>(data);
            }));
        ASSERT_EQ(0, endpoint.HandleCompletion(completion));
        EXPECT_TRUE(endpoint._sbuf[slot].empty());
        EXPECT_EQ((slot + 1) % capacity, endpoint._sq_sent);
        EXPECT_EQ(1u, endpoint._sq_window_size.fetch_sub(
                          1, butil::memory_order_acquire));
    }
    EXPECT_EQ(4 * capacity, reclaimed);
}

TEST_F(UrmaCommonServerTest, excess_send_completion_keeps_buffer_owned) {
    urma::UrmaEndpoint endpoint(socket.get());
    endpoint._local_window_capacity = 8;
    endpoint._sq_window_size.store(8, butil::memory_order_relaxed);
    endpoint._sbuf.resize(8);
    endpoint._sbuf[0].append("still owned");
    urma_cr_t completion{};
    completion.status = URMA_CR_SUCCESS;
    completion.flag.bs.s_r = 0;
    completion.user_ctx = 1;
    errno = 0;
    EXPECT_EQ(-1, endpoint.HandleCompletion(completion));
    EXPECT_EQ(EPROTO, errno);
    EXPECT_EQ("still owned", endpoint._sbuf[0].to_string());
    EXPECT_EQ(0u, endpoint._sq_sent);
    EXPECT_EQ(8u, endpoint._sq_window_size.load(butil::memory_order_acquire));
}

TEST_F(UrmaCommonServerTest, cq_events_cannot_start_inside_tcp_parser) {
    urma::UrmaEndpoint endpoint(socket.get());
    socket->set_parsing_stream_type(InputMessengerProcessor::STREAM_TCP_FD);
    EXPECT_EQ(-1, endpoint.StartCqEvents());
    EXPECT_EQ(EPROTO, errno);
    EXPECT_EQ(INVALID_SOCKET_ID, endpoint._cq_sid);
    socket->set_parsing_stream_type(InputMessengerProcessor::STREAM_NONE);
}

TEST_F(UrmaCommonServerTest, rejects_unidentified_receive_completion) {
    urma::UrmaEndpoint endpoint(socket.get());
    endpoint._rq_size = 4;
    endpoint._recv_completions.resize(4);
    endpoint._recv_completed.assign(4, false);
    urma_cr_t cr{};
    cr.status = URMA_CR_SUCCESS;
    cr.flag.bs.s_r = 1;
    cr.opcode = URMA_CR_OPC_SEND;
    cr.completion_len = 4;
    for (uint64_t slot : {0u, 5u}) {
        cr.user_ctx = slot;
        EXPECT_EQ(-1, endpoint.HandleCompletion(cr));
        EXPECT_EQ(EPROTO, errno);
        EXPECT_EQ(0u, endpoint._rq_received);
        EXPECT_TRUE(endpoint._input_processor.read_buf().empty());
    }
}

TEST_F(UrmaCommonServerTest, resumes_fragmented_hello_and_ack_without_rpc_handoff) {
    for (int version : {2, 3}) {
        BufferedUrmaTestAdapter adapter(version);
        std::string hello;
        ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
            adapter.protocol.HelloFrameSpec(), adapter.protocol.local_payload,
            &hello));
        butil::IOBuf source;
        source.append(hello.data(), 3);
        ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA,
                  adapter.ExecuteServerHandshake(&source, socket.get()).error());
        EXPECT_FALSE(adapter.resources.allocated);
        source.append(hello.data() + 3, hello.size() - 3);
        ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA,
                  adapter.ExecuteServerHandshake(&source, socket.get()).error());
        ASSERT_NE(nullptr, socket->parsing_context());
        EXPECT_TRUE(adapter.resources.allocated);
        const uint32_t ack = butil::HostToNet32(0x1);
        source.append(&ack, 2);
        ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA,
                  adapter.ExecuteServerHandshake(&source, socket.get()).error());
        EXPECT_FALSE(adapter.resources.active);
        source.append(reinterpret_cast<const char*>(&ack) + 2, 2);
        ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA,
                  adapter.ExecuteServerHandshake(&source, socket.get()).error());
        EXPECT_EQ(handshake::ESTABLISHED,
                  adapter.session.phase(butil::memory_order_acquire));
        EXPECT_TRUE(adapter.resources.active);
        EXPECT_EQ(0, adapter.resources.cleanup_count);
        EXPECT_EQ(nullptr, socket->parsing_context());
        EXPECT_TRUE(source.empty());
    }
}

TEST_F(UrmaCommonServerTest, rejects_coalesced_tcp_data_and_cleans_failed_resources) {
    for (int version : {2, 3}) {
        BufferedUrmaTestAdapter adapter(version);
        std::string hello;
        ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
            adapter.protocol.HelloFrameSpec(), adapter.protocol.local_payload,
            &hello));
        butil::IOBuf source;
        source.append(hello);
        const uint32_t ack = butil::HostToNet32(0x1);
        source.append(&ack, sizeof(ack));
        source.append("RPC", 3);
        EXPECT_EQ(PARSE_ERROR_ABSOLUTELY_WRONG,
                  adapter.ExecuteServerHandshake(&source, socket.get()).error());
        EXPECT_EQ(handshake::FAILED,
                  adapter.session.phase(butil::memory_order_acquire));
        EXPECT_FALSE(adapter.resources.allocated);
        EXPECT_FALSE(adapter.resources.active);
        EXPECT_EQ(1, adapter.resources.cleanup_count);
        EXPECT_EQ("RPC", source.to_string());
    }
}

TEST_F(UrmaCommonServerTest, disabled_ack_cleans_resources_and_preserves_rpc) {
    for (int version : {2, 3}) {
        BufferedUrmaTestAdapter adapter(version);
        std::string hello;
        ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
            adapter.protocol.HelloFrameSpec(), adapter.protocol.local_payload,
            &hello));
        butil::IOBuf source;
        source.append(hello);
        const uint32_t ack = 0;
        source.append(&ack, sizeof(ack));
        source.append("RPC", 3);
        EXPECT_EQ(PARSE_ERROR_TRY_OTHERS,
                  adapter.ExecuteServerHandshake(&source, socket.get()).error());
        EXPECT_EQ(handshake::FALLBACK_TCP,
                  adapter.session.phase(butil::memory_order_acquire));
        EXPECT_FALSE(adapter.resources.allocated);
        EXPECT_FALSE(adapter.resources.active);
        EXPECT_EQ(1, adapter.resources.cleanup_count);
        EXPECT_EQ("RPC", source.to_string());
    }
}

class UrmaUpgradeTestMessenger : public InputMessenger {
public:
    using InputMessenger::OnNewMessagesUntil;
    BufferedUrmaTestAdapter adapter{3};
    int peer_fd = -1;
    int parse_calls = 0;
    int write_result = -1;
    static ParseResult Parse(butil::IOBuf* source, Socket* socket,
                             bool, const void*) {
        auto* messenger = static_cast<UrmaUpgradeTestMessenger*>(socket->user());
        ++messenger->parse_calls;
        ParseResult result = messenger->adapter.ExecuteServerHandshake(
            source, socket);
        if (messenger->adapter.resources.active) {
            messenger->write_result = write(messenger->peer_fd, "RPC", 3);
        }
        return result;
    }
    static void Process(InputMessageBase*) {}
    static bool StopReading(Socket* socket) {
        return static_cast<UrmaUpgradeTestMessenger*>(socket->user())
            ->adapter.session.phase(butil::memory_order_acquire) ==
                handshake::ESTABLISHED;
    }
};

TEST(UrmaHandshakeTest, tcp_read_loop_stops_after_urma_upgrade) {
    UrmaUpgradeTestMessenger messenger;
    InputMessageHandler handler{};
    handler.parse = UrmaUpgradeTestMessenger::Parse;
    handler.process = UrmaUpgradeTestMessenger::Process;
    handler.name = "test_urma_upgrade";
    ASSERT_EQ(0, messenger.AddNonProtocolHandler(handler));
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    butil::fd_guard peer_fd(fds[1]);
    messenger.peer_fd = peer_fd;
    SocketOptions options;
    options.fd = fds[0];
    options.user = &messenger;
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));
    std::string hello;
    ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
        messenger.adapter.protocol.HelloFrameSpec(),
        messenger.adapter.protocol.local_payload, &hello));
    const uint32_t ack = butil::HostToNet32(0x1);
    hello.append(reinterpret_cast<const char*>(&ack), sizeof(ack));
    ASSERT_EQ(static_cast<ssize_t>(hello.size()),
              write(peer_fd, hello.data(), hello.size()));
    messenger.OnNewMessagesUntil(socket.get(),
                                 UrmaUpgradeTestMessenger::StopReading);
    EXPECT_EQ(1, messenger.parse_calls);
    EXPECT_EQ(3, messenger.write_result);
    EXPECT_TRUE(messenger.adapter.resources.active);
    char remaining[3];
    ASSERT_EQ(3, recv(socket->fd(), remaining, sizeof(remaining), MSG_DONTWAIT));
    EXPECT_EQ(0, memcmp(remaining, "RPC", 3));
    socket->SetFailed();
}

// ---------------------------------------------------------------------------
// Common server adapters support both URMA wire versions.
// ---------------------------------------------------------------------------
TEST(UrmaHandshakeTest, common_server_factory_supports_both_wire_versions) {
    auto adapters = urma::CreateServerHandshakeAdapters(nullptr);
    ASSERT_EQ(2u, adapters.size());
    for (size_t i = 0; i < adapters.size(); ++i) {
        ASSERT_NE(nullptr, adapters[i]);
        EXPECT_EQ(static_cast<int>(i) + 2, adapters[i]->ProtocolVersion());
        const auto& spec = adapters[i]->HelloFrameSpec();
        ASSERT_EQ(4u, spec.magic_len);
        EXPECT_EQ(i == 0 ? "URMA" : "URM3",
                  std::string(spec.magic, spec.magic_len));
    }
}

// ---------------------------------------------------------------------------
// The common client factory picks the version from the gflag.
// ---------------------------------------------------------------------------
TEST(UrmaHandshakeTest, common_client_factory_respects_flag) {
    GFLAGS_NAMESPACE::FlagSaver flag_saver;

    for (int version : {2, 3, 0, 99}) {
        urma::FLAGS_urma_client_handshake_version = version;
        auto adapter = urma::CreateClientHandshakeAdapter(nullptr);
        ASSERT_NE(nullptr, adapter);
        EXPECT_EQ(version == 3 ? 3 : 2, adapter->ProtocolVersion());
    }
}

// ---------------------------------------------------------------------------
// 4-byte ACK: HELLO_ACK_URMA_OK bit.
// ---------------------------------------------------------------------------
TEST(UrmaHandshakeTest, ack_bit_is_urma_ok) {
    // The ACK is a 4-byte big-endian flags word; bit 0 means "I want URMA".
    // Verify the round-trip: host -> net -> host preserves the bit.
    uint32_t flags = 0x1;  // HELLO_ACK_URMA_OK
    uint32_t flags_be = butil::HostToNet32(flags);
    uint32_t flags_back = butil::NetToHost32(flags_be);
    EXPECT_EQ(flags, flags_back);
    EXPECT_NE(0u, flags_back & 0x1);
}

// ---------------------------------------------------------------------------
// ParsedHello field layout: covers the flattened segment (seg_* fields).
// ---------------------------------------------------------------------------
TEST(UrmaHandshakeTest, parsed_hello_segment_fields) {
    urma::ParsedHello p{};
    p.buffer_size = 8192;
    p.recv_buffer_cnt = 127;
    p.jetty_id = 42;
    p.tp_type = 1;
    p.seg_va = 0x1000;
    p.seg_len = 0x100000;
    p.seg_token_id = 7;
    EXPECT_EQ(8192u, p.buffer_size);
    EXPECT_EQ(127u, p.recv_buffer_cnt);
    EXPECT_EQ(42u, p.jetty_id);
    EXPECT_EQ(1u, p.tp_type);
    EXPECT_EQ(0x1000u, p.seg_va);
    EXPECT_EQ(0x100000u, p.seg_len);
    EXPECT_EQ(7u, p.seg_token_id);
}

TEST(UrmaHandshakeTest, rejects_invalid_resource_and_window_values) {
    urma::ParsedHello hello;
    hello.buffer_size = 8192;
    hello.recv_buffer_cnt = 127;
    hello.jetty_id = 1;
    hello.tp_type = URMA_CTP;
    hello.seg_va = 0x1000;
    hello.seg_len = 8192;
    EXPECT_TRUE(urma::ValidHello(hello));

    hello.recv_buffer_cnt = 2;
    EXPECT_FALSE(urma::ValidHello(hello));
    hello.recv_buffer_cnt = 127;
    hello.seg_len = 0;
    EXPECT_FALSE(urma::ValidHello(hello));
    hello.seg_len = 8192;
    hello.jetty_id = 0;
    EXPECT_FALSE(urma::ValidHello(hello));

    hello.jetty_id = 1;
    hello.seg_va = std::numeric_limits<uint64_t>::max() - 7;
    hello.seg_len = 8;
    EXPECT_FALSE(urma::ValidHello(hello));
}

TEST(UrmaTraceTest, fingerprints_ignore_sge_boundaries) {
    const std::string payload = "hello";
    urma_sge_t whole{reinterpret_cast<uint64_t>(payload.data()), 5, nullptr, nullptr};
    urma_sge_t parts[] = {
        {reinterpret_cast<uint64_t>(payload.data()), 2, nullptr, nullptr},
        {reinterpret_cast<uint64_t>(payload.data() + 2), 3, nullptr, nullptr}};
    const auto a = urma::UrmaStreamTrace::Summarize(&whole, 1);
    const auto b = urma::UrmaStreamTrace::Summarize(parts, 2);
    EXPECT_EQ(UINT64_C(0xa430d84680aabd0b), a.hash);
    EXPECT_EQ(a.hash, b.hash);
    EXPECT_EQ(a.length, b.length);
    EXPECT_EQ("68656c6c6f", a.prefix);
    EXPECT_EQ(a.prefix, b.prefix);
}

TEST(UrmaTraceTest, bounds_payload_prefix_and_dump_history) {
    const std::string payload(64, 'a');
    urma_sge_t sge{reinterpret_cast<uint64_t>(payload.data()), 64, nullptr, nullptr};
    const auto summary = urma::UrmaStreamTrace::Summarize(&sge, 1);
    EXPECT_EQ(64u, summary.length);
    EXPECT_EQ(2 * urma::UrmaStreamTrace::PREFIX_SIZE, summary.prefix.size());
    urma::UrmaStreamTrace trace;
    EXPECT_TRUE(trace.DumpOnce().empty());
    for (size_t i = 0; i < urma::UrmaStreamTrace::CAPACITY + 8; ++i) {
        trace.Record(urma::UrmaStreamTrace::TX, i, 1, 0, &sge, 1);
    }
    const std::string dump = trace.DumpOnce();
    EXPECT_NE(std::string::npos, dump.find("records=32 total=40"));
    EXPECT_EQ(std::string::npos, dump.find(" seq=7 "));
    EXPECT_NE(std::string::npos, dump.find(" seq=8 offset=512 len=64"));
    EXPECT_NE(std::string::npos, dump.find(" seq=39 offset=2496 len=64"));
    EXPECT_TRUE(trace.DumpOnce().empty());
}

TEST(UrmaTraceTest, concurrent_directions_keep_independent_offsets) {
    const std::string payload = "xy";
    urma_sge_t sge{reinterpret_cast<uint64_t>(payload.data()), 2, nullptr, nullptr};
    urma::UrmaStreamTrace trace;
    auto record = [&](urma::UrmaStreamTrace::Stage stage) {
        for (size_t i = 0; i < 50; ++i) {
            trace.Record(stage, i, 1, 0, &sge, 1);
        }
    };
    std::thread sender(record, urma::UrmaStreamTrace::TX);
    std::thread receiver(record, urma::UrmaStreamTrace::RX_CQ);
    sender.join();
    receiver.join();
    const std::string dump = trace.DumpOnce();
    EXPECT_NE(std::string::npos, dump.find("records=32 total=100"));
    EXPECT_NE(std::string::npos, dump.find("seq=49 offset=98 len=2"));
}

TEST_F(UrmaCommonServerTest, stream_trace_is_opt_in) {
    const bool saved = urma::FLAGS_urma_trace_stream;
    urma::FLAGS_urma_trace_stream = false;
    urma::UrmaEndpoint disabled(socket.get());
    urma::FLAGS_urma_trace_stream = true;
    urma::UrmaEndpoint enabled(socket.get());
    urma::FLAGS_urma_trace_stream = saved;
    EXPECT_EQ(nullptr, disabled._stream_trace.get());
    EXPECT_NE(nullptr, enabled._stream_trace.get());
}

TEST(UrmaHelperTest, standalone_bonding_uses_regular_jetty_import) {
    EXPECT_FALSE(urma::NeedsUrmaBondingJettyExtension(true, 0));
    EXPECT_TRUE(urma::NeedsUrmaBondingJettyExtension(true, 1));
    EXPECT_TRUE(urma::NeedsUrmaBondingJettyExtension(true, 2));
    for (int mode = 0; mode <= 2; ++mode) {
        EXPECT_FALSE(urma::NeedsUrmaBondingJettyExtension(false, mode));
    }
    EXPECT_FALSE(urma::NeedsUrmaBondingJettyExtension(true, -1));
    EXPECT_FALSE(urma::NeedsUrmaBondingJettyExtension(true, 3));
}

TEST(UrmaHelperTest, selects_priority_matching_transport_path_type) {
    urma_device_attr_t attr{};
    attr.dev_cap.priority_info[3].tp_type.bs.rtp = 1;
    attr.dev_cap.priority_info[6].tp_type.bs.ctp = 1;

    EXPECT_EQ(3, urma::FindUrmaPriorityForTpType(attr, URMA_RTP));
    EXPECT_EQ(6, urma::FindUrmaPriorityForTpType(attr, URMA_CTP));
    EXPECT_EQ(-1, urma::FindUrmaPriorityForTpType(attr, URMA_UTP));
}

// ---------------------------------------------------------------------------
// SupportedByUrma: only baidu_std.
// ---------------------------------------------------------------------------
TEST(UrmaHandshakeTest, supported_by_urma_protocol_allowlist) {
    EXPECT_TRUE(urma::SupportedByUrma("baidu_std"));
    EXPECT_FALSE(urma::SupportedByUrma("http"));
    EXPECT_FALSE(urma::SupportedByUrma("hulu_pbrpc"));
    EXPECT_FALSE(urma::SupportedByUrma("nshead"));
}

// ---------------------------------------------------------------------------
// URMA mock smoke test: drives urma_init / device enumeration / context /
// jetty / post / poll. These tests rely on mock semantics and are skipped
// when the test binary links a real liburma provider.
// ---------------------------------------------------------------------------
class UrmaMockTest : public ::testing::Test {
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
            GTEST_SKIP() << "UrmaMockTest requires the URMA link-time mock";
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

static void CheckUrmaFallbackCleanup(bool zerocopy) {
    alarm(20);
    gflags::SetCommandLineOption("urma_prepared_jetty_cnt", "0");
    gflags::SetCommandLineOption("urma_buffer_count", "2048");
    gflags::SetCommandLineOption("urma_recv_zerocopy", zerocopy ? "true" : "false");
    urma::GlobalUrmaInitializeOrDie();
    ASSERT_NE(nullptr, urma::GetUrmaContext());
    for (int version : {2, 3}) {
        urma::FLAGS_urma_client_handshake_version = version;
        urma::UrmaHandshakeAdapter disabled(nullptr, version);
        std::string payload;
        ASSERT_EQ(handshake::STEP_OK, disabled.BuildHello(false, &payload));
        std::string frame;
        ASSERT_EQ(handshake::FRAME_OK, handshake::FrameCodec::Encode(
            disabled.HelloFrameSpec(), payload, &frame));
        for (int round = 0; round < 16; ++round) {
            SocketOptions options;
            options.socket_mode = SOCKET_MODE_URMA;
            SocketId id;
            ASSERT_EQ(0, Socket::Create(options, &id));
            SocketUniquePtr socket;
            ASSERT_EQ(0, Socket::Address(id, &socket));
            auto* adapter = AdapterTransport::Get(socket.get());
            auto* transport = static_cast<UrmaTransport*>(
                adapter->high_speed_transport());
            ScriptedUrmaHandshakeIO io;
            io.input = frame;
            adapter->handshake_session()->SetIOForTest(&io);
            std::promise<int> connected;
            auto ready = connected.get_future();
            ASSERT_EQ(0, AdapterTransport::StartClientUpgrade(socket.get(),
                [](int error, void* data) {
                    static_cast<std::promise<int>*>(data)->set_value(error);
                }, &connected));
            ASSERT_EQ(std::future_status::ready,
                      ready.wait_for(std::chrono::seconds(3)));
            EXPECT_EQ(0, ready.get());
            EXPECT_EQ(handshake::FALLBACK_TCP, adapter->handshake_phase());
            auto* endpoint = transport->GetUrmaEp();
            EXPECT_EQ(nullptr, endpoint->_resource);
            EXPECT_TRUE(endpoint->_rbuf.empty());
            EXPECT_TRUE(endpoint->_sbuf.empty());
            EXPECT_TRUE(endpoint->_rbuf_data.empty());
            EXPECT_TRUE(endpoint->_recv_completions.empty());
            EXPECT_TRUE(endpoint->_recv_completed.empty());
            EXPECT_EQ(0, endpoint->_local_window_capacity);
            EXPECT_EQ(0, endpoint->_remote_window_capacity);
            EXPECT_EQ(urma::UrmaEndpoint::FALLBACK_TCP,
                      endpoint->_state.load(butil::memory_order_acquire));
            socket->SetFailed();
        }
    }
}

TEST_F(UrmaMockTest, repeated_copy_fallback_releases_preposted_buffers) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT({
        CheckUrmaFallbackCleanup(false);
        _exit(::testing::Test::HasFailure() ? 1 : 0);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, repeated_zerocopy_fallback_releases_preposted_buffers) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT({
        CheckUrmaFallbackCleanup(true);
        _exit(::testing::Test::HasFailure() ? 1 : 0);
    }, ::testing::ExitedWithCode(0), "");
}

class CleanupErrnoUrmaHandshakeIO : public CapturingUrmaHandshakeIO {
public:
    explicit CleanupErrnoUrmaHandshakeIO(urma::UrmaEndpoint* ep) : endpoint(ep) {}
    int ReadExact(void*, size_t) override {
        errno = EIO;
        return -1;
    }
    int WriteAll(const void* data, size_t len) override {
        endpoint->_sbuf.emplace_back();
        endpoint->_sbuf.back().append_user_data(new int(0), sizeof(int),
            [](void* data) {
                delete static_cast<int*>(data);
                errno = ENOENT;
            });
        return CapturingUrmaHandshakeIO::WriteAll(data, len);
    }
    urma::UrmaEndpoint* endpoint;
};

static void CheckUrmaFailureErrno() {
    alarm(10);
    gflags::SetCommandLineOption("urma_prepared_jetty_cnt", "0");
    gflags::SetCommandLineOption("urma_buffer_count", "2048");
    urma::GlobalUrmaInitializeOrDie();
    SocketOptions options;
    options.socket_mode = SOCKET_MODE_URMA;
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));
    auto* adapter = AdapterTransport::Get(socket.get());
    auto* endpoint = static_cast<UrmaTransport*>(
        adapter->high_speed_transport())->GetUrmaEp();
    CleanupErrnoUrmaHandshakeIO io(endpoint);
    adapter->handshake_session()->SetIOForTest(&io);
    std::promise<int> connected;
    auto ready = connected.get_future();
    ASSERT_EQ(0, AdapterTransport::StartClientUpgrade(socket.get(),
        [](int error, void* data) {
            static_cast<std::promise<int>*>(data)->set_value(error);
        }, &connected));
    ASSERT_EQ(std::future_status::ready,
              ready.wait_for(std::chrono::seconds(3)));
    EXPECT_EQ(EIO, ready.get());
    EXPECT_EQ(handshake::FAILED, adapter->handshake_phase());
    EXPECT_TRUE(endpoint->_sbuf.empty());
    EXPECT_TRUE(endpoint->_rbuf.empty());
    socket->SetFailed();
}

TEST_F(UrmaMockTest, client_failure_preserves_errno_across_buffer_cleanup) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT({
        CheckUrmaFailureErrno();
        _exit(::testing::Test::HasFailure() ? 1 : 0);
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, send_completion_releases_source_only_after_copy) {
    int device_count = 0;
    urma_device_t** devices = urma_get_device_list(&device_count);
    ASSERT_NE(nullptr, devices);
    urma_context_t* raw_context = urma_create_context(devices[0], 0);
    urma_free_device_list(devices);
    std::unique_ptr<urma_context_t, decltype(&urma_delete_context)> context(
        raw_context, urma_delete_context);
    ASSERT_NE(nullptr, context.get());
    urma::UrmaResource resource;
    resource.jfce = urma_create_jfce(context.get());
    ASSERT_NE(nullptr, resource.jfce);
    urma_jfc_cfg_t jfc_cfg{};
    jfc_cfg.depth = 8;
    jfc_cfg.jfce = resource.jfce;
    resource.jfc = urma_create_jfc(context.get(), &jfc_cfg);
    ASSERT_NE(nullptr, resource.jfc);
    urma_jfr_cfg_t jfr_cfg{};
    jfr_cfg.depth = 4;
    jfr_cfg.trans_mode = URMA_TM_RM;
    jfr_cfg.max_sge = 1;
    jfr_cfg.jfc = resource.jfc;
    resource.jfr = urma_create_jfr(context.get(), &jfr_cfg);
    ASSERT_NE(nullptr, resource.jfr);
    urma_jetty_cfg_t jetty_cfg{};
    jetty_cfg.flag.bs.share_jfr = 1;
    jetty_cfg.jfs_cfg.depth = 4;
    jetty_cfg.jfs_cfg.trans_mode = URMA_TM_RM;
    jetty_cfg.jfs_cfg.max_sge = 1;
    jetty_cfg.jfs_cfg.jfc = resource.jfc;
    jetty_cfg.shared.jfr = resource.jfr;
    jetty_cfg.shared.jfc = resource.jfc;
    resource.jetty = urma_create_jetty(context.get(), &jetty_cfg);
    ASSERT_NE(nullptr, resource.jetty);
    urma_rjetty_t remote{};
    remote.jetty_id = resource.jetty->jetty_id;
    remote.trans_mode = URMA_TM_RM;
    remote.type = URMA_JETTY;
    remote.tp_type = URMA_CTP;
    urma_token_t token{};
    resource.remote_jetty = urma_import_jetty(context.get(), &remote, &token);
    ASSERT_NE(nullptr, resource.remote_jetty);

    const std::string expected(1024 * 1024, 'x');
    std::string payload(expected);
    std::string received(expected.size(), '\0');
    for (int round = 0; round < 8; ++round) {
        payload = expected;
        urma_sge_t receive_sge{reinterpret_cast<uint64_t>(&received[0]),
            static_cast<uint32_t>(received.size()), nullptr, nullptr};
        urma_jfr_wr_t receive_wr{{&receive_sge, 1}, 0, nullptr};
        urma_jfr_wr_t* bad_receive = nullptr;
        ASSERT_EQ(URMA_SUCCESS, urma_post_jfr_wr(
            resource.jfr, &receive_wr, &bad_receive));
        urma_sge_t send_sge{reinterpret_cast<uint64_t>(&payload[0]),
            static_cast<uint32_t>(payload.size()), nullptr, nullptr};
        urma_jfs_wr_t send_wr{};
        send_wr.opcode = URMA_OPC_SEND;
        send_wr.flag.bs.complete_enable = 1;
        send_wr.tjetty = resource.remote_jetty;
        send_wr.send.src = {&send_sge, 1};
        std::atomic<bool> ready(false);
        std::atomic<bool> stop(false);
        int completions = 0;
        std::thread poller([&]() {
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(5);
            ready.store(true, std::memory_order_release);
            while (completions < 2 && !stop.load(std::memory_order_relaxed) &&
                   std::chrono::steady_clock::now() < deadline) {
                urma_cr_t completion{};
                if (urma_poll_jfc(resource.jfc, 1, &completion) == 1) {
                    if (completion.flag.bs.s_r == 0) {
                        // A real caller may reuse the source immediately
                        // after observing the send completion.
                        memset(&payload[0], 'z', payload.size());
                    }
                    ++completions;
                } else {
                    std::this_thread::yield();
                }
            }
        });
        while (!ready.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        urma_jfs_wr_t* bad_send = nullptr;
        const urma_status_t status = urma_post_jetty_send_wr(
            resource.jetty, &send_wr, &bad_send);
        if (status != URMA_SUCCESS) {
            stop.store(true, std::memory_order_relaxed);
        }
        poller.join();
        ASSERT_EQ(URMA_SUCCESS, status);
        ASSERT_EQ(2, completions);
        EXPECT_EQ(0, memcmp(expected.data(), received.data(), expected.size()));
    }
}

TEST_F(UrmaMockTest, segmented_stream_survives_send_queue_wraparound) {
    int device_count = 0;
    urma_device_t** devices = urma_get_device_list(&device_count);
    ASSERT_NE(nullptr, devices);
    urma_context_t* raw_context = urma_create_context(devices[0], 0);
    urma_free_device_list(devices);
    std::unique_ptr<urma_context_t, decltype(&urma_delete_context)> context(
        raw_context, urma_delete_context);
    ASSERT_NE(nullptr, context.get());

    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    butil::fd_guard peer(fds[1]);
    SocketOptions options;
    options.fd = fds[0];
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));
    struct FailSocket {
        Socket* socket;
        ~FailSocket() { socket->SetFailed(); }
    } fail_socket{socket.get()};
    urma::UrmaEndpoint endpoint(socket.get());
    endpoint._resource = new urma::UrmaResource;
    auto* resource = endpoint._resource;
    resource->jfce = urma_create_jfce(context.get());
    ASSERT_NE(nullptr, resource->jfce);
    urma_jfc_cfg_t jfc_cfg{};
    jfc_cfg.depth = 32;
    jfc_cfg.jfce = resource->jfce;
    resource->jfc = urma_create_jfc(context.get(), &jfc_cfg);
    ASSERT_NE(nullptr, resource->jfc);
    urma_jfr_cfg_t jfr_cfg{};
    jfr_cfg.depth = 8;
    jfr_cfg.trans_mode = URMA_TM_RM;
    jfr_cfg.max_sge = 1;
    jfr_cfg.jfc = resource->jfc;
    resource->jfr = urma_create_jfr(context.get(), &jfr_cfg);
    ASSERT_NE(nullptr, resource->jfr);
    urma_jetty_cfg_t jetty_cfg{};
    jetty_cfg.flag.bs.share_jfr = 1;
    jetty_cfg.jfs_cfg.depth = 8;
    jetty_cfg.jfs_cfg.trans_mode = URMA_TM_RM;
    jetty_cfg.jfs_cfg.max_sge = 13;
    jetty_cfg.jfs_cfg.jfc = resource->jfc;
    jetty_cfg.shared.jfr = resource->jfr;
    jetty_cfg.shared.jfc = resource->jfc;
    resource->jetty = urma_create_jetty(context.get(), &jetty_cfg);
    ASSERT_NE(nullptr, resource->jetty);
    urma_rjetty_t remote{};
    remote.jetty_id = resource->jetty->jetty_id;
    remote.trans_mode = URMA_TM_RM;
    remote.type = URMA_JETTY;
    urma_token_t token{};
    resource->remote_jetty = urma_import_jetty(context.get(), &remote, &token);
    ASSERT_NE(nullptr, resource->remote_jetty);

    const uint16_t capacity = 5;
    const size_t block_size = 128;
    endpoint._sq_size = capacity + urma::UrmaEndpoint::RESERVED_WR_NUM;
    endpoint._local_window_capacity = capacity;
    endpoint._sbuf.resize(capacity);
    endpoint._remote_recv_block_size = block_size;
    std::vector<std::vector<char>> receive_buffers(
        capacity, std::vector<char>(block_size));
    auto post_receive = [&](size_t slot) {
        urma_sge_t sge{reinterpret_cast<uint64_t>(receive_buffers[slot].data()),
            static_cast<uint32_t>(block_size), nullptr, nullptr};
        urma_jfr_wr_t wr{{&sge, 1}, slot + 1, nullptr};
        urma_jfr_wr_t* bad = nullptr;
        return urma_post_jfr_wr(resource->jfr, &wr, &bad);
    };
    for (size_t slot = 0; slot < capacity; ++slot) {
        ASSERT_EQ(URMA_SUCCESS, post_receive(slot));
    }

    // The link-time mock does not DMA, so a synthetic registered segment is
    // sufficient. Each part owns separate storage to force real SGE boundaries.
    urma_target_seg_t segment{};
    segment.seg.len = std::numeric_limits<uint64_t>::max();
    for (int round = 0; round < 64; ++round) {
        endpoint._sq_window_size.store(capacity, butil::memory_order_relaxed);
        endpoint._remote_rq_window_size.store(capacity, butil::memory_order_relaxed);
        std::vector<butil::IOBuf> messages(64);
        std::vector<butil::IOBuf*> list;
        std::string expected;
        for (size_t i = 0; i < messages.size(); ++i) {
            for (const std::string& part : {
                     std::string("PRPC:") + std::to_string(round) + ":" +
                         std::to_string(i) + std::string(i % 37, 'x'),
                     std::string("\x08\0", 2)}) {
                char* data = new char[part.size()];
                memcpy(data, part.data(), part.size());
                ASSERT_EQ(0, messages[i].append_user_data_with_meta(
                    data, part.size(), [](void* p) { delete[] static_cast<char*>(p); },
                    reinterpret_cast<uint64_t>(&segment)));
                expected.append(part);
            }
            list.push_back(&messages[i]);
        }
        std::string received;
        size_t sent = 0;
        while (sent < expected.size()) {
            const ssize_t bytes = endpoint.CutFromIOBufList(list.data(), list.size());
            ASSERT_GT(bytes, 0);
            sent += bytes;
            urma_cr_t cr{};
            while (urma_poll_jfc(resource->jfc, 1, &cr) == 1) {
                if (cr.flag.bs.s_r == 0) {
                    ASSERT_EQ(0, endpoint.HandleCompletion(cr));
                } else {
                    ASSERT_GE(cr.user_ctx, 1u);
                    ASSERT_LE(cr.user_ctx, capacity);
                    const size_t slot = cr.user_ctx - 1;
                    received.append(receive_buffers[slot].data(), cr.completion_len);
                    ASSERT_EQ(URMA_SUCCESS, post_receive(slot));
                    endpoint._remote_rq_window_size.fetch_add(
                        1, butil::memory_order_relaxed);
                }
            }
        }
        ASSERT_EQ(expected, received) << "round=" << round;
    }
}

class UrmaStreamEchoService : public test::EchoService {
public:
    void Echo(google::protobuf::RpcController* controller,
              const test::EchoRequest* request, test::EchoResponse* response,
              google::protobuf::Closure* done) override {
        ClosureGuard guard(done);
        response->set_message(request->message());
        auto* cntl = static_cast<Controller*>(controller);
        cntl->response_attachment().append(cntl->request_attachment());
        calls.fetch_add(1, std::memory_order_release);
    }
    std::atomic<int> calls{0};
};

// Use the retained legacy state machines/codecs, not a second common adapter
// or a synthesized Hello. Only transport installation is reconstructed here:
// production TransportFactory now always creates AdapterTransport.
static void InstallLegacyUrmaTransport(Socket* socket, SocketUser* user) {
    socket->_transport->Release();
    SocketOptions options;
    options.socket_mode = SOCKET_MODE_URMA;
    options.user = user;
    options.need_on_edge_trigger = true;
    auto transport = std::unique_ptr<UrmaTransport>(new UrmaTransport);
    transport->Init(socket, options);
    transport->_on_edge_trigger = urma::UrmaEndpoint::OnNewDataFromTcp;
    transport->_default_connect = std::make_shared<urma::UrmaConnect>();
    socket->_app_connect = transport->_default_connect;
    socket->_transport = std::move(transport);
}

static int CreateUrmaInteropTcpPair(int fds[2]) {
    butil::fd_guard listener(socket(AF_INET, SOCK_STREAM, 0));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    if (listener < 0 || bind(listener, reinterpret_cast<sockaddr*>(&address), size) != 0 ||
        listen(listener, 1) != 0 ||
        getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
        return -1;
    }
    butil::fd_guard client(socket(AF_INET, SOCK_STREAM, 0));
    if (client < 0 || connect(client, reinterpret_cast<sockaddr*>(&address), size) != 0) {
        return -1;
    }
    butil::fd_guard accepted(accept(listener, nullptr, nullptr));
    if (accepted < 0) {
        return -1;
    }
    fds[0] = client.release();
    fds[1] = accepted.release();
    return 0;
}

class UrmaFinalAckGate {
public:
    void Pause(Socket* socket) {
        std::unique_lock<std::mutex> lock(mutex);
        tcp_parser_active = socket->parsing_stream_type() ==
                            InputMessengerProcessor::STREAM_TCP_FD;
        entered = true;
        cv.notify_all();
        if (!cv.wait_for(lock, std::chrono::seconds(3), [&] { return released; })) {
            timed_out = true;
        }
    }
    bool Wait() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [&] { return entered; });
    }
    void Release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        cv.notify_all();
    }
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool released = false;
    bool tcp_parser_active = false;
    std::atomic<bool> timed_out{false};
};

// Each scenario runs in an isolated subprocess; its server has one connection.
static UrmaFinalAckGate* g_urma_final_ack_gate = nullptr;

static ParseResult GateUrmaFinalAck(butil::IOBuf* source, Socket* socket,
                                    bool eof, const void* arg) {
    ParseResult result = policy::ParseTransportHandshake(source, socket, eof, arg);
    if (socket->parsing_stream_type() == InputMessengerProcessor::STREAM_TCP_FD &&
        AdapterTransport::Get(socket)->handshake_phase() == handshake::ESTABLISHED) {
        g_urma_final_ack_gate->Pause(socket);
    }
    return result;
}

static ParseResult SkipLegacyUrmaHandshake(butil::IOBuf*, Socket*, bool,
                                           const void*) {
    // The old dedicated transport already consumed the TCP handshake. This is
    // the baseline parser's URMA exclusion, before the common migration.
    return MakeParseError(PARSE_ERROR_TRY_OTHERS);
}

class UrmaDelayedAckIO : public handshake::HandshakeIO {
public:
    explicit UrmaDelayedAckIO(handshake::HandshakeIO* io) : delegate(io) {}
    int ReadExact(void* data, size_t len) override {
        return delegate->ReadExact(data, len);
    }
    int PushBack(const void* data, size_t len) override {
        return delegate->PushBack(data, len);
    }
    int WriteAll(const void* data, size_t len) override {
        if (len == sizeof(uint32_t)) {
            const char* ack = static_cast<const char*>(data);
            held.assign(ack + len - 1, 1);
            // Model cross-channel delay: TCP's final ACK byte arrives after
            // the URMA SEND, although the client's write has completed.
            return delegate->WriteAll(data, len - 1);
        }
        return delegate->WriteAll(data, len);
    }
    int FlushAck() { return delegate->WriteAll(held.data(), held.size()); }
    handshake::HandshakeIO* delegate;
    std::string held;
};

enum class UrmaInteropPeer { COMMON, LEGACY_CLIENT, LEGACY_SERVER };
enum class UrmaFirstPacket { AFTER_ACK, BEFORE_LAST_ACK_BYTE, IN_ACK_PARSER };

static ParseResult PauseUrmaCq(butil::IOBuf* source, Socket* socket, bool,
                                const void* arg) {
    auto* gate = static_cast<UrmaFinalAckGate*>(const_cast<void*>(arg));
    gate->Pause(socket);
    source->clear();
    return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
}

// A real endpoint/carrier and mock SEND-to-self give the CQ callback a receive
// to dispatch. The parser barrier keeps that callback in flight during close.
class UrmaCqCloseTest {
public:
    int Init(bool polling, bool zerocopy, UrmaFinalAckGate* gate,
             bool start_poller = true) {
        urma::g_skip_urma_init = false;
        gflags::SetCommandLineOption("urma_prepared_jetty_cnt", "0");
        gflags::SetCommandLineOption("urma_buffer_count", "2048");
        gflags::SetCommandLineOption("urma_recv_zerocopy", zerocopy ? "true" : "false");
        gflags::SetCommandLineOption("urma_zerocopy_min_size", "0");
        gflags::SetCommandLineOption("urma_use_polling", polling ? "true" : "false");
        urma::GlobalUrmaInitializeOrDie();
        if (!urma::GetUrmaContext() ||
            (polling && start_poller &&
             !urma::InitPollingModeWithTag(bthread_self_tag()))) {
            return 10;
        }
        InputMessageHandler handler{};
        handler.parse = PauseUrmaCq;
        handler.process = [](InputMessageBase* msg) { msg->Destroy(); };
        handler.arg = gate;
        handler.name = "urma_cq_close";
        if (messenger.AddNonProtocolHandler(handler) != 0) {
            return 11;
        }
        SocketOptions options;
        options.socket_mode = SOCKET_MODE_URMA;
        options.user = &messenger;
        if (Socket::Create(options, &id) != 0 ||
            Socket::Address(id, &socket) != 0) {
            return 12;
        }
        endpoint = static_cast<UrmaTransport*>(
            AdapterTransport::Get(socket.get())->high_speed_transport())->GetUrmaEp();
        return Prepare(zerocopy);
    }

    int Prepare(bool zerocopy) {
        if (endpoint->AllocateResources() != 0 ||
            endpoint->PostRecv(endpoint->_rq_size, zerocopy) != 0) {
            return 13;
        }
        urma::ParsedHello hello;
        endpoint->MakeLocalParsedHello(&hello);
        endpoint->ApplyRemoteHello(hello);
        if (endpoint->ImportPeer(hello) != 0) {
            return 14;
        }
        endpoint->_state = urma::UrmaEndpoint::ESTABLISHED;
        if (endpoint->StartCqEvents() != 0 ||
            Socket::Address(endpoint->_cq_sid, &carrier) != 0) {
            return 15;
        }
        return 0;
    }

    bool Send() {
        butil::IOBuf data;
        data.append("cq-lifetime");
        butil::IOBuf* list[] = {&data};
        return endpoint->CutFromIOBufList(list, 1) == 11;
    }

    ~UrmaCqCloseTest() {
        if (socket) {
            socket->SetFailed();
            socket.reset();
        }
    }

    InputMessenger messenger;
    SocketId id{INVALID_SOCKET_ID};
    SocketUniquePtr socket;
    SocketUniquePtr carrier;
    urma::UrmaEndpoint* endpoint{nullptr};
};

static int RunUrmaCqConcurrentClose(bool polling, bool zerocopy,
                                     bool release_last_reference) {
    alarm(10);
    UrmaFinalAckGate gate;
    UrmaCqCloseTest test;
    const int init = test.Init(polling, zerocopy, &gate);
    if (init != 0) {
        return init;
    }
    auto lifetime = test.endpoint->_cq_lifetime;
    if (!test.Send()) {
        return 16;
    }
    if (!gate.Wait()) {
        return 17;
    }
    std::future<void> close;
    std::promise<void> started;
    if (release_last_reference) {
        test.socket->SetFailed();
        test.socket.reset();
        // PollCq's reference must still keep the endpoint/resources alive.
        if (lifetime->mutex.try_lock()) {
            lifetime->mutex.unlock();
            return 18;
        }
    } else {
        auto ready = started.get_future();
        close = std::async(std::launch::async, [&] {
            started.set_value();
            test.endpoint->DeallocateResources();
        });
        ready.wait();
        if (close.wait_for(std::chrono::milliseconds(20)) !=
            std::future_status::timeout) {
            return 19;
        }
    }
    gate.Release();
    if (close.valid()) {
        close.get();
    }
    // The event dispatcher / poller owns the callback. Wait for its final main
    // reference to trigger endpoint teardown.
    if (release_last_reference) {
        for (;;) {
            {
                std::unique_lock<bthread::Mutex> lock(lifetime->mutex);
                if (lifetime->closed) {
                    break;
                }
            }
            std::this_thread::yield();
        }
    }
    {
        std::unique_lock<bthread::Mutex> lock(lifetime->mutex);
        if (!lifetime->closed || gate.timed_out.load()) {
            return 20;
        }
    }
    // Keep a queued carrier callback alive beyond close / endpoint deletion.
    // It must not touch the old endpoint, even if the main Socket is gone.
    urma::UrmaEndpoint::PollCq(test.carrier.get());
    if (!release_last_reference &&
        (test.endpoint->_resource != nullptr ||
         test.endpoint->_cq_sid != INVALID_SOCKET_ID)) {
        return 21;
    }
    std::weak_ptr<urma::UrmaEndpoint::CqLifetime> weak = lifetime;
    lifetime.reset();
    test.carrier.reset();
    if (release_last_reference) {
        while (!weak.expired()) {
            std::this_thread::yield();
        }
    }
    return 0;
}

static int RunUrmaCqCallbackAfterReset(bool polling, bool zerocopy) {
    alarm(10);
    UrmaFinalAckGate gate;
    UrmaCqCloseTest test;
    // Keep pollers stopped so the new CQ remains untouched by dispatchers.
    const int init = test.Init(polling, zerocopy, &gate, false);
    if (init != 0) {
        return init;
    }
    SocketUniquePtr old_carrier = std::move(test.carrier);
    const int old_fd = test.endpoint->_resource->jfce->fd;
    test.endpoint->Reset();
    if (test.Prepare(zerocopy) != 0) {
        return 23;
    }
    // No dispatcher may drain the new generation before this assertion.
    if (!polling) {
        test.carrier->_io_event.RemoveConsumer(test.carrier->fd());
    }
    if (!test.Send()) {
        return 24;
    }
    urma::UrmaEndpoint::PollCq(old_carrier.get());
    urma_cr_t completions[2];
    if (urma_poll_jfc(test.endpoint->_resource->jfc, 2, completions) != 2) {
        return 25;
    }
    // Old carrier destruction must not close a JFCE fd reused by the reset.
    old_carrier.reset();
    if (old_fd == test.endpoint->_resource->jfce->fd &&
        fcntl(old_fd, F_GETFD) < 0) {
        return 26;
    }
    return 0;
}

TEST_F(UrmaMockTest, cq_close_waits_for_running_callback) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    for (bool polling : {false, true}) {
        for (bool zerocopy : {false, true}) {
            SCOPED_TRACE(::testing::Message() << polling << ":" << zerocopy);
            ASSERT_EXIT(_exit(RunUrmaCqConcurrentClose(polling, zerocopy, false)),
                        ::testing::ExitedWithCode(0), "");
        }
    }
}

TEST_F(UrmaMockTest, cq_callback_releases_last_socket_reference_before_teardown) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    for (bool polling : {false, true}) {
        for (bool zerocopy : {false, true}) {
            SCOPED_TRACE(::testing::Message() << polling << ":" << zerocopy);
            ASSERT_EXIT(_exit(RunUrmaCqConcurrentClose(polling, zerocopy, true)),
                        ::testing::ExitedWithCode(0), "");
        }
    }
}

TEST_F(UrmaMockTest, cq_callback_rejects_reset_generation) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    for (bool polling : {false, true}) {
        for (bool zerocopy : {false, true}) {
            SCOPED_TRACE(::testing::Message() << polling << ":" << zerocopy);
            ASSERT_EXIT(_exit(RunUrmaCqCallbackAfterReset(polling, zerocopy)),
                        ::testing::ExitedWithCode(0), "");
        }
    }
}

static int RunUrmaHandshakeInterop(int version, bool polling, bool zerocopy,
                                   UrmaInteropPeer peer, UrmaFirstPacket order) {
    alarm(15);
    urma::g_skip_urma_init = false;
    gflags::SetCommandLineOption("urma_prepared_jetty_cnt", "0");
    gflags::SetCommandLineOption("urma_buffer_count", "2048");
    gflags::SetCommandLineOption("urma_use_polling", polling ? "true" : "false");
    gflags::SetCommandLineOption("urma_recv_zerocopy", zerocopy ? "true" : "false");
    urma::FLAGS_urma_client_handshake_version = version;
    urma::GlobalUrmaInitializeOrDie();
    if (!urma::GetUrmaContext()) {
        return 30;
    }
    UrmaFinalAckGate gate;
    UrmaStreamEchoService service;
    Server server;
    ServerOptions server_options;
    server_options.socket_mode = SOCKET_MODE_URMA;
    if (server.AddService(&service, SERVER_DOESNT_OWN_SERVICE) != 0 ||
        server.Start(0, &server_options) != 0) {
        return 31;
    }
    for (int i = 0; i <= server._am->_max_index.load(butil::memory_order_acquire); ++i) {
        auto& handler = server._am->_handlers[i];
        if (handler.parse == policy::ParseTransportHandshake) {
            if (peer == UrmaInteropPeer::LEGACY_SERVER) {
                handler.parse = SkipLegacyUrmaHandshake;
            } else if (order == UrmaFirstPacket::IN_ACK_PARSER) {
                g_urma_final_ack_gate = &gate;
                handler.parse = GateUrmaFinalAck;
            }
        }
    }
    Channel channel;
    ChannelOptions options;
    options.socket_mode = SOCKET_MODE_URMA;
    options.protocol = "baidu_std";
    options.connection_type = "single";
    options.timeout_ms = 3000;
    options.max_retry = 0;
    if (channel.Init(server.listen_address(), &options) != 0) {
        return 32;
    }
    SocketUniquePtr client;
    if (Socket::Address(channel._server_id, &client) != 0) {
        return 33;
    }
    SocketOptions accepted_options;
    accepted_options.socket_mode = SOCKET_MODE_URMA;
    accepted_options.user = server._am;
    accepted_options.need_on_edge_trigger = true;
    SocketId accepted_id;
    if (Socket::Create(accepted_options, &accepted_id) != 0) {
        return 34;
    }
    SocketUniquePtr accepted;
    if (Socket::Address(accepted_id, &accepted) != 0) {
        return 35;
    }
    // Pin both main sockets throughout the handoff/RPC checks. CQ teardown
    // lifetime races are a separate test and fix, not part of this scenario.
    if (peer == UrmaInteropPeer::LEGACY_CLIENT) {
        InstallLegacyUrmaTransport(client.get(), client->user());
    } else if (peer == UrmaInteropPeer::LEGACY_SERVER) {
        InstallLegacyUrmaTransport(accepted.get(), server._am);
    }
    int fds[2];
    if (CreateUrmaInteropTcpPair(fds) != 0 ||
        client->ResetFileDescriptor(fds[0]) != 0 ||
        accepted->ResetFileDescriptor(fds[1]) != 0) {
        return 36;
    }
    urma::UrmaEndpoint* accepted_ep = peer == UrmaInteropPeer::LEGACY_SERVER
        ? static_cast<UrmaTransport*>(accepted->_transport.get())->GetUrmaEp()
        : static_cast<UrmaTransport*>(AdapterTransport::Get(accepted.get())
                                         ->high_speed_transport())->GetUrmaEp();
    handshake::HandshakeSession* session = peer == UrmaInteropPeer::LEGACY_CLIENT
        ? nullptr : AdapterTransport::Get(client.get())->handshake_session();
    UrmaDelayedAckIO delayed(session ? &session->_socket_io : nullptr);
    if (order == UrmaFirstPacket::BEFORE_LAST_ACK_BYTE) {
        session->SetIOForTest(&delayed);
    }
    std::promise<int> connected;
    auto ready = connected.get_future();
    auto done = [](int error, void* data) {
        static_cast<std::promise<int>*>(data)->set_value(error);
    };
    if (peer == UrmaInteropPeer::LEGACY_CLIENT) {
        client->_app_connect->StartConnect(client.get(), done, &connected);
    } else if (AdapterTransport::StartClientUpgrade(client.get(), done, &connected) != 0) {
        return 37;
    }
    if (ready.wait_for(std::chrono::seconds(3)) != std::future_status::ready ||
        ready.get() != 0) {
        return 38;
    }
    if (peer == UrmaInteropPeer::LEGACY_CLIENT) {
        auto* transport = static_cast<UrmaTransport*>(client->_transport.get());
        if (transport->_urma_state != UrmaTransport::URMA_ON ||
            transport->GetUrmaEp()->_handshake_version != version) {
            return 39;
        }
    } else if (AdapterTransport::Get(client.get())->handshake_phase() !=
                   handshake::ESTABLISHED ||
               AdapterTransport::Get(client.get())->handshake_version() != version) {
        return 40;
    }
    if (order == UrmaFirstPacket::IN_ACK_PARSER && !gate.Wait()) {
        return 41;
    }
    if (order == UrmaFirstPacket::BEFORE_LAST_ACK_BYTE) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (AdapterTransport::Get(accepted.get())->handshake_phase() !=
                   handshake::ACK_WAIT) {
            if (std::chrono::steady_clock::now() > deadline) {
                return 42;
            }
            std::this_thread::yield();
        }
    }
    if (order == UrmaFirstPacket::AFTER_ACK) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (accepted_ep->_state.load(butil::memory_order_acquire) !=
                   urma::UrmaEndpoint::ESTABLISHED) {
            if (std::chrono::steady_clock::now() > deadline) {
                return 42;
            }
            std::this_thread::yield();
        }
    }
    test::EchoService::Stub stub(&channel);
    test::EchoRequest request;
    test::EchoResponse response;
    Controller cntl;
    request.set_message("first URMA RPC after final TCP ACK");
    cntl.request_attachment().append(std::string(512, 'a'));
    stub.Echo(&cntl, &request, &response, DoNothing());
    if (order != UrmaFirstPacket::AFTER_ACK) {
        pollfd cq_event{accepted_ep->_resource->jfce->fd, POLLIN, 0};
        if (poll(&cq_event, 1, 3000) != 1 || !(cq_event.revents & POLLIN) ||
            service.calls.load(std::memory_order_acquire) != 0) {
            return 43;
        }
        // A real receive completion is queued, but no CQ consumer may run
        // while ACK is incomplete or its TCP parser is still on the stack.
        if (accepted_ep->_cq_sid != INVALID_SOCKET_ID) {
            return 44;
        }
        if (order == UrmaFirstPacket::BEFORE_LAST_ACK_BYTE) {
            if (AdapterTransport::Get(accepted.get())->handshake_phase() !=
                    handshake::ACK_WAIT || delayed.held.size() != 1 ||
                delayed.FlushAck() != 0) {
                return 45;
            }
        } else {
            if (!gate.tcp_parser_active) {
                return 46;
            }
            gate.Release();
        }
    }
    Join(cntl.call_id());
    if (cntl.Failed() || response.message() != request.message() ||
        cntl.response_attachment().to_string() != std::string(512, 'a')) {
        return 47;
    }
    for (int i = 0; i < 16; ++i) {
        Controller next;
        test::EchoResponse reply;
        request.set_message(std::to_string(i));
        const std::string attachment(256 + i * 257, static_cast<char>('b' + i));
        next.request_attachment().append(attachment);
        stub.Echo(&next, &request, &reply, nullptr);
        if (next.Failed() || reply.message() != request.message() ||
            next.response_attachment().to_string() != attachment) {
            return 48;
        }
    }
    if (service.calls.load(std::memory_order_acquire) != 17 ||
        gate.timed_out.load(std::memory_order_acquire)) {
        return 49;
    }
    if (peer == UrmaInteropPeer::LEGACY_SERVER &&
        accepted_ep->_handshake_version != version) {
        return 50;
    }
    if (peer != UrmaInteropPeer::LEGACY_SERVER &&
        (AdapterTransport::Get(accepted.get())->handshake_phase() !=
             handshake::ESTABLISHED ||
         AdapterTransport::Get(accepted.get())->handshake_version() != version)) {
        return 51;
    }
    client->SetFailed();
    accepted->SetFailed();
    server.Stop(0);
    server.Join();
    return 0;
}

TEST_F(UrmaMockTest, legacy_client_exchanges_rpc_with_common_server) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    for (int version : {2, 3}) {
        for (bool polling : {false, true}) {
            SCOPED_TRACE(::testing::Message() << "version=" << version
                                              << " polling=" << polling);
            ASSERT_EXIT(_exit(RunUrmaHandshakeInterop(version, polling, false,
                UrmaInteropPeer::LEGACY_CLIENT, UrmaFirstPacket::AFTER_ACK)),
                ::testing::ExitedWithCode(0), "");
        }
    }
}

TEST_F(UrmaMockTest, common_client_exchanges_rpc_with_legacy_server) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    for (int version : {2, 3}) {
        for (bool polling : {false, true}) {
            SCOPED_TRACE(::testing::Message() << "version=" << version
                                              << " polling=" << polling);
            ASSERT_EXIT(_exit(RunUrmaHandshakeInterop(version, polling, false,
                UrmaInteropPeer::LEGACY_SERVER, UrmaFirstPacket::AFTER_ACK)),
                ::testing::ExitedWithCode(0), "");
        }
    }
}

TEST_F(UrmaMockTest, first_urma_packet_waits_for_final_ack_byte) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    for (int version : {2, 3}) {
        for (bool polling : {false, true}) {
            for (bool zerocopy : {false, true}) {
                SCOPED_TRACE(::testing::Message() << "version=" << version
                    << " polling=" << polling << " zerocopy=" << zerocopy);
                ASSERT_EXIT(_exit(RunUrmaHandshakeInterop(version, polling, zerocopy,
                    UrmaInteropPeer::COMMON, UrmaFirstPacket::BEFORE_LAST_ACK_BYTE)),
                    ::testing::ExitedWithCode(0), "");
            }
        }
    }
}

TEST_F(UrmaMockTest, first_urma_packet_after_ack_exchanges_rpc) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    for (int version : {2, 3}) {
        for (bool polling : {false, true}) {
            for (bool zerocopy : {false, true}) {
                SCOPED_TRACE(::testing::Message() << "version=" << version
                    << " polling=" << polling << " zerocopy=" << zerocopy);
                ASSERT_EXIT(_exit(RunUrmaHandshakeInterop(version, polling, zerocopy,
                    UrmaInteropPeer::COMMON, UrmaFirstPacket::AFTER_ACK)),
                    ::testing::ExitedWithCode(0), "");
            }
        }
    }
}

TEST_F(UrmaMockTest, first_urma_packet_waits_for_ack_parser_to_exit) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    for (int version : {2, 3}) {
        for (bool polling : {false, true}) {
            for (bool zerocopy : {false, true}) {
                SCOPED_TRACE(::testing::Message() << "version=" << version
                    << " polling=" << polling << " zerocopy=" << zerocopy);
                ASSERT_EXIT(_exit(RunUrmaHandshakeInterop(version, polling, zerocopy,
                    UrmaInteropPeer::COMMON, UrmaFirstPacket::IN_ACK_PARSER)),
                    ::testing::ExitedWithCode(0), "");
            }
        }
    }
}

static int RunUrmaMockConcurrentRpc(bool zerocopy, bool tcp_server = false) {
    urma::g_skip_urma_init = false;
    gflags::SetCommandLineOption("urma_prepared_jetty_cnt", "0");
    gflags::SetCommandLineOption("urma_buffer_count", "2048");
    gflags::SetCommandLineOption("urma_recv_zerocopy", zerocopy ? "true" : "false");
    urma::GlobalUrmaInitializeOrDie();
    if (!urma::GetUrmaContext()) {
        return 10;
    }
    UrmaStreamEchoService service;
    Server server;
    ServerOptions server_options;
    server_options.socket_mode = tcp_server ? SOCKET_MODE_TCP : SOCKET_MODE_URMA;
    if (server.AddService(&service, SERVER_DOESNT_OWN_SERVICE) != 0 ||
        server.Start(0, &server_options) != 0) {
        return 11;
    }
    Channel channel;
    ChannelOptions options;
    options.socket_mode = SOCKET_MODE_URMA;
    options.protocol = "baidu_std";
    options.timeout_ms = 3000;
    options.max_retry = 0;
    options.connection_type = "single";
    if (channel.Init(server.listen_address(), &options) != 0) {
        return 12;
    }
    std::atomic<int> errors(0);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&, worker]() {
            test::EchoService::Stub stub(&channel);
            for (int round = 0; round < 8; ++round) {
                const size_t depth = 16;
                std::vector<Controller> cntls(depth);
                std::vector<test::EchoRequest> requests(depth);
                std::vector<test::EchoResponse> responses(depth);
                for (size_t i = 0; i < depth; ++i) {
                    requests[i].set_message(std::to_string(worker) + ":" +
                        std::to_string(round) + ":" + std::to_string(i));
                    cntls[i].request_attachment().append(std::string(i * 13, 'x'));
                    stub.Echo(&cntls[i], &requests[i], &responses[i], DoNothing());
                }
                for (size_t i = 0; i < depth; ++i) {
                    Join(cntls[i].call_id());
                    if (cntls[i].Failed() ||
                        responses[i].message() != requests[i].message() ||
                        cntls[i].response_attachment().to_string() !=
                            cntls[i].request_attachment().to_string()) {
                        ++errors;
                    }
                }
                if (errors.load() != 0) {
                    break;
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    SocketUniquePtr main_socket;
    if (Socket::Address(channel._server_id, &main_socket) != 0 ||
        AdapterTransport::Get(main_socket.get())->handshake_phase() !=
            (tcp_server ? handshake::FALLBACK_TCP : handshake::ESTABLISHED)) {
        ++errors;
    }
    server.Stop(0);
    server.Join();
    return errors.load() == 0 ? 0 : 13;
}

TEST_F(UrmaMockTest, concurrent_rpc_preserves_stream_boundaries) {
    // Isolate the process-global pool/IOBuf hooks from tests which intentionally
    // bypass URMA initialization. Threadsafe death tests exec a fresh process.
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockConcurrentRpc(true)),
                ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, concurrent_copy_rpc_preserves_stream_boundaries) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockConcurrentRpc(false)),
                ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, v2_client_falls_back_to_plain_tcp_server) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT({
        alarm(15);
        urma::FLAGS_urma_client_handshake_version = 2;
        _exit(RunUrmaMockConcurrentRpc(false, true));
    }, ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, v3_client_falls_back_to_plain_tcp_server) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT({
        alarm(15);
        urma::FLAGS_urma_client_handshake_version = 3;
        _exit(RunUrmaMockConcurrentRpc(false, true));
    }, ::testing::ExitedWithCode(0), "");
}

static int RunUrmaMockTracedRpc() {
    gflags::SetCommandLineOption("urma_trace_stream", "true");
    return RunUrmaMockConcurrentRpc(false);
}

static int RunUrmaMockOrderedRpc(bool zerocopy) {
    if (gflags::SetCommandLineOption(
            "urma_mock_require_send_order", "true").empty()) {
        return 20;
    }
    // Exercise the real posting paths, including credit-only SEND_IMM WRs,
    // rather than just checking a separately constructed flag value.
    // A rejected ACK can exhaust credits. Bound this isolated subprocess so
    // a regression fails the test instead of hanging the test runner.
    alarm(10);
    const int result = RunUrmaMockConcurrentRpc(zerocopy);
    alarm(0);
    return result;
}

TEST_F(UrmaMockTest, copy_rpc_requests_ordered_data_and_credit_sends) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockOrderedRpc(false)),
                ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, zerocopy_rpc_requests_ordered_data_and_credit_sends) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockOrderedRpc(true)),
                ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, traced_rpc_dumps_bounded_history_on_teardown) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockTracedRpc()),
                ::testing::ExitedWithCode(0), "URMA_TRACE");
}

static int RunUrmaMockCtpLargeRpc(bool zerocopy, bool polling) {
    // Isolated by ASSERT_EXIT: bound failures that stop credit progress.
    alarm(15);
    urma::g_skip_urma_init = false;
    if (gflags::SetCommandLineOption("urma_mock_max_send_size", "4096").empty()) {
        return 20;
    }
    gflags::SetCommandLineOption("urma_buffer_size", "8192");
    gflags::SetCommandLineOption("urma_buffer_count", "2048");
    gflags::SetCommandLineOption("urma_prepared_jetty_cnt", "0");
    gflags::SetCommandLineOption("urma_recv_zerocopy", zerocopy ? "true" : "false");
    gflags::SetCommandLineOption("urma_use_polling", polling ? "true" : "false");
    urma::GlobalUrmaInitializeOrDie();
    // The fix must split sends, not silently shrink receive/IOBuf blocks.
    if (!urma::GetUrmaContext() || urma::GetUrmaRecvBlockSize() <= 4096) {
        return 21;
    }
    UrmaStreamEchoService service;
    Server server;
    ServerOptions server_options;
    server_options.socket_mode = SOCKET_MODE_URMA;
    if (server.AddService(&service, SERVER_DOESNT_OWN_SERVICE) != 0 ||
        server.Start(0, &server_options) != 0) {
        return 22;
    }
    Channel channel;
    ChannelOptions options;
    options.socket_mode = SOCKET_MODE_URMA;
    options.protocol = "baidu_std";
    options.connection_type = "single";
    options.timeout_ms = 3000;
    options.max_retry = 0;
    if (channel.Init(server.listen_address(), &options) != 0) {
        return 23;
    }
    test::EchoService::Stub stub(&channel);
    bool ok = true;
    for (size_t size : {size_t(0), size_t(4095), size_t(4096), size_t(4097),
                        size_t(8192), size_t(1048576), size_t(1048577)}) {
        // A varying pattern catches omissions/reordering hidden by all-'a'
        // attachments. 1 MiB crosses the 125-WR send-credit window repeatedly.
        std::string expected(size, '\0');
        for (size_t i = 0; i < size; ++i) {
            expected[i] = static_cast<char>((i * 37 + i / 4096) % 251);
        }
        Controller cntl;
        test::EchoRequest request;
        test::EchoResponse response;
        request.set_message("ctp-large-" + std::to_string(size));
        cntl.request_attachment().append(expected);
        stub.Echo(&cntl, &request, &response, nullptr);
        if (cntl.Failed() || response.message() != request.message() ||
            cntl.response_attachment().to_string() != expected) {
            ok = false;
            break;
        }
    }
    SocketUniquePtr socket;
    if (Socket::Address(channel._server_id, &socket) != 0 ||
        AdapterTransport::Get(socket.get())->handshake_phase() !=
            handshake::ESTABLISHED) {
        ok = false;
    }
    server.Stop(0);
    server.Join();
    alarm(0);
    return ok ? 0 : 24;
}

TEST_F(UrmaMockTest, ctp_large_copy_rpc_with_default_buffers) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockCtpLargeRpc(false, false)),
                ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, ctp_large_zerocopy_rpc_with_default_buffers) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockCtpLargeRpc(true, false)),
                ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, ctp_large_copy_rpc_with_polling) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockCtpLargeRpc(false, true)),
                ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, ctp_large_zerocopy_rpc_with_polling) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockCtpLargeRpc(true, true)),
                ::testing::ExitedWithCode(0), "");
}

static int RunUrmaMalformedResponseTrace() {
    GlobalInitializeOrDie();
    gflags::SetCommandLineOption("urma_trace_stream", "true");
    SocketOptions options;
    options.user = get_or_new_client_side_messenger();
    SocketId id;
    if (Socket::Create(options, &id) != 0) {
        return 10;
    }
    SocketUniquePtr socket;
    if (Socket::Address(id, &socket) != 0) {
        return 11;
    }
    struct FailSocket {
        Socket* socket;
        ~FailSocket() { socket->SetFailed(); }
    } fail_socket{socket.get()};
    socket->set_preferred_index(PROTOCOL_BAIDU_STD);
    urma::UrmaEndpoint endpoint(socket.get());
    endpoint._state.store(urma::UrmaEndpoint::ESTABLISHED,
                          butil::memory_order_release);
    const std::string bad = "BAD!";
    urma_sge_t sge{reinterpret_cast<uint64_t>(bad.data()), 4, nullptr, nullptr};
    endpoint._stream_trace->Record(urma::UrmaStreamTrace::RX_USE, 0, 1, 0, &sge, 1);
    endpoint._input_processor.read_buf().append(bad);
    endpoint.DispatchReceivedBytes(socket, bad.size());
    return socket->Failed() ? 0 : 12;
}

TEST_F(UrmaMockTest, malformed_response_dumps_exact_bounded_prefix) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMalformedResponseTrace()),
                ::testing::ExitedWithCode(0),
                "reason=RPC parse failed buffered=4 parse_prefix=42414421");
}

static int RunUrmaMockReorderedReceives(bool zerocopy) {
    urma::g_skip_urma_init = false;
    gflags::SetCommandLineOption("urma_prepared_jetty_cnt", "0");
    gflags::SetCommandLineOption("urma_buffer_count", "2048");
    gflags::SetCommandLineOption("urma_recv_zerocopy", zerocopy ? "true" : "false");
    gflags::SetCommandLineOption("urma_zerocopy_min_size", "0");
    urma::GlobalUrmaInitializeOrDie();
    if (!urma::GetUrmaContext()) {
        return 10;
    }
    SocketId id;
    SocketOptions options;
    if (Socket::Create(options, &id) != 0) {
        return 11;
    }
    SocketUniquePtr socket;
    if (Socket::Address(id, &socket) != 0) {
        return 12;
    }
    struct FailSocket {
        Socket* socket;
        ~FailSocket() { socket->SetFailed(); }
    } fail_socket{socket.get()};
    urma::UrmaEndpoint endpoint(socket.get());
    endpoint._rq_size = 4;
    endpoint._remote_window_capacity = 16;
    endpoint._local_window_capacity = 16;
    if (endpoint.AllocateResources() != 0 ||
        endpoint.PostRecv(endpoint._rq_size, zerocopy) != 0) {
        return 13;
    }
    const std::string payloads[] = {"PRPC", std::string("\x08\0", 2)};
    for (int round = 0; round < 32; ++round) {
        const size_t first = endpoint._rq_received;
        const size_t ack_slot = (first + 1) % endpoint._rq_size;
        const size_t second = (first + 2) % endpoint._rq_size;
        memcpy(endpoint._rbuf_data[first], payloads[0].data(), payloads[0].size());
        memcpy(endpoint._rbuf_data[second], payloads[1].data(), payloads[1].size());
        urma_cr_t later{};
        later.status = URMA_CR_SUCCESS;
        later.flag.bs.s_r = 1;
        later.opcode = URMA_CR_OPC_SEND;
        later.user_ctx = second + 1;
        later.completion_len = payloads[1].size();
        // Let the pure credit ACK and the later payload complete before the
        // first payload. Neither may consume the first payload's buffer.
        urma_cr_t ack = later;
        ack.opcode = URMA_CR_OPC_SEND_WITH_IMM;
        ack.user_ctx = ack_slot + 1;
        ack.completion_len = 0;
        ack.imm_data = 1;
        endpoint._remote_rq_window_size.store(15, butil::memory_order_relaxed);
        if (endpoint.HandleCompletion(ack) != 0 ||
            endpoint._remote_rq_window_size.load(butil::memory_order_relaxed) != 15) {
            return 17;
        }
        if (endpoint.HandleCompletion(later) != 0 ||
            !endpoint._input_processor.read_buf().empty() ||
            endpoint._rq_received != first) {
            return 14;
        }
        // A duplicate must be rejected without consuming the queued payload.
        if (endpoint.HandleCompletion(later) != -1 || errno != EPROTO) {
            return 15;
        }
        urma_cr_t earlier = later;
        earlier.user_ctx = first + 1;
        earlier.completion_len = payloads[0].size();
        if (endpoint.HandleCompletion(earlier) != 6 ||
            endpoint._input_processor.read_buf().to_string() !=
                payloads[0] + payloads[1] ||
            endpoint._remote_rq_window_size.load(butil::memory_order_relaxed) != 16) {
            return 16;
        }
        endpoint._input_processor.read_buf().clear();
        // This test does not connect a peer; suppress credit-ACK posting.
        endpoint._new_rq_wrs.store(0, butil::memory_order_relaxed);
    }
    return 0;
}

TEST_F(UrmaMockTest, reordered_receives_preserve_copy_stream) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockReorderedReceives(false)),
                ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, reordered_receives_preserve_zerocopy_stream) {
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(_exit(RunUrmaMockReorderedReceives(true)),
                ::testing::ExitedWithCode(0), "");
}

TEST_F(UrmaMockTest, init_and_enumerate_device) {
    urma_init_attr_t init_attr{};
    EXPECT_EQ(URMA_EEXIST, urma_init(&init_attr));

    int num_devices = 0;
    urma_device_t** devices = urma_get_device_list(&num_devices);
    ASSERT_NE(nullptr, devices);
    ASSERT_GE(num_devices, 1);
    EXPECT_STREQ("mock_urma_device", devices[0]->name);
    urma_free_device_list(devices);
}

TEST_F(UrmaMockTest, create_context_and_query_device) {
    int num_devices = 0;
    urma_device_t** devices = urma_get_device_list(&num_devices);
    ASSERT_NE(nullptr, devices);
    ASSERT_GE(num_devices, 1);

    uint32_t eid_cnt = 0;
    urma_eid_info_t* eids = urma_get_eid_list(devices[0], &eid_cnt);
    ASSERT_NE(nullptr, eids);
    ASSERT_GE(eid_cnt, 1u);
    urma_free_eid_list(eids);

    urma_context_t* ctx = urma_create_context(devices[0], 0);
    ASSERT_NE(nullptr, ctx);

    urma_device_attr_t attr{};
    ASSERT_EQ(URMA_SUCCESS, urma_query_device(devices[0], &attr));
    EXPECT_GE(attr.dev_cap.max_jfc, 1u);
    EXPECT_GE(attr.dev_cap.max_jetty, 1u);

    EXPECT_EQ(URMA_SUCCESS, urma_delete_context(ctx));
    urma_free_device_list(devices);
}

TEST_F(UrmaMockTest, rejects_send_without_target_jetty) {
    // A SEND without a target jetty is invalid. Keeping the mock strict here
    // prevents tests from relying on input that a real provider cannot post.
    int num_devices = 0;
    urma_device_t** devices = urma_get_device_list(&num_devices);
    ASSERT_NE(nullptr, devices);
    ASSERT_GE(num_devices, 1);
    uint32_t eid_cnt = 0;
    urma_eid_info_t* eids = urma_get_eid_list(devices[0], &eid_cnt);
    urma_free_eid_list(eids);
    urma_context_t* ctx = urma_create_context(devices[0], 0);
    ASSERT_NE(nullptr, ctx);

    urma_jfce_t* jfce = urma_create_jfce(ctx);
    ASSERT_NE(nullptr, jfce);
    urma_jfc_cfg_t jfc_cfg{};
    jfc_cfg.depth = 16;
    jfc_cfg.jfce = jfce;
    urma_jfc_t* jfc = urma_create_jfc(ctx, &jfc_cfg);
    ASSERT_NE(nullptr, jfc);

    urma_jfr_cfg_t jfr_cfg{};
    jfr_cfg.depth = 16;
    jfr_cfg.trans_mode = URMA_TM_RM;
    jfr_cfg.max_sge = 1;
    jfr_cfg.min_rnr_timer = URMA_TYPICAL_MIN_RNR_TIMER;
    jfr_cfg.jfc = jfc;
    urma_jfr_t* jfr = urma_create_jfr(ctx, &jfr_cfg);
    ASSERT_NE(nullptr, jfr);

    urma_jetty_cfg_t jetty_cfg{};
    jetty_cfg.flag.bs.share_jfr = 1;
    jetty_cfg.jfs_cfg.depth = 16;
    jetty_cfg.jfs_cfg.trans_mode = URMA_TM_RM;
    jetty_cfg.jfs_cfg.priority = URMA_MAX_PRIORITY;
    jetty_cfg.jfs_cfg.max_sge = 1;
    jetty_cfg.jfs_cfg.rnr_retry = URMA_TYPICAL_RNR_RETRY;
    jetty_cfg.jfs_cfg.err_timeout = URMA_TYPICAL_ERR_TIMEOUT;
    jetty_cfg.jfs_cfg.jfc = jfc;
    jetty_cfg.shared.jfr = jfr;
    jetty_cfg.shared.jfc = jfc;
    urma_jetty_t* jetty = urma_create_jetty(ctx, &jetty_cfg);
    ASSERT_NE(nullptr, jetty);

    urma_jfs_wr_t wr{};
    memset(&wr, 0, sizeof(wr));
    wr.opcode = URMA_OPC_SEND;
    wr.flag.bs.complete_enable = 1;
    wr.user_ctx = 0xABCD;
    wr.next = nullptr;
    urma_jfs_wr_t* bad = nullptr;
    EXPECT_EQ(URMA_EINVAL, urma_post_jetty_send_wr(jetty, &wr, &bad));
    EXPECT_EQ(&wr, bad);

    urma_cr_t crs[4];
    EXPECT_EQ(0, urma_poll_jfc(jfc, 4, crs));

    urma_delete_jetty(jetty);
    urma_delete_jfr(jfr);
    urma_delete_jfc(jfc);
    urma_delete_jfce(jfce);
    urma_delete_context(ctx);
    urma_free_device_list(devices);
}

TEST_F(UrmaMockTest,
       paired_send_is_bidirectional_and_separates_immediate_credit) {
    int num_devices = 0;
    urma_device_t** devices = urma_get_device_list(&num_devices);
    ASSERT_NE(nullptr, devices);
    ASSERT_GT(num_devices, 0);
    urma_context_t* ctx = urma_create_context(devices[0], 0);
    ASSERT_NE(nullptr, ctx);

    urma_jfce_t* sender_jfce = urma_create_jfce(ctx);
    urma_jfce_t* receiver_jfce = urma_create_jfce(ctx);
    ASSERT_NE(nullptr, sender_jfce);
    ASSERT_NE(nullptr, receiver_jfce);
    urma_jfc_cfg_t sender_jfc_cfg{};
    sender_jfc_cfg.depth = 8;
    sender_jfc_cfg.jfce = sender_jfce;
    urma_jfc_t* sender_jfc = urma_create_jfc(ctx, &sender_jfc_cfg);
    ASSERT_NE(nullptr, sender_jfc);
    urma_jfc_cfg_t receiver_jfc_cfg{};
    receiver_jfc_cfg.depth = 8;
    receiver_jfc_cfg.jfce = receiver_jfce;
    urma_jfc_t* receiver_jfc = urma_create_jfc(ctx, &receiver_jfc_cfg);
    ASSERT_NE(nullptr, receiver_jfc);

    urma_jfr_cfg_t sender_jfr_cfg{};
    sender_jfr_cfg.depth = 4;
    sender_jfr_cfg.trans_mode = URMA_TM_RM;
    sender_jfr_cfg.max_sge = 1;
    sender_jfr_cfg.jfc = sender_jfc;
    urma_jfr_t* sender_jfr = urma_create_jfr(ctx, &sender_jfr_cfg);
    ASSERT_NE(nullptr, sender_jfr);
    urma_jfr_cfg_t receiver_jfr_cfg = sender_jfr_cfg;
    receiver_jfr_cfg.jfc = receiver_jfc;
    urma_jfr_t* receiver_jfr = urma_create_jfr(ctx, &receiver_jfr_cfg);
    ASSERT_NE(nullptr, receiver_jfr);

    auto create_jetty = [&](urma_jfc_t* jfc, urma_jfr_t* jfr) {
        urma_jetty_cfg_t cfg{};
        cfg.flag.bs.share_jfr = 1;
        cfg.jfs_cfg.depth = 4;
        cfg.jfs_cfg.trans_mode = URMA_TM_RM;
        cfg.jfs_cfg.max_sge = 1;
        cfg.jfs_cfg.jfc = jfc;
        cfg.shared.jfr = jfr;
        cfg.shared.jfc = jfc;
        return urma_create_jetty(ctx, &cfg);
    };
    urma_jetty_t* sender = create_jetty(sender_jfc, sender_jfr);
    urma_jetty_t* receiver = create_jetty(receiver_jfc, receiver_jfr);
    ASSERT_NE(nullptr, sender);
    ASSERT_NE(nullptr, receiver);

    urma_rjetty_t remote{};
    remote.jetty_id = receiver->jetty_id;
    remote.trans_mode = URMA_TM_RM;
    remote.type = URMA_JETTY;
    remote.tp_type = URMA_CTP;
    urma_token_t token{};
    urma_target_jetty_t* target =
        urma_import_jetty(ctx, &remote, &token);
    ASSERT_NE(nullptr, target);

    char recv_buf[64]{};
    urma_sge_t recv_sge{
        reinterpret_cast<uint64_t>(recv_buf), sizeof(recv_buf), nullptr,
        nullptr};
    urma_sg_t recv_sg{&recv_sge, 1};
    urma_jfr_wr_t recv_wr{recv_sg, 99, nullptr};
    char credit_recv_buf[1]{};
    urma_sge_t credit_recv_sge{
        reinterpret_cast<uint64_t>(credit_recv_buf),
        sizeof(credit_recv_buf), nullptr, nullptr};
    urma_sg_t credit_recv_sg{&credit_recv_sge, 1};
    urma_jfr_wr_t credit_recv_wr{credit_recv_sg, 100, nullptr};
    recv_wr.next = &credit_recv_wr;
    urma_jfr_wr_t* bad_recv = nullptr;
    ASSERT_EQ(URMA_SUCCESS,
              urma_post_jfr_wr(receiver_jfr, &recv_wr, &bad_recv));

    const char payload[] = "urma-payload";
    urma_sge_t send_sge{
        reinterpret_cast<uint64_t>(payload), sizeof(payload), nullptr,
        nullptr};
    urma_sg_t send_sg{&send_sge, 1};
    urma_jfs_wr_t send_wr{};
    send_wr.opcode = URMA_OPC_SEND;
    send_wr.flag.bs.complete_enable = 1;
    send_wr.tjetty = target;
    send_wr.user_ctx = 7;
    send_wr.send.src = send_sg;
    urma_jfs_wr_t credit_wr{};
    credit_wr.opcode = URMA_OPC_SEND_IMM;
    credit_wr.flag.bs.complete_enable = 1;
    credit_wr.tjetty = target;
    credit_wr.user_ctx = 8;
    credit_wr.send.imm_data = 13;
    send_wr.next = &credit_wr;
    urma_jfs_wr_t* bad_send = nullptr;
    ASSERT_EQ(URMA_SUCCESS,
              urma_post_jetty_send_wr(sender, &send_wr, &bad_send));

    urma_cr_t sender_cr[2]{};
    ASSERT_EQ(2, urma_poll_jfc(sender_jfc, 2, sender_cr));
    EXPECT_EQ(0, sender_cr[0].flag.bs.s_r);
    EXPECT_EQ(7u, sender_cr[0].user_ctx);
    EXPECT_EQ(0, sender_cr[1].flag.bs.s_r);
    EXPECT_EQ(8u, sender_cr[1].user_ctx);

    urma_cr_t receiver_cr[2]{};
    ASSERT_EQ(2, urma_poll_jfc(receiver_jfc, 2, receiver_cr));
    EXPECT_EQ(1, receiver_cr[0].flag.bs.s_r);
    EXPECT_EQ(URMA_CR_OPC_SEND, receiver_cr[0].opcode);
    EXPECT_EQ(0u, receiver_cr[0].imm_data);
    EXPECT_EQ(sizeof(payload), receiver_cr[0].completion_len);
    EXPECT_EQ(0, memcmp(payload, recv_buf, sizeof(payload)));
    EXPECT_EQ(1, receiver_cr[1].flag.bs.s_r);
    EXPECT_EQ(URMA_CR_OPC_SEND_WITH_IMM, receiver_cr[1].opcode);
    EXPECT_EQ(13u, receiver_cr[1].imm_data);
    EXPECT_EQ(0u, receiver_cr[1].completion_len);

    // Exercise the response direction as well. Production posts all receive
    // WRs through the shared JFR.
    urma_rjetty_t sender_remote = remote;
    sender_remote.jetty_id = sender->jetty_id;
    urma_target_jetty_t* sender_target =
        urma_import_jetty(ctx, &sender_remote, &token);
    ASSERT_NE(nullptr, sender_target);
    char response_buf[64]{};
    urma_sge_t response_recv_sge{
        reinterpret_cast<uint64_t>(response_buf), sizeof(response_buf),
        nullptr, nullptr};
    urma_sg_t response_recv_sg{&response_recv_sge, 1};
    urma_jfr_wr_t response_recv_wr{response_recv_sg, 101, nullptr};
    ASSERT_EQ(URMA_SUCCESS,
              urma_post_jfr_wr(sender_jfr, &response_recv_wr, &bad_recv));

    const char response[] = "urma-response";
    urma_sge_t response_send_sge{
        reinterpret_cast<uint64_t>(response), sizeof(response), nullptr,
        nullptr};
    urma_sg_t response_send_sg{&response_send_sge, 1};
    urma_jfs_wr_t response_send_wr{};
    response_send_wr.opcode = URMA_OPC_SEND;
    response_send_wr.flag.bs.complete_enable = 1;
    response_send_wr.tjetty = sender_target;
    response_send_wr.user_ctx = 9;
    response_send_wr.send.src = response_send_sg;
    ASSERT_EQ(URMA_SUCCESS,
              urma_post_jetty_send_wr(receiver, &response_send_wr, &bad_send));

    urma_cr_t response_send_cr{};
    ASSERT_EQ(1, urma_poll_jfc(receiver_jfc, 1, &response_send_cr));
    EXPECT_EQ(0, response_send_cr.flag.bs.s_r);
    EXPECT_EQ(9u, response_send_cr.user_ctx);

    urma_cr_t response_recv_cr{};
    ASSERT_EQ(1, urma_poll_jfc(sender_jfc, 1, &response_recv_cr));
    EXPECT_EQ(1, response_recv_cr.flag.bs.s_r);
    EXPECT_EQ(URMA_CR_OPC_SEND, response_recv_cr.opcode);
    EXPECT_EQ(sizeof(response), response_recv_cr.completion_len);
    EXPECT_EQ(0, memcmp(response, response_buf, sizeof(response)));

    urma_unimport_jetty(sender_target);
    urma_unimport_jetty(target);
    urma_delete_jetty(receiver);
    urma_delete_jetty(sender);
    urma_delete_jfr(receiver_jfr);
    urma_delete_jfr(sender_jfr);
    urma_delete_jfc(receiver_jfc);
    urma_delete_jfc(sender_jfc);
    urma_delete_jfce(receiver_jfce);
    urma_delete_jfce(sender_jfce);
    urma_delete_context(ctx);
    urma_free_device_list(devices);
}

#else  // BRPC_WITH_URMA

// When URMA is not compiled in, the test file is a no-op so the build stays
// clean. The brpc_urma_unittest target still links (against brpc-shared which
// provides the empty stubs).

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
