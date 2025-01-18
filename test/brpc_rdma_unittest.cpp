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


#include <netinet/in.h>
#include <sys/socket.h>
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#if BRPC_WITH_RDMA
#include <google/protobuf/descriptor.h>
#include "butil/endpoint.h"
#include "butil/fd_guard.h"
#include "butil/fd_utility.h"
#include "butil/iobuf.h"
#include "butil/sys_byteorder.h"
#include "butil/files/temp_file.h"
#include "brpc/acceptor.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/socket.h"
#include "brpc/errno.pb.h"
#include "brpc/parallel_channel.h"
#include "brpc/selective_channel.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_helper.h"
#include "echo.pb.h"

static const int PORT = 8713;
static const size_t RDMA_HELLO_MSG_LEN = 40; 

using namespace brpc;

namespace brpc {

DECLARE_int64(socket_max_unwritten_bytes);
DECLARE_bool(log_idle_connection_close);
DEFINE_bool(rdma_test_enable, false, "Enable tests requring rdma runtime.");

namespace rdma {

struct HelloMessage {
    void Serialize(void* data) const;
    void Deserialize(void* data);

    uint16_t msg_len;
    uint16_t hello_ver;
    uint16_t impl_ver;
    uint16_t block_size;
    uint16_t sq_size;
    uint16_t rq_size;
    ibv_gid gid;
    uint32_t qp_num;
};

DECLARE_bool(rdma_trace_verbose);
DECLARE_int32(rdma_memory_pool_max_regions);
extern ibv_cq* (*IbvCreateCq)(ibv_context*, int, void*, ibv_comp_channel*, int);
extern int (*IbvDestroyCq)(ibv_cq*);
extern ibv_qp* (*IbvCreateQp)(ibv_pd*, ibv_qp_init_attr*);
extern int (*IbvModifyQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask);
extern int (*IbvQueryQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask, ibv_qp_init_attr*);
extern int (*IbvDestroyQp)(ibv_qp*);
extern butil::atomic<bool> g_rdma_available;
extern bool g_skip_rdma_init;
}
}

static std::string g_ip = "127.0.0.1";
static butil::EndPoint g_ep;

class MyEchoService : public ::test::EchoService {
    void Echo(google::protobuf::RpcController* cntl_base,
              const ::test::EchoRequest* req,
              ::test::EchoResponse* res,
              google::protobuf::Closure* done) {
        Controller* cntl = static_cast<Controller*>(cntl_base);
        ClosureGuard done_guard(done);
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
        res->set_message(req->message());
        if (req->code() != 0) {
            res->add_code_list(req->code());
        }
        cntl->response_attachment().append(cntl->request_attachment());
    }
};

class RdmaTest : public ::testing::Test {
protected:
    RdmaTest() {
        butil::ip_t ip;
        EXPECT_EQ(0, butil::str2ip(g_ip.c_str(), &ip));
        butil::EndPoint ep(ip, PORT);
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

private:
    void StartServer(bool use_rdma = true) {
        ServerOptions options;
        options.use_rdma = use_rdma;
        options.idle_timeout_sec = 5;
        options.max_concurrency = 0;
        options.internal_port = -1;
        EXPECT_EQ(0, _server.Start(PORT, &options));
    }

    void StopServer() {
        _server.Stop(0);
        _server.Join();
    }

    Socket* GetSocketFromServer(size_t index) {
        std::vector<SocketId> sids;
        _server._am->ListConnections(&sids);
        if (index >= sids.size()) {
            return NULL;
        }
        SocketUniquePtr s;
        if (Socket::Address(sids[index], &s) == 0) {
            return s.get();
        }
        return NULL;
    }

    butil::TempFile _server_list;
    std::string _naming_url;

    Server _server;
    MyEchoService _svc;
};

TEST_F(RdmaTest, client_close_before_hello_send) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    Socket* s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    close(sockfd);
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_magic_str) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    Socket* s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);

    uint8_t data[RDMA_HELLO_MSG_LEN];
    memcpy(data, "PRPC", 4);  // send as normal baidu_std protocol
    memset(data + 4, 0, 32);
    ASSERT_EQ(38, write(sockfd, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);

    StopServer();
}

