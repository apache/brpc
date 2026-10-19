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

// Unit tests of the nshead_mcpack protocol, focused on the client-side
// response handler: `ProcessNsheadMcpackResponse' must always release the
// bthread_id lock (via `accessor.OnResponse') on every path, otherwise the
// RPC caller hangs on Join() forever and even the timeout mechanism cannot
// rescue it.

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "bthread/id.h"
#include "brpc/socket.h"
#include "brpc/acceptor.h"
#include "brpc/server.h"
#include "brpc/nshead.h"
#include "brpc/controller.h"
#include "brpc/policy/nshead_mcpack_protocol.h"
#include "brpc/policy/nshead_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "mcpack2pb/mcpack2pb.h"
#include "echo.pb.h"

namespace {

static const std::string EXP_RESPONSE = "world";

// Simulates the mcpack parser facing a garbage body: drains the input and
// returns 0 so that `parse_from_iobuf' reports failure.
static size_t FailingParse(google::protobuf::Message*,
                           google::protobuf::io::ZeroCopyInputStream* input) {
    const void* data;
    int size;
    while (input->Next(&data, &size)) {}
    return 0;
}

// Simulates a successful mcpack parsing which fills the response.
static size_t SuccessfulParse(google::protobuf::Message* msg,
                              google::protobuf::io::ZeroCopyInputStream* input) {
    const void* data;
    int size;
    size_t total = 0;
    while (input->Next(&data, &size)) {
        total += size;
    }
    static_cast<test::EchoRequest*>(msg)->set_message(EXP_RESPONSE);
    return total;
}

static void RegisterMcpackHandlers() {
    static bool registered = []() {
        mcpack2pb::MessageHandler failing_handler = {
            FailingParse, nullptr, nullptr, nullptr};
        // Note: `full_name()' returns absl::string_view in newer protobuf,
        // wrap it into std::string explicitly.
        mcpack2pb::register_message_handler_or_die(
            std::string(test::EchoResponse::descriptor()->full_name()), failing_handler);
        mcpack2pb::MessageHandler working_handler = {
            SuccessfulParse, nullptr, nullptr, nullptr};
        mcpack2pb::register_message_handler_or_die(
            std::string(test::EchoRequest::descriptor()->full_name()), working_handler);
        return true;
    }();
    (void)registered;
}

class NsheadMcpackTest : public ::testing::Test {
protected:
    NsheadMcpackTest() {
        RegisterMcpackHandlers();
        EXPECT_EQ(0, pipe(_pipe_fds));

        brpc::SocketId id;
        brpc::SocketOptions options;
        options.fd = _pipe_fds[1];
        EXPECT_EQ(0, brpc::Socket::Create(options, &id));
        EXPECT_EQ(0, brpc::Socket::Address(id, &_socket));
    }

    virtual ~NsheadMcpackTest() {
        // The write end (_pipe_fds[1]) is owned by `_socket' and closed
        // when it is released; close the unused read end here to avoid
        // leaking fds across the per-case fixtures.
        close(_pipe_fds[0]);
    };
    virtual void SetUp() {};
    virtual void TearDown() {};

    void ProcessMessage(brpc::InputMessageBase* msg) {
        if (msg->_socket == nullptr) {
            _socket->ReAddress(&msg->_socket);
        }
        _socket->PostponeEOF();
        brpc::policy::ProcessNsheadMcpackResponse(msg);
    }

    // Make a response with a valid nshead header followed by a garbage
    // body which cannot be parsed as an mcpack message.
    brpc::policy::MostCommonMessage* MakeMalformedResponseMessage() {
        brpc::policy::MostCommonMessage* msg =
                brpc::policy::MostCommonMessage::Get();
        brpc::nshead_t head;
        memset(&head, 0, sizeof(head));
        head.magic_num = brpc::NSHEAD_MAGICNUM;
        const char garbage[] = "\xde\xad\xbe\xef";
        head.body_len = sizeof(garbage) - 1;
        msg->meta.append(&head, sizeof(head));
        msg->payload.append(garbage, sizeof(garbage) - 1);
        return msg;
    }

    // Assert that the correlation_id bound to the RPC is no longer locked.
    // If the lock was leaked, `brpc::Join' in user code would hang forever
    // and even the RPC timeout could not rescue it, because the timeout
    // error is only consumed during unlocking.
    void AssertLockReleased(const brpc::CallId& cid) {
        // A locked id returns EBUSY while a destroyed (released) id
        // returns EINVAL.
        ASSERT_EQ(EINVAL, bthread_id_trylock(cid, nullptr))
            << "correlation_id lock was leaked";
    }

    int _pipe_fds[2];
    brpc::SocketUniquePtr _socket;
};

// A malformed mcpack body must fail the RPC instead of leaking the lock.
TEST_F(NsheadMcpackTest, process_response_with_malformed_body) {
    test::EchoResponse res;
    brpc::Controller cntl;
    cntl._response = &res;
    brpc::policy::MostCommonMessage* msg = MakeMalformedResponseMessage();
    _socket->set_correlation_id(cntl.call_id().value);
    ProcessMessage(msg);
    ASSERT_TRUE(cntl.Failed());
    AssertLockReleased(cntl.call_id());
}

// A response message whose mcpack handler was never registered must not
// leak the lock either.
TEST_F(NsheadMcpackTest, process_response_with_unregistered_handler) {
    // test::ComboRequest has no registered mcpack handler at all.
    test::ComboRequest res;
    brpc::Controller cntl;
    cntl._response = &res;
    brpc::policy::MostCommonMessage* msg = MakeMalformedResponseMessage();
    _socket->set_correlation_id(cntl.call_id().value);
    ProcessMessage(msg);
    ASSERT_TRUE(cntl.Failed());
    AssertLockReleased(cntl.call_id());
}

// The controller without a response object must not leak the lock.
TEST_F(NsheadMcpackTest, process_response_without_response_object) {
    brpc::Controller cntl;
    brpc::policy::MostCommonMessage* msg = MakeMalformedResponseMessage();
    _socket->set_correlation_id(cntl.call_id().value);
    ProcessMessage(msg);
    AssertLockReleased(cntl.call_id());
}

// A well-formed response must be delivered to the user and unlock normally.
TEST_F(NsheadMcpackTest, process_response_success) {
    // Reuse test::EchoRequest whose registered handler always succeeds.
    test::EchoRequest res;
    brpc::Controller cntl;
    cntl._response = &res;
    brpc::policy::MostCommonMessage* msg = MakeMalformedResponseMessage();
    _socket->set_correlation_id(cntl.call_id().value);
    ProcessMessage(msg);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(EXP_RESPONSE, res.message());
    AssertLockReleased(cntl.call_id());
}

} //namespace
