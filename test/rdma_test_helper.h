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

#ifndef BRPC_TEST_RDMA_TEST_HELPER_H
#define BRPC_TEST_RDMA_TEST_HELPER_H

#include <unistd.h>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "butil/atomicops.h"
#include "butil/endpoint.h"
#include "butil/files/temp_file.h"
#include "butil/iobuf.h"
#include "butil/time.h"
#include "brpc/acceptor.h"
#include "brpc/channel.h"
#include "brpc/closure_guard.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/socket.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma_transport.h"
#include "echo.pb.h"

namespace brpc {
namespace rdma_test {

static std::string g_ip = "127.0.0.1";
static butil::EndPoint g_ep;

// Number of Echo requests the server has actually served. The churn tests below
// cut the connection while requests are in flight, and from the client side
// "the server never saw this request" is indistinguishable from "the server
// answered it and the reply died with the connection" -- both just look like a
// failed RPC. Only this counter tells the two apart, and it is the server having
// real work in flight that makes the race those tests hunt reachable at all.
static butil::atomic<int> g_echo_served(0);

// The server side runs in its own threads, so the only thing a test can rely on
// is that an expected transition happens *eventually*. Polling with a generous
// upper bound keeps the common case as fast as the machine allows and does not
// turn into a flake when CI is loaded, which a fixed sleep does.
static const int64_t WAIT_TIMEOUT_US = 5000000;
static const int64_t WAIT_INTERVAL_US = 1000;

// Returns true if `pred` turned true before the timeout expired.
static bool WaitUntil(const std::function<bool()>& pred,
                      int64_t timeout_us = WAIT_TIMEOUT_US) {
    const int64_t deadline = butil::gettimeofday_us() + timeout_us;
    while (!pred()) {
        if (butil::gettimeofday_us() >= deadline) {
            return false;
        }
        usleep((useconds_t)WAIT_INTERVAL_US);
    }
    return true;
}

class MyEchoService : public ::test::EchoService {
    void Echo(google::protobuf::RpcController* cntl_base,
              const ::test::EchoRequest* req,
              ::test::EchoResponse* res,
              google::protobuf::Closure* done) {
        Controller* cntl = static_cast<Controller*>(cntl_base);
        ClosureGuard done_guard(done);
        g_echo_served.fetch_add(1, butil::memory_order_relaxed);
        if (req->server_fail()) {
            cntl->SetFailed(req->server_fail(), "Server fail1");
            cntl->SetFailed(req->server_fail(), "Server fail2");
            return;
        }
        if (req->close_fd()) {
            usleep(1);
            LOG(INFO) << "close fd...";
            cntl->CloseConnection("Close connection according to request");
            return;
        }
        if (req->sleep_us() > 0) {
            LOG(INFO) << "sleep " << req->sleep_us() << "us...";
            bthread_usleep(req->sleep_us());
        }
        res->set_message("MyEchoService");
        if (req->code() != 0) {
            res->add_code_list(req->code());
        }
        cntl->response_attachment().append(cntl->request_attachment());
    }
};

class RdmaTest : public ::testing::Test {
protected:
    explicit RdmaTest(int port = 8713) {
        butil::ip_t ip;
        EXPECT_EQ(0, butil::str2ip(g_ip.c_str(), &ip));
        butil::EndPoint ep(ip, port);
        g_ep = ep;
        EXPECT_EQ(0, _server_list.save(butil::endpoint2str(g_ep).c_str()));
        _naming_url = std::string("File://") + _server_list.fname();
        _server.AddService(&_svc, SERVER_DOESNT_OWN_SERVICE);
    }

    ~RdmaTest() { }

    virtual void SetUp() { }

    virtual void TearDown() {
        rdma::DumpMemoryPoolInfo(std::cout);
    }

protected:
    void StartServer(bool use_rdma = true) {
        ServerOptions options;
        options.enabled_protocols = "baidu_std";
        options.socket_mode = use_rdma ? SOCKET_MODE_RDMA : SOCKET_MODE_TCP;
        options.idle_timeout_sec = 5;
        options.max_concurrency = 0;
        options.internal_port = -1;
        EXPECT_EQ(0, _server.Start(g_ep.port, &options));
    }

