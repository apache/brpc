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
#include <errno.h>
#include <unistd.h>
#include <functional>
#include <vector>
#include <google/protobuf/descriptor.h>
#include "butil/endpoint.h"
#include "butil/fd_guard.h"
#include "butil/iobuf.h"
#include "butil/sys_byteorder.h"
#include "butil/time.h"
#include "butil/files/temp_file.h"
#include "brpc/acceptor.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/socket.h"
#include "brpc/errno.pb.h"
#include "brpc/parallel_channel.h"
#include "brpc/selective_channel.h"
#include "brpc/rdma_transport.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_handshake.h"
#include "brpc/rdma/rdma_handshake_constants.h"
#include "brpc/rdma/rdma_handshake.pb.h"
#include "brpc/rdma/rdma_helper.h"
#include "echo.pb.h"

static const int PORT = 8713;

using namespace brpc;

namespace brpc {

DECLARE_int64(socket_max_unwritten_bytes);
DECLARE_bool(log_idle_connection_close);
DEFINE_bool(rdma_test_enable, false, "Enable tests requring rdma runtime.");

namespace rdma {

// HELLO_V2_VERSION / IMPL_V2_VERSION come from
// brpc/rdma/rdma_handshake_constants.h (shared wire constants).

DECLARE_bool(rdma_trace_verbose);
DECLARE_int32(rdma_memory_pool_max_regions);
DECLARE_int32(rdma_client_handshake_version);
DECLARE_bool(rdma_ece);

extern ibv_cq* (*IbvCreateCq)(ibv_context*, int, void*, ibv_comp_channel*, int);
extern int (*IbvDestroyCq)(ibv_cq*);
extern ibv_qp* (*IbvCreateQp)(ibv_pd*, ibv_qp_init_attr*);
extern int (*IbvModifyQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask);
extern int (*IbvQueryQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask, ibv_qp_init_attr*);
extern int (*IbvDestroyQp)(ibv_qp*);
extern butil::atomic<bool> g_rdma_available;
extern bool g_skip_rdma_init;
extern bool g_fail_resource_alloc_for_test;
} // namespace rdma
} // namespace brpc

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

