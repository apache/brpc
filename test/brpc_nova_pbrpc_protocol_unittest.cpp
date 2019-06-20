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

// Date: Sun Jul 13 15:04:18 CST 2014

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "brpc/socket.h"
#include "brpc/acceptor.h"
#include "brpc/server.h"
#include "brpc/policy/nova_pbrpc_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/controller.h"
#include "echo.pb.h"

namespace {

static const std::string EXP_REQUEST = "hello";
static const std::string EXP_RESPONSE = "world";

static const std::string MOCK_CREDENTIAL = "mock credential";
static const std::string MOCK_USER = "mock user";

class MyAuthenticator : public brpc::Authenticator {
public:
    MyAuthenticator() {}

    int GenerateCredential(std::string* auth_str) const {
        *auth_str = MOCK_CREDENTIAL;
        return 0;
    }

    int VerifyCredential(const std::string& auth_str,
                         const butil::EndPoint&,
                         brpc::AuthContext* ctx) const {
        EXPECT_EQ(MOCK_CREDENTIAL, auth_str);
        ctx->set_user(MOCK_USER);
        return 0;
    }
};

class MyEchoService : public ::test::EchoService {
    void Echo(::google::protobuf::RpcController* cntl_base,
              const ::test::EchoRequest* req,
              ::test::EchoResponse* res,
              ::google::protobuf::Closure* done) {
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);
        brpc::ClosureGuard done_guard(done);

        if (req->close_fd()) {
            cntl->CloseConnection("Close connection according to request");
            return;
        }
        EXPECT_EQ(EXP_REQUEST, req->message());
        res->set_message(EXP_RESPONSE);
    }
};
    
