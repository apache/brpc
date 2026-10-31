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
#include "brpc/server.h"
#include "brpc/policy/http_rpc_protocol.h"
#include "brpc/policy/http2_rpc_protocol.h"
#include "gperftools_helper.h"

namespace brpc {
DECLARE_int64(socket_max_unwritten_bytes);
}

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    brpc::get_or_new_client_side_messenger();
    return RUN_ALL_TESTS();
}

namespace {

const uint8_t H2_FLAGS_END_STREAM = 0x01;
const uint8_t H2_FLAGS_END_HEADERS = 0x04;

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
    EXPECT_EQ(5u, ctx->_pending_data_size.load());
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
    EXPECT_EQ(2u, ctx->_pending_data_size.load());

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
    EXPECT_EQ(0u, ctx->_pending_data_size.load());
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
    EXPECT_EQ(7u, ctx->_pending_data_size.load());
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
    {
        butil::IOBuf payload;
        payload.append(increment, sizeof(increment));
        butil::IOBufBytesIterator it(payload);
        const brpc::policy::H2FrameHead frame = {
            4, brpc::policy::H2_FRAME_WINDOW_UPDATE, 0, 0};
        const brpc::policy::H2ParseResult result = ctx->OnWindowUpdate(it, frame);

        EXPECT_EQ(brpc::H2_FLOW_CONTROL_ERROR, result.error());
        EXPECT_EQ(0, result.stream_id());
        EXPECT_EQ(max_window_size,
                  ctx->_remote_window_left.load(butil::memory_order_relaxed));
    }

    // Stream-level WINDOW_UPDATE overflow must return a stream error (stream_id = 1)
    // per RFC 7540 Section 6.9.1 rather than a connection error.
    brpc::policy::H2StreamContext* sctx =
        new brpc::policy::H2StreamContext(false);
    sctx->Init(ctx, 1);
    ASSERT_EQ(0, ctx->TryToInsertStream(1, sctx));
    sctx->_remote_window_left = max_window_size;
    {
        butil::IOBuf payload;
        payload.append(increment, sizeof(increment));
        butil::IOBufBytesIterator it(payload);
        const brpc::policy::H2FrameHead frame = {
            4, brpc::policy::H2_FRAME_WINDOW_UPDATE, 0, 1};
        const brpc::policy::H2ParseResult result = ctx->OnWindowUpdate(it, frame);

        EXPECT_EQ(brpc::H2_FLOW_CONTROL_ERROR, result.error());
        EXPECT_EQ(1, result.stream_id());
        EXPECT_EQ(max_window_size,
                  sctx->_remote_window_left.load(butil::memory_order_relaxed));
    }
    sctx->Destroy();
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
    ASSERT_EQ(body.size(), ctx->_pending_data_size.load());
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
    EXPECT_EQ(body.size(), ctx->_pending_data_size.load());

    EXPECT_TRUE(ctx->ClearPendingData(1));
    EXPECT_FALSE(ctx->ClearPendingData(1));
    EXPECT_FALSE(ctx->PendingDataOvercrowded());

    EXPECT_TRUE(inserted_sctx->_pending_data.empty());
    EXPECT_EQ(0u, ctx->_pending_data_size.load());
}

