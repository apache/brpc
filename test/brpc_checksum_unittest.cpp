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

// Unit tests for Controller::set_request/response_checksum_attachment(),
// i.e. extending checksum coverage (see set_request/response_checksum_type())
// to also fold in the attachment.

#include <gtest/gtest.h>
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/checksum.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/policy/crc32c_checksum.h"
#include "butil/iobuf.h"
#include "echo.pb.h"

namespace {

// ---------------------------------------------------------------------
// Focused unit tests on Crc32cCompute/Crc32cVerify: exercise
// ChecksumIn::attachment directly without going through the network, so
// that we can precisely control and corrupt the bytes being checksummed.
// ---------------------------------------------------------------------
class ChecksumAttachmentTest : public ::testing::Test {};

TEST_F(ChecksumAttachmentTest, verify_succeeds_when_body_only) {
    brpc::Controller cntl;
    butil::IOBuf body;
    body.append("request body");

    brpc::ChecksumIn compute_in{&body, &cntl, NULL};
    brpc::policy::Crc32cCompute(compute_in);

    brpc::ChecksumIn verify_in{&body, &cntl, NULL};
    EXPECT_TRUE(brpc::policy::Crc32cVerify(verify_in));
}

TEST_F(ChecksumAttachmentTest, verify_succeeds_when_attachment_included) {
    brpc::Controller cntl;
    butil::IOBuf body;
    body.append("request body");
    butil::IOBuf attachment;
    attachment.append("some attachment bytes");

    brpc::ChecksumIn compute_in{&body, &cntl, &attachment};
    brpc::policy::Crc32cCompute(compute_in);

    brpc::ChecksumIn verify_in{&body, &cntl, &attachment};
    EXPECT_TRUE(brpc::policy::Crc32cVerify(verify_in));
}

TEST_F(ChecksumAttachmentTest, verify_fails_when_attachment_corrupted) {
    brpc::Controller cntl;
    butil::IOBuf body;
    body.append("request body");
    butil::IOBuf attachment;
    attachment.append("some attachment bytes");

    brpc::ChecksumIn compute_in{&body, &cntl, &attachment};
    brpc::policy::Crc32cCompute(compute_in);

    // Attacker/bit-flip changes one byte of the attachment in flight.
    butil::IOBuf corrupted_attachment;
    corrupted_attachment.append("some attachment Bytes");
    brpc::ChecksumIn verify_in{&body, &cntl, &corrupted_attachment};
    EXPECT_FALSE(brpc::policy::Crc32cVerify(verify_in));
}

TEST_F(ChecksumAttachmentTest, verify_fails_when_attachment_dropped) {
    brpc::Controller cntl;
    butil::IOBuf body;
    body.append("request body");
    butil::IOBuf attachment;
    attachment.append("some attachment bytes");

    // Sender includes the attachment in the checksum...
    brpc::ChecksumIn compute_in{&body, &cntl, &attachment};
    brpc::policy::Crc32cCompute(compute_in);

    // ...but receiver (e.g. due to a mismatched
    // request/response_checksum_attachment() setting) verifies body only.
    brpc::ChecksumIn verify_in{&body, &cntl, NULL};
    EXPECT_FALSE(brpc::policy::Crc32cVerify(verify_in));
}

TEST_F(ChecksumAttachmentTest, verify_fails_when_body_and_attachment_swapped) {
    // Sanity check that the two parts are not simply concatenated in a
    // position-independent way, i.e. order matters and is deterministic.
    brpc::Controller cntl;
    butil::IOBuf part_a;
    part_a.append("AAAA");
    butil::IOBuf part_b;
    part_b.append("BBBBBB");

    brpc::ChecksumIn compute_in{&part_a, &cntl, &part_b};
    brpc::policy::Crc32cCompute(compute_in);

    brpc::ChecksumIn verify_in{&part_b, &cntl, &part_a};
    EXPECT_FALSE(brpc::policy::Crc32cVerify(verify_in));
}

// ---------------------------------------------------------------------
// End-to-end test that Controller::set_request/response_checksum_attachment
// is wired all the way through baidu_std serialization/wire format/
// deserialization without breaking normal RPCs.
// ---------------------------------------------------------------------
class ChecksumEchoServiceImpl : public test::EchoService {
public:
    void Echo(google::protobuf::RpcController* cntl_base,
              const test::EchoRequest* req,
              test::EchoResponse* res,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
        res->set_message("received " + req->message());
        // Echo the attachment back and ask the checksum (set by the test
        // below) to also cover it, exercising the response-side path.
        cntl->response_attachment().append(cntl->request_attachment());
        if (response_checksum_type_ != brpc::CHECKSUM_TYPE_NONE) {
            cntl->set_response_checksum_type(response_checksum_type_);
            cntl->set_response_checksum_attachment(
                response_checksum_with_attachment_);
        }
    }

