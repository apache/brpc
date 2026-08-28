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

// brpc - A framework to host and access services throughout Baidu.

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

// The malformed T_EXCEPTION reply is decoded by the Thrift framed protocol,
// which is only compiled in when the build has THRIFT support enabled (the
// WITH_THRIFT build flag / brpc_with_thrift config, off by default). Keep this
// test a trivial pass on builds without Thrift so it links cleanly there too.
#ifdef ENABLE_THRIFT_FRAMED_PROTOCOL

#include <arpa/inet.h>
#include "butil/macros.h"
#include "brpc/socket.h"
#include "brpc/controller.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/policy/thrift_protocol.h"

namespace {

// A nominal thrift message-type value for a T_EXCEPTION reply.
static const uint32_t THRIFT_TYPE_EXCEPTION = 3;
static const uint32_t THRIFT_HEAD_VERSION_1 = 0x80010000;

class ThriftProtocolTest : public ::testing::Test {
protected:
    ThriftProtocolTest() {
        _pipe_fds[0] = _pipe_fds[1] = -1;
        EXPECT_EQ(0, pipe(_pipe_fds));
        brpc::SocketId id;
        brpc::SocketOptions options;
        options.fd = _pipe_fds[1];
        EXPECT_EQ(0, brpc::Socket::Create(options, &id));
        EXPECT_EQ(0, brpc::Socket::Address(id, &_socket));
    }

    virtual void SetUp() {}
    virtual void TearDown() {
        // Close the pipe fds and destroy the test socket so no OS resources
        // are leaked across tests.
        if (_pipe_fds[0] >= 0) {
            close(_pipe_fds[0]);
            _pipe_fds[0] = -1;
        }
        if (_pipe_fds[1] >= 0) {
            close(_pipe_fds[1]);
            _pipe_fds[1] = -1;
        }
        _socket.reset();
    }

    // Attach the test socket to the message so that ProcessThriftResponse
    // can read the correlation_id out of it.
    void AttachMessage(brpc::InputMessageBase* msg) {
        if (msg->_socket == nullptr) {
            _socket->ReAddress(&msg->_socket);
        }
    }

    // Build a MostCommonMessage whose payload is a thrift T_EXCEPTION reply
    // whose exception struct is malformed (a T_STRING field whose length is
    // huge and never bounded by real bytes).
    brpc::policy::MostCommonMessage* MakeMalformedExceptionMessage() {
        const char method_name[] = "echo";
        const uint32_t version_and_type = htonl(
            THRIFT_HEAD_VERSION_1 | THRIFT_TYPE_EXCEPTION);
        const uint32_t method_name_length = htonl(sizeof(method_name) - 1);
        const uint32_t seq_id = htonl(0);
        // Malformed exception struct body: one field, id 1, type T_STRING,
        // string length 0x7FFFFFFF and no following bytes.
        static const uint8_t malformed_body[] =
            { 0x0B, 0x00, 0x01, 0x7F, 0xFF, 0xFF, 0xFF };

        brpc::policy::MostCommonMessage* msg =
                brpc::policy::MostCommonMessage::Get();
        msg->payload.append(&version_and_type, sizeof(version_and_type));
        msg->payload.append(&method_name_length, sizeof(method_name_length));
        msg->payload.append(method_name, sizeof(method_name) - 1);
        msg->payload.append(&seq_id, sizeof(seq_id));
        msg->payload.append(malformed_body, sizeof(malformed_body));
        return msg;
    }

    int _pipe_fds[2];
    brpc::SocketUniquePtr _socket;
};

TEST_F(ThriftProtocolTest, malformed_exception_reply_does_not_crash_client) {
    brpc::Controller cntl;
    // Simulate a real client: the socket's correlation_id matches the
    // controller's call_id so that ProcessThriftResponse can find the
    // controller.
    _socket->set_correlation_id(cntl.call_id().value);

    brpc::policy::MostCommonMessage* msg = MakeMalformedExceptionMessage();
    AttachMessage(msg);

    // Before the fix, ReadThriftException lets the malformed reply unwind out
    // of ProcessThriftResponse as an uncaught thrift exception (in production
    // that reaches the bthread task frame and calls std::terminate). After the
    // fix it must be contained and reported as a normal RPC failure instead.
    EXPECT_NO_THROW(brpc::policy::ProcessThriftResponse(msg));
}

TEST_F(ThriftProtocolTest, malformed_exception_reply_sets_failed) {
    brpc::Controller cntl;
    _socket->set_correlation_id(cntl.call_id().value);

    brpc::policy::MostCommonMessage* msg = MakeMalformedExceptionMessage();
    AttachMessage(msg);
    brpc::policy::ProcessThriftResponse(msg);
    // After the fix the malformed exception reply must be surfaced to the
    // caller as a failed RPC rather than crashing the process.
    EXPECT_TRUE(cntl.Failed());
}

}  // namespace

#else

namespace {
// Trivial placeholder so the test links successfully on builds without
// THRIFT support (the guarded code above is compiled out).
class ThriftProtocolTest : public ::testing::Test {};
TEST_F(ThriftProtocolTest, skipped_without_thrift_support) {}
}  // namespace

#endif
