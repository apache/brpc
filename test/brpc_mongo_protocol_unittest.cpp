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

// Date: Thu Oct 15 21:08:31 CST 2015

#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <google/protobuf/descriptor.h>
#include "butil/time.h"
#include "butil/macros.h"
#include "butil/sys_byteorder.h"
#include "brpc/socket.h"
#include "brpc/acceptor.h"
#include "brpc/server.h"
#include "brpc/policy/mongo_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/controller.h"
#include "brpc/mongo_head.h"
#include "brpc/mongo_service_adaptor.h"
#include "brpc/policy/mongo.pb.h"

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

namespace {

static const std::string EXP_REQUEST = "hello";
static const std::string EXP_RESPONSE = "world";
static const int64_t EXP_CURSOR_ID = 0x0102030405060708LL;
static const int32_t EXP_NUMBER_RETURNED = 1;

// Everything on the wire is little-endian, whatever the host byte order.
static void AppendHeader(const brpc::mongo_head_t& header, butil::IOBuf* buf) {
    brpc::mongo_head_t wire = header;
    wire.make_wire_endian();
    buf->append(static_cast<const void*>(&wire), sizeof(wire));
}

static void AppendLE32(int32_t value, butil::IOBuf* buf) {
    const uint32_t wire = butil::ByteSwapToLE32(value);
    buf->append(&wire, sizeof(wire));
}

static void AppendLE64(int64_t value, butil::IOBuf* buf) {
    const uint64_t wire = butil::ByteSwapToLE64(value);
    buf->append(&wire, sizeof(wire));
}

class MyEchoService : public ::brpc::policy::MongoService {
public:
    // op_code of the last request, as decoded by brpc.
    int last_op_code = -1;

private:
    void default_method(::google::protobuf::RpcController*,
                        const ::brpc::policy::MongoRequest* req,
                        ::brpc::policy::MongoResponse* res,
                        ::google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);

        EXPECT_EQ(EXP_REQUEST, req->message());
        last_op_code = req->header().op_code();

        res->mutable_header()->set_message_length(
                sizeof(brpc::mongo_head_t) + sizeof(int32_t) * 3 +
                sizeof(int64_t) + EXP_REQUEST.length());
        res->set_cursor_id(EXP_CURSOR_ID);
        res->set_number_returned(EXP_NUMBER_RETURNED);
        res->set_message(EXP_RESPONSE);
    }
};

class MyContext : public ::brpc::MongoContext {
};

class MyAuthenticator : public brpc::Authenticator {
public:
    int GenerateCredential(std::string*) const override { return 0; }
    int VerifyCredential(const std::string&, const butil::EndPoint&,
                         brpc::AuthContext*) const override { return 0; }
};

class MyMongoAdaptor : public brpc::MongoServiceAdaptor {
public:
    virtual void SerializeError(int /*response_to*/, butil::IOBuf* out_buf) const {
        brpc::mongo_head_t header = {
            (int32_t)(sizeof(brpc::mongo_head_t) + sizeof(int32_t) * 3 +
                sizeof(int64_t) + EXP_REQUEST.length()),
            0, 0, 0};
        AppendHeader(header, out_buf);
        AppendLE32(0, out_buf);  // response_flags
        AppendLE64(0, out_buf);  // cursor_id
        AppendLE32(0, out_buf);  // starting_from
        AppendLE32(0, out_buf);  // number_returned
        out_buf->append(EXP_RESPONSE);
    }

    virtual ::brpc::MongoContext* CreateSocketContext() const {
        return new MyContext;
    }
};

class MongoTest : public ::testing::Test{
protected:
    MongoTest() {
        EXPECT_EQ(0, _server.AddService(
            &_svc, brpc::SERVER_DOESNT_OWN_SERVICE));
        // Hack: Regard `_server' as running 
        _server._status = brpc::Server::RUNNING;
        _server._options.mongo_service_adaptor = &_adaptor;
        
        EXPECT_EQ(0, pipe(_pipe_fds));

        brpc::SocketId id;
        brpc::SocketOptions options;
        options.fd = _pipe_fds[1];
        EXPECT_EQ(0, brpc::Socket::Create(options, &id));
        EXPECT_EQ(0, brpc::Socket::Address(id, &_socket));
    };

    virtual ~MongoTest() {};
    virtual void SetUp() {};
    virtual void TearDown() {};