TEST_F(RdmaTest, client_close_during_hello_send) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    Socket* s = NULL;
    uint8_t data[8];

    butil::fd_guard sockfd1(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd1 >= 0);
    ASSERT_EQ(0, connect(sockfd1, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    memcpy(data, "RD", 2);
    ASSERT_EQ(2, write(sockfd1, data, 2));  // break in magic str
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_HELLO_WAIT, s->_rdma_ep->_state);
    close(sockfd1);
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    butil::fd_guard sockfd2(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd2 >= 0);
    ASSERT_EQ(0, connect(sockfd2, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    ASSERT_EQ(4, write(sockfd2, data, 4));  // break after magic str
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_HELLO_WAIT, s->_rdma_ep->_state);
    close(sockfd2);
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    butil::fd_guard sockfd3(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd3 >= 0);
    ASSERT_EQ(0, connect(sockfd3, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    memset(data + 4, 0, 4);
    ASSERT_EQ(8, write(sockfd3, data, 8));  // break after magic str
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_HELLO_WAIT, s->_rdma_ep->_state);
    close(sockfd3);
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_len) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    Socket* s = NULL;
    uint8_t data[RDMA_HELLO_MSG_LEN];

    butil::fd_guard sockfd1(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd1 >= 0);
    ASSERT_EQ(0, connect(sockfd1, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    memset(data + 4, 0, 34);
    ASSERT_EQ(38, write(sockfd1, data, 38));  // write invalid length
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    butil::fd_guard sockfd2(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd2 >= 0);
    ASSERT_EQ(0, connect(sockfd2, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    uint16_t len = butil::HostToNet16(35);
    memcpy(data + 4, &len, 2);
    memset(data + 6, 0, 32);
    ASSERT_EQ(38, write(sockfd2, data, 38));  // write invalid length
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_version) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    Socket* s = NULL;
    uint8_t data[RDMA_HELLO_MSG_LEN];
    uint16_t len = butil::HostToNet16(38);
    uint16_t ver = butil::HostToNet16(1);

    butil::fd_guard sockfd1(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd1 >= 0);
    ASSERT_EQ(0, connect(sockfd1, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    memcpy(data + 4, &len, 2);
    memset(data + 6, 0, 32);
    memcpy(data + 6, &ver, 2);  // hello_ver == 1, impl_ver == 0
    ASSERT_EQ(38, write(sockfd1, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    memset(data, 0, 4);
    ASSERT_EQ(4, write(sockfd1, data, 4));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);
    close(sockfd1);
    usleep(100000);
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    butil::fd_guard sockfd2(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd2 >= 0);
    ASSERT_EQ(0, connect(sockfd2, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    memcpy(data + 4, &len, 2);
    memset(data + 6, 0, 32);
    memcpy(data + 8, &ver, 2);  // hello_ver == 0, impl_ver == 1
    ASSERT_EQ(38, write(sockfd2, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    uint32_t flag = butil::HostToNet32(1);
    ASSERT_EQ(4, write(sockfd2, &flag, 4));
    usleep(100000);
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_sq_rq_block_size) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    Socket* s = NULL;
    rdma::HelloMessage msg;
    uint8_t data[RDMA_HELLO_MSG_LEN];
    msg.msg_len = 38;
    msg.hello_ver = 1;
    msg.impl_ver = 1;

    msg.sq_size = 10;
    msg.rq_size = 16;
    msg.block_size = 8192;
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    butil::fd_guard sockfd1(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd1 >= 0);
    ASSERT_EQ(0, connect(sockfd1, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    ASSERT_EQ(38, write(sockfd1, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    memset(data, 0, 4);
    ASSERT_EQ(4, write(sockfd1, data, 4));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);
    close(sockfd1);

    msg.sq_size = 16;
    msg.rq_size = 10;
    msg.block_size = 8192;
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    butil::fd_guard sockfd2(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd2 >= 0);
    ASSERT_EQ(0, connect(sockfd2, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    ASSERT_EQ(38, write(sockfd2, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    memset(data, 0, 4);
    ASSERT_EQ(4, write(sockfd1, data, 4));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);
    close(sockfd2);

    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 1000;
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    butil::fd_guard sockfd3(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd3 >= 0);
    ASSERT_EQ(0, connect(sockfd3, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    ASSERT_EQ(38, write(sockfd3, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    memset(data, 0, 4);
    ASSERT_EQ(4, write(sockfd3, data, 4));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);

    StopServer();
}

TEST_F(RdmaTest, client_close_after_qp_build) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    Socket* s = NULL;
    rdma::HelloMessage msg;
    uint8_t data[RDMA_HELLO_MSG_LEN];
    msg.msg_len = 38;
    msg.hello_ver = 1;
    msg.impl_ver = 1;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);

    butil::fd_guard sockfd1(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd1 >= 0);
    ASSERT_EQ(0, connect(sockfd1, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    ASSERT_EQ(38, write(sockfd1, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    close(sockfd1);
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    StopServer();
}

TEST_F(RdmaTest, client_close_during_ack_send) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    Socket* s = NULL;
    rdma::HelloMessage msg;
    uint8_t data[RDMA_HELLO_MSG_LEN];
    msg.msg_len = 38;
    msg.hello_ver = 1;
    msg.impl_ver = 1;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);

    butil::fd_guard sockfd1(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd1 >= 0);
    ASSERT_EQ(0, connect(sockfd1, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    ASSERT_EQ(38, write(sockfd1, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ(2, write(sockfd1, &flags, 2));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    close(sockfd1);
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    StopServer();
}

TEST_F(RdmaTest, client_close_after_ack_send) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    Socket* s = NULL;
    rdma::HelloMessage msg;
    uint8_t data[RDMA_HELLO_MSG_LEN];
    msg.msg_len = 38;
    msg.hello_ver = 1;
    msg.impl_ver = 1;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);

    butil::fd_guard sockfd1(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd1 >= 0);
    ASSERT_EQ(0, connect(sockfd1, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    ASSERT_EQ(38, write(sockfd1, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ(4, write(sockfd1, &flags, 4));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);
    close(sockfd1);
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    butil::fd_guard sockfd2(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd2 >= 0);
    ASSERT_EQ(0, connect(sockfd2, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    ASSERT_EQ(38, write(sockfd2, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    flags = butil::HostToNet32(1);
    ASSERT_EQ(4, write(sockfd2, &flags, 4));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::ESTABLISHED, s->_rdma_ep->_state);
    close(sockfd2);
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    StopServer();
}

TEST_F(RdmaTest, client_send_data_on_tcp_after_ack_send) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    Socket* s = NULL;
    rdma::HelloMessage msg;
    uint8_t data[RDMA_HELLO_MSG_LEN];
    msg.msg_len = 38;
    msg.hello_ver = 1;
    msg.impl_ver = 1;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);

    butil::fd_guard sockfd1(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd1 >= 0);
    ASSERT_EQ(0, connect(sockfd1, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    ASSERT_EQ(38, write(sockfd1, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ(4, write(sockfd1, &flags, 4));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);
    ASSERT_EQ(4, write(sockfd1, &flags, 4));
    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);
    close(sockfd1);
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    butil::fd_guard sockfd2(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd2 >= 0);
    ASSERT_EQ(0, connect(sockfd2, (sockaddr*)&addr, sizeof(sockaddr)));
    usleep(100000);  // wait for server to handle the msg
    s = GetSocketFromServer(0);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, s->_rdma_ep->_state);
    ASSERT_EQ(38, write(sockfd2, data, 38));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::S_ACK_WAIT, s->_rdma_ep->_state);
    flags = butil::HostToNet32(1);
    ASSERT_EQ(4, write(sockfd2, &flags, 4));
    usleep(100000);  // wait for server to handle the msg
    ASSERT_EQ(rdma::RdmaEndpoint::ESTABLISHED, s->_rdma_ep->_state);
    ASSERT_EQ(4, write(sockfd1, &flags, 4));
    usleep(100000);
    ASSERT_EQ(NULL, GetSocketFromServer(0));

    StopServer();
}

TEST_F(RdmaTest, server_miss_before_hello_send) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(ERPCTIMEDOUT, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_close_before_hello_send) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));
    close(acc_fd);
    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::FAILED, s->_rdma_ep->_state);
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EEOF, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_miss_during_magic_str) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));
    ASSERT_EQ(2, write(acc_fd, "RD", 2));
    usleep(100000);
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(ERPCTIMEDOUT, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_close_during_magic_str) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));
    ASSERT_EQ(2, write(acc_fd, "RD", 2));
    close(acc_fd);
    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::FAILED, s->_rdma_ep->_state);
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EEOF, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_hello_invalid_magic_str) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));
    ASSERT_EQ(4, write(acc_fd, "ABCD", 4));
    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::FAILED, s->_rdma_ep->_state);
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EPROTO, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_miss_during_hello_msg) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));
    ASSERT_EQ(4, write(acc_fd, "RDMA", 4));
    ASSERT_EQ(2, write(acc_fd, "00", 2));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(ERPCTIMEDOUT, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_close_during_hello_msg) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));
    ASSERT_EQ(4, write(acc_fd, "RDMA", 4));
    ASSERT_EQ(2, write(acc_fd, "00", 2));
    close(acc_fd);
    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::FAILED, s->_rdma_ep->_state);
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EEOF, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_hello_invalid_msg_len) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));
    memcpy(data, "RDMA", 4);
    uint16_t len = butil::HostToNet16(35);
    memcpy(data + 4, &len, 2);
    memset(data + 6, 0, 32);
    ASSERT_EQ(38, write(acc_fd, data, 38));
    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::FAILED, s->_rdma_ep->_state);
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EPROTO, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_hello_invalid_version) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));
    memcpy(data, "RDMA", 4);
    uint16_t len = butil::HostToNet16(38);
    memcpy(data + 4, &len, 2);
    memset(data + 6, 0, 32);
    ASSERT_EQ(38, write(acc_fd, data, 38));
    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);
    ASSERT_EQ(4, read(acc_fd, data, 4));
    uint32_t* tmp = (uint32_t*)data;
    ASSERT_EQ(0, butil::NetToHost32(*tmp));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(ERPCTIMEDOUT, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_hello_invalid_sq_rq_size) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));

    rdma::HelloMessage msg;
    msg.msg_len = 38;
    msg.hello_ver = 1;
    msg.impl_ver = 1;
    msg.sq_size = 0;
    msg.rq_size = 0;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    ASSERT_EQ(38, write(acc_fd, data, 38));

    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);
    ASSERT_EQ(4, read(acc_fd, data, 4));
    uint32_t* tmp = (uint32_t*)data;
    ASSERT_EQ(0, butil::NetToHost32(*tmp));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(ERPCTIMEDOUT, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_miss_after_ack) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));

    rdma::HelloMessage msg;
    msg.msg_len = 38;
    msg.hello_ver = 1;
    msg.impl_ver = 1;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    ASSERT_EQ(38, write(acc_fd, data, 38));

    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::ESTABLISHED, s->_rdma_ep->_state);
    ASSERT_EQ(4, read(acc_fd, data, 4));
    uint32_t* tmp = (uint32_t*)data;
    ASSERT_EQ(1, butil::NetToHost32(*tmp));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(ERPCTIMEDOUT, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_close_after_ack) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));

    rdma::HelloMessage msg;
    msg.msg_len = 38;
    msg.hello_ver = 1;
    msg.impl_ver = 1;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    ASSERT_EQ(38, write(acc_fd, data, 38));

    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::ESTABLISHED, s->_rdma_ep->_state);
    ASSERT_EQ(4, read(acc_fd, data, 4));
    uint32_t* tmp = (uint32_t*)data;
    ASSERT_EQ(1, butil::NetToHost32(*tmp));
    close(acc_fd);
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EEOF, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_send_data_on_tcp_after_ack) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::C_HELLO_WAIT, s->_rdma_ep->_state);

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[RDMA_HELLO_MSG_LEN];
    ASSERT_EQ(38, read(acc_fd, data, 38));

    rdma::HelloMessage msg;
    msg.msg_len = 38;
    msg.hello_ver = 1;
    msg.impl_ver = 1;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    ASSERT_EQ(38, write(acc_fd, data, 38));

    usleep(100000);
    ASSERT_EQ(rdma::RdmaEndpoint::ESTABLISHED, s->_rdma_ep->_state);
    ASSERT_EQ(38, write(acc_fd, data, 38));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EPROTO, cntl.ErrorCode());
}

