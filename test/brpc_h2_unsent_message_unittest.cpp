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
    ctx->_last_sent_stream_id = 0;
    ctx->_remote_window_left = brpc::H2Settings::MAX_WINDOW_SIZE;

    int64_t ntotal = 500000;

    // calc H2UnsentRequest throughput
    butil::IOBuf dummy_buf;
    ProfilerStart("h2_unsent_req.prof");
    int64_t start_us = butil::gettimeofday_us();
    for (int i = 0; i < ntotal; ++i) {
        brpc::policy::H2UnsentRequest* req = brpc::policy::H2UnsentRequest::New(&cntl);
        req->AppendAndDestroySelf(&dummy_buf, h2_client_sock.get());
    }
    int64_t end_us = butil::gettimeofday_us();
    ProfilerStop();
    int64_t elapsed = end_us - start_us;
    LOG(INFO) << "H2UnsentRequest average qps="
        << (ntotal * 1000000L) / elapsed << "/s, data throughput="
        << dummy_buf.size() * 1000000L / elapsed << "/s";

    // calc H2UnsentResponse throughput
    dummy_buf.clear();
    start_us = butil::gettimeofday_us();
    for (int i = 0; i < ntotal; ++i) {
        // H2UnsentResponse::New would release cntl.http_response() and swap
        // cntl.response_attachment()
        cntl.http_response().set_content_type("text/plain");
        cntl.response_attachment().append("0123456789abcedef");
        brpc::policy::H2UnsentResponse* res = brpc::policy::H2UnsentResponse::New(&cntl, 0, false);
        res->AppendAndDestroySelf(&dummy_buf, h2_client_sock.get());
    }
    end_us = butil::gettimeofday_us();
    elapsed = end_us - start_us;
    LOG(INFO) << "H2UnsentResponse average qps="
        << (ntotal * 1000000L) / elapsed << "/s, data throughput="
        << dummy_buf.size() * 1000000L / elapsed << "/s";
}
