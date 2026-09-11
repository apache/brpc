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

#include <gtest/gtest.h>

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <string>

#include "butil/fd_guard.h"
#include "butil/sys_byteorder.h"
#include "brpc/adapter_transport.h"
#include "brpc/policy/transport_handshake_protocol.h"
#include "brpc/socket.h"
#include "brpc/transport_handshake.h"

namespace brpc {
namespace handshake {

class MemoryHandshakeIO : public HandshakeIO {
public:
    explicit MemoryHandshakeIO(const std::string& input = std::string())
        : _input(input), _offset(0) {}

    int ReadExact(void* data, size_t len) override {
        if (_input.size() - _offset < len) {
            errno = EIO;
            return -1;
        }
        memcpy(data, _input.data() + _offset, len);
        _offset += len;
        return 0;
    }

    int WriteAll(const void* data, size_t len) override {
        _output.append(static_cast<const char*>(data), len);
        return 0;
    }

    int PushBack(const void* data, size_t len) override {
        _pushed_back.append(static_cast<const char*>(data), len);
        return 0;
    }

    const std::string& output() const { return _output; }
    const std::string& pushed_back() const { return _pushed_back; }

private:
    std::string _input;
    size_t _offset;
    std::string _output;
    std::string _pushed_back;
};

static FrameSpec FixedSpec(const char* magic, size_t magic_len,
                           size_t total_len) {
    return FrameSpec(magic, magic_len, total_len, total_len,
                     FrameSpec::FIXED);
}

static HandshakeCodec MakeTestCodec(std::string* calls = NULL) {
    HandshakeCodec codec{};
    codec.protocol_version = 7;
    codec.hello_frame = FixedSpec("HS", 2, 4);
    codec.ack_frame = FixedSpec(NULL, 0, 1);
    codec.build_hello = [calls](bool enabled, std::string* payload) {
        if (calls) *calls += "build ";
        *payload = enabled ? "LO" : "NO";
        return STEP_OK;
    };
    codec.parse_hello = [calls](const std::string& payload) {
        if (calls) *calls += "parse ";
        return payload == "OK" ? STEP_OK : STEP_FALLBACK;
    };
    codec.build_ack = [calls](bool enabled, std::string* payload) {
        if (calls) *calls += enabled ? "ack1 " : "ack0 ";
        *payload = enabled ? "1" : "0";
        return STEP_OK;
    };
    codec.parse_ack = [calls](const std::string& payload, bool* enabled) {
        if (calls) *calls += "parse_ack ";
        *enabled = payload == "1";
        return payload == "1" || payload == "0" ? STEP_OK : STEP_ERROR;
    };
    return codec;
}

static std::string MakeUBShmHello() {
    std::string frame(64, '\0');
    memcpy(&frame[0], "UB", 2);
    const uint16_t msg_len = butil::HostToNet16(64);
    const uint16_t hello_ver = butil::HostToNet16(2);
    const uint16_t impl_ver = butil::HostToNet16(1);
    memcpy(&frame[2], &msg_len, sizeof(msg_len));
    memcpy(&frame[4], &hello_ver, sizeof(hello_ver));
    memcpy(&frame[6], &impl_ver, sizeof(impl_ver));
    return frame;
}

TEST(HandshakeFrameTest, supports_two_byte_fixed_magic) {
    const FrameSpec spec = FixedSpec("UB", 2, 6);
    std::string frame;
    ASSERT_EQ(FRAME_OK, FrameCodec::Encode(spec, "data", &frame));
    ASSERT_EQ("UBdata", frame);
}

TEST(HandshakeFrameTest, encodes_u16_total_length) {
    const FrameSpec spec(
        "RDMA", 4, 8, 64, FrameSpec::U16_TOTAL_LENGTH);
    std::string frame;
    ASSERT_EQ(FRAME_OK, FrameCodec::Encode(spec, "xy", &frame));
    ASSERT_EQ(8UL, frame.size());
    uint16_t total_be = 0;
    memcpy(&total_be, frame.data() + 4, sizeof(total_be));
    ASSERT_EQ(8, butil::NetToHost16(total_be));
    ASSERT_EQ("xy", frame.substr(6));
}

TEST(HandshakeFrameTest, encodes_u32_body_length) {
    const FrameSpec spec(
        "RDM3", 4, 9, 32, FrameSpec::U32_BODY_LENGTH);
    std::string frame;
    ASSERT_EQ(FRAME_OK, FrameCodec::Encode(spec, "abc", &frame));
    uint32_t body_be = 0;
    memcpy(&body_be, frame.data() + 4, sizeof(body_be));
    ASSERT_EQ(3U, butil::NetToHost32(body_be));
    ASSERT_EQ("abc", frame.substr(8));
}

TEST(HandshakeFrameTest, buffered_partial_frame_is_not_consumed) {
    const FrameSpec spec(
        "RDMA", 4, 8, 64, FrameSpec::U16_TOTAL_LENGTH);
    std::string frame;
    ASSERT_EQ(FRAME_OK, FrameCodec::Encode(spec, "payload", &frame));
    butil::IOBuf source;
    source.append(frame.data(), frame.size() - 1);
    IOBufHandshakeInput input(&source);
    std::string payload;
    ASSERT_EQ(FRAME_NEED_MORE,
              FrameCodec::ParseBufferedFrame(&input, spec, &payload));
    ASSERT_EQ(frame.size() - 1, source.size());

    source.append(frame.data() + frame.size() - 1, 1);
    ASSERT_EQ(FRAME_OK,
              FrameCodec::ParseBufferedFrame(&input, spec, &payload));
    ASSERT_EQ("payload", payload);
    ASSERT_TRUE(source.empty());
}

TEST(HandshakeFrameTest, buffered_magic_mismatch_is_not_consumed) {
    const FrameSpec spec = FixedSpec("UB", 2, 6);
    butil::IOBuf source;
    source.append("XXdata", 6);
    IOBufHandshakeInput input(&source);
    std::string payload;
    ASSERT_EQ(FRAME_NOT_MINE,
              FrameCodec::ParseBufferedFrame(&input, spec, &payload));
    ASSERT_EQ(6UL, source.size());
}

TEST(HandshakeFrameTest, blocking_magic_mismatch_is_pushed_back) {
    MemoryHandshakeIO io("XX");
    const FrameSpec spec = FixedSpec("UB", 2, 6);
    std::string payload;
    ASSERT_EQ(FRAME_NOT_MINE,
              FrameCodec::ReadFrame(&io, spec, true, &payload));
    ASSERT_EQ("XX", io.pushed_back());
}

TEST(HandshakeFrameTest, rejects_lengths_outside_bounds) {
    const FrameSpec spec(
        "RDMA", 4, 8, 16, FrameSpec::U16_TOTAL_LENGTH);
    std::string header("RDMA", 4);
    const uint16_t total_be = butil::HostToNet16(17);
    header.append(reinterpret_cast<const char*>(&total_be), sizeof(total_be));
    butil::IOBuf source;
    source.append(header);
    IOBufHandshakeInput input(&source);
    std::string payload;
    ASSERT_EQ(FRAME_PROTOCOL_ERROR,
              FrameCodec::ParseBufferedFrame(&input, spec, &payload));
    ASSERT_EQ(header.size(), source.size());
}

TEST(TransportHandshakeTest, publish_fallback_after_tcp_state) {
    HandshakeSession session;
    int tcp_active = 0;
    session.SetPhase(NEGOTIATING);
    session.PublishFallback([&tcp_active]() { tcp_active = 1; });
    ASSERT_EQ(1, tcp_active);
    ASSERT_EQ(FALLBACK_TCP, session.phase());
}

TEST(TransportHandshakeTest, client_runs_codec_and_resource_sequence) {
    MemoryHandshakeIO io("HSOK");
    HandshakeSession session;
    session.SetIOForTest(&io);
    std::string calls;

    ClientHandshakeCallbacks callbacks{};
    callbacks.codec = MakeTestCodec(&calls);
    callbacks.transport.prepare_resources = [&]() {
        calls += "prepare ";
        return STEP_OK;
    };
    callbacks.transport.negotiate_resources = [&]() {
        calls += "negotiate ";
        return STEP_OK;
    };
    callbacks.transport.set_high_speed_active = [&]() { calls += "activate"; };
    callbacks.transport.set_tcp_active = []() {};
    callbacks.transport.on_failed = []() {};

    ASSERT_EQ(STEP_OK, session.RunClient(callbacks));
    ASSERT_EQ("prepare build parse negotiate ack1 activate", calls);
    ASSERT_EQ("HSLO1", io.output());
    ASSERT_EQ(ESTABLISHED, session.phase());
    ASSERT_EQ(7, session.protocol_version());
}

TEST(TransportHandshakeTest, client_resource_failure_falls_back_before_io) {
    MemoryHandshakeIO io;
    HandshakeSession session;
    session.SetIOForTest(&io);
    bool tcp_active = false;

    ClientHandshakeCallbacks callbacks{};
    callbacks.codec = MakeTestCodec();
    callbacks.transport.prepare_resources = []() { return STEP_FALLBACK; };
    callbacks.transport.negotiate_resources = []() { return STEP_ERROR; };
    callbacks.transport.set_high_speed_active = []() {};
    callbacks.transport.set_tcp_active = [&]() { tcp_active = true; };
    callbacks.transport.on_failed = []() {};

    ASSERT_EQ(STEP_FALLBACK, session.RunClient(callbacks));
    ASSERT_TRUE(tcp_active);
    ASSERT_TRUE(io.output().empty());
    ASSERT_EQ(FALLBACK_TCP, session.phase());
}

TEST(TransportHandshakeTest, server_resumes_at_buffered_ack) {
    MemoryHandshakeIO io;
    HandshakeSession session;
    session.SetIOForTest(&io);
    butil::IOBuf source;
    source.append("HSOK", 4);
    IOBufHandshakeInput input(&source);
    std::string calls;

    ServerHandshakeCallbacks callbacks{};
    callbacks.fallback_on_not_mine = false;
    callbacks.codecs.push_back(MakeTestCodec(&calls));
    callbacks.input = &input;
    callbacks.transport.prepare_resources = [&]() {
        calls += "prepare ";
        return STEP_OK;
    };
    callbacks.transport.negotiate_resources = [&]() {
        calls += "negotiate ";
        return STEP_OK;
    };
    callbacks.validate_established = [&]() {
        calls += "validate ";
        return STEP_OK;
    };
    callbacks.transport.set_high_speed_active = [&]() { calls += "activate"; };
    callbacks.transport.set_tcp_active = []() {};
    callbacks.transport.on_failed = []() {};

    ASSERT_EQ(STEP_NEED_MORE, session.RunServer(callbacks));
    ASSERT_EQ(ACK_WAIT, session.phase());
    ASSERT_EQ("HSLO", io.output());
    ASSERT_TRUE(source.empty());

    source.append("1", 1);
    ASSERT_EQ(STEP_OK, session.RunServer(callbacks));
    ASSERT_EQ("parse prepare negotiate build parse_ack validate activate",
              calls);
    ASSERT_EQ(ESTABLISHED, session.phase());
}

TEST(TransportHandshakeTest, server_resource_failure_falls_back_after_ack) {
    MemoryHandshakeIO io;
    HandshakeSession session;
    session.SetIOForTest(&io);
    butil::IOBuf source;
    source.append("HSOK0", 5);
    IOBufHandshakeInput input(&source);
    bool tcp_active = false;

    ServerHandshakeCallbacks callbacks{};
    callbacks.fallback_on_not_mine = false;
    callbacks.codecs.push_back(MakeTestCodec());
    callbacks.input = &input;
    callbacks.transport.prepare_resources = []() { return STEP_FALLBACK; };
    callbacks.transport.negotiate_resources = []() { return STEP_ERROR; };
    callbacks.transport.set_high_speed_active = []() {};
    callbacks.transport.set_tcp_active = [&]() { tcp_active = true; };
    callbacks.transport.on_failed = []() {};

    ASSERT_EQ(STEP_FALLBACK, session.RunServer(callbacks));
    ASSERT_EQ("HSNO", io.output());
    ASSERT_TRUE(source.empty());
    ASSERT_TRUE(tcp_active);
    ASSERT_EQ(FALLBACK_TCP, session.phase());
}

TEST(TransportHandshakeTest, server_falls_back_without_consuming_other_magic) {
    MemoryHandshakeIO io;
    HandshakeSession session;
    session.SetIOForTest(&io);
    butil::IOBuf source;
    source.append("XXok", 4);
    IOBufHandshakeInput input(&source);
    bool tcp_active = false;

    ServerHandshakeCallbacks callbacks{};
    callbacks.fallback_on_not_mine = true;
    callbacks.codecs.push_back(MakeTestCodec());
    callbacks.input = &input;
    callbacks.transport.prepare_resources = []() { return STEP_ERROR; };
    callbacks.transport.negotiate_resources = []() { return STEP_ERROR; };
    callbacks.transport.set_high_speed_active = []() {};
    callbacks.transport.set_tcp_active = [&]() { tcp_active = true; };
    callbacks.transport.on_failed = []() {};

    ASSERT_EQ(STEP_FALLBACK, session.RunServer(callbacks));
    ASSERT_TRUE(tcp_active);
    ASSERT_EQ(4UL, source.size());
    ASSERT_EQ(FALLBACK_TCP, session.phase());

    ASSERT_EQ(STEP_NOT_MINE, session.RunServer(callbacks));
    ASSERT_EQ(4UL, source.size());
    ASSERT_EQ(FALLBACK_TCP, session.phase());
}

TEST(TransportHandshakeTest, server_enters_hello_phase_after_magic_matches) {
    MemoryHandshakeIO io;
    HandshakeSession session;
    session.SetIOForTest(&io);
    butil::IOBuf source;
    IOBufHandshakeInput input(&source);

    ServerHandshakeCallbacks callbacks{};
    callbacks.fallback_on_not_mine = false;
    callbacks.codecs.push_back(MakeTestCodec());
    callbacks.input = &input;
    callbacks.transport.prepare_resources = []() { return STEP_OK; };
    callbacks.transport.negotiate_resources = []() { return STEP_OK; };
    callbacks.transport.set_high_speed_active = []() {};
    callbacks.transport.set_tcp_active = []() {};
    callbacks.transport.on_failed = []() {};

    source.append("H", 1);
    ASSERT_EQ(STEP_NEED_MORE, session.RunServer(callbacks));
    ASSERT_EQ(UNINITIALIZED, session.phase());

    source.append("S", 1);
    ASSERT_EQ(STEP_NEED_MORE, session.RunServer(callbacks));
    ASSERT_EQ(HELLO_WAIT, session.phase());
    ASSERT_EQ(7, session.protocol_version());
    ASSERT_EQ(2UL, source.size());
}

TEST(TransportHandshakeTest,
     plain_tcp_server_incrementally_rejects_ubshm_upgrade) {
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    butil::fd_guard peer_fd(fds[1]);
    SocketOptions options;
    options.fd = fds[0];
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));