TEST(H2UnsentMessage, request_throughput) {
    brpc::Controller cntl;
    butil::IOBuf request_buf;
    cntl.http_request().uri() = "0.0.0.0:8010/HttpService/Echo";
    brpc::policy::SerializeHttpRequest(&request_buf, &cntl, nullptr);

    brpc::SocketId id;
    brpc::SocketUniquePtr h2_client_sock;
    brpc::SocketOptions h2_client_options;
    h2_client_options.user = brpc::get_client_side_messenger();
    EXPECT_EQ(0, brpc::Socket::Create(h2_client_options, &id));
    EXPECT_EQ(0, brpc::Socket::Address(id, &h2_client_sock));

    brpc::policy::H2Context* ctx =
        new brpc::policy::H2Context(h2_client_sock.get(), nullptr);
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
    brpc::Server server;
    brpc::SocketId srv_id;
    brpc::SocketUniquePtr h2_server_sock;
    brpc::SocketOptions h2_server_options;
    EXPECT_EQ(0, brpc::Socket::Create(h2_server_options, &srv_id));
    EXPECT_EQ(0, brpc::Socket::Address(srv_id, &h2_server_sock));

    brpc::policy::H2Context* srv_ctx =
        new brpc::policy::H2Context(h2_server_sock.get(), &server);
    CHECK_EQ(srv_ctx->Init(), 0);
    h2_server_sock->initialize_parsing_context(&srv_ctx);
    srv_ctx->_unack_local_settings.max_concurrent_streams =
        std::numeric_limits<uint32_t>::max();
    srv_ctx->_remote_settings.stream_window_size = brpc::H2Settings::MAX_WINDOW_SIZE;
    srv_ctx->_remote_window_left = brpc::H2Settings::MAX_WINDOW_SIZE;
    for (int i = 0; i < ntotal; ++i) {
        const int stream_id = 2 * i + 1;
        brpc::policy::H2StreamContext* sctx =
            new brpc::policy::H2StreamContext(false);
        sctx->Init(srv_ctx, stream_id);
        sctx->_remote_ended = true;
        CHECK_EQ(0, srv_ctx->TryToInsertStream(stream_id, sctx));
    }

    dummy_buf.clear();
    start_us = butil::cpuwide_time_us();
    for (int i = 0; i < ntotal; ++i) {
        // H2UnsentResponse::New would release cntl.http_response() and swap
        // cntl.response_attachment()
        cntl.http_response().set_content_type("text/plain");
        cntl.response_attachment().append("0123456789abcedef");
        brpc::policy::H2UnsentResponse* res =
            brpc::policy::H2UnsentResponse::New(&cntl, 2 * i + 1, false);
        res->AppendAndDestroySelf(&dummy_buf, h2_server_sock.get());
    }
    end_us = butil::cpuwide_time_us();
    elapsed = end_us - start_us;
    LOG(INFO) << "H2UnsentResponse average qps="
        << (ntotal * 1000000L) / elapsed << "/s, data throughput="
        << dummy_buf.size() * 1000000L / elapsed << "/s";
}