// write(2) may transfer less than asked for, so a test that cares about the
// whole buffer reaching the peer has to loop.
static bool WriteAll(int fd, const void* buf, size_t len) {
    const uint8_t* p = (const uint8_t*)buf;
    for (size_t done = 0; done < len; ) {
        ssize_t n = write(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        done += n;
    }
    return true;
}

// Same for read(2). Returns false on error, on timeout (see ConnectToServer)
// and on EOF before the whole buffer arrived.
static bool ReadAll(int fd, void* buf, size_t len) {
    uint8_t* p = (uint8_t*)buf;
    for (size_t done = 0; done < len; ) {
        ssize_t n = read(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        done += n;
    }
    return true;
}

// Connect a raw socket to the test server. Note that sin_addr is taken from
// `g_ep`: a zeroed one means 0.0.0.0, which only happens to reach the local
// server on some systems. The receive timeout keeps a missing server reply from
// hanging the test forever.
static void ConnectToServer(butil::fd_guard* sockfd) {
    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_ep.port);
    addr.sin_addr = g_ep.ip;
    sockfd->reset(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(*sockfd >= 0);
    timeval tv;
    tv.tv_sec = WAIT_TIMEOUT_US / 1000000;
    tv.tv_usec = WAIT_TIMEOUT_US % 1000000;
    ASSERT_EQ(0, setsockopt(*sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)));
    ASSERT_EQ(0, connect(*sockfd, (sockaddr*)&addr, sizeof(addr)));
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

protected:
    void StartServer(bool use_rdma = true) {
        ServerOptions options;
        options.enabled_protocols = "baidu_std";
        options.socket_mode = use_rdma ? SOCKET_MODE_RDMA : SOCKET_MODE_TCP;
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

// Polls until the endpoint reaches `expected` and returns the last state seen,
// so that ASSERT_RDMA_STATE() reports what the endpoint actually settled on.
static rdma::RdmaEndpoint::State WaitForRdmaState(
        RdmaTransport* transport, rdma::RdmaEndpoint::State expected) {
    rdma::RdmaEndpoint::State state = transport->_rdma_ep->_state;
    WaitUntil([transport, expected, &state] {
        state = transport->_rdma_ep->_state;
        return state == expected;
    });
    return state;
}

// Waits for `transport` to reach `expected`, failing the test if it does not.
#define ASSERT_RDMA_STATE(expected, transport) \
    ASSERT_EQ(expected, WaitForRdmaState(transport, expected))

// Polls until the fd stream of `s` holds exactly `size` bytes. Tests asserting
// that a state did NOT change need this: waiting for the state itself would
// return before the peer had read anything at all.
static bool WaitForFdReadBuf(Socket* s, size_t size) {
    return WaitUntil([s, size] {
        return s->fd_input_processor().read_buf().size() == size;
    });
}

// Build a well-formed v2 client hello: "RDMA" followed by the 36B body.
static void MakeV2ClientHello(uint8_t (&data)[rdma::HELLO_V2_MSG_LEN_MIN]) {
    rdma::v2_wire::HelloMessage msg{};
    msg.msg_len = rdma::HELLO_V2_MSG_LEN_MIN;
    msg.hello_ver = rdma::HELLO_V2_VERSION;
    msg.impl_ver = rdma::IMPL_V2_VERSION;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
}

// Parameterized fixture used by upper-layer RPC tests that have no
// dependency on the handshake wire format. The parameter is the
// client-side handshake protocol version (FLAGS_rdma_client_handshake_version),
// so every TEST_P below is automatically executed once per supported
// version. Add a new version to INSTANTIATE_TEST_SUITE_P at the bottom
// of this file and these RPC tests will gain coverage for free.
class RdmaRpcTest : public RdmaTest,
                    public ::testing::WithParamInterface<int> {
protected:
    void SetUp() override {
        RdmaTest::SetUp();
        _saved_handshake_version = rdma::FLAGS_rdma_client_handshake_version;
        rdma::FLAGS_rdma_client_handshake_version = GetParam();
    }
    void TearDown() override {
        rdma::FLAGS_rdma_client_handshake_version = _saved_handshake_version;
        RdmaTest::TearDown();
    }

private:
    int _saved_handshake_version = 2;
};

TEST_F(RdmaTest, client_close_before_hello_send) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_magic_str) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    memcpy(data, "PRPC", 4);  // send as normal baidu_std protocol
    ASSERT_TRUE(WriteAll(sockfd, data, 4));
    // Wait for the bytes to show up in the fd stream (baidu_std wants 12B of
    // header, so they stay buffered). Waiting on the state instead would prove
    // nothing: it is already UNINIT before the server has read anything.
    ASSERT_TRUE(WaitForFdReadBuf(s, 4));
    // A non-RDMA magic makes ParseRdmaHandshake return TRY_OTHERS and hand the
    // bytes to other protocols; it does not touch the endpoint state, so it
    // stays UNINIT (the old blocking handshake used to set FALLBACK_TCP here).
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    StopServer();
}

TEST_F(RdmaTest, client_close_during_hello_send) {
    StartServer();

    Socket* s = nullptr;
    uint8_t data[8];

    butil::fd_guard sockfd1;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd1));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    memcpy(data, "RD", 2);
    ASSERT_TRUE(WriteAll(sockfd1, data, 2));  // break in magic str
    // Fewer than 4 magic bytes: ParseRdmaHandshake can't tell yet, returns
    // NOT_ENOUGH_DATA and leaves the endpoint UNINIT (the old blocking
    // handshake used to set S_HELLO_WAIT before reading the magic). Wait for
    // the bytes to be buffered, the state alone would prove nothing.
    ASSERT_TRUE(WaitForFdReadBuf(s, 2));
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    sockfd1.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    butil::fd_guard sockfd2;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd2));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    ASSERT_TRUE(WriteAll(sockfd2, data, 4));  // break after magic str
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    sockfd2.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    butil::fd_guard sockfd3;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd3));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    // Send the 4B magic plus a valid msg_len (=40) but no body, so the server
    // recognizes an RDMA v2 hello and waits for the remaining bytes. (A zero
    // msg_len would now be rejected up-front as a protocol error.)
    memcpy(data, "RDMA", 4);
    uint16_t v2_len = butil::HostToNet16(rdma::HELLO_V2_MSG_LEN_MIN);
    memcpy(data + 4, &v2_len, sizeof(v2_len));
    ASSERT_TRUE(WriteAll(sockfd3, data, 6));  // magic + msg_len, body missing
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    sockfd3.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_len) {
    StartServer();

    Socket* s = nullptr;
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];

    butil::fd_guard sockfd1;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd1));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    ASSERT_TRUE(WriteAll(sockfd1, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    memset(data + 4, 0, 36);
    ASSERT_TRUE(WriteAll(sockfd1, data + 4, 36));  // Write invalid length.
    ASSERT_TRUE(WaitForServerSocketGone());

    butil::fd_guard sockfd2;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd2));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    ASSERT_TRUE(WriteAll(sockfd2, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    uint16_t len = butil::HostToNet16(35);
    memcpy(data + 4, &len, sizeof(len));
    memset(data + 6, 0, 34);
    ASSERT_TRUE(WriteAll(sockfd2, data + 4, 36));  // write invalid length
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_version) {
    StartServer();

    Socket* s = nullptr;
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    uint16_t len = butil::HostToNet16(rdma::HELLO_V2_MSG_LEN_MIN);
    uint16_t ver = butil::HostToNet16(1);

    butil::fd_guard sockfd1;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd1));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    ASSERT_TRUE(WriteAll(sockfd1, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    memcpy(data + 4, &len, 2);
    memset(data + 6, 0, 34);
    memcpy(data + 6, &ver, 2);  // hello_ver == 1, impl_ver == 0
    // Write the 36B base starting at data + 4 (NOT data). Pre-Step-1 this
    // UT mistakenly wrote `data, 36` which included the leftover "RDMA"
    // magic at data[0..4); the server parsed it as msg_len = 0x5244 and
    // happened to fall through to NegotiationValid (which then failed on
    // hello_ver). Now that Step 1 enforces a HELLO_V2_MSG_LEN_MAX upper bound,
    // such an oversized msg_len would be rejected before reaching the
    // version check, breaking the intent of this UT.
    ASSERT_TRUE(WriteAll(sockfd1, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    uint32_t flags = 0;
    ASSERT_TRUE(WriteAll(sockfd1, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    sockfd1.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    butil::fd_guard sockfd2;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd2));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    memcpy(data, "RDMA", 4);
    ASSERT_TRUE(WriteAll(sockfd2, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    memcpy(data + 4, &len, 2);
    memset(data + 6, 0, 32);
    memcpy(data + 8, &ver, 2);  // hello_ver == 0, impl_ver == 1
    // See comment above on `WriteAll(sockfd1, data + 4, 36)` for why we
    // write from data + 4 instead of data.
    ASSERT_TRUE(WriteAll(sockfd2, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    ASSERT_TRUE(WriteAll(sockfd2, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    sockfd2.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_sq_rq_block_size) {
    StartServer();

    Socket* s = nullptr;
    uint32_t flags = butil::HostToNet32(0);
    rdma::v2_wire::HelloMessage msg{};
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    msg.msg_len = rdma::HELLO_V2_MSG_LEN_MIN;
    msg.hello_ver = rdma::HELLO_V2_VERSION;
    msg.impl_ver = rdma::IMPL_V2_VERSION;

    msg.sq_size = 10;
    msg.rq_size = 16;
    msg.block_size = 8192;
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    butil::fd_guard sockfd1;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd1));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    ASSERT_TRUE(WriteAll(sockfd1, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    ASSERT_TRUE(WriteAll(sockfd1, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    ASSERT_TRUE(WriteAll(sockfd1, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    sockfd1.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    msg.sq_size = 16;
    msg.rq_size = 10;
    msg.block_size = 8192;
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    butil::fd_guard sockfd2;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd2));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    ASSERT_TRUE(WriteAll(sockfd2, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    ASSERT_TRUE(WriteAll(sockfd2, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    ASSERT_TRUE(WriteAll(sockfd2, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    sockfd2.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 1000;
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    butil::fd_guard sockfd3;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd3));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    ASSERT_TRUE(WriteAll(sockfd3, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    ASSERT_TRUE(WriteAll(sockfd3, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    ASSERT_TRUE(WriteAll(sockfd3, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    sockfd3.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_close_after_qp_build) {
    StartServer();

    Socket* s = nullptr;
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    MakeV2ClientHello(data);

    butil::fd_guard sockfd1;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd1));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    ASSERT_TRUE(WriteAll(sockfd1, data, sizeof(data)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    sockfd1.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_close_during_ack_send) {
    StartServer();

    Socket* s = nullptr;
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    MakeV2ClientHello(data);

    butil::fd_guard sockfd1;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd1));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    ASSERT_TRUE(WriteAll(sockfd1, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    ASSERT_TRUE(WriteAll(sockfd1, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    uint32_t flags = butil::HostToNet32(1);
    ASSERT_TRUE(WriteAll(sockfd1, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, RdmaTransportOf(s));
    sockfd1.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_close_after_ack_send) {
    StartServer();

    Socket* s = nullptr;
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    MakeV2ClientHello(data);

    butil::fd_guard sockfd1;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd1));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    ASSERT_TRUE(WriteAll(sockfd1, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    ASSERT_TRUE(WriteAll(sockfd1, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_TRUE(WriteAll(sockfd1, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    sockfd1.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    butil::fd_guard sockfd2;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd2));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    ASSERT_TRUE(WriteAll(sockfd2, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    ASSERT_TRUE(WriteAll(sockfd2, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    flags = butil::HostToNet32(1);
    ASSERT_TRUE(WriteAll(sockfd2, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, RdmaTransportOf(s));
    sockfd2.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_send_data_on_tcp_after_ack_send) {
    StartServer();

    Socket* s = nullptr;
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    MakeV2ClientHello(data);

    butil::fd_guard sockfd1;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd1));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    ASSERT_TRUE(WriteAll(sockfd1, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    ASSERT_TRUE(WriteAll(sockfd1, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_TRUE(WriteAll(sockfd1, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    // 4 more bytes on a fd that fell back to TCP are not a protocol baidu_std
    // knows, so the connection is dropped.
    ASSERT_TRUE(WriteAll(sockfd1, &flags, sizeof(flags)));
    ASSERT_TRUE(WaitForServerSocketGone());

    butil::fd_guard sockfd2;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd2));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    ASSERT_TRUE(WriteAll(sockfd2, data, 4)); // Write magic string.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    ASSERT_TRUE(WriteAll(sockfd2, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    flags = butil::HostToNet32(1);
    ASSERT_TRUE(WriteAll(sockfd2, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, RdmaTransportOf(s));
    // Once RDMA is on the fd carries no RPC data at all, so this is an error.
    ASSERT_TRUE(WriteAll(sockfd2, &flags, sizeof(flags)));
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// Connect, push a well-formed v2 hello and read back the server's reply, which
// leaves the server in S_ACK_WAIT waiting for the 4B ACK.
static void HandshakeUntilAckWait(butil::fd_guard* sockfd) {
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(sockfd));

    uint8_t hello[rdma::HELLO_V2_MSG_LEN_MIN];
    MakeV2ClientHello(hello);
    ASSERT_TRUE(WriteAll(*sockfd, hello, sizeof(hello)));
    // The server answers only once it has consumed our hello, so reading the
    // whole reply is a synchronization point: no sleeping needed here.
    uint8_t reply[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_TRUE(ReadAll(*sockfd, reply, sizeof(reply)));
}

// A client is free to pipeline its first request right behind the handshake
// ACK. Only the 4B ACK belongs to the handshake. Whatever follows it must be
// handed over to the real protocol instead of dropping the connection.
TEST_F(RdmaTest, server_accepts_data_pipelined_behind_fallback_ack) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(HandshakeUntilAckWait(&sockfd));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    auto* transport = RdmaTransportOf(s);
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, transport);

    // An ACK asking for TCP, plus the first 4 bytes of a baidu_std request. One
    // write, so that both end up in the same read on the server.
    uint8_t ack_and_data[rdma::HELLO_ACK_LEN + 4];
    const uint32_t flags = butil::HostToNet32(0);
    memcpy(ack_and_data, &flags, rdma::HELLO_ACK_LEN);
    memcpy(ack_and_data + rdma::HELLO_ACK_LEN, "PRPC", 4);
    ASSERT_TRUE(WriteAll(sockfd, ack_and_data, sizeof(ack_and_data)));

    // The handshake took the ACK only and left "PRPC" to baidu_std, which is
    // now waiting for the rest of its 12B header. So the connection lives on
    // with those 4 bytes still buffered. Note that baidu_std gets them a moment
    // after the handshake gave up the stream, hence the wait.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, transport);
    ASSERT_EQ(RdmaTransport::RDMA_OFF, transport->_rdma_state);
    ASSERT_TRUE(GetSocketFromServer(0) != nullptr);
    ASSERT_TRUE(WaitForFdReadBuf(s, 4));

    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// Once RDMA is on, the TCP fd is no longer an RPC channel, so bytes trailing
// the ACK can only be a protocol error.
TEST_F(RdmaTest, server_rejects_data_pipelined_behind_rdma_ack) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(HandshakeUntilAckWait(&sockfd));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    auto* transport = RdmaTransportOf(s);
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, transport);

    uint8_t ack_and_data[rdma::HELLO_ACK_LEN + 4];
    const uint32_t flags = butil::HostToNet32(rdma::HELLO_ACK_RDMA_OK);
    memcpy(ack_and_data, &flags, rdma::HELLO_ACK_LEN);
    memcpy(ack_and_data + rdma::HELLO_ACK_LEN, "PRPC", 4);
    ASSERT_TRUE(WriteAll(sockfd, ack_and_data, sizeof(ack_and_data)));

    // Note that `transport->_rdma_ep` is gone by now: dropping the connection
    // recycles the Socket, and RdmaTransport::Release() deletes the endpoint.
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// Once RDMA is on, the server must stop parsing its TCP fd altogether.
TEST_F(RdmaTest, server_stops_parsing_tcp_fd_once_rdma_is_on) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(HandshakeUntilAckWait(&sockfd));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    auto* transport = RdmaTransportOf(s);
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, transport);

    // A bare ACK asking for RDMA. Nothing trails it, so the handshake ends in
    // ESTABLISHED instead of being rejected (see the test above).
    const uint32_t flags = butil::HostToNet32(rdma::HELLO_ACK_RDMA_OK);
    ASSERT_TRUE(WriteAll(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, transport);
    ASSERT_EQ(RdmaTransport::RDMA_ON, transport->_rdma_state);
    ASSERT_TRUE(GetSocketFromServer(0) != nullptr);

    ASSERT_TRUE(WriteAll(sockfd, "PRPC", 4));
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// The same bytes on the stream carried by the QP are a real RPC, and the handler
// must decline so that CutInputMessage() moves on to the protocol handlers.
TEST_F(RdmaTest, server_parses_qp_stream_after_rdma_is_on) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(HandshakeUntilAckWait(&sockfd));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    auto* transport = RdmaTransportOf(s);
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, transport);

    const uint32_t flags = butil::HostToNet32(rdma::HELLO_ACK_RDMA_OK);
    ASSERT_TRUE(WriteAll(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, transport);

    InputMessengerProcessor& qp_stream = transport->_rdma_ep->_input_processor;
    ASSERT_TRUE(qp_stream.read_buf().empty());
    qp_stream.read_buf().append("PRPC");
    InputMessageClosure last_msg;
    // The real caller stamps messages with cpuwide_time_us() and derives
    // base_realtime from it, so feed ProcessNewMessage() the same time domain:
    // received_us also ends up in Socket::_last_readtime_us.
    const uint64_t received_us = butil::cpuwide_time_us();
    const uint64_t base_realtime = butil::gettimeofday_us() - received_us;
    ASSERT_EQ(0, qp_stream.ProcessNewMessage(
                     4, false, received_us, base_realtime, last_msg));
    // baidu_std claimed the stream and is waiting for the rest of its header.
    ASSERT_EQ((int)PROTOCOL_BAIDU_STD, s->preferred_index());
    ASSERT_EQ(4u, qp_stream.read_buf().size());
    ASSERT_TRUE(s->fd_input_processor().read_buf().empty());
    ASSERT_EQ(rdma::RdmaEndpoint::ESTABLISHED, transport->_rdma_ep->_state);
    ASSERT_FALSE(s->Failed());

    StopServer();
}

// After the handshake is over, CutInputMessage() still offers the data to every
// registered handler, this one included. It must decline instead of reading the
// data as a fresh client hello.
TEST_F(RdmaTest, server_declines_handshake_bytes_after_fallback) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(HandshakeUntilAckWait(&sockfd));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    auto* transport = RdmaTransportOf(s);

    const uint32_t flags = butil::HostToNet32(0);
    ASSERT_TRUE(WriteAll(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, transport);

    // Replay a valid hello. baidu_std rejects it and no other protocol claims
    // it, so the connection is dropped. What must NOT happen is a second
    // handshake: that would answer with another server hello.
    uint8_t hello[rdma::HELLO_V2_MSG_LEN_MIN];
    MakeV2ClientHello(hello);
    ASSERT_TRUE(WriteAll(sockfd, hello, sizeof(hello)));

    // Note that `transport->_rdma_ep` is gone by now: dropping the connection
    // recycles the Socket, and RdmaTransport::Release() deletes the endpoint.
    ASSERT_TRUE(WaitForServerSocketGone());
    uint8_t reply[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_LE(recv(sockfd, reply, sizeof(reply), MSG_DONTWAIT), 0);

    StopServer();
}

TEST_F(RdmaTest, fd_and_qp_input_streams_are_separate) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    auto* transport = RdmaTransportOf(s);

    // The fd stream belongs to the Socket, the QP stream to the endpoint.
    InputMessengerProcessor& fd_stream = s->fd_input_processor();
    InputMessengerProcessor& qp_stream = transport->_rdma_ep->_input_processor;
    ASSERT_NE(&fd_stream, &qp_stream);
    ASSERT_NE(&fd_stream.read_buf(), &qp_stream.read_buf());

    // Two magic bytes are too few to dispatch on, so they stay buffered. In the
    // fd stream, and only there.
    ASSERT_TRUE(WriteAll(sockfd, "RD", 2));
    ASSERT_TRUE(WaitForFdReadBuf(s, 2));
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, transport->_rdma_ep->_state);
    ASSERT_EQ(2u, fd_stream.read_buf().size());
    ASSERT_TRUE(qp_stream.read_buf().empty());

    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, server_miss_before_hello_send) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(ERPCTIMEDOUT, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_close_before_hello_send) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    close(acc_fd);
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FAILED, RdmaTransportOf(s));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EEOF, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_miss_during_magic_str) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    ASSERT_TRUE(WriteAll(acc_fd, "RD", 2));
    // Half a magic is not enough to decide anything, so the client stays stuck
    // in the handshake read and the RPC runs into its timeout. Joining below
    // waits for exactly that, no sleeping needed.
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(ERPCTIMEDOUT, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_close_during_magic_str) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    // Half a magic and then EOF. TCP keeps the order, so the client always sees
    // the two bytes first and then the close, which is what this test is about.
    ASSERT_TRUE(WriteAll(acc_fd, "RD", 2));
    acc_fd.reset(-1);
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FAILED, RdmaTransportOf(s));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EEOF, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_hello_invalid_magic_str) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    ASSERT_EQ(4, write(acc_fd, "ABCD", 4));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FAILED, RdmaTransportOf(s));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EPROTO, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_miss_during_hello_msg) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
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
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    ASSERT_EQ(4, write(acc_fd, "RDMA", 4));
    ASSERT_EQ(2, write(acc_fd, "00", 2));
    close(acc_fd);
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FAILED, RdmaTransportOf(s));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EEOF, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_hello_invalid_msg_len) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    memcpy(data, "RDMA", 4);
    uint16_t len = butil::HostToNet16(35);
    memcpy(data + 4, &len, 2);
    memset(data + 6, 0, 32);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, write(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FAILED, RdmaTransportOf(s));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EPROTO, cntl.ErrorCode());
}

TEST_F(RdmaTest, server_hello_invalid_version) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    memcpy(data, "RDMA", 4);
    uint16_t len = butil::HostToNet16(rdma::HELLO_V2_MSG_LEN_MIN);
    memcpy(data + 4, &len, 2);
    memset(data + 6, 0, 32);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, write(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
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
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));

    rdma::v2_wire::HelloMessage msg{};
    msg.msg_len = rdma::HELLO_V2_MSG_LEN_MIN;
    msg.hello_ver = 1;
    msg.impl_ver = 1;
    msg.sq_size = 0;
    msg.rq_size = 0;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, write(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));

    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
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
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));

    rdma::v2_wire::HelloMessage msg{};
    msg.msg_len = rdma::HELLO_V2_MSG_LEN_MIN;
    msg.hello_ver = rdma::HELLO_V2_VERSION;
    msg.impl_ver = rdma::IMPL_V2_VERSION;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, write(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));

    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, RdmaTransportOf(s));
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
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));

    rdma::v2_wire::HelloMessage msg{};
    msg.msg_len = rdma::HELLO_V2_MSG_LEN_MIN;
    msg.hello_ver = rdma::HELLO_V2_VERSION;
    msg.impl_ver = rdma::IMPL_V2_VERSION;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, write(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));

    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, RdmaTransportOf(s));
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
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT, RdmaTransportOf(s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);
    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));

    rdma::v2_wire::HelloMessage msg{};
    msg.msg_len = rdma::HELLO_V2_MSG_LEN_MIN;
    msg.hello_ver = rdma::HELLO_V2_VERSION;
    msg.impl_ver = rdma::IMPL_V2_VERSION;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, write(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));

    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, RdmaTransportOf(s));
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, write(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    bthread_id_join(cntl.call_id());

    ASSERT_EQ(EPROTO, cntl.ErrorCode());
}


TEST_F(RdmaTest, v2_client_hello_bytes_baseline) {
    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);

    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(acc_fd, data, rdma::HELLO_V2_MSG_LEN_MIN));

    // [0..4) magic
    ASSERT_EQ(0, memcmp(data, "RDMA", 4));
    // [4..6) msg_len, big-endian uint16 == 40
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN,
              (size_t)(((uint16_t)data[4] << 8) | (uint16_t)data[5]));
    // [6..8) hello_ver, big-endian uint16 == rdma::HELLO_V2_VERSION
    ASSERT_EQ(rdma::HELLO_V2_VERSION,
              (uint16_t)(((uint16_t)data[6] << 8) | (uint16_t)data[7]));
    // [8..10) impl_ver, big-endian uint16 == rdma::IMPL_V2_VERSION
    ASSERT_EQ(rdma::IMPL_V2_VERSION,
              (uint16_t)(((uint16_t)data[8] << 8) | (uint16_t)data[9]));

    rdma::v2_wire::HelloMessage msg{};
    msg.Deserialize(data + 4);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, msg.msg_len);
    ASSERT_EQ(rdma::HELLO_V2_VERSION, msg.hello_ver);
    ASSERT_EQ(rdma::IMPL_V2_VERSION,  msg.impl_ver);

    bthread_id_join(cntl.call_id());
}

TEST_F(RdmaTest, v2_server_hello_bytes_baseline) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    // Send a well-formed v2 hello so the server enters S_ACK_WAIT.
    rdma::v2_wire::HelloMessage msg{};
    msg.msg_len = rdma::HELLO_V2_MSG_LEN_MIN;
    msg.hello_ver = rdma::HELLO_V2_VERSION;
    msg.impl_ver = rdma::IMPL_V2_VERSION;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();

    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, write(sockfd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));

    // Read server's reply hello and assert its byte-level layout.
    uint8_t reply[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, read(sockfd, reply, rdma::HELLO_V2_MSG_LEN_MIN));

    ASSERT_EQ(0, memcmp(reply, "RDMA", 4));
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN,
              (size_t)(((uint16_t)reply[4] << 8) | (uint16_t)reply[5]));
    ASSERT_EQ(rdma::HELLO_V2_VERSION,
              (uint16_t)(((uint16_t)reply[6] << 8) | (uint16_t)reply[7]));
    ASSERT_EQ(rdma::IMPL_V2_VERSION,
              (uint16_t)(((uint16_t)reply[8] << 8) | (uint16_t)reply[9]));

    rdma::v2_wire::HelloMessage reply_msg{};
    reply_msg.Deserialize(reply + 4);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, reply_msg.msg_len);
    ASSERT_EQ(rdma::HELLO_V2_VERSION, reply_msg.hello_ver);
    ASSERT_EQ(rdma::IMPL_V2_VERSION,  reply_msg.impl_ver);

    // Drive the server into FALLBACK_TCP via ACK flags=0 so the test ends
    // cleanly without requiring real RDMA hardware.
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ(sizeof(flags), write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));

    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, v2_server_drains_tail_then_reads_ack) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    // Build a v2 hello with msg_len = 48 (40 base + 8B zero tail).
    rdma::v2_wire::HelloMessage msg{};
    msg.msg_len = 48;
    msg.hello_ver = rdma::HELLO_V2_VERSION;
    msg.impl_ver = rdma::IMPL_V2_VERSION;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();

    uint8_t buf[48];
    memcpy(buf, "RDMA", 4);
    msg.Serialize(buf + 4);
    memset(buf + 40, 0x00, 8);  // 8B zero tail
    ASSERT_TRUE(WriteAll(sockfd, buf, 48));
    // The tail is drained as part of the hello, so the server ends up waiting
    // for the ACK. Wait for that before sending it, otherwise the ACK could
    // ride along in the same read and this would no longer test the drain.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));

    // Send the real ACK (flags=1 = ACK_MSG_RDMA_OK).
    uint32_t flags = butil::HostToNet32(1);
    ASSERT_EQ(sizeof(flags), write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, RdmaTransportOf(s));

    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, v2_server_rejects_oversized_msg_len) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    // Build a v2 hello with msg_len = 4097 (HELLO_V2_MSG_LEN_MAX + 1).
    // We only send the 40B base; the server must reject before reading
    // (and definitely before attempting to drain) any "tail".
    rdma::v2_wire::HelloMessage msg{};
    msg.msg_len = 4097;
    msg.hello_ver = rdma::HELLO_V2_VERSION;
    msg.impl_ver = rdma::IMPL_V2_VERSION;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();

    uint8_t buf[rdma::HELLO_V2_MSG_LEN_MIN];
    memcpy(buf, "RDMA", 4);
    msg.Serialize(buf + 4);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN, write(sockfd, buf, rdma::HELLO_V2_MSG_LEN_MIN));
    ASSERT_TRUE(WaitForServerSocketGone());

    sockfd.reset(-1);

    StopServer();
}