    const std::string hello = MakeUBShmHello();
    butil::IOBuf source;
    source.append(hello.data(), 1);
    ParseResult result = policy::ParseTransportHandshake(
        &source, socket.get(), false, NULL);
    ASSERT_FALSE(result.is_ok());
    ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA, result.error());
    ASSERT_EQ(1UL, source.size());
    ASSERT_EQ(UNINITIALIZED,
              AdapterTransport::Get(socket.get())->handshake_phase());

    source.append(hello.data() + 1, hello.size() - 1);
    result = policy::ParseTransportHandshake(
        &source, socket.get(), false, NULL);
    ASSERT_FALSE(result.is_ok());
    ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA, result.error());
    ASSERT_TRUE(source.empty());
    ASSERT_NE(nullptr, socket->parsing_context());

    char reply[64];
    ASSERT_EQ(sizeof(reply), read(peer_fd, reply, sizeof(reply)));
    EXPECT_EQ(0, memcmp(reply, "UB", 2));
    uint16_t reply_len = 0;
    memcpy(&reply_len, reply + 2, sizeof(reply_len));
    EXPECT_EQ(64, butil::NetToHost16(reply_len));
    EXPECT_EQ(0, reply[4]);
    EXPECT_EQ(0, reply[5]);
    EXPECT_EQ(0, reply[6]);
    EXPECT_EQ(0, reply[7]);

    const uint32_t ack = 0;
    source.append(&ack, sizeof(ack));
    result = policy::ParseTransportHandshake(
        &source, socket.get(), false, NULL);
    ASSERT_FALSE(result.is_ok());
    ASSERT_EQ(PARSE_ERROR_TRY_OTHERS, result.error());
    ASSERT_TRUE(source.empty());
    ASSERT_EQ(FALLBACK_TCP,
              AdapterTransport::Get(socket.get())->handshake_phase());
    ASSERT_EQ(nullptr, socket->parsing_context());
    socket->SetFailed();
}

TEST(TransportHandshakeTest, plain_tcp_server_consumes_coalesced_ubshm_ack) {
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    butil::fd_guard peer_fd(fds[1]);
    SocketOptions options;
    options.fd = fds[0];
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));

    butil::IOBuf source;
    source.append(MakeUBShmHello());
    const uint32_t ack = 0;
    source.append(&ack, sizeof(ack));
    const ParseResult result = policy::ParseTransportHandshake(
        &source, socket.get(), false, NULL);
    ASSERT_FALSE(result.is_ok());
    ASSERT_EQ(PARSE_ERROR_TRY_OTHERS, result.error());
    ASSERT_TRUE(source.empty());
    ASSERT_EQ(FALLBACK_TCP,
              AdapterTransport::Get(socket.get())->handshake_phase());
    ASSERT_EQ(nullptr, socket->parsing_context());
    socket->SetFailed();
}

}  // namespace handshake
}  // namespace brpc