    void StopServer() {
        _server.Stop(0);
        _server.Join();
    }

    Socket* GetSocketFromServer(size_t index) {
        std::vector<SocketId> sids;
        _server._am->ListConnections(&sids);
        if (index >= sids.size()) {
            return nullptr;
        }
        SocketUniquePtr s;
        if (Socket::Address(sids[index], &s) == 0) {
            return s.get();
        }
        return nullptr;
    }

    // Accepting the connection happens in the server threads, so poll for it
    // rather than sleeping. Returns nullptr if it never showed up.
    Socket* WaitForServerSocket() {
        Socket* s = nullptr;
        WaitUntil([this, &s] { return (s = GetSocketFromServer(0)) != nullptr; });
        return s;
    }

    // Match the current client connection, not the first acceptor entry. A
    // previous connection may still be waiting for its close event to run.
    SocketUniquePtr WaitForServerSocket(SocketId client_id) {
        SocketUniquePtr client;
        if (Socket::Address(client_id, &client) != 0) {
            return {};
        }
        butil::EndPoint peer = client->local_side();
        SocketUniquePtr result;
        WaitUntil([this, &result, &peer] {
            std::vector<SocketId> sids;
            _server._am->ListConnections(&sids);
            for (SocketId sid : sids) {
                SocketUniquePtr socket;
                if (Socket::Address(sid, &socket) == 0 &&
                    socket->remote_side() == peer) {
                    result = std::move(socket);
                    return true;
                }
            }
            return false;
        });
        return result;
    }

    // Ditto for the connection going away.
    bool WaitForServerSocketGone() {
        return WaitUntil([this] { return GetSocketFromServer(0) == nullptr; });
    }

    butil::TempFile _server_list;
    std::string _naming_url;

    Server _server;
    MyEchoService _svc;
};

// Shorthand for the RDMA transport behind a Socket, which every endpoint state
// check below has to go through.
static RdmaTransport* RdmaTransportOf(Socket* s) {
    return static_cast<RdmaTransport*>(s->_transport.get());
}

static RdmaTransport* RdmaTransportOf(const SocketUniquePtr& s) {
    return RdmaTransportOf(s.get());
}

// Returns the number of RPCs that succeeded. A test that severs the connection
// cannot predict which ones make it, but the ones that do must still be right,
// so failures are tolerated here and the caller decides how many it demands.
static int SendEchoRpcs(Channel& channel, int rpc_num, size_t attach_size,
                        const std::function<void(Controller&)>& disturb = nullptr,
                        int disturb_at = 0) {
    std::vector<Controller> cntl(rpc_num);
    std::vector<::test::EchoRequest> req(rpc_num);
    std::vector<::test::EchoResponse> res(rpc_num);
    std::vector<butil::IOBuf> attach(rpc_num);
    for (int i = 0; i < rpc_num; ++i) {
        req[i].set_message("hello");
        req[i].set_code(i + 1);
        if (attach_size > 0) {
            EXPECT_EQ(0, attach[i].resize(
                attach_size, static_cast<char>('a' + i % 26)));
            cntl[i].request_attachment().append(attach[i]);
        }
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], DoNothing());
        if (disturb && i == disturb_at) {
            disturb(cntl[i]);
        }
    }
    int succeeded = 0;
    for (int i = 0; i < rpc_num; ++i) {
        bthread_id_join(cntl[i].call_id());
        if (cntl[i].Failed()) {
            continue;
        }
        ++succeeded;
        EXPECT_EQ("MyEchoService", res[i].message()) << "rpc[" << i << "]";
        EXPECT_EQ(1, res[i].code_list_size()) << "rpc[" << i << "]";
        if (res[i].code_list_size() == 1) {
            EXPECT_EQ(i + 1, res[i].code_list(0)) << "rpc[" << i << "]";
        }
        EXPECT_EQ(attach_size, cntl[i].response_attachment().size()) << "rpc[" << i << "]";
        EXPECT_TRUE(attach[i].equals(cntl[i].response_attachment())) << "rpc[" << i << "]";
    }
    return succeeded;
}

}  // namespace rdma_test
}  // namespace brpc

#endif  // BRPC_TEST_RDMA_TEST_HELPER_H