// RAII for FLAGS_rdma_client_handshake_version: lets us flip the
// client-side handshake version for a single test and restore it on
// scope exit so subsequent tests stay on the v2 default.
class HandshakeVersionFlag {
public:
    explicit HandshakeVersionFlag(int v)
        : _saved(rdma::FLAGS_rdma_client_handshake_version) {
        rdma::FLAGS_rdma_client_handshake_version = v;
    }
    ~HandshakeVersionFlag() {
        rdma::FLAGS_rdma_client_handshake_version = _saved;
    }
private:
    int _saved;
};

// Build a v3 wire packet from an RdmaHello: "RDM3" + pb_size_be + body.
std::string MakeV3Packet(const rdma::RdmaHello& msg) {
    std::string body;
    EXPECT_TRUE(msg.SerializeToString(&body));
    std::string packet;
    packet.reserve(4 + 4 + body.size());
    packet.append("RDM3", 4);
    uint32_t pb_size_be =
        butil::HostToNet32(static_cast<uint32_t>(body.size()));
    packet.append(reinterpret_cast<const char*>(&pb_size_be), 4);
    packet.append(body);
    return packet;
}

// Build a fully-valid RdmaHello: all 6 required fields are set, with
// values that pass RdmaHelloV3Wire::RdmaHelloValid().
//   - block_size = 8192 (>= MIN_BLOCK_SIZE)
//   - sq_size / rq_size = 16 (>= MIN_QP_SIZE)
//   - gid = exactly 16B (sizeof(ibv_gid))
//   - qp_num = 0  (allowed because g_skip_rdma_init in UT)
rdma::RdmaHello MakeValidV3Hello() {
    rdma::RdmaHello msg;
    msg.set_block_size(8192);
    msg.set_sq_size(16);
    msg.set_rq_size(16);
    msg.set_lid(0);
    ibv_gid gid = rdma::GetRdmaGid();
    msg.set_gid(std::string(reinterpret_cast<const char*>(gid.raw),
                            sizeof(gid.raw)));
    msg.set_qp_num(0);
    return msg;
}