TEST(H2UnsentMessage, split_response_data_and_defer_grpc_trailer_by_remote_window) {
    brpc::Server server;
    brpc::SocketId id;
    brpc::SocketUniquePtr sock;
    brpc::SocketOptions options;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    ASSERT_EQ(0, brpc::Socket::Address(id, &sock));
    ASSERT_TRUE( !sock->CreatedByConnect());

    brpc::policy::H2Context* ctx =
        new brpc::policy::H2Context(sock.get(), &server);
    ASSERT_EQ(0, ctx->Init());
    sock->initialize_parsing_context(&ctx);
    ctx->_remote_settings.max_frame_size = 4;
    ctx->_remote_settings.stream_window_size = 5;
    ctx->_remote_window_left = 6;

    brpc::policy::H2StreamContext* sctx =
        new brpc::policy::H2StreamContext(false);
    sctx->Init(ctx, 1);
    ASSERT_EQ(0, ctx->TryToInsertStream(1, sctx));
    // Simulate OnEndStream() + Stage 2 ProcessHttpRequest consuming its ref
    sctx->_remote_ended = true;

    brpc::Controller cntl;
    cntl.http_response().set_content_type("application/grpc");
    cntl.response_attachment().append("abcdefghij");
    brpc::policy::H2UnsentResponse* response =
        brpc::policy::H2UnsentResponse::New(&cntl, 1, true /* is_grpc */);
    ASSERT_TRUE(response != nullptr);

    butil::IOBuf out;
    ASSERT_TRUE(response->AppendAndDestroySelf(&out, sock.get()).ok());

    std::string payload;
    // Frame 1+: initial HEADERS (+ CONTINUATION if max_frame_size=4 splits it)
    brpc::policy::H2FrameHead frame = PopFrame(&out, &payload);
    EXPECT_EQ(brpc::policy::H2_FRAME_HEADERS, frame.type);
    EXPECT_EQ(1, frame.stream_id);
    while ((frame.flags & H2_FLAGS_END_HEADERS) == 0) {
        frame = PopFrame(&out, &payload);
        EXPECT_EQ(brpc::policy::H2_FRAME_CONTINUATION, frame.type);
        EXPECT_EQ(1, frame.stream_id);
    }
    EXPECT_EQ(0, frame.flags & H2_FLAGS_END_STREAM);

    // Next Frame: DATA "abcd" (4 bytes)
    frame = PopFrame(&out, &payload);
    EXPECT_EQ(brpc::policy::H2_FRAME_DATA, frame.type);
    EXPECT_EQ(4u, frame.payload_size);
    EXPECT_EQ(0, frame.flags & H2_FLAGS_END_STREAM);
    EXPECT_EQ("abcd", payload);

    // Next Frame: DATA "e" (1 byte, limited by stream window 5)
    frame = PopFrame(&out, &payload);
    EXPECT_EQ(brpc::policy::H2_FRAME_DATA, frame.type);
    EXPECT_EQ(1u, frame.payload_size);
    EXPECT_EQ(0, frame.flags & H2_FLAGS_END_STREAM);
    EXPECT_EQ("e", payload);

    // Trailer must be deferred because 5 bytes of DATA are still pending
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(5u, sctx->_pending_data.size());
    EXPECT_EQ(5u, ctx->_pending_data_size.load());
    EXPECT_TRUE(sctx->_has_pending_trailer);
    EXPECT_EQ(1, ctx->_remote_window_left);
    EXPECT_EQ(0, sctx->_remote_window_left);

    // Partial window update (+3 bytes): sends "fgh", trailer still deferred
    ctx->_remote_window_left.fetch_add(3, butil::memory_order_relaxed);
    sctx->_remote_window_left.fetch_add(3, butil::memory_order_relaxed);
    {
        std::unique_lock<butil::Mutex> mu(ctx->_stream_mutex);
        ctx->AppendPendingDataLocked(sctx, &out);
    }
    frame = PopFrame(&out, &payload);
    EXPECT_EQ(brpc::policy::H2_FRAME_DATA, frame.type);
    EXPECT_EQ(3u, frame.payload_size);
    EXPECT_EQ(0, frame.flags & H2_FLAGS_END_STREAM);
    EXPECT_EQ("fgh", payload);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(2u, ctx->_pending_data_size.load());
    EXPECT_TRUE(sctx->_has_pending_trailer);

    // Final window update (+2 bytes): sends "ij" followed by Trailer HEADERS(END_STREAM)
    ctx->_remote_window_left.fetch_add(2, butil::memory_order_relaxed);
    sctx->_remote_window_left.fetch_add(2, butil::memory_order_relaxed);
    {
        std::unique_lock<butil::Mutex> mu(ctx->_stream_mutex);
        ctx->AppendPendingDataLocked(sctx, &out);
    }
    frame = PopFrame(&out, &payload);
    EXPECT_EQ(brpc::policy::H2_FRAME_DATA, frame.type);
    EXPECT_EQ(2u, frame.payload_size);
    EXPECT_EQ(0, frame.flags & H2_FLAGS_END_STREAM);
    EXPECT_EQ("ij", payload);

    frame = PopFrame(&out, &payload);
    EXPECT_EQ(brpc::policy::H2_FRAME_HEADERS, frame.type);
    EXPECT_NE(0, frame.flags & H2_FLAGS_END_STREAM);
    EXPECT_EQ(1, frame.stream_id);
    while ((frame.flags & H2_FLAGS_END_HEADERS) == 0) {
        frame = PopFrame(&out, &payload);
        EXPECT_EQ(brpc::policy::H2_FRAME_CONTINUATION, frame.type);
        EXPECT_EQ(1, frame.stream_id);
    }
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(0u, ctx->_pending_data_size.load());
}

