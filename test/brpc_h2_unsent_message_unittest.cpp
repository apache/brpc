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

// Date: Tue Oct 9 20:27:18 CST 2018

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "bthread/bthread.h"
#include "butil/atomicops.h"
#include "brpc/policy/http_rpc_protocol.h"
#include "brpc/policy/http2_rpc_protocol.h"
#include "gperftools_helper.h"

namespace brpc {
DECLARE_int64(socket_max_unwritten_bytes);
}

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {

brpc::policy::H2FrameHead PopFrame(butil::IOBuf* buf, std::string* payload) {
    char head[brpc::policy::FRAME_HEAD_SIZE];
    CHECK_EQ(sizeof(head), buf->cutn(head, sizeof(head)));
    brpc::policy::H2FrameHead frame;
    frame.payload_size =
        (static_cast<uint8_t>(head[0]) << 16) |
        (static_cast<uint8_t>(head[1]) << 8) |
        static_cast<uint8_t>(head[2]);
    frame.type = static_cast<brpc::policy::H2FrameType>(head[3]);
    frame.flags = head[4];
    frame.stream_id =
        (static_cast<uint8_t>(head[5]) << 24) |
        (static_cast<uint8_t>(head[6]) << 16) |
        (static_cast<uint8_t>(head[7]) << 8) |
        static_cast<uint8_t>(head[8]);
    payload->resize(frame.payload_size);
    if (frame.payload_size != 0) {
        CHECK_EQ(frame.payload_size,
                 buf->cutn(&(*payload)[0], frame.payload_size));
    }
    return frame;
}

}  // namespace

TEST(H2UnsentMessage, split_request_data_by_remote_window) {
    brpc::SocketId id;
    brpc::SocketUniquePtr sock;
    brpc::SocketOptions options;
    options.user = brpc::get_client_side_messenger();
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    ASSERT_EQ(0, brpc::Socket::Address(id, &sock));

    brpc::policy::H2Context* ctx =
        new brpc::policy::H2Context(sock.get(), nullptr);
    ASSERT_EQ(0, ctx->Init());
    sock->initialize_parsing_context(&ctx);
    ctx->_remote_settings.max_frame_size = 4;
    ctx->_remote_settings.stream_window_size = 5;
    ctx->_remote_window_left = 6;

    brpc::policy::H2StreamContext* sctx =
        new brpc::policy::H2StreamContext(false);
    sctx->Init(ctx, 1);

    butil::IOBuf body;
    body.append("abcdefghij");
    butil::IOBuf out;
    ASSERT_TRUE(ctx->TryToInsertClientStream(1, sctx, body, &out).ok());

    std::string payload;
    brpc::policy::H2FrameHead frame = PopFrame(&out, &payload);
    EXPECT_EQ(4u, frame.payload_size);
    EXPECT_EQ(brpc::policy::H2_FRAME_DATA, frame.type);
    EXPECT_EQ(0, frame.flags & 0x1);
    EXPECT_EQ("abcd", payload);
    frame = PopFrame(&out, &payload);
    EXPECT_EQ(1u, frame.payload_size);
    EXPECT_EQ(0, frame.flags & 0x1);
    EXPECT_EQ("e", payload);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(5u, sctx->_pending_data.size());
    EXPECT_EQ(5u, ctx->_pending_data_size);
    EXPECT_EQ(1, ctx->_remote_window_left);
    EXPECT_EQ(0, sctx->_remote_window_left);

    ctx->_remote_window_left.fetch_add(3, butil::memory_order_relaxed);
    sctx->_remote_window_left.fetch_add(3, butil::memory_order_relaxed);
    {
        std::unique_lock<butil::Mutex> mu(ctx->_stream_mutex);
        ctx->AppendPendingDataLocked(sctx, &out);
    }
    frame = PopFrame(&out, &payload);
    EXPECT_EQ(3u, frame.payload_size);
    EXPECT_EQ(0, frame.flags & 0x1);
    EXPECT_EQ("fgh", payload);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(2u, ctx->_pending_data_size);

    ctx->_remote_window_left.fetch_add(2, butil::memory_order_relaxed);
    sctx->_remote_window_left.fetch_add(2, butil::memory_order_relaxed);
    {
        std::unique_lock<butil::Mutex> mu(ctx->_stream_mutex);
        ctx->AppendPendingDataLocked(sctx, &out);
    }
    frame = PopFrame(&out, &payload);
    EXPECT_EQ(2u, frame.payload_size);
    EXPECT_NE(0, frame.flags & 0x1);
    EXPECT_EQ("ij", payload);
    EXPECT_TRUE(out.empty());
    EXPECT_TRUE(sctx->_pending_data.empty());
    EXPECT_EQ(0u, ctx->_pending_data_size);
}

