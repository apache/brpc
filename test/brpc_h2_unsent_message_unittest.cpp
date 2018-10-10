// brpc - A framework to host and access services throughout Baidu.
// Copyright (c) 2018 BiliBili, Inc.

// Date: Tue Oct 9 20:27:18 CST 2018

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "bthread/bthread.h"
#include "butil/atomicops.h"
#include "brpc/policy/http_rpc_protocol.h"
#include "brpc/policy/http2_rpc_protocol.h"
#include "butil/gperftools_profiler.h"

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
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
    ctx->_last_client_stream_id = 0;
    ctx->_remote_window_left = brpc::H2Settings::MAX_WINDOW_SIZE;

    int64_t ntotal = 1000000;

    // calc H2UnsentRequest throughput
    std::vector<brpc::policy::H2UnsentRequest*> req_msgs;
    req_msgs.resize(ntotal);
    for (int i = 0; i < ntotal; ++i) {
        req_msgs[i] = brpc::policy::H2UnsentRequest::New(&cntl);
    }
    butil::IOBuf dummy_buf;
    ProfilerStart("h2_unsent_req.prof");
    int64_t start_us = butil::gettimeofday_us();
    for (int i = 0; i < ntotal; ++i) {
        req_msgs[i]->AppendAndDestroySelf(&dummy_buf, h2_client_sock.get());
    }
    int64_t end_us = butil::gettimeofday_us();
    ProfilerStop();
    int64_t elapsed = end_us - start_us;
    LOG(INFO) << "H2UnsentRequest average qps="
        << (ntotal * 1000000L) / elapsed << "/s, data throughput="
        << dummy_buf.size() * 1000000L / elapsed;
    req_msgs.clear();

    // calc H2UnsentResponse throughput
    std::vector<brpc::policy::H2UnsentResponse*> res_msgs;
    res_msgs.resize(ntotal);
    for (int i = 0; i < ntotal; ++i) {
        cntl.http_response().set_content_type("text/plain");
        cntl.response_attachment().append("0123456789abcedef");
        res_msgs[i] = brpc::policy::H2UnsentResponse::New(&cntl, 0, false);
    }
    dummy_buf.clear();
    start_us = butil::gettimeofday_us();
    for (int i = 0; i < ntotal; ++i) {
        res_msgs[i]->AppendAndDestroySelf(&dummy_buf, h2_client_sock.get());
    }
    end_us = butil::gettimeofday_us();
    elapsed = end_us - start_us;
    LOG(INFO) << "H2UnsentResponse average qps="
        << (ntotal * 1000000L) / elapsed << "/s, data throughput="
        << dummy_buf.size() * 1000000L / elapsed;
    res_msgs.clear();
}