TEST(H2UnsentMessage, deferred_trailer_does_not_corrupt_cross_stream_hpack) {
    brpc::Server server;
    brpc::SocketId id;
    brpc::SocketUniquePtr sock;
    brpc::SocketOptions options;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    ASSERT_EQ(0, brpc::Socket::Address(id, &sock));

    brpc::policy::H2Context* server_ctx =
        new brpc::policy::H2Context(sock.get(), &server);
    ASSERT_EQ(0, server_ctx->Init());
    sock->initialize_parsing_context(&server_ctx);
    server_ctx->_remote_settings.stream_window_size = 0;
    server_ctx->_remote_window_left = 100;

    // Stream 1: window is 0, so its DATA and gRPC Trailer are deferred
    brpc::policy::H2StreamContext* sctx1 =
        new brpc::policy::H2StreamContext(false);
    sctx1->Init(server_ctx, 1);
    ASSERT_EQ(0, server_ctx->TryToInsertStream(1, sctx1));
    sctx1->_remote_ended = true;

    // Stream 3: has sufficient window (20 bytes), sends HEADERS + DATA + Trailer immediately
    brpc::policy::H2StreamContext* sctx3 =
        new brpc::policy::H2StreamContext(false);
    sctx3->Init(server_ctx, 3);
    ASSERT_EQ(0, server_ctx->TryToInsertStream(3, sctx3));
    sctx3->_remote_window_left = 20;
    sctx3->_remote_ended = true;

    butil::IOBuf wire_buf;
    {
        brpc::Controller cntl1;
        cntl1.http_response().set_content_type("application/grpc");
        cntl1.http_response().SetHeader("x-custom-stream", "stream-one");
        cntl1.response_attachment().append("body1");
        brpc::policy::H2UnsentResponse* res1 =
            brpc::policy::H2UnsentResponse::New(&cntl1, 1, true);
        ASSERT_TRUE(res1->AppendAndDestroySelf(&wire_buf, sock.get()).ok());
    }
    {
        brpc::Controller cntl3;
        cntl3.http_response().set_content_type("application/grpc");
        cntl3.http_response().SetHeader("x-custom-stream", "stream-three");
        cntl3.response_attachment().append("body3");
        brpc::policy::H2UnsentResponse* res3 =
            brpc::policy::H2UnsentResponse::New(&cntl3, 3, true);
        ASSERT_TRUE(res3->AppendAndDestroySelf(&wire_buf, sock.get()).ok());
    }
    EXPECT_EQ(nullptr, server_ctx->FindStream(3));
    ASSERT_NE(nullptr, server_ctx->FindStream(1));

    // Now replenish Stream 1's window and flush its DATA + deferred Trailer
    sctx1->_remote_window_left.fetch_add(10, butil::memory_order_relaxed);
    {
        std::unique_lock<butil::Mutex> mu(server_ctx->_stream_mutex);
        server_ctx->AppendPendingDataLocked(sctx1, &wire_buf);
    }

    // Decode all HEADERS frames in wire transmission order using a client HPacker
    brpc::HPacker client_hpacker;
    ASSERT_EQ(0, client_hpacker.Init(brpc::H2Settings::DEFAULT_HEADER_TABLE_SIZE));
    int trailer_stream1_count = 0;
    while (!wire_buf.empty()) {
        std::string payload;
        brpc::policy::H2FrameHead frame = PopFrame(&wire_buf, &payload);
        if (frame.type == brpc::policy::H2_FRAME_HEADERS) {
            butil::IOBuf hdr_buf;
            hdr_buf.append(payload);
            butil::IOBufBytesIterator it(hdr_buf);
            while (it != NULL) {
                brpc::HPacker::Header h;
                ASSERT_GT(client_hpacker.Decode(it, &h), 0);
                if (frame.stream_id == 1 &&
                    (frame.flags & H2_FLAGS_END_STREAM) &&
                    h.name == "grpc-status") {
                    EXPECT_EQ("0", h.value);
                    ++trailer_stream1_count;
                }
            }
        }
    }
    EXPECT_EQ(1, trailer_stream1_count);
}