TEST(H2UnsentMessage, request_does_not_fail_when_body_exceeds_window) {
    brpc::SocketId id;
    brpc::SocketUniquePtr sock;
    brpc::SocketOptions options;
    options.user = brpc::get_client_side_messenger();
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    ASSERT_EQ(0, brpc::Socket::Address(id, &sock));

    brpc::policy::H2Context* ctx =
        new brpc::policy::H2Context(sock.get(), nullptr);
    ASSERT_EQ(0, ctx->Init());
    sock->initialize_parsing_context(&ctx);
    ctx->_last_sent_stream_id = 1;
    ctx->_remote_settings.max_frame_size = 4;
    ctx->_remote_settings.stream_window_size = 3;
    ctx->_remote_window_left = 3;

    brpc::Controller cntl;
    cntl.http_request().uri() = "http://example.com/echo";
    cntl.request_attachment().append("abcdefghij");
    brpc::policy::H2UnsentRequest* request =
        brpc::policy::H2UnsentRequest::New(&cntl);
    ASSERT_TRUE(request != nullptr);

    butil::IOBuf out;
    const butil::Status status =
        request->AppendAndDestroySelf(&out, sock.get());
    EXPECT_TRUE(status.ok()) << status;
    brpc::policy::H2StreamContext* sctx = ctx->FindStream(1);
    ASSERT_TRUE(sctx != nullptr);
    EXPECT_EQ(7u, sctx->_pending_data.size());
    EXPECT_EQ(7u, ctx->_pending_data_size);
    EXPECT_EQ(0, ctx->_remote_window_left);
    EXPECT_EQ(0, sctx->_remote_window_left);
}

TEST(H2UnsentMessage, invalid_window_update_does_not_change_window) {
    brpc::SocketId id;
    brpc::SocketUniquePtr sock;
    brpc::SocketOptions options;
    options.user = brpc::get_client_side_messenger();
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    ASSERT_EQ(0, brpc::Socket::Address(id, &sock));

    brpc::policy::H2Context* ctx =
        new brpc::policy::H2Context(sock.get(), nullptr);
    ASSERT_EQ(0, ctx->Init());
    sock->initialize_parsing_context(&ctx);
    const int64_t max_window_size = std::numeric_limits<int32_t>::max();
    ctx->_remote_window_left = max_window_size;

    const char increment[] = {0, 0, 0, 1};
    butil::IOBuf payload;
    payload.append(increment, sizeof(increment));
    butil::IOBufBytesIterator it(payload);
    const brpc::policy::H2FrameHead frame = {
        4, brpc::policy::H2_FRAME_WINDOW_UPDATE, 0, 0};
    const brpc::policy::H2ParseResult result = ctx->OnWindowUpdate(it, frame);

    EXPECT_EQ(brpc::H2_FLOW_CONTROL_ERROR, result.error());
    EXPECT_EQ(max_window_size,
              ctx->_remote_window_left.load(butil::memory_order_relaxed));
}