TEST_F(RdmaTest, v3_client_hello_bytes_baseline) {
    HandshakeVersionFlag _hsv(3);

    butil::fd_guard sockfd(butil::tcp_listen(g_ep));
    EXPECT_TRUE(sockfd >= 0);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    butil::fd_guard acc_fd(accept(sockfd, nullptr, nullptr));
    ASSERT_TRUE(acc_fd >= 0);

    // [0..4) magic "RDM3"
    uint8_t magic[4];
    ASSERT_EQ(4, read(acc_fd, magic, 4));
    ASSERT_EQ(0, memcmp(magic, "RDM3", 4));

    // [4..8) pb_size, big-endian uint32, must be in (0, 4096]
    uint8_t size_buf[4];
    ASSERT_EQ(4, read(acc_fd, size_buf, 4));
    uint32_t pb_size =
        butil::NetToHost32(*reinterpret_cast<uint32_t*>(size_buf));
    ASSERT_GT(pb_size, 0u);
    ASSERT_LE(pb_size, 4096u);

    // [8..8+pb_size) RdmaHello protobuf body.
    std::string body(pb_size, '\0');
    ASSERT_EQ((ssize_t)pb_size, read(acc_fd, &body[0], pb_size));
    rdma::RdmaHello msg;
    ASSERT_TRUE(msg.ParseFromString(body));

    // All 6 required fields must be present (ParseFromString would
    // have already returned false otherwise).
    ASSERT_TRUE(msg.has_block_size());
    ASSERT_TRUE(msg.has_sq_size());
    ASSERT_TRUE(msg.has_rq_size());
    ASSERT_TRUE(msg.has_lid());
    ASSERT_TRUE(msg.has_gid());
    ASSERT_TRUE(msg.has_qp_num());
    // gid wire encoding must be exactly 16 bytes (sizeof(ibv_gid)).
    ASSERT_EQ(sizeof(ibv_gid), msg.gid().size());

    // Let the RPC time out and release resources.
    bthread_id_join(cntl.call_id());
}