TEST(H2UnsentMessage, server_pending_response_cleaned_up_on_rst_stream) {
    brpc::Server server;
    brpc::SocketId id;
    brpc::SocketUniquePtr sock;
    brpc::SocketOptions options;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    ASSERT_EQ(0, brpc::Socket::Address(id, &sock));

    brpc::policy::H2Context* ctx =
        new brpc::policy::H2Context(sock.get(), &server);
    ASSERT_EQ(0, ctx->Init());
    sock->initialize_parsing_context(&ctx);
    ctx->_remote_window_left = 0;

    brpc::policy::H2StreamContext* sctx =
        new brpc::policy::H2StreamContext(false);
    sctx->Init(ctx, 1);
    ASSERT_EQ(0, ctx->TryToInsertStream(1, sctx));
    sctx->_remote_ended = true;

    brpc::Controller cntl;
    cntl.http_response().set_content_type("application/grpc");
    cntl.response_attachment().append("0123456789");
    brpc::policy::H2UnsentResponse* response =
        brpc::policy::H2UnsentResponse::New(&cntl, 1, true);
    butil::IOBuf out;
    ASSERT_TRUE(response->AppendAndDestroySelf(&out, sock.get()).ok());
    EXPECT_EQ(10u, ctx->_pending_data_size.load());
    ASSERT_NE(nullptr, ctx->FindStream(1));

    // Simulate client sending RST_STREAM(CANCEL) for stream 1
    const char rst_payload[] = {0, 0, 0, 8 /* H2_CANCEL */};
    butil::IOBuf payload_buf;
    payload_buf.append(rst_payload, sizeof(rst_payload));
    butil::IOBufBytesIterator it(payload_buf);
    const brpc::policy::H2FrameHead rst_head = {
        4, brpc::policy::H2_FRAME_RST_STREAM, 0, 1};
    brpc::policy::H2ParseResult res = ctx->OnResetStream(it, rst_head);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(nullptr, ctx->FindStream(1));
    EXPECT_EQ(0u, ctx->_pending_data_size.load());
}

TEST(H2UnsentMessage, server_pending_response_overcrowded_sends_rst_stream) {
    brpc::Server server;
    brpc::SocketId id;
    brpc::SocketUniquePtr sock;
    brpc::SocketOptions options;
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    ASSERT_EQ(0, brpc::Socket::Address(id, &sock));

    brpc::policy::H2Context* ctx =
        new brpc::policy::H2Context(sock.get(), &server);
    ASSERT_EQ(0, ctx->Init());
    sock->initialize_parsing_context(&ctx);
    ctx->_remote_window_left = 0;

    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    brpc::FLAGS_socket_max_unwritten_bytes = 8;

    // Stream 1 queues 8 bytes, reaching socket_max_unwritten_bytes
    brpc::policy::H2StreamContext* sctx1 =
        new brpc::policy::H2StreamContext(false);
    sctx1->Init(ctx, 1);
    ASSERT_EQ(0, ctx->TryToInsertStream(1, sctx1));
    sctx1->_remote_ended = true;

    brpc::Controller cntl1;
    cntl1.response_attachment().append("12345678");
    brpc::policy::H2UnsentResponse* res1 =
        brpc::policy::H2UnsentResponse::New(&cntl1, 1, false);
    butil::IOBuf out1;
    ASSERT_TRUE(res1->AppendAndDestroySelf(&out1, sock.get()).ok());
    EXPECT_EQ(8u, ctx->_pending_data_size.load());
    EXPECT_TRUE(ctx->PendingDataOvercrowded());

    // Stream 3 arrives while overcrowded -> emits RST_STREAM(ENHANCE_YOUR_CALM)
    brpc::policy::H2StreamContext* sctx3 =
        new brpc::policy::H2StreamContext(false);
    sctx3->Init(ctx, 3);
    ASSERT_EQ(0, ctx->TryToInsertStream(3, sctx3));
    sctx3->_remote_ended = true;

    brpc::Controller cntl3;
    cntl3.response_attachment().append("abcdefgh");
    brpc::policy::H2UnsentResponse* res3 =
        brpc::policy::H2UnsentResponse::New(&cntl3, 3, true);
    butil::IOBuf out3;
    ASSERT_TRUE(res3->AppendAndDestroySelf(&out3, sock.get()).ok());

    std::string payload;
    brpc::policy::H2FrameHead frame = PopFrame(&out3, &payload);
    EXPECT_EQ(brpc::policy::H2_FRAME_RST_STREAM, frame.type);
    EXPECT_EQ(3, frame.stream_id);
    EXPECT_EQ(4u, frame.payload_size);
    EXPECT_TRUE(out3.empty());
    EXPECT_EQ(nullptr, ctx->FindStream(3));
    EXPECT_EQ(8u, ctx->_pending_data_size.load());
}