TEST_F(RdmaTest, try_global_disable_rdma) {
    StartServer();
    rdma::g_rdma_available.store(false, butil::memory_order_relaxed);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;

    req.set_message(__FUNCTION__);
    req.set_sleep_us(200000);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);
    usleep(100000);
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_EQ(rdma::RdmaEndpoint::FALLBACK_TCP, s->_rdma_ep->_state);
    bthread_id_join(cntl.call_id());
    ASSERT_EQ(0, cntl.ErrorCode());

    StopServer();
    rdma::g_rdma_available.store(true, butil::memory_order_relaxed);
}

TEST_F(RdmaTest, server_option_invalid) {
    Server server;
    ServerOptions options;
    options.use_rdma = true;

    // rtmp and rdma are incompatible
    options.rtmp_service = (RtmpService*)1;
    ASSERT_EQ(-1, server.Start(PORT, &options));

    // nshead and rdma are incompatible
    options.rtmp_service = NULL;
    options.nshead_service = (NsheadService*)1;
    ASSERT_EQ(-1, server.Start(PORT, &options));

    // mongo and rdma are incompatible
    options.nshead_service = NULL;
    options.mongo_service_adaptor = (MongoServiceAdaptor*)1;
    ASSERT_EQ(-1, server.Start(PORT, &options));

    // ssl and rdma are incompatible
    options.mongo_service_adaptor = NULL;
    options.mutable_ssl_options()->default_cert.certificate = "test";
    ASSERT_EQ(-1, server.Start(PORT, &options));
}