TEST_F(RdmaTest, v3_server_hello_bytes_baseline) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    // Send a valid v3 hello.
    std::string packet = MakeV3Packet(MakeValidV3Hello());
    ASSERT_EQ((ssize_t)packet.size(),
              write(sockfd, packet.data(), packet.size()));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));

    // Read server's reply hello: 4B magic + 4B pb_size + body.
    uint8_t reply_magic[4];
    ASSERT_EQ(4, read(sockfd, reply_magic, 4));
    ASSERT_EQ(0, memcmp(reply_magic, "RDM3", 4));

    uint8_t size_buf[4];
    ASSERT_EQ(4, read(sockfd, size_buf, 4));
    uint32_t pb_size =
        butil::NetToHost32(*reinterpret_cast<uint32_t*>(size_buf));
    ASSERT_GT(pb_size, 0u);
    ASSERT_LE(pb_size, 4096u);

    std::string body(pb_size, '\0');
    ASSERT_EQ((ssize_t)pb_size, read(sockfd, &body[0], pb_size));
    rdma::RdmaHello reply;
    ASSERT_TRUE(reply.ParseFromString(body));
    ASSERT_TRUE(reply.has_block_size());
    ASSERT_TRUE(reply.has_sq_size());
    ASSERT_TRUE(reply.has_rq_size());
    ASSERT_TRUE(reply.has_gid());
    ASSERT_EQ(sizeof(ibv_gid), reply.gid().size());

    // Drive the server into FALLBACK_TCP via ACK flags=0 so the test ends
    // cleanly without requiring real RDMA hardware.
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ((ssize_t)sizeof(flags),
              write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));

    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, v3_server_rejects_zero_pb_size) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    // "RDM3" + pb_size = 0 (4B big-endian zero).
    uint8_t buf[8] = {'R', 'D', 'M', '3', 0, 0, 0, 0};
    ASSERT_EQ(8, write(sockfd, buf, 8));
    ASSERT_TRUE(WaitForServerSocketGone());

    sockfd.reset(-1);
    StopServer();
}

TEST_F(RdmaTest, v3_server_rejects_oversized_pb_size) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    uint8_t buf[8];
    memcpy(buf, "RDM3", 4);
    // pb_size just above the allowed maximum -> rejected.
    uint32_t pb_size_be =
        butil::HostToNet32(static_cast<uint32_t>(rdma::HELLO_V3_MAX_PB_SIZE + 1));
    memcpy(buf + 4, &pb_size_be, 4);
    ASSERT_EQ(8, write(sockfd, buf, 8));
    ASSERT_TRUE(WaitForServerSocketGone());

    sockfd.reset(-1);
    StopServer();
}

TEST_F(RdmaTest, v3_server_rejects_invalid_pb_bytes) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    // "RDM3" + pb_size = 8 + 8 bytes of 0xff (invalid protobuf body).
    uint8_t buf[16];
    memcpy(buf, "RDM3", 4);
    uint32_t pb_size_be = butil::HostToNet32(8);
    memcpy(buf + 4, &pb_size_be, 4);
    memset(buf + 8, 0xff, 8);
    ASSERT_EQ(16, write(sockfd, buf, 16));
    ASSERT_TRUE(WaitForServerSocketGone());

    sockfd.reset(-1);
    StopServer();
}