TEST(H2UnsentMessage, flush_pending_data_round_robin_fairness) {
    brpc::Server server;
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    brpc::SocketId id;
    brpc::SocketUniquePtr sock;
    brpc::SocketOptions options;
    options.fd = fds[1];
    ASSERT_EQ(0, brpc::Socket::Create(options, &id));
    ASSERT_EQ(0, brpc::Socket::Address(id, &sock));

    brpc::policy::H2Context* ctx =
        new brpc::policy::H2Context(sock.get(), &server);
    ASSERT_EQ(0, ctx->Init());
    sock->initialize_parsing_context(&ctx);
    ctx->_remote_settings.max_frame_size = 4;
    ctx->_remote_settings.stream_window_size = 16;
    ctx->_remote_window_left = 0;

    for (int stream_id : {1, 3}) {
        brpc::policy::H2StreamContext* sctx =
            new brpc::policy::H2StreamContext(false);
        sctx->Init(ctx, stream_id);
        ASSERT_EQ(0, ctx->TryToInsertStream(stream_id, sctx));
        sctx->_remote_ended = true;

        brpc::Controller cntl;
        cntl.response_attachment().append(stream_id == 1 ? "11112222" : "33334444");
        brpc::policy::H2UnsentResponse* res =
            brpc::policy::H2UnsentResponse::New(&cntl, stream_id, false);
        butil::IOBuf hdr_out;
        ASSERT_TRUE(res->AppendAndDestroySelf(&hdr_out, sock.get()).ok());
    }
    EXPECT_EQ(16u, ctx->_pending_data_size.load());

    // Replenish connection window by 4 bytes -> FlushPendingData flushes stream 1 ("1111")
    ctx->_remote_window_left.fetch_add(4, butil::memory_order_relaxed);
    ASSERT_TRUE(ctx->FlushPendingData(0));
    EXPECT_EQ(1, ctx->_last_flushed_stream_id);
    EXPECT_EQ(12u, ctx->_pending_data_size.load());

    // Replenish connection window by 4 bytes -> Round-robin flushes stream 3 ("3333") next!
    ctx->_remote_window_left.fetch_add(4, butil::memory_order_relaxed);
    ASSERT_TRUE(ctx->FlushPendingData(0));
    EXPECT_EQ(3, ctx->_last_flushed_stream_id);
    EXPECT_EQ(8u, ctx->_pending_data_size.load());

    // Replenish connection window by 8 bytes -> Flushes remaining stream 1 ("2222") and stream 3 ("4444")
    ctx->_remote_window_left.fetch_add(8, butil::memory_order_relaxed);
    ASSERT_TRUE(ctx->FlushPendingData(0));
    EXPECT_EQ(0u, ctx->_pending_data_size.load());
    EXPECT_EQ(nullptr, ctx->FindStream(1));
    EXPECT_EQ(nullptr, ctx->FindStream(3));

    butil::IOPortal pipe_buf;
    ASSERT_GT(pipe_buf.append_from_file_descriptor(fds[0], 1024), 0);
    std::string payload;
    brpc::policy::H2FrameHead f1 = PopFrame(&pipe_buf, &payload);
    EXPECT_EQ(1, f1.stream_id);
    EXPECT_EQ("1111", payload);
    brpc::policy::H2FrameHead f2 = PopFrame(&pipe_buf, &payload);
    EXPECT_EQ(3, f2.stream_id);
    EXPECT_EQ("3333", payload);
    brpc::policy::H2FrameHead f3 = PopFrame(&pipe_buf, &payload);
    EXPECT_EQ(1, f3.stream_id);
    EXPECT_EQ("2222", payload);
    EXPECT_NE(0, f3.flags & H2_FLAGS_END_STREAM);
    brpc::policy::H2FrameHead f4 = PopFrame(&pipe_buf, &payload);
    EXPECT_EQ(3, f4.stream_id);
    EXPECT_EQ("4444", payload);
    EXPECT_NE(0, f4.flags & H2_FLAGS_END_STREAM);
    close(fds[0]);
}