    void set_response_checksum(brpc::ChecksumType type, bool with_attachment) {
        response_checksum_type_ = type;
        response_checksum_with_attachment_ = with_attachment;
    }

private:
    brpc::ChecksumType response_checksum_type_ = brpc::CHECKSUM_TYPE_NONE;
    bool response_checksum_with_attachment_ = false;
};

class ChecksumAttachmentEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(0, server_.AddService(&svc_, brpc::SERVER_DOESNT_OWN_SERVICE));
        ASSERT_EQ(0, server_.Start(port_, NULL));
        brpc::ChannelOptions options;
        ASSERT_EQ(0, channel_.Init(butil::EndPoint(butil::my_ip(), port_),
                                    &options));
    }

    void TearDown() override {
        server_.Stop(0);
        server_.Join();
    }

    const int port_ = 8934;
    brpc::Server server_;
    ChecksumEchoServiceImpl svc_;
    brpc::Channel channel_;
};

TEST_F(ChecksumAttachmentEndToEndTest, request_checksum_with_attachment) {
    svc_.set_response_checksum(brpc::CHECKSUM_TYPE_NONE, false);

    brpc::Controller cntl;
    cntl.request_attachment().append("request attachment payload");
    cntl.set_request_checksum_type(brpc::CHECKSUM_TYPE_CRC32C);
    cntl.set_request_checksum_attachment(true);

    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    test::EchoService::Stub(&channel_).Echo(&cntl, &req, &res, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ("received " + std::string(__FUNCTION__), res.message());
    EXPECT_EQ("request attachment payload", cntl.response_attachment());
}

TEST_F(ChecksumAttachmentEndToEndTest, response_checksum_with_attachment) {
    svc_.set_response_checksum(brpc::CHECKSUM_TYPE_CRC32C, true);

    brpc::Controller cntl;
    cntl.request_attachment().append("round-trip payload");

    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    test::EchoService::Stub(&channel_).Echo(&cntl, &req, &res, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ("received " + std::string(__FUNCTION__), res.message());
    EXPECT_EQ("round-trip payload", cntl.response_attachment());
}

TEST_F(ChecksumAttachmentEndToEndTest, both_directions_checksum_with_attachment) {
    svc_.set_response_checksum(brpc::CHECKSUM_TYPE_CRC32C, true);

    brpc::Controller cntl;
    cntl.request_attachment().append("both directions payload");
    cntl.set_request_checksum_type(brpc::CHECKSUM_TYPE_CRC32C);
    cntl.set_request_checksum_attachment(true);

    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    test::EchoService::Stub(&channel_).Echo(&cntl, &req, &res, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ("received " + std::string(__FUNCTION__), res.message());
    EXPECT_EQ("both directions payload", cntl.response_attachment());
}

TEST_F(ChecksumAttachmentEndToEndTest, checksum_without_attachment_still_works) {
    // Sanity check that turning the new flag off preserves the pre-existing
    // body-only checksum behavior.
    svc_.set_response_checksum(brpc::CHECKSUM_TYPE_CRC32C, false);

    brpc::Controller cntl;
    cntl.request_attachment().append("legacy behavior payload");
    cntl.set_request_checksum_type(brpc::CHECKSUM_TYPE_CRC32C);
    // Deliberately not calling set_request_checksum_attachment(true).

    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    test::EchoService::Stub(&channel_).Echo(&cntl, &req, &res, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_EQ("received " + std::string(__FUNCTION__), res.message());
    EXPECT_EQ("legacy behavior payload", cntl.response_attachment());
}

}  // namespace