TEST_F(RdmaTest, v3_server_invalid_sq_size_falls_back) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    rdma::RdmaHello msg = MakeValidV3Hello();
    msg.set_sq_size(0);  // invalid: < MIN_QP_SIZE (16)
    std::string packet = MakeV3Packet(msg);
    ASSERT_TRUE(WriteAll(sockfd, packet.data(), packet.size()));

    // Server validated the hello as invalid -> _rdma_state = RDMA_OFF,
    // but still proceeds to S_ACK_WAIT (sends its own reply hello).
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);

    // Drain server's reply hello (content not asserted here; covered
    // by v3_server_hello_bytes_baseline).
    uint8_t reply_hdr[8];
    ASSERT_EQ(8, read(sockfd, reply_hdr, 8));
    ASSERT_EQ(0, memcmp(reply_hdr, "RDM3", 4));
    uint32_t reply_pb_size = butil::NetToHost32(
            *reinterpret_cast<uint32_t*>(reply_hdr + 4));
    std::string reply_body(reply_pb_size, '\0');
    ASSERT_EQ((ssize_t)reply_pb_size,
              read(sockfd, &reply_body[0], reply_pb_size));

    // Client ACK flags=0 -> server settles into FALLBACK_TCP.
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ((ssize_t)sizeof(flags),
              write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));

    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// RAII guard to toggle FLAGS_rdma_ece for a single test and restore it.
class EceFlagGuard {
public:
    explicit EceFlagGuard(bool v) : _saved(rdma::FLAGS_rdma_ece) {
        rdma::FLAGS_rdma_ece = v;
    }
    ~EceFlagGuard() {
        rdma::FLAGS_rdma_ece = _saved;
    }
private:
    bool _saved;
};

// Build a valid v3 hello that also carries an ECE block.
rdma::RdmaHello MakeValidV3HelloWithEce(uint32_t vendor_id,
                                        uint32_t options,
                                        uint32_t comp_mask) {
    rdma::RdmaHello msg = MakeValidV3Hello();
    rdma::RdmaEce* ece = msg.mutable_ece();
    ece->set_vendor_id(vendor_id);
    ece->set_options(options);
    ece->set_comp_mask(comp_mask);
    return msg;
}

// Read the server's v3 reply hello (4B magic + 4B pb_size + body) and parse
// it into `reply`. Asserts the framing along the way.
static void ReadServerV3Reply(int fd, rdma::RdmaHello* reply) {
    uint8_t reply_hdr[8];
    ASSERT_EQ(8, read(fd, reply_hdr, 8));
    ASSERT_EQ(0, memcmp(reply_hdr, "RDM3", 4));
    uint32_t reply_pb_size = butil::NetToHost32(*reinterpret_cast<uint32_t*>(reply_hdr + 4));
    ASSERT_GT(reply_pb_size, 0u);
    ASSERT_LE(reply_pb_size, 4096u);
    std::string reply_body(reply_pb_size, '\0');
    ASSERT_EQ((ssize_t)reply_pb_size,
              read(fd, &reply_body[0], reply_pb_size));
    ASSERT_TRUE(reply->ParseFromString(reply_body));
}

// A client hello carrying ECE must not break the server handshake: with ECE
// enabled the server still parses the hello and advances to S_ACK_WAIT.
TEST_F(RdmaTest, v3_server_accepts_client_hello_with_ece) {
    EceFlagGuard ece_flag_guard(true);
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    rdma::RdmaHello msg = MakeValidV3HelloWithEce(0x02c9, 0x1, 0x0);
    std::string packet = MakeV3Packet(msg);
    ASSERT_EQ((ssize_t)packet.size(),
              write(sockfd, packet.data(), packet.size()));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));

    rdma::RdmaHello reply;
    ReadServerV3Reply(sockfd, &reply);

    // ACK flags=0 -> clean FALLBACK_TCP so the test ends without hardware.
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ((ssize_t)sizeof(flags), write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));

    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());
    StopServer();
}

// When ECE negotiation is disabled, the server reply must NOT advertise ECE,
// even if the client advertised it (FillLocalRdmaHello degrade branch #1).
TEST_F(RdmaTest, v3_server_reply_has_no_ece_when_disabled) {
    EceFlagGuard ece_flag_guard(false);
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    rdma::RdmaHello msg = MakeValidV3HelloWithEce(0x02c9, 0x1, 0x0);
    std::string packet = MakeV3Packet(msg);
    ASSERT_TRUE(WriteAll(sockfd, packet.data(), packet.size()));

    // Reading the reply in full doubles as the synchronization point.
    rdma::RdmaHello reply;
    ReadServerV3Reply(sockfd, &reply);
    EXPECT_FALSE(reply.has_ece());

    uint32_t flags = butil::HostToNet32(0);
    ASSERT_TRUE(WriteAll(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));

    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());
    StopServer();
}

// When ECE is enabled but there is no negotiated result (UT skips the real QP
// bring-up, so the server never fills _outgoing_ece), the server reply must
// still NOT advertise ECE (FillLocalRdmaHello degrade branch #2 -> degrade-safe).
TEST_F(RdmaTest, v3_server_reply_has_no_ece_without_hw_negotiation) {
    EceFlagGuard ece_flag_guard(true);
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    rdma::RdmaHello msg = MakeValidV3HelloWithEce(0x02c9, 0x1, 0x0);
    std::string packet = MakeV3Packet(msg);
    ASSERT_TRUE(WriteAll(sockfd, packet.data(), packet.size()));

    // Reading the reply in full doubles as the synchronization point.
    rdma::RdmaHello reply;
    ReadServerV3Reply(sockfd, &reply);
    EXPECT_FALSE(reply.has_ece());

    uint32_t flags = butil::HostToNet32(0);
    ASSERT_TRUE(WriteAll(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));

    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());
    StopServer();
}

class ResourceAllocFailGuard {
public:
    explicit ResourceAllocFailGuard(bool v)
        : _saved(rdma::g_fail_resource_alloc_for_test) {
        rdma::g_fail_resource_alloc_for_test = v;
    }
    ~ResourceAllocFailGuard() {
        rdma::g_fail_resource_alloc_for_test = _saved;
    }
private:
    bool _saved;
};

TEST_F(RdmaTest, client_alloc_resource_fail_fallback_tcp) {
    StartServer();
    ResourceAllocFailGuard alloc_fail_guard(true);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    // The socket must not be failed, otherwise it can no longer carry TCP.
    ASSERT_FALSE(s->Failed());

    // The RPC still completes over TCP.
    bthread_id_join(cntl.call_id());
    ASSERT_EQ(0, cntl.ErrorCode()) << cntl.ErrorText();

    StopServer();
}

TEST_F(RdmaTest, server_alloc_resource_fail_fallback_tcp) {
    StartServer();
    ResourceAllocFailGuard alloc_fail_guard(true);

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    Socket* s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    // Send a well-formed v2 hello: the negotiation succeeds
    // but the resource allocation does not.
    rdma::v2_wire::HelloMessage msg{};
    msg.msg_len = rdma::HELLO_V2_MSG_LEN_MIN;
    msg.hello_ver = rdma::HELLO_V2_VERSION;
    msg.impl_ver = rdma::IMPL_V2_VERSION;
    msg.sq_size = 16;
    msg.rq_size = 16;
    msg.block_size = 8192;
    msg.qp_num = 0;
    msg.gid = rdma::GetRdmaGid();

    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    memcpy(data, "RDMA", 4);
    msg.Serialize(data + 4);
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN,
              write(sockfd, data, rdma::HELLO_V2_MSG_LEN_MIN));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    ASSERT_FALSE(s->Failed());

    // Ack without RDMA so that the server finishes the handshake in TCP mode.
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ(sizeof(flags), write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    ASSERT_FALSE(s->Failed());

    sockfd.reset(-1);
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, try_global_disable_rdma) {
    StartServer();
    rdma::g_rdma_available.store(false, butil::memory_order_relaxed);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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
    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    bthread_id_join(cntl.call_id());
    ASSERT_EQ(0, cntl.ErrorCode());

    StopServer();
    rdma::g_rdma_available.store(true, butil::memory_order_relaxed);
}