class NovaTest : public ::testing::Test{
protected:
    NovaTest() {
        EXPECT_EQ(0, _server.AddService(
            &_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
        // Hack: Regard `_server' as running 
        _server._status = brpc::Server::RUNNING;
        _server._options.nshead_service = new brpc::policy::NovaServiceAdaptor;
        // Nova doesn't support authentication
        // _server._options.auth = &_auth;
        
        EXPECT_EQ(0, pipe(_pipe_fds));

        brpc::SocketId id;
        brpc::SocketOptions options;
        options.fd = _pipe_fds[1];
        EXPECT_EQ(0, brpc::Socket::Create(options, &id));
        EXPECT_EQ(0, brpc::Socket::Address(id, &_socket));
    };

    virtual ~NovaTest() {};
    virtual void SetUp() {};
    virtual void TearDown() {};

    void VerifyMessage(brpc::InputMessageBase* msg) {
        if (msg->_socket == NULL) {
            _socket->ReAddress(&msg->_socket);
        }
        msg->_arg = &_server;
        EXPECT_TRUE(brpc::policy::VerifyNsheadRequest(msg));
    }

    void ProcessMessage(void (*process)(brpc::InputMessageBase*),
                        brpc::InputMessageBase* msg, bool set_eof) {
        if (msg->_socket == NULL) {
            _socket->ReAddress(&msg->_socket);
        }
        msg->_arg = &_server;
        _socket->PostponeEOF();
        if (set_eof) {
            _socket->SetEOF();
        }
        (*process)(msg);
    }

    brpc::policy::MostCommonMessage* MakeRequestMessage(
        const brpc::nshead_t& head) {
        brpc::policy::MostCommonMessage* msg =
                brpc::policy::MostCommonMessage::Get();
        msg->meta.append(&head, sizeof(head));

        test::EchoRequest req;
        req.set_message(EXP_REQUEST);
        butil::IOBufAsZeroCopyOutputStream req_stream(&msg->payload);
        EXPECT_TRUE(req.SerializeToZeroCopyStream(&req_stream));
        return msg;
    }

    brpc::policy::MostCommonMessage* MakeResponseMessage() {
        brpc::policy::MostCommonMessage* msg =
                brpc::policy::MostCommonMessage::Get();
        brpc::nshead_t head;
        memset(&head, 0, sizeof(head));
        msg->meta.append(&head, sizeof(head));
        
        test::EchoResponse res;
        res.set_message(EXP_RESPONSE);
        butil::IOBufAsZeroCopyOutputStream res_stream(&msg->payload);
        EXPECT_TRUE(res.SerializeToZeroCopyStream(&res_stream));
        return msg;
    }

    void CheckEmptyResponse() {
        int bytes_in_pipe = 0;
        ioctl(_pipe_fds[0], FIONREAD, &bytes_in_pipe);
        EXPECT_EQ(0, bytes_in_pipe);
    }

    int _pipe_fds[2];
    brpc::SocketUniquePtr _socket;
    brpc::Server _server;

    MyEchoService _svc;
    MyAuthenticator _auth;
};

TEST_F(NovaTest, process_request_failed_socket) {
    brpc::nshead_t head;
    memset(&head, 0, sizeof(head));
    brpc::policy::MostCommonMessage* msg = MakeRequestMessage(head);
    _socket->SetFailed();
    ProcessMessage(brpc::policy::ProcessNsheadRequest, msg, false);
    ASSERT_EQ(0ll, _server._nerror_bvar.get_value());
    CheckEmptyResponse();
}

TEST_F(NovaTest, process_request_logoff) {
    brpc::nshead_t head;
    head.reserved = 0;
    brpc::policy::MostCommonMessage* msg = MakeRequestMessage(head);
    _server._status = brpc::Server::READY;
    ProcessMessage(brpc::policy::ProcessNsheadRequest, msg, false);
    ASSERT_EQ(1ll, _server._nerror_bvar.get_value());
    ASSERT_TRUE(_socket->Failed());
    CheckEmptyResponse();
}

TEST_F(NovaTest, process_request_wrong_method) {
    brpc::nshead_t head;
    head.reserved = 10;
    brpc::policy::MostCommonMessage* msg = MakeRequestMessage(head);
    ProcessMessage(brpc::policy::ProcessNsheadRequest, msg, false);
    ASSERT_EQ(1ll, _server._nerror_bvar.get_value());
    ASSERT_TRUE(_socket->Failed());
    CheckEmptyResponse();
}

TEST_F(NovaTest, process_response_after_eof) {
    test::EchoResponse res;
    brpc::Controller cntl;
    cntl._response = &res;
    brpc::policy::MostCommonMessage* msg = MakeResponseMessage();
    _socket->set_correlation_id(cntl.call_id().value);
    ProcessMessage(brpc::policy::ProcessNovaResponse, msg, true);
    ASSERT_EQ(EXP_RESPONSE, res.message());
    ASSERT_TRUE(_socket->Failed());
}

TEST_F(NovaTest, complete_flow) {
    butil::IOBuf request_buf;
    butil::IOBuf total_buf;
    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    cntl._response = &res;
    cntl._connection_type = brpc::CONNECTION_TYPE_SHORT;
    ASSERT_EQ(0, brpc::Socket::Address(_socket->id(), &cntl._current_call.sending_sock));

    // Send request
    req.set_message(EXP_REQUEST);
    brpc::SerializeRequestDefault(&request_buf, &cntl, &req);
    ASSERT_FALSE(cntl.Failed());
    brpc::policy::PackNovaRequest(
        &total_buf, NULL, cntl.call_id().value,
        test::EchoService::descriptor()->method(0), &cntl, request_buf, &_auth);
    ASSERT_FALSE(cntl.Failed());

    // Verify and handle request
    brpc::ParseResult req_pr =
            brpc::policy::ParseNsheadMessage(&total_buf, NULL, false, NULL);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    brpc::InputMessageBase* req_msg = req_pr.message();
    VerifyMessage(req_msg);
    ProcessMessage(brpc::policy::ProcessNsheadRequest, req_msg, false);

    // Read response from pipe
    butil::IOPortal response_buf;
    response_buf.append_from_file_descriptor(_pipe_fds[0], 1024);
    brpc::ParseResult res_pr =
            brpc::policy::ParseNsheadMessage(&response_buf, NULL, false, NULL);
    ASSERT_EQ(brpc::PARSE_OK, res_pr.error());
    brpc::InputMessageBase* res_msg = res_pr.message();
    ProcessMessage(brpc::policy::ProcessNovaResponse, res_msg, false);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_EQ(EXP_RESPONSE, res.message());
}

TEST_F(NovaTest, close_in_callback) {
    butil::IOBuf request_buf;
    butil::IOBuf total_buf;
    brpc::Controller cntl;
    test::EchoRequest req;
    cntl._connection_type = brpc::CONNECTION_TYPE_SHORT;
    ASSERT_EQ(0, brpc::Socket::Address(_socket->id(), &cntl._current_call.sending_sock));

    // Send request
    req.set_message(EXP_REQUEST);
    req.set_close_fd(true);
    brpc::SerializeRequestDefault(&request_buf, &cntl, &req);
    ASSERT_FALSE(cntl.Failed());
    brpc::policy::PackNovaRequest(
        &total_buf, NULL, cntl.call_id().value,
        test::EchoService::descriptor()->method(0), &cntl, request_buf, &_auth);
    ASSERT_FALSE(cntl.Failed());

    // Handle request
    brpc::ParseResult req_pr =
            brpc::policy::ParseNsheadMessage(&total_buf, NULL, false, NULL);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    brpc::InputMessageBase* req_msg = req_pr.message();
    ProcessMessage(brpc::policy::ProcessNsheadRequest, req_msg, false);

    // Socket should be closed
    ASSERT_TRUE(_socket->Failed());
}
} //namespace