TEST_F(RdmaTest, channel_option_invalid) {
    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;

    // rtmp and rdma are incompatible
    chan_options.protocol = "rtmp";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    chan_options.protocol = "streaming_rpc";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // nshead and rdma are incompatible
    chan_options.protocol = "nshead";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));
    chan_options.protocol = "nshead_mcpack";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // nova_pbrpc and rdma are incompatible
    chan_options.protocol = "nova_pbrpc";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // public_pbrpc and rdma are incompatible
    chan_options.protocol = "public_pbrpc";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // redis and rdma are incompatible
    chan_options.protocol = "redis";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // memcache and rdma are incompatible
    chan_options.protocol = "memcache";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // ubrpc and rdma are incompatible
    chan_options.protocol = "ubrpc_compack";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // itp and rdma are incompatible
    chan_options.protocol = "itp";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // esp and rdma are incompatible
    chan_options.protocol = "esp";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // hulu_pbrpc and rdma are incompatible
    chan_options.protocol = "hulu_pbrpc";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // sofa_pbrpc and rdma are incompatible
    chan_options.protocol = "sofa_pbrpc";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // http and rdma are incompatible
    chan_options.protocol = "http";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));

    // ssl and rdma are incompatible
    chan_options.protocol = "baidu_std";
    chan_options.mutable_ssl_options()->sni_name = "test";
    ASSERT_EQ(-1, channel.Init(g_ep, &chan_options));
}

