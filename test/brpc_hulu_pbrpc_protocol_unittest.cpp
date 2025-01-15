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
#include "butil/gperftools_profiler.h"
#include "brpc/socket.h"
#include "brpc/acceptor.h"
#include "brpc/server.h"
#include "brpc/policy/hulu_pbrpc_meta.pb.h"
#include "brpc/policy/hulu_pbrpc_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/controller.h"
#include "echo.pb.h"

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

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
        if (cntl->auth_context()) {
            EXPECT_EQ(MOCK_USER, cntl->auth_context()->user());
        }
        EXPECT_EQ(EXP_REQUEST, req->message());
        if (!cntl->request_attachment().empty()) {
            EXPECT_EQ(EXP_REQUEST, cntl->request_attachment().to_string());
            cntl->response_attachment().append(EXP_RESPONSE);
        }
        res->set_message(EXP_RESPONSE);
    }
};
    
class HuluTest : public ::testing::Test{
protected:
    HuluTest() {
        EXPECT_EQ(0, _server.AddService(
            &_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
        // Hack: Regard `_server' as running 
        _server._status = brpc::Server::RUNNING;
        _server._options.auth = &_auth;
        
        EXPECT_EQ(0, pipe(_pipe_fds));

        brpc::SocketId id;
        brpc::SocketOptions options;
        options.fd = _pipe_fds[1];
        EXPECT_EQ(0, brpc::Socket::Create(options, &id));
        EXPECT_EQ(0, brpc::Socket::Address(id, &_socket));
    };

    virtual ~HuluTest() {};
    virtual void SetUp() {};
    virtual void TearDown() {};

    void VerifyMessage(brpc::InputMessageBase* msg) {
        if (msg->_socket == NULL) {
            _socket->ReAddress(&msg->_socket);
        }
        msg->_arg = &_server;
        EXPECT_TRUE(brpc::policy::VerifyHuluRequest(msg));
    }

    void ProcessMessage(void (*process)(brpc::InputMessageBase*),
                        brpc::InputMessageBase* msg, bool set_eof) {
        if (msg->_socket == NULL) {
            _socket.get()->ReAddress(&msg->_socket);
        }
        msg->_arg = &_server;
        _socket->PostponeEOF();
        if (set_eof) {
            _socket->SetEOF();
        }
        (*process)(msg);
    }

    brpc::policy::MostCommonMessage* MakeRequestMessage(
        const brpc::policy::HuluRpcRequestMeta& meta) {
        brpc::policy::MostCommonMessage* msg =
                brpc::policy::MostCommonMessage::Get();
        butil::IOBufAsZeroCopyOutputStream meta_stream(&msg->meta);
        EXPECT_TRUE(meta.SerializeToZeroCopyStream(&meta_stream));

        test::EchoRequest req;
        req.set_message(EXP_REQUEST);
        butil::IOBufAsZeroCopyOutputStream req_stream(&msg->payload);
        EXPECT_TRUE(req.SerializeToZeroCopyStream(&req_stream));
        return msg;
    }

    brpc::policy::MostCommonMessage* MakeResponseMessage(
        const brpc::policy::HuluRpcResponseMeta& meta) {
        brpc::policy::MostCommonMessage* msg =
                brpc::policy::MostCommonMessage::Get();
        butil::IOBufAsZeroCopyOutputStream meta_stream(&msg->meta);
        EXPECT_TRUE(meta.SerializeToZeroCopyStream(&meta_stream));

        test::EchoResponse res;
        res.set_message(EXP_RESPONSE);
        butil::IOBufAsZeroCopyOutputStream res_stream(&msg->payload);
        EXPECT_TRUE(res.SerializeToZeroCopyStream(&res_stream));
        return msg;
    }

    void CheckResponseCode(bool expect_empty, int expect_code) {
        int bytes_in_pipe = 0;
        ioctl(_pipe_fds[0], FIONREAD, &bytes_in_pipe);
        if (expect_empty) {
            EXPECT_EQ(0, bytes_in_pipe);
            return;
        }

        EXPECT_GT(bytes_in_pipe, 0);
        butil::IOPortal buf;
        EXPECT_EQ((ssize_t)bytes_in_pipe,
                  buf.append_from_file_descriptor(_pipe_fds[0], 1024));
        brpc::ParseResult pr = brpc::policy::ParseHuluMessage(&buf, NULL, false, NULL);
        EXPECT_EQ(brpc::PARSE_OK, pr.error());
        brpc::policy::MostCommonMessage* msg =
            static_cast<brpc::policy::MostCommonMessage*>(pr.message());

        brpc::policy::HuluRpcResponseMeta meta;
        butil::IOBufAsZeroCopyInputStream meta_stream(msg->meta);
        EXPECT_TRUE(meta.ParseFromZeroCopyStream(&meta_stream));
        EXPECT_EQ(expect_code, meta.error_code());
    }

    void TestHuluCompress(brpc::CompressType type) {
        butil::IOBuf request_buf;
        butil::IOBuf total_buf;
        brpc::Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        cntl._response = &res;

        req.set_message(EXP_REQUEST);
        cntl.set_request_compress_type(type);
        brpc::SerializeRequestDefault(&request_buf, &cntl, &req);
        ASSERT_FALSE(cntl.Failed());
        brpc::policy::PackHuluRequest(
            &total_buf, NULL, cntl.call_id().value,
            test::EchoService::descriptor()->method(0),
            &cntl, request_buf, &_auth);
        ASSERT_FALSE(cntl.Failed());

        brpc::ParseResult req_pr =
                brpc::policy::ParseHuluMessage(&total_buf, NULL, false, NULL);
        ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
        brpc::InputMessageBase* req_msg = req_pr.message();
        ProcessMessage(brpc::policy::ProcessHuluRequest, req_msg, false);
        CheckResponseCode(false, 0);
    }

    int _pipe_fds[2];
    brpc::SocketUniquePtr _socket;
    brpc::Server _server;

    MyEchoService _svc;
    MyAuthenticator _auth;
};

TEST_F(HuluTest, process_request_failed_socket) {
    brpc::policy::HuluRpcRequestMeta meta;
    meta.set_service_name("EchoService");
    meta.set_method_index(0);
    brpc::policy::MostCommonMessage* msg = MakeRequestMessage(meta);
    _socket->SetFailed();
    ProcessMessage(brpc::policy::ProcessHuluRequest, msg, false);
    ASSERT_EQ(0ll, _server._nerror_bvar.get_value());
    CheckResponseCode(true, 0);
}

TEST_F(HuluTest, process_request_logoff) {
    brpc::policy::HuluRpcRequestMeta meta;
    meta.set_service_name("EchoService");
    meta.set_method_index(0);
    brpc::policy::MostCommonMessage* msg = MakeRequestMessage(meta);
    _server._status = brpc::Server::READY;
    ProcessMessage(brpc::policy::ProcessHuluRequest, msg, false);
    ASSERT_EQ(1ll, _server._nerror_bvar.get_value());
    CheckResponseCode(false, brpc::ELOGOFF);
}

TEST_F(HuluTest, process_request_wrong_method) {
    brpc::policy::HuluRpcRequestMeta meta;
    meta.set_service_name("EchoService");
    meta.set_method_index(10);
    brpc::policy::MostCommonMessage* msg = MakeRequestMessage(meta);
    ProcessMessage(brpc::policy::ProcessHuluRequest, msg, false);
    ASSERT_EQ(1ll, _server._nerror_bvar.get_value());
    CheckResponseCode(false, brpc::ENOMETHOD);
}

TEST_F(HuluTest, process_response_after_eof) {
    brpc::policy::HuluRpcResponseMeta meta;
    test::EchoResponse res;
    brpc::Controller cntl;
    meta.set_correlation_id(cntl.call_id().value);
    cntl._response = &res;
    brpc::policy::MostCommonMessage* msg = MakeResponseMessage(meta);
    ProcessMessage(brpc::policy::ProcessHuluResponse, msg, true);
    ASSERT_EQ(EXP_RESPONSE, res.message());
    ASSERT_TRUE(_socket->Failed());
}

TEST_F(HuluTest, process_response_error_code) {
    const int ERROR_CODE = 12345;
    brpc::policy::HuluRpcResponseMeta meta;
    brpc::Controller cntl;
    meta.set_correlation_id(cntl.call_id().value);
    meta.set_error_code(ERROR_CODE);
    brpc::policy::MostCommonMessage* msg = MakeResponseMessage(meta);
    ProcessMessage(brpc::policy::ProcessHuluResponse, msg, false);
    ASSERT_EQ(ERROR_CODE, cntl.ErrorCode());
}

TEST_F(HuluTest, complete_flow) {
    butil::IOBuf request_buf;
    butil::IOBuf total_buf;
    brpc::Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    cntl._response = &res;

    // Send request
    req.set_message(EXP_REQUEST);
    brpc::SerializeRequestDefault(&request_buf, &cntl, &req);
    ASSERT_FALSE(cntl.Failed());
    cntl.request_attachment().append(EXP_REQUEST);
    brpc::policy::PackHuluRequest(
        &total_buf, NULL, cntl.call_id().value,
        test::EchoService::descriptor()->method(0), &cntl, request_buf, &_auth);
    ASSERT_FALSE(cntl.Failed());

    // Verify and handle request
    brpc::ParseResult req_pr =
            brpc::policy::ParseHuluMessage(&total_buf, NULL, false, NULL);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    brpc::InputMessageBase* req_msg = req_pr.message();
    VerifyMessage(req_msg);
    ProcessMessage(brpc::policy::ProcessHuluRequest, req_msg, false);

    // Read response from pipe
    butil::IOPortal response_buf;
    response_buf.append_from_file_descriptor(_pipe_fds[0], 1024);
    brpc::ParseResult res_pr =
            brpc::policy::ParseHuluMessage(&response_buf, NULL, false, NULL);
    ASSERT_EQ(brpc::PARSE_OK, res_pr.error());
    brpc::InputMessageBase* res_msg = res_pr.message();
    ProcessMessage(brpc::policy::ProcessHuluResponse, res_msg, false);

    ASSERT_FALSE(cntl.Failed());
    ASSERT_EQ(EXP_RESPONSE, res.message());
}

TEST_F(HuluTest, close_in_callback) {
    butil::IOBuf request_buf;
    butil::IOBuf total_buf;
    brpc::Controller cntl;
    test::EchoRequest req;

    // Send request
    req.set_message(EXP_REQUEST);
    req.set_close_fd(true);
    brpc::SerializeRequestDefault(&request_buf, &cntl, &req);
    ASSERT_FALSE(cntl.Failed());
    brpc::policy::PackHuluRequest(
        &total_buf, NULL, cntl.call_id().value,
        test::EchoService::descriptor()->method(0), &cntl, request_buf, &_auth);
    ASSERT_FALSE(cntl.Failed());

    // Handle request
    brpc::ParseResult req_pr =
            brpc::policy::ParseHuluMessage(&total_buf, NULL, false, NULL);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    brpc::InputMessageBase* req_msg = req_pr.message();
    ProcessMessage(brpc::policy::ProcessHuluRequest, req_msg, false);

    // Socket should be closed
    ASSERT_TRUE(_socket->Failed());
}

TEST_F(HuluTest, hulu_compress) {
    TestHuluCompress(brpc::COMPRESS_TYPE_SNAPPY);
    TestHuluCompress(brpc::COMPRESS_TYPE_GZIP);
    TestHuluCompress(brpc::COMPRESS_TYPE_ZLIB);
}
} //namespace
