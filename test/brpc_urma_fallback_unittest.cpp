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
#include <gtest/gtest.h>

#include "butil/fd_guard.h"
#include "butil/sys_byteorder.h"
#include "brpc/adapter_transport.h"
#include "brpc/policy/transport_handshake_protocol.h"
#include "brpc/socket.h"
#include "brpc/urma/urma_handshake.pb.h"

using namespace brpc;

// Keep these tests runnable without URMA SDK headers or a device.
class UrmaTcpFallbackTest : public ::testing::TestWithParam<int> {
protected:
    void SetUp() override {
        int fds[2];
        ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
        peer.reset(fds[1]);
        SocketOptions options;
        options.fd = fds[0];
        SocketId id;
        ASSERT_EQ(0, Socket::Create(options, &id));
        ASSERT_EQ(0, Socket::Address(id, &socket));
    }
    void TearDown() override { socket->SetFailed(); }

    std::string Hello() const {
        // A known legacy v2 packet, including its original version fields.
        if (GetParam() == 2) {
            std::string frame(86, '\0');
            frame.replace(0, 4, "URMA");
            const uint16_t fields[] = {butil::HostToNet16(86),
                                       butil::HostToNet16(2),
                                       butil::HostToNet16(1)};
            memcpy(&frame[4], fields, sizeof(fields));
            return frame;
        }
        urma::UrmaHello hello;
        hello.set_buffer_size(8192);
        hello.set_recv_buffer_cnt(127);
        hello.set_jetty_id(7);
        hello.set_eid(std::string(16, '\0'));
        hello.set_uasid(0);
        hello.set_tp_type(1);
        hello.set_seg_eid(std::string(16, '\0'));
        hello.set_seg_uasid(0);
        hello.set_seg_va(4096);
        hello.set_seg_len(8192);
        hello.set_seg_token_id(0);
        std::string payload;
        EXPECT_TRUE(hello.SerializeToString(&payload));
        const uint32_t size = butil::HostToNet32(payload.size());
        std::string frame("URM3", 4);
        frame.append(reinterpret_cast<const char*>(&size), sizeof(size));
        return frame + payload;
    }

    void CheckDisabledReply() {
        char reply[256];
        const ssize_t nr = recv(peer, reply, sizeof(reply), MSG_DONTWAIT);
        ASSERT_GT(nr, 8);
        if (GetParam() == 2) {
            ASSERT_EQ(86, nr);
            EXPECT_EQ(0, memcmp(reply, "URMA", 4));
            EXPECT_EQ(std::string(80, '\0'), std::string(reply + 6, nr - 6));
        } else {
            EXPECT_EQ(0, memcmp(reply, "URM3", 4));
            urma::UrmaHello hello;
            ASSERT_TRUE(hello.ParseFromArray(reply + 8, nr - 8));
            EXPECT_EQ(0u, hello.jetty_id());
            EXPECT_EQ(0u, hello.buffer_size());
            EXPECT_EQ(16u, hello.eid().size());
        }
    }

    SocketUniquePtr socket;
    butil::fd_guard peer;
};

TEST_P(UrmaTcpFallbackTest, fragmented_legacy_hello_needs_no_ack) {
    const std::string hello = Hello();
    butil::IOBuf source;
    source.append(hello.data(), 3);
    EXPECT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA,
              policy::ParseTransportHandshake(&source, socket.get(), false,
                                               nullptr).error());
    EXPECT_EQ(3u, source.size());
    source.append(hello.data() + 3, hello.size() - 3);
    EXPECT_EQ(PARSE_ERROR_TRY_OTHERS,
              policy::ParseTransportHandshake(&source, socket.get(), false,
                                               nullptr).error());
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(handshake::FALLBACK_TCP,
              AdapterTransport::Get(socket.get())->handshake_phase());
    EXPECT_EQ(nullptr, socket->parsing_context());
    CheckDisabledReply();
    // Legacy clients send RPC bytes immediately, without a four-byte ACK.
    source.append("PRPC", 4);
    EXPECT_EQ(PARSE_ERROR_TRY_OTHERS,
              policy::ParseTransportHandshake(&source, socket.get(), false,
                                               nullptr).error());
    EXPECT_EQ("PRPC", source.to_string());
}

TEST_P(UrmaTcpFallbackTest, preserves_rpc_coalesced_after_rejected_hello) {
    butil::IOBuf source;
    source.append(Hello());
    source.append("PRPC", 4);
    EXPECT_EQ(PARSE_ERROR_TRY_OTHERS,
              policy::ParseTransportHandshake(&source, socket.get(), false,
                                               nullptr).error());
    EXPECT_EQ("PRPC", source.to_string());
    EXPECT_EQ(handshake::FALLBACK_TCP,
              AdapterTransport::Get(socket.get())->handshake_phase());
    CheckDisabledReply();
}

TEST_P(UrmaTcpFallbackTest, rejects_bad_frame_length) {
    std::string hello = Hello();
    memset(&hello[4], 0, GetParam() == 2 ? 2 : 4);
    if (GetParam() == 3) {
        // A 4097-byte body exceeds the maximum accepted v3 frame.
        const uint32_t length = butil::HostToNet32(4097);
        memcpy(&hello[4], &length, sizeof(length));
    }
    butil::IOBuf source;
    source.append(hello);
    EXPECT_EQ(PARSE_ERROR_ABSOLUTELY_WRONG,
              policy::ParseTransportHandshake(&source, socket.get(), false,
                                               nullptr).error());
    EXPECT_EQ(handshake::FAILED,
              AdapterTransport::Get(socket.get())->handshake_phase());
}

TEST_P(UrmaTcpFallbackTest, unsupported_hello_never_establishes_urma) {
    std::string hello = Hello();
    if (GetParam() == 2) {
        const uint16_t version = butil::HostToNet16(99);
        memcpy(&hello[6], &version, sizeof(version));
    } else {
        hello[8] = 0;  // Invalid protobuf tag inside an otherwise valid frame.
    }
    butil::IOBuf source;
    source.append(hello);
    EXPECT_EQ(GetParam() == 2 ? PARSE_ERROR_TRY_OTHERS
                              : PARSE_ERROR_ABSOLUTELY_WRONG,
              policy::ParseTransportHandshake(&source, socket.get(), false,
                                               nullptr).error());
    EXPECT_EQ(GetParam() == 2 ? handshake::FALLBACK_TCP : handshake::FAILED,
              AdapterTransport::Get(socket.get())->handshake_phase());
    if (GetParam() == 2) {
        CheckDisabledReply();
    }
}

INSTANTIATE_TEST_SUITE_P(WireVersions, UrmaTcpFallbackTest,
                         ::testing::Values(2, 3));