TEST(H2UnsentMessage, clear_pending_data_releases_overcrowded_buffer) {
    brpc::SocketId id;
    brpc::SocketUniquePtr sock;
    brpc::SocketOptions options;
    options.user = brpc::get_client_side_messenger();
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    ASSERT_EQ(0, brpc::Socket::Address(id, &sock));

    brpc::policy::H2Context* ctx =
        new brpc::policy::H2Context(sock.get(), nullptr);
    ASSERT_EQ(0, ctx->Init());
    sock->initialize_parsing_context(&ctx);
    ctx->_remote_window_left = 0;

    butil::IOBuf body;
    body.append("pending");
    butil::IOBuf out;
    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    brpc::FLAGS_socket_max_unwritten_bytes = body.size();

    std::unique_ptr<brpc::policy::H2StreamContext> sctx(
        new brpc::policy::H2StreamContext(false));
    sctx->Init(ctx, 1);
    ASSERT_TRUE(
        ctx->TryToInsertClientStream(1, sctx.get(), body, &out).ok());
    brpc::policy::H2StreamContext* inserted_sctx = sctx.release();
    ASSERT_TRUE(out.empty());
    ASSERT_EQ(body.size(), ctx->_pending_data_size);
    EXPECT_TRUE(ctx->PendingDataOvercrowded());

    std::unique_ptr<brpc::policy::H2StreamContext> rejected_sctx(
        new brpc::policy::H2StreamContext(false));
    rejected_sctx->Init(ctx, 3);
    butil::IOBuf rejected_body;
    rejected_body.append("x");
    const butil::Status rejected = ctx->TryToInsertClientStream(
        3, rejected_sctx.get(), rejected_body, &out);
    EXPECT_EQ(brpc::EOVERCROWDED, rejected.error_code());
    EXPECT_EQ(nullptr, ctx->FindStream(3));
    EXPECT_EQ(body.size(), ctx->_pending_data_size);

    ctx->ClearPendingData(1);
    EXPECT_FALSE(ctx->PendingDataOvercrowded());

    EXPECT_TRUE(inserted_sctx->_pending_data.empty());
    EXPECT_EQ(0u, ctx->_pending_data_size);
}

TEST(H2UnsentMessage, request_throughput) {
    brpc::Controller cntl;
    butil::IOBuf request_buf;
    cntl.http_request().uri() = "0.0.0.0:8010/HttpService/Echo";
    brpc::policy::SerializeHttpRequest(&request_buf, &cntl, NULL);

    brpc::SocketId id;
    brpc::SocketUniquePtr h2_client_sock;
    brpc::SocketOptions h2_client_options;
    h2_client_options.user = brpc::get_client_side_messenger();
    EXPECT_EQ(0, brpc::Socket::Create(h2_client_options, &id));
    EXPECT_EQ(0, brpc::Socket::Address(id, &h2_client_sock));

    brpc::policy::H2Context* ctx =
        new brpc::policy::H2Context(h2_client_sock.get(), NULL);
    CHECK_EQ(ctx->Init(), 0);
    h2_client_sock->initialize_parsing_context(&ctx);
    ctx->_last_sent_stream_id = 0;
    ctx->_remote_window_left = brpc::H2Settings::MAX_WINDOW_SIZE;

    int64_t ntotal = 500000;

    // calc H2UnsentRequest throughput
    butil::IOBuf dummy_buf;
    ProfilerStart("h2_unsent_req.prof");
    int64_t start_us = butil::cpuwide_time_us();
    for (int i = 0; i < ntotal; ++i) {
        brpc::policy::H2UnsentRequest* req = brpc::policy::H2UnsentRequest::New(&cntl);
        req->AppendAndDestroySelf(&dummy_buf, h2_client_sock.get());
    }
    int64_t end_us = butil::cpuwide_time_us();
    ProfilerStop();
    int64_t elapsed = end_us - start_us;
    LOG(INFO) << "H2UnsentRequest average qps="
        << (ntotal * 1000000L) / elapsed << "/s, data throughput="
        << dummy_buf.size() * 1000000L / elapsed << "/s";

    // calc H2UnsentResponse throughput
    dummy_buf.clear();
    start_us = butil::cpuwide_time_us();
    for (int i = 0; i < ntotal; ++i) {
        // H2UnsentResponse::New would release cntl.http_response() and swap
        // cntl.response_attachment()
        cntl.http_response().set_content_type("text/plain");
        cntl.response_attachment().append("0123456789abcedef");
        brpc::policy::H2UnsentResponse* res = brpc::policy::H2UnsentResponse::New(&cntl, 0, false);
        res->AppendAndDestroySelf(&dummy_buf, h2_client_sock.get());
    }
    end_us = butil::cpuwide_time_us();
    elapsed = end_us - start_us;
    LOG(INFO) << "H2UnsentResponse average qps="
        << (ntotal * 1000000L) / elapsed << "/s, data throughput="
        << dummy_buf.size() * 1000000L / elapsed << "/s";
}