TEST_F(RdmaTest, rdma_client_to_rdma_server) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);
    usleep(100000);
    bthread_id_join(cntl.call_id());
    ASSERT_EQ(0, cntl.ErrorCode());

    StopServer();
}

TEST_F(RdmaTest, tcp_client_to_tcp_server) {
    StartServer(false);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);
    usleep(100000);
    bthread_id_join(cntl.call_id());
    ASSERT_EQ(0, cntl.ErrorCode());

    StopServer();
}

TEST_F(RdmaTest, tcp_client_to_rdma_server) {
    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);
    usleep(100000);
    bthread_id_join(cntl.call_id());
    ASSERT_EQ(0, cntl.ErrorCode());

    StopServer();
}

TEST_F(RdmaTest, rdma_client_to_tcp_server) {
    StartServer(false);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);
    usleep(100000);
    bthread_id_join(cntl.call_id());
    ASSERT_EQ(EEOF, cntl.ErrorCode());

    StopServer();
}

static const int RPC_NUM = 1024;

void DumpRdmaEndpointInfo(Socket* client, Socket* server) {
    std::cout << std::endl << "client:";
    client->_rdma_ep->DebugInfo(std::cout);
    std::cout << std::endl << "server:";
    server->_rdma_ep->DebugInfo(std::cout);
}