TEST_F(RdmaTest, server_option_invalid) {
    Server server;
    ServerOptions options;
    options.socket_mode = SOCKET_MODE_RDMA;

    // rtmp and rdma are incompatible
    options.rtmp_service = (RtmpService*)1;
    ASSERT_EQ(-1, server.Start(PORT, &options));

    // nshead and rdma are incompatible
    options.rtmp_service = nullptr;
    options.nshead_service = (NsheadService*)1;
    ASSERT_EQ(-1, server.Start(PORT, &options));

    // mongo and rdma are incompatible
    options.nshead_service = nullptr;
    options.mongo_service_adaptor = (MongoServiceAdaptor*)1;
    ASSERT_EQ(-1, server.Start(PORT, &options));

    // ssl and rdma are incompatible
    options.mongo_service_adaptor = nullptr;
    options.mutable_ssl_options()->default_cert.certificate = "test";
    ASSERT_EQ(-1, server.Start(PORT, &options));
}

TEST_F(RdmaTest, channel_option_invalid) {
    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;

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

// Rounds, per-round RPC count and attachment sizes shared by the end-to-end
// tests below. One RPC per test leaves everything that only shows up on the
// second message untouched -- buffer reuse, an EOF read racing another writer
// of the same input stream, resource recycling.
static const int E2E_ROUND_NUM = 3;
static const int E2E_RPC_NUM = 32;
static const size_t E2E_ATTACH_SIZE[] = { 0, 4096, 128 * 1024 };

static void ShutdownClientConnection(Controller& cntl) {
    SocketUniquePtr s;
    if (Socket::Address(cntl._single_server_id, &s) == 0) {
        ::shutdown(s->fd(), SHUT_WR);
    }
}

// Returns the number of RPCs that succeeded. A test that severs the connection
// cannot predict which ones make it, but the ones that do must still be right,
// so failures are tolerated here and the caller decides how many it demands.
static int SendEchoRpcs(Channel& channel, int rpc_num, size_t attach_size,
                        const std::function<void(Controller&)>& disturb = nullptr,
                        int disturb_at = 0) {
    std::vector<Controller> cntl(rpc_num);
    std::vector<test::EchoRequest> req(rpc_num);
    std::vector<test::EchoResponse> res(rpc_num);
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

static void SendEchoRpcsInRounds(Channel& channel) {
    for (int round = 0; round < E2E_ROUND_NUM; ++round) {
        for (size_t i = 0; i < arraysize(E2E_ATTACH_SIZE); ++i) {
            ASSERT_EQ(E2E_RPC_NUM,
                      SendEchoRpcs(channel, E2E_RPC_NUM, E2E_ATTACH_SIZE[i]))
                    << "round=" << round
                    << " attach_size=" << E2E_ATTACH_SIZE[i];
        }
    }
}

TEST_P(RdmaRpcTest, rdma_client_to_rdma_server) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 5000;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    ASSERT_NO_FATAL_FAILURE(SendEchoRpcsInRounds(channel));

    StopServer();
}

TEST_P(RdmaRpcTest, tcp_client_to_tcp_server) {
    StartServer(false);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 5000;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    ASSERT_NO_FATAL_FAILURE(SendEchoRpcsInRounds(channel));

    StopServer();
}

TEST_P(RdmaRpcTest, tcp_client_to_rdma_server) {
    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 5000;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    ASSERT_NO_FATAL_FAILURE(SendEchoRpcsInRounds(channel));

    StopServer();
}

TEST_P(RdmaRpcTest, rdma_client_to_tcp_server) {
    StartServer(false);

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 5000;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    ASSERT_NO_FATAL_FAILURE(SendEchoRpcsInRounds(channel));

    StopServer();
}

TEST_P(RdmaRpcTest, tcp_client_to_rdma_server_short_connection) {
    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.connect_timeout_ms = 1000;
    chan_options.timeout_ms = 10000;
    chan_options.max_retry = 0;
    chan_options.connection_type = "short";
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    for (int round = 0; round < 8; ++round) {
        ASSERT_EQ(E2E_RPC_NUM, SendEchoRpcs(channel, E2E_RPC_NUM, 4096))
                << "round=" << round;
    }

    StopServer();
}

// Rounds of connection churn: a race needs attempts, not one well-timed shot.
static const int CHURN_ROUND_NUM = 16;
static const int CHURN_RPC_NUM = 64;
static const size_t CHURN_ATTACH_SIZE = 32 * 1024;

TEST_P(RdmaRpcTest, rdma_server_survives_connection_churn) {
    StartServer();

    ChannelOptions chan_options;
    chan_options.connect_timeout_ms = 1000;
    chan_options.timeout_ms = 3000;
    chan_options.max_retry = 0;
    const int served_before = g_echo_served.load(butil::memory_order_relaxed);
    int succeeded = 0;
    for (int round = 0; round < CHURN_ROUND_NUM; ++round) {
        // A fresh Channel per round so the Socket is dropped from the socket
        // map when the Channel dies, instead of the next round inheriting it.
        Channel channel;
        ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
        // Warm up before cutting. Without this the cut of round 0 lands on a
        // connection that has not finished connecting yet, where fd() is still
        // -1 and shutdown() is a silent no-op.
        ASSERT_EQ(1, SendEchoRpcs(channel, 1, 0)) << "round=" << round;
        succeeded += SendEchoRpcs(channel, CHURN_RPC_NUM, CHURN_ATTACH_SIZE,
                                  ShutdownClientConnection,
                                  round * CHURN_RPC_NUM / CHURN_ROUND_NUM);
        ASSERT_FALSE(HasFailure()) << "round=" << round;
    }

    const int served = g_echo_served.load(butil::memory_order_relaxed) -
                       served_before - CHURN_ROUND_NUM;
    LOG(INFO) << "server served " << served << " of "
              << CHURN_ROUND_NUM * CHURN_RPC_NUM << " requests during the churn, "
              << succeeded << " replies made it back";
    ASSERT_GT(served, 0);

    // The churn must leave the server able to serve a fresh connection, and
    // serve it correctly.
    Channel channel;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    ASSERT_EQ(CHURN_RPC_NUM, SendEchoRpcs(channel, CHURN_RPC_NUM, CHURN_ATTACH_SIZE));

    StopServer();
}

static const int RPC_NUM = 1024;

void DumpRdmaEndpointInfo(Socket* client, Socket* server) {
    std::cout << std::endl << "client:";
    static_cast<RdmaTransport*>(client->_transport.get())->_rdma_ep->DebugInfo(std::cout);
    std::cout << std::endl << "server:";
    static_cast<RdmaTransport*>(server->_transport.get())->_rdma_ep->DebugInfo(std::cout);
}

TEST_P(RdmaRpcTest, send_rpcs_in_one_qp) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 50000;
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
                    EOVERCROWDED == cntl[i].ErrorCode()) << "req[" << i << "] " << berror(cntl[i].ErrorCode());
    }

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl[0]._single_server_id, &s));
    Socket* m = GetSocketFromServer(0);
    DumpRdmaEndpointInfo(s.get(), m);

    StopServer();
}