TEST(H2UnsentMessage, client_rejected_request_does_not_corrupt_preface_or_hpack) {
    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    brpc::FLAGS_socket_max_unwritten_bytes = 4;

    brpc::SocketId client_id;
    brpc::SocketUniquePtr client_sock;
    brpc::SocketOptions client_options;
    client_options.user = brpc::get_client_side_messenger();
    // CreatedByConnect() is true when remote_side is non-empty.
    ASSERT_EQ(0, butil::str2endpoint("127.0.0.1:8010", &client_options.remote_side));
    ASSERT_EQ(0, brpc::Socket::Create(client_options, &client_id));
    ASSERT_EQ(0, brpc::Socket::Address(client_id, &client_sock));

    // First request on brand-new connection exceeds (initial_window=65535 + max_unwritten=4).
    brpc::Controller cntl1;
    cntl1.http_request().uri() = "http://example.com/echo";
    cntl1.http_request().SetHeader("x-custom-dynamic-header", "first-rejected-value");
    cntl1.request_attachment().append(std::string(65535 + 16, 'x'));
    brpc::policy::H2UnsentRequest* req1 =
        brpc::policy::H2UnsentRequest::New(&cntl1);
    butil::IOBuf out1;
    butil::Status st1 = req1->AppendAndDestroySelf(&out1, client_sock.get());
    EXPECT_EQ(brpc::EOVERCROWDED, st1.error_code());
    EXPECT_TRUE(out1.empty());

    brpc::policy::H2Context* client_ctx =
        static_cast<brpc::policy::H2Context*>(client_sock->parsing_context());
    ASSERT_NE(nullptr, client_ctx);
#if defined(UNIT_TEST)
    const int initial_stream_id = 0x7fffffff - 10000;
#else
    const int initial_stream_id = 1;
#endif
    // Stream ID must not have been consumed, and preface must still be pending.
    EXPECT_EQ(initial_stream_id, client_ctx->_last_sent_stream_id);
    EXPECT_TRUE(client_ctx->_client_preface_pending);

    // Second request fits within window and must emit connection preface + SETTINGS + valid HPACK.
    brpc::Controller cntl2;
    cntl2.http_request().uri() = "http://example.com/echo";
    cntl2.http_request().SetHeader("x-custom-dynamic-header", "first-rejected-value");
    cntl2.request_attachment().append("ok");
    brpc::policy::H2UnsentRequest* req2 =
        brpc::policy::H2UnsentRequest::New(&cntl2);
    butil::IOBuf out2;
    butil::Status st2 = req2->AppendAndDestroySelf(&out2, client_sock.get());
    ASSERT_TRUE(st2.ok()) << st2;
    EXPECT_EQ(initial_stream_id + 2, client_ctx->_last_sent_stream_id);
    EXPECT_FALSE(client_ctx->_client_preface_pending);

    // Verify a fresh server H2Context can parse out2 (including preface, SETTINGS, and HPACK headers).
    brpc::Server server;
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    brpc::SocketId server_id;
    brpc::SocketUniquePtr server_sock;
    brpc::SocketOptions server_options;
    server_options.fd = fds[1];
    ASSERT_EQ(0, brpc::Socket::Create(server_options, &server_id));
    ASSERT_EQ(0, brpc::Socket::Address(server_id, &server_sock));

    brpc::ParseResult pr = brpc::policy::ParseH2Message(
        &out2, server_sock.get(), false, &server);
    ASSERT_TRUE(pr.is_ok());
    ASSERT_NE(nullptr, pr.message());
    auto* received_sctx =
        static_cast<brpc::policy::H2StreamContext*>(pr.message());
    EXPECT_EQ(initial_stream_id, received_sctx->stream_id());
    const std::string* hdr_val =
        received_sctx->header().GetHeader("x-custom-dynamic-header");
    ASSERT_NE(nullptr, hdr_val);
    EXPECT_EQ("first-rejected-value", *hdr_val);
    received_sctx->Destroy();
    close(fds[0]);
}