TEST_F(RdmaTest, send_rpcs_in_one_qp) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 3000;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];

    LOG(INFO) << "send 0 attachment";
    for (int i = 0; i < RPC_NUM; ++i) {
        req[i].set_message(__FUNCTION__);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }
    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        if (cntl[i].ErrorCode() == ERPCTIMEDOUT) {
            SocketUniquePtr s;
            ASSERT_EQ(0, Socket::Address(cntl[i]._single_server_id, &s));
            Socket* m = GetSocketFromServer(0);
            DumpRdmaEndpointInfo(s.get(), m);
        }
        ASSERT_EQ(0, cntl[i].ErrorCode()) << "req[" << i << "]";
    }

    LOG(INFO) << "send 4KB attachment";
    butil::IOBuf attach;
    attach.resize(4096);
    for (int i = 0; i < RPC_NUM; ++i) {
        cntl[i].Reset();
        cntl[i].request_attachment().append(attach);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }
    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        if (cntl[i].ErrorCode() == ERPCTIMEDOUT) {
            SocketUniquePtr s;
            ASSERT_EQ(0, Socket::Address(cntl[i]._single_server_id, &s));
            Socket* m = GetSocketFromServer(0);
            DumpRdmaEndpointInfo(s.get(), m);
        }
        ASSERT_EQ(0, cntl[i].ErrorCode()) << "req[" << i << "]";
    }

    LOG(INFO) << "send 1MB attachment";
    attach.resize(1048576);
    for (int i = 0; i < RPC_NUM; ++i) {
        cntl[i].Reset();
        cntl[i].request_attachment().append(attach);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }
    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        if (cntl[i].ErrorCode() == ERPCTIMEDOUT) {
            SocketUniquePtr s;
            ASSERT_EQ(0, Socket::Address(cntl[i]._single_server_id, &s));
            Socket* m = GetSocketFromServer(0);
            DumpRdmaEndpointInfo(s.get(), m);
        }
        ASSERT_TRUE(0 == cntl[i].ErrorCode() ||
                    EOVERCROWDED == cntl[i].ErrorCode()) << "req[" << i << "]";
    }

    StopServer();
}

TEST_F(RdmaTest, send_rpc_in_many_qp) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    Server server[100];
    MyEchoService svc[100];
    int num = 100;
    for (int i = 0; i < num; ++i) {
        ServerOptions options;
        options.use_rdma = true;
        options.idle_timeout_sec = 1;
        options.max_concurrency = 0;
        options.internal_port = -1;
        server[i].AddService(&svc[i], SERVER_DOESNT_OWN_SERVICE);
        EXPECT_EQ(0, server[i].Start(i + 8000, &options));
    }

    int port = 0;
    butil::IOBuf attach;
    attach.resize(4096);
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    Channel channel[RPC_NUM];
    Server* svr[RPC_NUM];
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];
    butil::ip_t ip;
    butil::str2ip(g_ip.c_str(), &ip);
    for (int i = 0; i < RPC_NUM; ++i) {
        svr[i] = &server[i % num];
        butil::EndPoint ep(ip, 8000 + ((port++) % num));
        ASSERT_EQ(0, channel[i].Init(ep, &chan_options));
        req[i].set_message(__FUNCTION__);
        cntl[i].request_attachment().append(attach);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel[i]).Echo(&cntl[i], &req[i], &res[i], done);
    }
    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        if (cntl[i].ErrorCode() == ERPCTIMEDOUT) {
            SocketUniquePtr s;
            ASSERT_EQ(0, Socket::Address(cntl[i]._single_server_id, &s));
            std::vector<SocketId> sids;
            svr[i]->_am->ListConnections(&sids);
            for (size_t i = 0; i < sids.size(); ++i) {
                SocketUniquePtr m;
                ASSERT_EQ(0, Socket::AddressFailedAsWell(sids[i], &m));
                DumpRdmaEndpointInfo(s.get(), m.get());
            }
        }
        ASSERT_EQ(0, cntl[i].ErrorCode()) << "req[" << i << "]";
    }

    for (int i = 0; i < num; ++i) {
        server[i].Stop(0);
        server[i].Join();
    }
}

TEST_F(RdmaTest, send_rpcs_as_pooled_connection) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 30000;  // it may very slow
    chan_options.timeout_ms = 30000;
    chan_options.max_retry = 0;
    chan_options.connection_type = "pooled";
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];

    butil::IOBuf attach;
    attach.resize(4096);
    for (int i = 0; i < RPC_NUM; ++i) {
        req[i].set_message(__FUNCTION__);
        cntl[i].request_attachment().append(attach);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }
    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        if (cntl[i].ErrorCode() == ERPCTIMEDOUT) {
            SocketUniquePtr s;
            ASSERT_EQ(0, Socket::Address(cntl[i]._single_server_id, &s));
            Socket* m = GetSocketFromServer(0);
            DumpRdmaEndpointInfo(s.get(), m);
        }
        ASSERT_EQ(0, cntl[i].ErrorCode()) << "req[" << i << "]";
    }

    StopServer();
}