TEST_P(RdmaRpcTest, send_rpc_in_many_qp) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    butil::ip_t ip;
    ASSERT_EQ(0, butil::str2ip(g_ip.c_str(), &ip));

    Server server[100];
    MyEchoService svc[100];
    int num = 100;
    butil::EndPoint server_eps[100];
    for (int i = 0; i < num; ++i) {
        ServerOptions options;
        options.socket_mode = SOCKET_MODE_RDMA;
        options.idle_timeout_sec = 1;
        options.max_concurrency = 0;
        options.internal_port = -1;
        server[i].AddService(&svc[i], SERVER_DOESNT_OWN_SERVICE);
        ASSERT_EQ(0, server[i].Start(0, &options));
        server_eps[i] = butil::EndPoint(ip, server[i].listen_address().port);
    }

    int port = 0;
    butil::IOBuf attach;
    attach.resize(4096);
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 100000;
    chan_options.max_retry = 0;
    Channel channel[RPC_NUM];
    Server* svr[RPC_NUM];
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];
    for (int i = 0; i < RPC_NUM; ++i) {
        svr[i] = &server[i % num];
        ASSERT_EQ(0, channel[i].Init(server_eps[(port++) % num], &chan_options));
        req[i].set_message(__FUNCTION__);
        cntl[i].request_attachment().append(attach);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel[i]).Echo(&cntl[i], &req[i], &res[i], done);
    }
    for (int i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        if (cntl[i].ErrorCode() == ERPCTIMEDOUT) {
            SocketUniquePtr s;
            EXPECT_EQ(0, Socket::Address(cntl[i]._single_server_id, &s));
            if (s && svr[i] && svr[i]->_am) {
                std::vector<SocketId> sids;
                svr[i]->_am->ListConnections(&sids);
                for (size_t j = 0; j < sids.size(); ++j) {
                    SocketUniquePtr m;
                    if (Socket::AddressFailedAsWell(sids[j], &m) == 0) {
                        DumpRdmaEndpointInfo(s.get(), m.get());
                    }
                }
            }
        }
        EXPECT_EQ(0, cntl[i].ErrorCode()) << "req[" << i << "]";
    }

    for (int i = 0; i < num; ++i) {
        server[i].Stop(0);
        server[i].Join();
    }
}

TEST_P(RdmaRpcTest, send_rpcs_as_pooled_connection) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

TEST_P(RdmaRpcTest, send_rpcs_as_short_connection) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

TEST_P(RdmaRpcTest, server_stop_during_rpc) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

TEST_P(RdmaRpcTest, server_close_during_rpc) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

TEST_P(RdmaRpcTest, client_close_during_rpc) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

TEST_P(RdmaRpcTest, rdma_client_close_during_rpc_repeatedly) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
    chan_options.connect_timeout_ms = 1000;
    chan_options.timeout_ms = 3000;
    chan_options.max_retry = 0;
    const int served_before = g_echo_served.load(butil::memory_order_relaxed);
    int succeeded = 0;
    for (int round = 0; round < CHURN_ROUND_NUM; ++round) {
        Channel channel;
        ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
        // Warm up so the cut lands on an established RDMA connection.
        ASSERT_EQ(1, SendEchoRpcs(channel, 1, 0)) << "round=" << round;
        succeeded += SendEchoRpcs(channel, CHURN_RPC_NUM, CHURN_ATTACH_SIZE,
                                  ShutdownClientConnection,
                                  round * CHURN_RPC_NUM / CHURN_ROUND_NUM);
        ASSERT_FALSE(HasFailure()) << "round=" << round;
    }


    const int served = g_echo_served.load(butil::memory_order_relaxed) -
                       served_before - CHURN_ROUND_NUM;
    LOG(INFO) << "server served " << served << " of "
              << CHURN_ROUND_NUM * CHURN_RPC_NUM << " requests during the churn, "
              << succeeded << " replies made it back";
    ASSERT_GT(served, 0);

    Channel channel;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    ASSERT_EQ(CHURN_RPC_NUM, SendEchoRpcs(channel, CHURN_RPC_NUM, CHURN_ATTACH_SIZE));

    StopServer();
}

TEST_P(RdmaRpcTest, verbs_error_handling) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &s));
    // The QP below only exists once the handshake is over.
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, RdmaTransportOf(s));
    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    ibv_sge sge;
    void* buf = malloc(8192);
    sge.addr = (uint64_t)buf;
    sge.length = 8192;
    sge.lkey = 1;  // incorrect lkey
    wr.sg_list = &sge;
    wr.num_sge = 1;
    ibv_send_wr* bad = nullptr;
    auto rdma_transport = RdmaTransportOf(s);
    ibv_post_send(rdma_transport->_rdma_ep->_resource->qp, &wr, &bad);
    bthread_id_join(cntl.call_id());
    ASSERT_EQ(ERDMA, cntl.ErrorCode());
    free(buf);

    StopServer();
}

TEST_P(RdmaRpcTest, rdma_use_parallel_channel) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    const size_t NCHANS = 8;
    Channel subchans[NCHANS];
    ParallelChannel channel;
    ChannelOptions opts;
    opts.socket_mode = SOCKET_MODE_RDMA;
    for (size_t i = 0; i < NCHANS; ++i) {
        ASSERT_EQ(0, subchans[i].Init(_naming_url.c_str(), "rR", &opts));
        ASSERT_EQ(0, channel.AddChannel(
                    &subchans[i], DOESNT_OWN_CHANNEL,
                    nullptr, nullptr));
    }
    ASSERT_EQ(0, channel.Init(nullptr));

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, nullptr);

    ASSERT_EQ(0, cntl.ErrorCode());
    ASSERT_EQ(NCHANS, (size_t)cntl.sub_count());

    StopServer();
}

TEST_P(RdmaRpcTest, rdma_use_selective_channel) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    const size_t NCHANS = 8;
    SelectiveChannel channel;
    ChannelOptions opts;
    opts.socket_mode = SOCKET_MODE_RDMA;
    ASSERT_EQ(0, channel.Init("rr", &opts));
    for (size_t i = 0; i < NCHANS; ++i) {
        Channel* subchan = new Channel;
        ASSERT_EQ(0, subchan->Init(_naming_url.c_str(), "rR", &opts));
        ASSERT_EQ(0, channel.AddChannel(subchan, nullptr));
    }

    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message(__FUNCTION__);
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, nullptr);

    ASSERT_EQ(0, cntl.ErrorCode()) << cntl.ErrorText();
    ASSERT_EQ(1, cntl.sub_count());

    StopServer();
}

static void MockFree(void* buf) { }

TEST_P(RdmaRpcTest, send_rpcs_with_user_defined_iobuf) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
    chan_options.connect_timeout_ms = 500;
    chan_options.timeout_ms = 500;
    chan_options.max_retry = 0;
    ASSERT_EQ(0, channel.Init(g_ep, &chan_options));
    Controller cntl[RPC_NUM];
    test::EchoRequest req[RPC_NUM];
    test::EchoResponse res[RPC_NUM];

    butil::IOBuf attach;
    void* data = malloc(4096);;
    attach.append_user_data(data, 4096, nullptr);
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

TEST_P(RdmaRpcTest, try_memory_pool_empty) {
    if (!FLAGS_rdma_test_enable) {
        return;
    }

    StartServer();

    Channel channel;
    ChannelOptions chan_options;
    chan_options.socket_mode = SOCKET_MODE_RDMA;
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

// Run every TEST_P(RdmaRpcTest, ...) above twice: once with the
// client-side handshake forced to v2 ("RDMA" magic + fixed-layout
// HelloMessage), once with v3 ("RDM3" magic + protobuf RdmaHello).
// The server always accepts both via magic-byte dispatch, so this
// proves the upper-layer RPC paths behave identically under either
// wire format.
INSTANTIATE_TEST_SUITE_P(
    HandshakeVersion, RdmaRpcTest,
    ::testing::Values(2, 3),
    [](const ::testing::TestParamInfo<int>& info) {
        return std::string("v") + std::to_string(info.param);
    });

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
