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
#include "brpc/policy/mongo_protocol.h"
#include "brpc/policy/most_common_message.h"
#include "brpc/controller.h"
#include "brpc/mongo_head.h"
#include "brpc/mongo_service_adaptor.h"
#include "brpc/policy/mongo.pb.h"

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

namespace {

static const std::string EXP_REQUEST = "hello";
static const std::string EXP_RESPONSE = "world";

class MyEchoService : public ::brpc::policy::MongoService {
    void default_method(::google::protobuf::RpcController*,
                        const ::brpc::policy::MongoRequest* req,
                        ::brpc::policy::MongoResponse* res,
                        ::google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);

        EXPECT_EQ(EXP_REQUEST, req->message());

        res->mutable_header()->set_message_length(
                sizeof(brpc::mongo_head_t) + sizeof(int32_t) * 3 +
                sizeof(int64_t) + EXP_REQUEST.length());
        res->set_message(EXP_RESPONSE);
    }
};

class MyContext : public ::brpc::MongoContext {
};

class MyMongoAdaptor : public brpc::MongoServiceAdaptor {
public:
    virtual void SerializeError(int /*response_to*/, butil::IOBuf* out_buf) const {
        brpc::mongo_head_t header = {
            (int32_t)(sizeof(brpc::mongo_head_t) + sizeof(int32_t) * 3 +
                sizeof(int64_t) + EXP_REQUEST.length()),
            0, 0, 0};
        out_buf->append(static_cast<const void*>(&header), sizeof(brpc::mongo_head_t));
        int32_t response_flags = 0;
        int64_t cursor_id = 0;
        int32_t starting_from = 0;
        int32_t number_returned = 0;
        out_buf->append(&response_flags, sizeof(response_flags));
        out_buf->append(&cursor_id, sizeof(cursor_id));
        out_buf->append(&starting_from, sizeof(starting_from));
        out_buf->append(&number_returned, sizeof(number_returned));
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
    total_buf.append(static_cast<const void*>(&header), sizeof(header));
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
    total_buf.append(static_cast<const void*>(&header), sizeof(header));
    total_buf.append(EXP_REQUEST);
    brpc::ParseResult req_pr = brpc::policy::ParseMongoMessage(
        &total_buf, _socket.get(), false, &_server);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    _socket->SetFailed();
    ProcessMessage(brpc::policy::ProcessMongoRequest, req_pr.message(), false);
    ASSERT_EQ(0ll, _server._nerror_bvar.get_value());
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
    total_buf.append(static_cast<const void*>(&header), sizeof(header));
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
    total_buf.append(static_cast<const void*>(&header), sizeof(header));
    total_buf.append(EXP_REQUEST);
    req_pr = brpc::policy::ParseMongoMessage(&total_buf, _socket.get(), false, &_server);
    ASSERT_EQ(brpc::PARSE_OK, req_pr.error());
    brpc::InputMessageBase* req_msg = req_pr.message();
    ProcessMessage(brpc::policy::ProcessMongoRequest, req_msg, false);

    // Read response from pipe
    butil::IOPortal response_buf;
    response_buf.append_from_file_descriptor(_pipe_fds[0], 1024);
    char buf[sizeof(brpc::mongo_head_t)];
    const brpc::mongo_head_t *phead = static_cast<const brpc::mongo_head_t*>(
            response_buf.fetch(buf, sizeof(buf)));
    response_buf.cutn(&header, sizeof(header));
    response_buf.cutn(buf, sizeof(int32_t));
    response_buf.cutn(buf, sizeof(int64_t));
    response_buf.cutn(buf, sizeof(int32_t));
    response_buf.cutn(buf, sizeof(int32_t));
    char msg_buf[200];
    int msg_length = phead->message_length - sizeof(brpc::mongo_head_t) - sizeof(int32_t) * 3 -
        sizeof(int64_t);
    response_buf.cutn(msg_buf, msg_length);
    msg_buf[msg_length] = '\0';

    ASSERT_FALSE(cntl.Failed());
    ASSERT_STREQ(EXP_RESPONSE.c_str(), msg_buf);
}
} //namespace