    void ProcessMessage(void (*process)(brpc::InputMessageBase*),
                        brpc::InputMessageBase* msg, bool set_eof) {
        if (msg->_socket == nullptr) {
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
        brpc::mongo_head_t* head) {
        head->message_length = sizeof(head) + EXP_REQUEST.length();
        head->op_code = brpc::MONGO_OPCODE_REPLY;
        brpc::policy::MostCommonMessage* msg =
                brpc::policy::MostCommonMessage::Get();
        msg->meta.append(&head, sizeof(head));
        msg->payload.append(EXP_REQUEST);
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
    MyMongoAdaptor _adaptor;

    MyEchoService _svc;
};

TEST_F(MongoTest, process_request_logoff) {
    brpc::mongo_head_t header = { 0, 0, 0, 0 };
    header.op_code = brpc::MONGO_OPCODE_REPLY;
    header.message_length = sizeof(header) + EXP_REQUEST.length();
    butil::IOBuf total_buf;
    AppendHeader(header, &total_buf);
    total_buf.append(EXP_REQUEST);
    brpc::ParseResult req_pr = brpc::policy::ParseMongoMessage(
        &total_buf, _socket.get(), false, &_server);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    _server._status = brpc::Server::READY;
    ProcessMessage(brpc::policy::ProcessMongoRequest, req_pr.message(), false);
    ASSERT_EQ(1ll, _server._nerror_bvar.get_value());
}

TEST_F(MongoTest, process_request_failed_socket) {
    brpc::mongo_head_t header = { 0, 0, 0, 0 };
    header.op_code = brpc::MONGO_OPCODE_REPLY;
    header.message_length = sizeof(header) + EXP_REQUEST.length();
    butil::IOBuf total_buf;
    AppendHeader(header, &total_buf);
    total_buf.append(EXP_REQUEST);
    brpc::ParseResult req_pr = brpc::policy::ParseMongoMessage(
        &total_buf, _socket.get(), false, &_server);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    _socket->SetFailed();
    ProcessMessage(brpc::policy::ProcessMongoRequest, req_pr.message(), false);
    ASSERT_EQ(0ll, _server._nerror_bvar.get_value());
}

TEST_F(MongoTest, process_request_from_internal_port) {
    // The fixture talks over a pipe, which has no local address at all while
    // Server::Start() never accepts 0 as ServerOptions.internal_port. Give the
    // connection an address the server can recognize as internal, the port is
    // arbitrary as nothing listens on it.
    _socket->_local_side = butil::EndPoint(butil::IP_ANY, 8888);
    _server._options.internal_port = _socket->local_side().port;

    brpc::mongo_head_t header = { 0, 0, 0, 0 };
    header.op_code = brpc::MONGO_OPCODE_REPLY;
    header.message_length = sizeof(header) + EXP_REQUEST.length();
    butil::IOBuf total_buf;
    AppendHeader(header, &total_buf);
    total_buf.append(EXP_REQUEST);
    brpc::ParseResult req_pr = brpc::policy::ParseMongoMessage(
        &total_buf, _socket.get(), false, &_server);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    ProcessMessage(brpc::policy::ProcessMongoRequest, req_pr.message(), false);
    // MyEchoService was never reached, the request was counted as an error of
    // the server rather than as a call of the method.
    ASSERT_EQ(1ll, _server._nerror_bvar.get_value());
}

TEST_F(MongoTest, verify_request) {
    brpc::mongo_head_t header = { 0, 0, 0, 0 };
    header.op_code = brpc::MONGO_OPCODE_REPLY;
    header.message_length = sizeof(header) + EXP_REQUEST.length();
    butil::IOBuf total_buf;
    AppendHeader(header, &total_buf);
    total_buf.append(EXP_REQUEST);
    brpc::ParseResult req_pr = brpc::policy::ParseMongoMessage(
        &total_buf, _socket.get(), false, &_server);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    brpc::InputMessageBase* msg = req_pr.message();
    if (msg->_socket == nullptr) {
        _socket->ReAddress(&msg->_socket);
    }
    msg->_arg = &_server;

    // Without ServerOptions.auth every request passes.
    ASSERT_TRUE(brpc::policy::VerifyMongoRequest(msg));

    // The mongo protocol carries no credential for ServerOptions.auth to
    // verify, so requests on an authenticated server must be rejected.
    MyAuthenticator auth;
    _server._options.auth = &auth;
    ASSERT_FALSE(brpc::policy::VerifyMongoRequest(msg));
    _server._options.auth = nullptr;

    msg->Destroy();
}

TEST_F(MongoTest, complete_flow) {
    butil::IOBuf request_buf;
    butil::IOBuf total_buf;
    brpc::Controller cntl;
    brpc::policy::MongoRequest req;
    brpc::policy::MongoResponse res;
    cntl._response = &res;

    // Assemble request
    brpc::mongo_head_t header = { 0, 0, 0, 0 };
    header.message_length = sizeof(header) + EXP_REQUEST.length();
    header.request_id = 1234;
    AppendHeader(header, &total_buf);
    total_buf.append(EXP_REQUEST);

    const size_t old_size = total_buf.size();
    // Handle request
    brpc::ParseResult req_pr =
            brpc::policy::ParseMongoMessage(&total_buf, _socket.get(), false, &_server);
    // head.op_code does not fit.
    ASSERT_EQ(brpc::PARSE_ERROR_TRY_OTHERS, req_pr.error());
    // no data should be consumed.
    ASSERT_EQ(old_size, total_buf.size());
    header.op_code = brpc::MONGO_OPCODE_REPLY;
    total_buf.clear();
    AppendHeader(header, &total_buf);
    total_buf.append(EXP_REQUEST);
    req_pr = brpc::policy::ParseMongoMessage(&total_buf, _socket.get(), false, &_server);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    brpc::InputMessageBase* req_msg = req_pr.message();
    ProcessMessage(brpc::policy::ProcessMongoRequest, req_msg, false);

    // Read response from pipe; everything on the wire is little-endian.
    butil::IOPortal response_buf;
    response_buf.append_from_file_descriptor(_pipe_fds[0], 1024);
    brpc::mongo_head_t head;
    ASSERT_EQ(sizeof(head), response_buf.cutn(&head, sizeof(head)));
    head.make_host_endian();
    EXPECT_EQ(header.request_id, head.response_to);
    uint32_t response_flags = 0;
    uint64_t cursor_id = 0;
    uint32_t starting_from = 0;
    uint32_t number_returned = 0;
    response_buf.cutn(&response_flags, sizeof(response_flags));
    response_buf.cutn(&cursor_id, sizeof(cursor_id));
    response_buf.cutn(&starting_from, sizeof(starting_from));
    response_buf.cutn(&number_returned, sizeof(number_returned));
    EXPECT_EQ(EXP_CURSOR_ID, (int64_t)butil::ByteSwapToLE64(cursor_id));
    EXPECT_EQ(EXP_NUMBER_RETURNED, (int32_t)butil::ByteSwapToLE32(number_returned));
    char msg_buf[200];
    int msg_length = head.message_length - sizeof(brpc::mongo_head_t) - sizeof(int32_t) * 3 -
        sizeof(int64_t);
    response_buf.cutn(msg_buf, msg_length);
    msg_buf[msg_length] = '\0';

    ASSERT_FALSE(cntl.Failed());
    ASSERT_STREQ(EXP_RESPONSE.c_str(), msg_buf);
}
TEST_F(MongoTest, little_endian_wire_format) {
    // A request header as it appears on the wire, independent of the host
    // byte order: message_length 21, request_id 7, response_to 0 and
    // op_code 2004 (OP_QUERY).
    static const uint8_t REQUEST_HEADER[sizeof(brpc::mongo_head_t)] = {
        0x15, 0x00, 0x00, 0x00,
        0x07, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xd4, 0x07, 0x00, 0x00,
    };
    butil::IOBuf total_buf;
    total_buf.append(REQUEST_HEADER, sizeof(REQUEST_HEADER));
    total_buf.append(EXP_REQUEST);
    brpc::ParseResult req_pr =
            brpc::policy::ParseMongoMessage(&total_buf, _socket.get(), false, &_server);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    ProcessMessage(brpc::policy::ProcessMongoRequest, req_pr.message(), false);
    EXPECT_EQ((int)brpc::MONGO_OPCODE_QUERY, _svc.last_op_code);

    // The response, byte by byte: header, response_flags, cursor_id,
    // starting_from, number_returned, message.
    butil::IOPortal response_buf;
    response_buf.append_from_file_descriptor(_pipe_fds[0], 1024);
    uint8_t response[sizeof(brpc::mongo_head_t) + sizeof(int32_t) * 3 + sizeof(int64_t)];
    ASSERT_EQ(sizeof(response), response_buf.cutn(response, sizeof(response)));
    // message_length = 16 + 12 + 8 + 5 = 41
    static const uint8_t MESSAGE_LENGTH[4] = { 0x29, 0x00, 0x00, 0x00 };
    EXPECT_EQ(0, memcmp(MESSAGE_LENGTH, response, 4));
    // response_to = request_id = 7
    static const uint8_t RESPONSE_TO[4] = { 0x07, 0x00, 0x00, 0x00 };
    EXPECT_EQ(0, memcmp(RESPONSE_TO, response + 8, 4));
    // cursor_id = EXP_CURSOR_ID
    static const uint8_t CURSOR_ID[8] = { 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 };
    EXPECT_EQ(0, memcmp(CURSOR_ID, response + 20, 8));
    // number_returned = EXP_NUMBER_RETURNED
    static const uint8_t NUMBER_RETURNED[4] = { 0x01, 0x00, 0x00, 0x00 };
    EXPECT_EQ(0, memcmp(NUMBER_RETURNED, response + 32, 4));
    EXPECT_EQ(EXP_RESPONSE, response_buf.to_string());
}
} //namespace