TEST_F(RdmaTest, send_rpcs_as_short_connection) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 30000;  // it may very slow
    chan_options.timeout_ms = 30000;
    chan_options.max_retry = 0;
    chan_options.connection_type = "short";
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];

    butil::IOBuf attach;
    attach.resize(4096);
    for (int i = 0; i < RPC_NUM; ++i) {
        req[i].set_message(__FUNCTION__);
        cntl[i].request_attachment().append(attach);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }
    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        if (cntl[i].ErrorCode() == ERPCTIMEDOUT) {
            SocketUniquePtr s;
            ASSERT_EQ(0, Socket::Address(cntl[i]._single_server_id, &s));
            Socket* m = GetSocketFromServer(0);
            DumpRdmaEndpointInfo(s.get(), m);
        }
        ASSERT_EQ(0, cntl[i].ErrorCode()) << "req[" << i << "]";
    }

    StopServer();
}

TEST_F(RdmaTest, server_stop_during_rpc) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 3000;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];

    butil::IOBuf attach;
    attach.resize(4096);
    for (int i = 0; i < RPC_NUM; ++i) {
        req[i].set_message(__FUNCTION__);
        cntl[i].request_attachment().append(attach);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }

    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        if (i == 0) StopServer();
        int error_code = cntl[i].ErrorCode();
        ASSERT_TRUE(error_code == 0 ||
                    error_code == EEOF ||
                    error_code == ELOGOFF ||
                    error_code == EHOSTDOWN) << "req[" << i << "]: " << error_code;
    }
}

TEST_F(RdmaTest, server_close_during_rpc) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 3000;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];

    butil::IOBuf attach;
    attach.resize(4096);
    for (int i = 0; i < RPC_NUM; ++i) {
        req[i].set_message(__FUNCTION__);
        cntl[i].request_attachment().append(attach);
        if (i == RPC_NUM / 2) {
            req[i].set_close_fd(true);
        }
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }

    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        int error_code = cntl[i].ErrorCode();
        ASSERT_TRUE(error_code == 0 ||
                    error_code == EEOF ||
                    error_code == EFAILEDSOCKET ||
                    error_code == EHOSTDOWN) << "req[" << i << "]: " << error_code;
    }

    StopServer();
}

TEST_F(RdmaTest, client_close_during_rpc) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 3000;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];

    butil::IOBuf attach;
    attach.resize(4096);
    for (int i = 0; i < RPC_NUM; ++i) {
        req[i].set_message(__FUNCTION__);
        cntl[i].request_attachment().append(attach);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }

    cntl[0].CloseConnection("Close connection");

    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        int error_code = cntl[i].ErrorCode();
        ASSERT_TRUE(error_code == 0 ||
                    error_code == ECLOSE ||
                    error_code == EHOSTDOWN) << "req[" << i << "]: " << error_code;
    }

    StopServer();
}

TEST_F(RdmaTest, verbs_error_handling) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    req.set_sleep_us(200000);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, done);

    usleep(100000);  // wait for rdma handshake complete

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    ibv_sge sge;
    void* buf = malloc(8192);
    sge.addr = (uint64_t)buf;
    sge.length = 8192;
    sge.lkey = 1;  // incorrect lkey
    wr.sg_list = &sge;
    wr.num_sge = 1;
    ibv_send_wr* bad = NULL;
    ibv_post_send(s->_rdma_ep->_resource->qp, &wr, &bad);
    bthread_id_join(cntl.call_id());
    ASSERT_EQ(ERDMA, cntl.ErrorCode());
    free(buf);

    StopServer();
}

TEST_F(RdmaTest, rdma_use_parallel_channel) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    const size_t NCHANS = 8;
    Channel subchans[NCHANS];
    ParallelChannel channel;
    ChannelOptions opts;
    opts.use_rdma = true;
    for (size_t i = 0; i < NCHANS; ++i) {
        ASSERT_EQ(0, subchans[i].Init(_naming_url.c_str(), "rR", &opts));
        ASSERT_EQ(0, channel.AddChannel(
                    &subchans[i], DOESNT_OWN_CHANNEL,
                    NULL, NULL));
    }

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, NULL);

    ASSERT_EQ(0, cntl.ErrorCode());
    ASSERT_EQ(NCHANS, (size_t)cntl.sub_count());

    StopServer();
}

TEST_F(RdmaTest, rdma_use_selective_channel) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    const size_t NCHANS = 8;
    SelectiveChannel channel;
    ChannelOptions opts;
    opts.use_rdma = true;
    ASSERT_EQ(0, channel.Init("rr", &opts));
    for (size_t i = 0; i < NCHANS; ++i) {
        Channel* subchan = new Channel;
        ASSERT_EQ(0, subchan->Init(_naming_url.c_str(), "rR", &opts));
        ASSERT_EQ(0, channel.AddChannel(subchan, NULL));
    }

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, NULL);

    ASSERT_EQ(0, cntl.ErrorCode()) << cntl.ErrorText();
    ASSERT_EQ(1, cntl.sub_count());

    StopServer();
}

static void MockFree(void* buf) { }

TEST_F(RdmaTest, send_rpcs_with_user_defined_iobuf) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];

    butil::IOBuf attach;
    void* data = malloc(4096);;
    attach.append_user_data(data, 4096, NULL);
    req[0].set_message(__FUNCTION__);
    cntl[0].request_attachment().append(attach);
    google::protobuf::Closure* done = DoNothing();
    ::test::EchoService::Stub(&channel).Echo(&cntl[0], &req[0], &res[0], done);
    bthread_id_join(cntl[0].call_id());
    ASSERT_EQ(ERDMAMEM, cntl[0].ErrorCode());
    attach.clear();
    sleep(2);  // wait for client recover from EHOSTDOWN
    cntl[0].Reset();

    char* mr[2 * RPC_NUM];
    uint32_t lkey[2 * RPC_NUM];
    for (size_t i = 0; i < RPC_NUM; ++i) {
        mr[2 * i] = (char*)malloc(4096);
        memset(mr[2 * i], i % 100, 4096);
        lkey[2 * i] = rdma::RegisterMemoryForRdma(mr[2 * i], 4096);
        ASSERT_TRUE(lkey[2 * i] != 0);
        cntl[i].request_attachment().append_user_data_with_meta(mr[2 * i] + i, 4096 - i, MockFree, lkey[2 * i]);
        mr[2 * i + 1] = (char*)malloc(4096);
        memset(mr[2 * i + 1], i % 100, 4096);
        lkey[2 * i + 1] = rdma::RegisterMemoryForRdma(mr[2 * i + 1], 4096);
        ASSERT_TRUE(lkey[2 * i + 1] != 0);
        cntl[i].request_attachment().append_user_data_with_meta(mr[2 * i + 1] + i, 4096 - i, MockFree, lkey[2 * i + 1]);
        req[i].set_message(__FUNCTION__);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }
    for (size_t i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        ASSERT_EQ(0, cntl[i].ErrorCode()) << "req[" << i << "]";
        rdma::DeregisterMemoryForRdma(mr[i]);
        ASSERT_EQ(2 * (4096 - i), cntl[i].response_attachment().size());
        char tmp[8192];
        cntl[i].response_attachment().copy_to(tmp, 2 * (4096 - i));
        ASSERT_EQ(0, memcmp(mr[2 * i] + i, tmp, 4096 - i));
        ASSERT_EQ(0, memcmp(mr[2 * i + 1] + i, tmp + 4096 - i, 4096 - i));
        free(mr[2 * i]);
        free(mr[2 * i + 1]);
    }

    StopServer();
}

TEST_F(RdmaTest, try_memory_pool_empty) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.use_rdma = true;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 60000;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];

    butil::IOBuf iobuf[RPC_NUM];
    for (int i = 0; i < 1024; ++i) {
        if (iobuf[i].resize(1048576 * 8)) {
            // 8MB for each iobuf
            break;
        }
    }

    for (int i = 0; i < RPC_NUM; ++i) {
        req[i].set_message(__FUNCTION__);
        cntl[i].request_attachment().append(iobuf[i]);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }
    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
    }

    StopServer();
}

#endif  // if BRPC_WITH_RDMA

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
#if BRPC_WITH_RDMA
    rdma::FLAGS_rdma_trace_verbose = true;
    rdma::FLAGS_rdma_memory_pool_max_regions = 2;
    FLAGS_log_idle_connection_close = true;
    if (!FLAGS_rdma_test_enable) {
        // skip UT requiring rdma runtime environment
        rdma::g_rdma_available.store(true, butil::memory_order_relaxed);
        rdma::g_skip_rdma_init = true;
    }
#endif  // if BRPC_WITH_RDMA
    return RUN_ALL_TESTS();
}
