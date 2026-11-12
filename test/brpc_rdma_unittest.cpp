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
#include <algorithm>                            // std::min
#include <functional>
#include <sstream>
#include <vector>
#include <google/protobuf/descriptor.h>
#include "butil/atomicops.h"
#include "butil/endpoint.h"
#include "butil/fd_guard.h"
#include "butil/iobuf.h"
#include "butil/memory/scope_guard.h"
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
#include "brpc/rdma/rdma_connection.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rq_connection.h"
#include "brpc/rdma/rdma_handshake.h"
#include "brpc/rdma/rdma_handshake_constants.h"
#include "brpc/rdma/rdma_handshake.pb.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/srq_worker.h"
#include "rdma_test_helper.h"


static const int PORT = 8713;

using namespace brpc;
using namespace brpc::rdma_test;

namespace brpc {

DECLARE_int64(socket_max_unwritten_bytes);
DECLARE_bool(log_idle_connection_close);
DEFINE_bool(rdma_test_enable, false, "Enable tests requring rdma runtime.");

namespace rdma {

namespace v3_wire {
bool ValidRdmaHello(const RdmaHello& msg);
void TranslateHello(const RdmaHello& msg, ParsedHello* out);
}

// HELLO_V2_VERSION / IMPL_V2_VERSION come from
// brpc/rdma/rdma_handshake_constants.h (shared wire constants).

DECLARE_bool(rdma_trace_verbose);
DECLARE_int32(rdma_memory_pool_max_regions);
DECLARE_int32(rdma_client_handshake_version);
DECLARE_bool(rdma_ece);

extern ibv_qp* (*IbvCreateQp)(ibv_pd*, ibv_qp_init_attr*);
extern int (*IbvModifyQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask);
extern int (*IbvQueryQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask, ibv_qp_init_attr*);
extern butil::atomic<bool> g_rdma_available;
extern bool g_skip_rdma_init;
extern bool g_fail_resource_alloc_for_test;
extern uint32_t InlineDataToRequest(uint32_t vendor_id, uint32_t size,
                                    bool refused);
extern ibv_qp* CreateQpWithInlineData(ibv_pd* pd, ibv_qp_init_attr* attr,
                                      uint32_t vendor_id,
                                      uint32_t* max_inline_data);
extern butil::atomic<uint32_t> g_inline_data_request;
extern bool ShouldPostInline(bool in_pool, size_t len,
                             uint32_t max_inline_data);
} // namespace rdma
} // namespace brpc

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

// Connect a raw socket to the test server. Note that `sin_addr` is taken from
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

// Parameterized fixture used by upper-layer RPC tests that have no dependency
// on the handshake wire format. The parameter is the client-side handshake
// protocol version (`FLAGS_rdma_client_handshake_version`), so every TEST_P
// below is automatically executed once per supported version. Add a new version
// to INSTANTIATE_TEST_SUITE_P at the bottom of this file and these RPC tests
// will gain coverage for free. v2 always uses private RQ. SRQ is selected only
// for v3 when `FLAGS_rdma_use_srq` is enabled.
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

TEST_F(RdmaTest, stale_cq_callback_does_not_poll_new_generation) {
    // The synthetic payload below enters the IOBuf TLS cache. In hardware
    // runs it must already use registered memory, or later RPC tests can reuse
    // an unregistered block allocated before RDMA initialization.
    if (FLAGS_rdma_test_enable) {
        rdma::GlobalRdmaInitializeOrDie();
    }
    SocketOptions main_options;
    main_options.socket_mode = SOCKET_MODE_RDMA;
    SocketId main_sid;
    ASSERT_EQ(0, Socket::Create(main_options, &main_sid));

    SocketUniquePtr main_socket;
    ASSERT_EQ(0, Socket::Address(main_sid, &main_socket));
    RdmaTransport* transport =
        static_cast<RdmaTransport*>(main_socket->_transport.get());
    rdma::RdmaEndpoint* ep = transport->_rdma_ep;

    auto connection = std::shared_ptr<rdma::RqConnection>(
        new rdma::RqConnection(main_socket.get()));
    ep->_connection = connection;
    SocketOptions cq_options;
    SocketId stale_cq_sid;
    SocketId current_cq_sid;
    ASSERT_EQ(0, Socket::Create(cq_options, &stale_cq_sid));
    ASSERT_EQ(0, Socket::Create(cq_options, &current_cq_sid));

    SocketUniquePtr stale_cq_socket;
    SocketUniquePtr current_cq_socket;
    ASSERT_EQ(0, Socket::Address(stale_cq_sid, &stale_cq_socket));
    ASSERT_EQ(0, Socket::Address(current_cq_sid, &current_cq_socket));
    stale_cq_socket->set_user(new rdma::RqConnection::CqContext(connection));
    current_cq_socket->set_user(new rdma::RqConnection::CqContext(connection));
    connection->_cq_sid = current_cq_sid;

    // A stale CQ identity must return before touching the null verbs resource.
    rdma::RqConnection::PollCq(stale_cq_socket.get());

    // Even a matching CQ cannot deliver into a replacement connection.
    auto replacement = std::shared_ptr<rdma::RqConnection>(
        new rdma::RqConnection(main_socket.get()));
    ep->_connection = replacement;
    rdma::RqConnection::PollCq(current_cq_socket.get());
    butil::IOBuf late_data;
    late_data.append("late data");
    InputMessageClosure last_msg;
    EXPECT_EQ(-1, ep->Deliver(connection.get(), &late_data,
                             late_data.size(), last_msg));
    EXPECT_EQ(ENOTCONN, errno);
    EXPECT_EQ("late data", late_data.to_string());
    EXPECT_TRUE(ep->_input_processor.read_buf().empty());

    // CQ ownership keeps the old connection alive until callbacks are gone.
    std::weak_ptr<rdma::RqConnection> retained = connection;
    connection->_cq_sid = INVALID_SOCKET_ID;
    connection.reset();
    EXPECT_FALSE(retained.expired());
    stale_cq_socket->SetFailed();
    current_cq_socket->SetFailed();
    stale_cq_socket.reset();
    current_cq_socket.reset();
    EXPECT_TRUE(retained.expired());
    main_socket->SetFailed();
}

TEST_F(RdmaTest, stale_private_rq_async_error_does_not_fail_revived_socket) {
    SocketOptions options;
    options.socket_mode = SOCKET_MODE_RDMA;
    SocketId sid;
    ASSERT_EQ(0, Socket::Create(options, &sid));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(sid, &socket));
    BUTIL_SCOPE_EXIT { socket->SetFailed(); };
    auto* endpoint = RdmaTransportOf(socket)->GetRdmaEp();
    auto old = std::make_shared<rdma::RqConnection>(socket.get());
    endpoint->_connection = old;
    std::weak_ptr<rdma::RqConnection> deferred = old;

    ASSERT_EQ(0, socket->SetFailed(ERDMA, "Simulate failure before revival"));
    endpoint->Reset();
    socket->Revive(1);
    auto current = std::make_shared<rdma::RqConnection>(socket.get());
    endpoint->_connection = current;
    ASSERT_FALSE(socket->Failed());

    // Model a queued error after revival while an old CQ context still keeps
    // its connection alive. SocketId alone and weak locking both succeed.
    auto retained = deferred.lock();
    ASSERT_NE(nullptr, retained);
    retained->FailFromAsyncEvent();
    EXPECT_FALSE(socket->Failed());
    EXPECT_TRUE(endpoint->OwnsConnection(current.get()));

    current->FailFromAsyncEvent();
    EXPECT_TRUE(socket->Failed());
}

TEST_F(RdmaTest, failed_socket_completes_connect_callback) {
    SocketOptions options;
    options.socket_mode = SOCKET_MODE_RDMA;
    SocketId sid;
    ASSERT_EQ(0, Socket::Create(options, &sid));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(sid, &socket));
    ASSERT_EQ(0, socket->SetFailed());
    struct Result {
        int calls = 0;
        int error = 0;
    } result;
    rdma::RdmaConnect connector;
    connector.StartConnect(socket.get(), [](int error, void* data) {
        auto* result = static_cast<Result*>(data);
        ++result->calls;
        result->error = error;
    }, &result);
    EXPECT_EQ(1, result.calls);
    EXPECT_EQ(EFAILEDSOCKET, result.error);
}

TEST_F(RdmaTest, private_rq_poller_remove_uses_registration_tag) {
    if (rdma::FLAGS_rdma_use_polling) {
        GTEST_SKIP() << "Requires no running private RQ pollers";
    }
    GFLAGS_NAMESPACE::FlagSaver flags;
    rdma::FLAGS_rdma_poller_num = 1;
    auto saved_groups = std::move(rdma::RqConnection::_poller_groups);
    auto restore = butil::MakeScopeGuard([&] {
        rdma::RqConnection::_poller_groups = std::move(saved_groups);
    });
    rdma::RqConnection::_poller_groups =
        std::vector<rdma::RqConnection::PollerGroup>(2);
    ASSERT_EQ(BTHREAD_TAG_DEFAULT, bthread_self_tag());
    SocketOptions options;
    SocketId sid;
    ASSERT_EQ(0, Socket::Create(options, &sid));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(sid, &socket));
    auto cleanup = butil::MakeScopeGuard([&] { socket->SetFailed(); });
    auto connection = std::shared_ptr<rdma::RqConnection>(
        new rdma::RqConnection(socket.get()));
    connection->_cq_sid = sid;
    connection->PollerAddCqSid();
    EXPECT_EQ(BTHREAD_TAG_DEFAULT, connection->_poller_tag);
    rdma::RqConnection::CqSidOp op;
    EXPECT_TRUE(rdma::RqConnection::_poller_groups[0].pollers[0].op_queue.Dequeue(op));
    EXPECT_EQ(rdma::RqConnection::CqSidOp::ADD, op.type);
    // Simulate registration under tag 1 and close from the current tag 0.
    connection->_poller_tag = 1;
    connection->PollerRemoveCqSid();
    EXPECT_TRUE(rdma::RqConnection::_poller_groups[1].pollers[0].op_queue.Dequeue(op));
    EXPECT_EQ(rdma::RqConnection::CqSidOp::REMOVE, op.type);
    EXPECT_EQ(sid, op.sid);
    EXPECT_FALSE(rdma::RqConnection::_poller_groups[0].pollers[0].op_queue.Dequeue(op));
    EXPECT_EQ(BTHREAD_TAG_INVALID, connection->_poller_tag);
    connection->_cq_sid = INVALID_SOCKET_ID;
}

// Force A and B to share the connector, keep both hellos pending, then
// finish A before B. This detects callback overwrite without dereferencing
// a real WriteRequest after the wrong callback would have released it.
TEST_F(RdmaTest, shared_connector_keeps_handshake_callbacks_separate) {
    if (!rdma::g_skip_rdma_init) {
        GTEST_SKIP();
    }
    GFLAGS_NAMESPACE::FlagSaver flags;
    rdma::FLAGS_rdma_client_handshake_version = 2;
    struct Result {
        // Number of completed callbacks for this specific handshake.
        butil::atomic<int> calls{0};
        // Error published before the callback count.
        int error = -1;
    } results[2];
    SocketOptions options;
    options.socket_mode = SOCKET_MODE_RDMA;
    options.remote_side = g_ep;
    options.on_edge_triggered_events =
        rdma::RdmaEndpoint::OnNewDataFromTcpAtClient;
    SocketId parent_id;
    ASSERT_EQ(0, Socket::Create(options, &parent_id));
    SocketUniquePtr parent;
    ASSERT_EQ(0, Socket::Address(parent_id, &parent));
    SocketUniquePtr children[2];
    butil::fd_guard peers[2];
    int started = 0;
    auto cleanup = butil::MakeScopeGuard([&] {
        for (int i = 0; i < 2; ++i) {
            if (children[i]) {
                children[i]->SetFailed();
            }
            peers[i].reset(-1);
        }
        parent->SetFailed();
        EXPECT_TRUE(WaitUntil([&] {
            return results[0].calls.load() + results[1].calls.load() == started;
        }));
    });
    for (int i = 0; i < 2; ++i) {
        ASSERT_EQ(0, parent->GetShortSocket(&children[i]));
        ASSERT_EQ(parent->_app_connect, children[i]->_app_connect);
        int fds[2];
        ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
        butil::fd_guard client_fd(fds[0]);
        peers[i].reset(fds[1]);
        timeval timeout = {5, 0};
        ASSERT_EQ(0, setsockopt(peers[i], SOL_SOCKET, SO_RCVTIMEO,
                               &timeout, sizeof(timeout)));
        ASSERT_EQ(0, children[i]->ResetFileDescriptor(client_fd));
        client_fd.release();
        ++started;
        children[i]->_app_connect->StartConnect(children[i].get(),
            [](int error, void* data) {
                auto* result = static_cast<Result*>(data);
                result->error = error;
                result->calls.fetch_add(1, butil::memory_order_release);
            }, &results[i]);
        ASSERT_RDMA_STATE(rdma::RdmaEndpoint::C_HELLO_WAIT,
                          RdmaTransportOf(children[i]));
        uint8_t hello[rdma::HELLO_V2_MSG_LEN_MIN];
        ASSERT_TRUE(ReadAll(peers[i], hello, sizeof(hello)));
    }
    EXPECT_EQ(0, results[0].calls.load());
    EXPECT_EQ(0, results[1].calls.load());
    for (int i = 0; i < 2; ++i) {
        uint8_t hello[rdma::HELLO_V2_MSG_LEN_MIN];
        MakeV2ClientHello(hello);
        ASSERT_TRUE(WriteAll(peers[i], hello, sizeof(hello)));
        uint32_t ack = 0;
        ASSERT_TRUE(ReadAll(peers[i], &ack, sizeof(ack)));
        ASSERT_EQ(rdma::HELLO_ACK_RDMA_OK, butil::NetToHost32(ack));
        ASSERT_TRUE(WaitUntil([&] {
            return results[0].calls.load() + results[1].calls.load() == i + 1;
        }));
        EXPECT_EQ(1, results[0].calls.load());
        EXPECT_EQ(i, results[1].calls.load());
    }
    EXPECT_EQ(0, results[0].error);
    EXPECT_EQ(0, results[1].error);
}

TEST_F(RdmaTest, client_handshake_cancels_failed_socket) {
    if (!rdma::g_skip_rdma_init) {
        GTEST_SKIP();
    }
    GFLAGS_NAMESPACE::FlagSaver flags;
    rdma::FLAGS_rdma_use_srq = false;
    for (int version : {2, 3}) {
        rdma::FLAGS_rdma_client_handshake_version = version;
        for (bool blocked_send : {false, true}) {
            SCOPED_TRACE(testing::Message() << "version=" << version
                         << " blocked_send=" << blocked_send);
            struct Result {
                butil::atomic<int> calls{0};
                int error{-1};
            } result;
            int fds[2];
            ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
            butil::fd_guard client_fd(fds[0]);
            butil::fd_guard peer(fds[1]);
            SocketOptions options;
            options.socket_mode = SOCKET_MODE_RDMA;
            options.remote_side = g_ep;
            options.on_edge_triggered_events =
                rdma::RdmaEndpoint::OnNewDataFromTcpAtClient;
            SocketId sid;
            ASSERT_EQ(0, Socket::Create(options, &sid));
            SocketUniquePtr socket;
            ASSERT_EQ(0, Socket::Address(sid, &socket));
            bool started = false;
            auto cleanup = butil::MakeScopeGuard([&] {
                socket->SetFailed();
                peer.reset(-1);
                if (started) {
                    EXPECT_TRUE(WaitUntil([&] {
                        return result.calls.load(butil::memory_order_acquire) == 1;
                    }));
                }
            });
            ASSERT_EQ(0, socket->ResetFileDescriptor(client_fd));
            client_fd.release();
            if (blocked_send) {
                int size = 4096;
                ASSERT_EQ(0, setsockopt(socket->fd(), SOL_SOCKET, SO_SNDBUF,
                                       &size, sizeof(size)));
                std::string padding(8192, 'x');
                while (write(socket->fd(), padding.data(), padding.size()) > 0 ||
                       errno == EINTR) {}
                ASSERT_EQ(EAGAIN, errno);
            }
            started = true;
            socket->_app_connect->StartConnect(socket.get(),
                [](int error, void* data) {
                    auto* result = static_cast<Result*>(data);
                    result->error = error;
                    result->calls.fetch_add(1, butil::memory_order_release);
                }, &result);
            auto* ep = RdmaTransportOf(socket)->GetRdmaEp();
            ASSERT_RDMA_STATE(blocked_send ? rdma::RdmaEndpoint::C_HELLO_SEND :
                                            rdma::RdmaEndpoint::C_HELLO_WAIT,
                              RdmaTransportOf(socket));
            char hello[rdma::HELLO_V3_MAX_PB_SIZE];
            ssize_t hello_size = 0;
            size_t prefix = version == 2 ? rdma::HELLO_MAGIC_LEN :
                rdma::HELLO_MAGIC_LEN + rdma::HELLO_V3_PB_SIZE_LEN;
            if (!blocked_send) {
                hello_size = recv(peer, hello, sizeof(hello), MSG_DONTWAIT);
                ASSERT_GT(hello_size, static_cast<ssize_t>(prefix));
                // Leave the body pending to cover both raw and IOPortal reads.
                ASSERT_TRUE(WriteAll(peer, hello, prefix));
                ASSERT_TRUE(WaitUntil([&] {
                    char byte;
                    return recv(socket->fd(), &byte, 1,
                                MSG_PEEK | MSG_DONTWAIT) == -1 && errno == EAGAIN;
                }));
            }
            ASSERT_EQ(0, socket->SetFailed(ECANCELED, "Cancel RDMA handshake"));
            // Keep the peer open and silent. No EOF or explicit `StopConnect()`
            // call may be needed to release the handshake task.
            ASSERT_TRUE(WaitUntil([&] {
                return result.calls.load(butil::memory_order_acquire) == 1;
            }));
            EXPECT_EQ(EFAILEDSOCKET, result.error);
            EXPECT_EQ(rdma::RdmaEndpoint::FAILED,
                      ep->_state.load(butil::memory_order_acquire));
            if (!blocked_send) {
                ASSERT_TRUE(WriteAll(peer, hello + prefix, hello_size - prefix));
                uint32_t ack;
                EXPECT_EQ(-1, recv(peer, &ack, sizeof(ack), MSG_DONTWAIT));
                EXPECT_EQ(EAGAIN, errno);
                EXPECT_EQ(1, result.calls.load(butil::memory_order_acquire));
            }
        }
    }
}

TEST_F(RdmaTest, client_close_before_hello_send) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd));
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    sockfd.reset(-1);
    // The helper's reference keeps the `Socket` alive even after the peer's
    // close event marks it failed. Recycling must wait until we release it.
    ASSERT_TRUE(WaitUntil([&s] { return s->Failed(); }));
    EXPECT_GE(s->fd(), 0);
    EXPECT_EQ(1u, _server._am->ConnectionCount());
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());
    ASSERT_TRUE(WaitUntil([this] {
        return _server._am->ConnectionCount() == 0;
    }));

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_magic_str) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd));
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    uint8_t data[rdma::HELLO_V2_MSG_LEN_MIN];
    memcpy(data, "PRPC", 4);  // send as normal baidu_std protocol
    ASSERT_TRUE(WriteAll(sockfd, data, 4));
    // Wait for the bytes to show up in the fd stream (baidu_std wants 12B of
    // header, so they stay buffered). Waiting on the state instead would prove
    // nothing: it is already UNINIT before the server has read anything.
    ASSERT_TRUE(WaitForFdReadBuf(s.get(), 4));
    // A non-RDMA magic makes ParseRdmaHandshake return TRY_OTHERS and hand the
    // bytes to other protocols. It does not touch the endpoint state, so it
    // stays UNINIT (the old blocking handshake used to set FALLBACK_TCP here).
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    s.reset();
    StopServer();
}

TEST_F(RdmaTest, client_close_during_hello_send) {
    StartServer();

    SocketUniquePtr s;
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
    ASSERT_TRUE(WaitForFdReadBuf(s.get(), 2));
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    sockfd1.reset(-1);
    s.reset();
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    butil::fd_guard sockfd3;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd3));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);
    // Send the 4B magic plus a valid `msg_len` (=40) but no body, so the server
    // recognizes an RDMA v2 hello and waits for the remaining bytes. (A zero
    // `msg_len` would now be rejected up-front as a protocol error.)
    memcpy(data, "RDMA", 4);
    uint16_t v2_len = butil::HostToNet16(rdma::HELLO_V2_MSG_LEN_MIN);
    memcpy(data + 4, &v2_len, sizeof(v2_len));
    ASSERT_TRUE(WriteAll(sockfd3, data, 6));  // magic + `msg_len`, body missing
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_HELLO_WAIT, RdmaTransportOf(s));
    sockfd3.reset(-1);
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_len) {
    StartServer();

    SocketUniquePtr s;
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
    s.reset();
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_version) {
    StartServer();

    SocketUniquePtr s;
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
    memcpy(data + 6, &ver, 2);  // `hello_ver` == 1, `impl_ver` == 0
    // Write the 36B base starting at `data + 4` (NOT `data`). Pre-Step-1 this
    // UT mistakenly wrote `data, 36` which included the leftover "RDMA"
    // magic at `data[0..4)`. The server parsed it as `msg_len` = 0x5244 and
    // happened to fall through to NegotiationValid (which then failed on
    // `hello_ver`). Now that Step 1 enforces a HELLO_V2_MSG_LEN_MAX upper bound,
    // such an oversized `msg_len` would be rejected before reaching the
    // version check, breaking the intent of this UT.
    ASSERT_TRUE(WriteAll(sockfd1, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    uint32_t flags = 0;
    ASSERT_TRUE(WriteAll(sockfd1, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    sockfd1.reset(-1);
    s.reset();
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
    memcpy(data + 8, &ver, 2);  // `hello_ver` == 0, `impl_ver` == 1
    // See comment above on `WriteAll(sockfd1, data + 4, 36)` for why we
    // write from `data + 4` instead of `data`.
    ASSERT_TRUE(WriteAll(sockfd2, data + 4, 36));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    ASSERT_TRUE(WriteAll(sockfd2, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    sockfd2.reset(-1);
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_hello_msg_invalid_sq_rq_block_size) {
    StartServer();

    SocketUniquePtr s;
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
    s.reset();
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
    s.reset();
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_close_after_qp_build) {
    StartServer();

    SocketUniquePtr s;
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_close_during_ack_send) {
    StartServer();

    SocketUniquePtr s;
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_close_after_ack_send) {
    StartServer();

    SocketUniquePtr s;
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
    s.reset();
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

TEST_F(RdmaTest, client_send_data_on_tcp_after_ack_send) {
    StartServer();

    SocketUniquePtr s;
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
    s.reset();
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
    s.reset();
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
    SocketUniquePtr s = WaitForServerSocket();
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
    ASSERT_TRUE(WaitForFdReadBuf(s.get(), 4));

    sockfd.reset(-1);
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// Once RDMA is on, the TCP fd is no longer an RPC channel, so bytes trailing
// the ACK can only be a protocol error.
TEST_F(RdmaTest, server_rejects_data_pipelined_behind_rdma_ack) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(HandshakeUntilAckWait(&sockfd));
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    auto* transport = RdmaTransportOf(s);
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, transport);

    uint8_t ack_and_data[rdma::HELLO_ACK_LEN + 4];
    const uint32_t flags = butil::HostToNet32(rdma::HELLO_ACK_RDMA_OK);
    memcpy(ack_and_data, &flags, rdma::HELLO_ACK_LEN);
    memcpy(ack_and_data + rdma::HELLO_ACK_LEN, "PRPC", 4);
    ASSERT_TRUE(WriteAll(sockfd, ack_and_data, sizeof(ack_and_data)));

    // Release the test's reference so recycling can destroy the endpoint.
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// Once RDMA is on, the server must stop parsing its TCP fd altogether.
TEST_F(RdmaTest, server_stops_parsing_tcp_fd_once_rdma_is_on) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(HandshakeUntilAckWait(&sockfd));
    SocketUniquePtr s = WaitForServerSocket();
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// The same bytes on the stream carried by the QP are a real RPC, and the handler
// must decline so that CutInputMessage() moves on to the protocol handlers.
TEST_F(RdmaTest, server_parses_qp_stream_after_rdma_is_on) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(HandshakeUntilAckWait(&sockfd));
    SocketUniquePtr s = WaitForServerSocket();
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
    // `base_realtime` from it, so feed ProcessNewMessage() the same time
    // domain: `received_us` also ends up in `Socket::_last_readtime_us`.
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

    s.reset();
    StopServer();
}

// After the handshake is over, CutInputMessage() still offers the data to every
// registered handler, this one included. It must decline instead of reading the
// data as a fresh client hello.
TEST_F(RdmaTest, server_declines_handshake_bytes_after_fallback) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(HandshakeUntilAckWait(&sockfd));
    SocketUniquePtr s = WaitForServerSocket();
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

    // Release the test's reference so recycling can destroy the endpoint.
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());
    uint8_t reply[rdma::HELLO_V2_MSG_LEN_MIN];
    ASSERT_LE(recv(sockfd, reply, sizeof(reply), MSG_DONTWAIT), 0);

    StopServer();
}

TEST_F(RdmaTest, fd_and_qp_input_streams_are_separate) {
    StartServer();

    butil::fd_guard sockfd;
    ASSERT_NO_FATAL_FAILURE(ConnectToServer(&sockfd));
    SocketUniquePtr s = WaitForServerSocket();
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
    ASSERT_TRUE(WaitForFdReadBuf(s.get(), 2));
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, transport->_rdma_ep->_state);
    ASSERT_EQ(2u, fd_stream.read_buf().size());
    ASSERT_TRUE(qp_stream.read_buf().empty());

    sockfd.reset(-1);
    s.reset();
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
    // [4..6) `msg_len`, big-endian uint16 == 40
    ASSERT_EQ(rdma::HELLO_V2_MSG_LEN_MIN,
              (size_t)(((uint16_t)data[4] << 8) | (uint16_t)data[5]));
    // [6..8) `hello_ver`, big-endian uint16 == rdma::HELLO_V2_VERSION
    ASSERT_EQ(rdma::HELLO_V2_VERSION,
              (uint16_t)(((uint16_t)data[6] << 8) | (uint16_t)data[7]));
    // [8..10) `impl_ver`, big-endian uint16 == rdma::IMPL_V2_VERSION
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
    SocketUniquePtr s = WaitForServerSocket();
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

    // Drive the server into FALLBACK_TCP via ACK `flags`=0 so the test ends
    // cleanly without requiring real RDMA hardware.
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ(sizeof(flags), write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));

    sockfd.reset(-1);
    s.reset();
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
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    // Build a v2 hello with `msg_len` = 48 (40 base + 8B zero tail).
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

    // Send the real ACK (`flags`=1 = ACK_MSG_RDMA_OK).
    uint32_t flags = butil::HostToNet32(1);
    ASSERT_EQ(sizeof(flags), write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, RdmaTransportOf(s));

    sockfd.reset(-1);
    s.reset();
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
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    // Build a v2 hello with `msg_len` = 4097 (HELLO_V2_MSG_LEN_MAX + 1).
    // We only send the 40B base. The server must reject before reading
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    sockfd.reset(-1);

    StopServer();
}

// RAII for `FLAGS_rdma_client_handshake_version`: lets us flip the
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

// Build a v3 wire packet from an RdmaHello: "RDM3" + `pb_size_be` + body.
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
//   - `block_size` = 8192 (>= MIN_BLOCK_SIZE)
//   - `sq_size` / `rq_size` = 16 (>= MIN_QP_SIZE)
//   - `gid` = exactly 16B (sizeof(ibv_gid))
//   - `qp_num` = 0  (allowed because `g_skip_rdma_init` in UT)
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

TEST_F(RdmaTest, v3_mtu_and_srq_wire_fields_are_independent) {
    rdma::RdmaHello msg = MakeValidV3Hello();
    msg.set_qp_num(1);
    std::string wire = msg.SerializeAsString();
    // Field 8 is the upstream MTU field. A legacy v3 hello must not select SRQ.
    wire.append("\x40\x05", 2);
    ASSERT_TRUE(msg.ParseFromString(wire));
    ASSERT_TRUE(rdma::v3_wire::ValidRdmaHello(msg));
    EXPECT_EQ(static_cast<uint32_t>(IBV_MTU_4096), msg.mtu());
    EXPECT_FALSE(msg.has_srq_protocol());
    EXPECT_FALSE(msg.has_fc_window());

    msg.set_rq_size(0);
    wire = msg.SerializeAsString();
    // Fields 9 and 10 select SRQ and its initial window independently of MTU.
    wire.append("\x48\x01\x50\x20", 4);
    ASSERT_TRUE(msg.ParseFromString(wire));
    ASSERT_TRUE(rdma::v3_wire::ValidRdmaHello(msg));
    rdma::ParsedHello parsed{};
    rdma::v3_wire::TranslateHello(msg, &parsed);
    ASSERT_TRUE(parsed.path_mtu.has_value());
    EXPECT_EQ(static_cast<uint32_t>(IBV_MTU_4096), *parsed.path_mtu);
    EXPECT_EQ(1u, parsed.srq_protocol);
    EXPECT_EQ(32u, parsed.fc_window);
    EXPECT_EQ(0u, parsed.rq_size);
}

TEST_F(RdmaTest, v3_srq_profile_requires_explicit_valid_window_and_zero_rq) {
    rdma::RdmaHello msg = MakeValidV3Hello();
    msg.set_qp_num(1);
    ASSERT_TRUE(rdma::v3_wire::ValidRdmaHello(msg));
    msg.set_srq_protocol(1);
    msg.set_fc_window(32);
    EXPECT_FALSE(rdma::v3_wire::ValidRdmaHello(msg));
    msg.set_rq_size(0);
    ASSERT_TRUE(rdma::v3_wire::ValidRdmaHello(msg));
    rdma::ParsedHello parsed{};
    rdma::v3_wire::TranslateHello(msg, &parsed);
    EXPECT_EQ(1u, parsed.srq_protocol);
    EXPECT_EQ(32u, parsed.fc_window);
    EXPECT_EQ(0u, parsed.rq_size);
    for (uint32_t window : {0u, 1u, 3u, 65536u, UINT32_MAX}) {
        msg.set_fc_window(window);
        EXPECT_FALSE(rdma::v3_wire::ValidRdmaHello(msg));
    }
    for (uint32_t window : {4u, 32u, 65535u}) {
        msg.set_fc_window(window);
        EXPECT_TRUE(rdma::v3_wire::ValidRdmaHello(msg));
    }
    msg.set_srq_protocol(2);
    EXPECT_FALSE(rdma::v3_wire::ValidRdmaHello(msg));
    msg.clear_srq_protocol();
    msg.set_rq_size(16);
    EXPECT_FALSE(rdma::v3_wire::ValidRdmaHello(msg));
    msg.clear_fc_window();
    EXPECT_TRUE(rdma::v3_wire::ValidRdmaHello(msg));
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

    // [4..8) `pb_size`, big-endian uint32, must be in (0, 4096]
    uint8_t size_buf[4];
    ASSERT_EQ(4, read(acc_fd, size_buf, 4));
    uint32_t pb_size =
        butil::NetToHost32(*reinterpret_cast<uint32_t*>(size_buf));
    ASSERT_GT(pb_size, 0u);
    ASSERT_LE(pb_size, 4096u);

    // `[8..8+pb_size)` RdmaHello protobuf body.
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
    // `gid` wire encoding must be exactly 16 bytes (sizeof(ibv_gid)).
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
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_EQ(rdma::RdmaEndpoint::UNINIT, RdmaTransportOf(s)->_rdma_ep->_state);

    // Send a valid v3 hello.
    std::string packet = MakeV3Packet(MakeValidV3Hello());
    ASSERT_EQ((ssize_t)packet.size(),
              write(sockfd, packet.data(), packet.size()));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));

    // Read server's reply hello: 4B magic + 4B `pb_size` + body.
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

    // Drive the server into FALLBACK_TCP via ACK `flags`=0 so the test ends
    // cleanly without requiring real RDMA hardware.
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ((ssize_t)sizeof(flags),
              write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));

    sockfd.reset(-1);
    s.reset();
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
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    // "RDM3" + `pb_size` = 0 (4B big-endian zero).
    uint8_t buf[8] = {'R', 'D', 'M', '3', 0, 0, 0, 0};
    ASSERT_EQ(8, write(sockfd, buf, 8));
    s.reset();
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
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    uint8_t buf[8];
    memcpy(buf, "RDM3", 4);
    // `pb_size` just above the allowed maximum -> rejected.
    uint32_t pb_size_be =
        butil::HostToNet32(static_cast<uint32_t>(rdma::HELLO_V3_MAX_PB_SIZE + 1));
    memcpy(buf + 4, &pb_size_be, 4);
    ASSERT_EQ(8, write(sockfd, buf, 8));
    s.reset();
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
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    // "RDM3" + `pb_size` = 8 + 8 bytes of 0xff (invalid protobuf body).
    uint8_t buf[16];
    memcpy(buf, "RDM3", 4);
    uint32_t pb_size_be = butil::HostToNet32(8);
    memcpy(buf + 4, &pb_size_be, 4);
    memset(buf + 8, 0xff, 8);
    ASSERT_EQ(16, write(sockfd, buf, 16));
    s.reset();
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
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    rdma::RdmaHello msg = MakeValidV3Hello();
    msg.set_sq_size(0);  // invalid: < MIN_QP_SIZE (16)
    std::string packet = MakeV3Packet(msg);
    ASSERT_TRUE(WriteAll(sockfd, packet.data(), packet.size()));

    // Server validated the hello as invalid -> `_rdma_state` = RDMA_OFF,
    // but still proceeds to S_ACK_WAIT (sends its own reply hello).
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);

    // Drain server's reply hello (content not asserted here, covered
    // by v3_server_hello_bytes_baseline).
    uint8_t reply_hdr[8];
    ASSERT_EQ(8, read(sockfd, reply_hdr, 8));
    ASSERT_EQ(0, memcmp(reply_hdr, "RDM3", 4));
    uint32_t reply_pb_size = butil::NetToHost32(
            *reinterpret_cast<uint32_t*>(reply_hdr + 4));
    std::string reply_body(reply_pb_size, '\0');
    ASSERT_EQ((ssize_t)reply_pb_size,
              read(sockfd, &reply_body[0], reply_pb_size));

    // Client ACK `flags`=0 -> server settles into FALLBACK_TCP.
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ((ssize_t)sizeof(flags),
              write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));

    sockfd.reset(-1);
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// RAII guard to toggle `FLAGS_rdma_ece` for a single test and restore it.
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

// Read the server's v3 reply hello (4B magic + 4B `pb_size` + body) and parse
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

TEST_F(RdmaTest, v3_srq_profile_is_selected_only_when_enabled) {
    if (!rdma::g_skip_rdma_init) {
        GTEST_SKIP() << "Synthetic hello has no peer QP for a hardware handshake";
    }
    GFLAGS_NAMESPACE::FlagSaver flag_saver;
    for (bool enabled : {false, true}) {
        rdma::FLAGS_rdma_use_srq = enabled;
        StartServer();
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        butil::fd_guard fd(socket(AF_INET, SOCK_STREAM, 0));
        ASSERT_GE(fd, 0);
        ASSERT_EQ(0, connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
        SocketUniquePtr server_socket = WaitForServerSocket();
        ASSERT_NE(nullptr, server_socket);
        auto hello = MakeValidV3Hello();
        hello.set_srq_protocol(1);
        hello.set_rq_size(0);
        hello.set_fc_window(32);
        const std::string packet = MakeV3Packet(hello);
        ASSERT_TRUE(WriteAll(fd, packet.data(), packet.size()));
        rdma::RdmaHello reply;
        ReadServerV3Reply(fd, &reply);
        EXPECT_EQ(enabled ? 1u : 0u, reply.srq_protocol());
        if (enabled) {
            EXPECT_EQ(0u, reply.rq_size());
            EXPECT_EQ(static_cast<uint32_t>(rdma::FLAGS_rdma_srq_window),
                      reply.fc_window());
        }
        const uint32_t ack = butil::HostToNet32(enabled ? rdma::HELLO_ACK_RDMA_OK : 0);
        ASSERT_TRUE(WriteAll(fd, &ack, sizeof(ack)));
        const auto expected = enabled ? rdma::RdmaEndpoint::ESTABLISHED :
                                        rdma::RdmaEndpoint::FALLBACK_TCP;
        ASSERT_RDMA_STATE(expected, RdmaTransportOf(server_socket));
        fd.reset(-1);
        server_socket.reset();
        ASSERT_TRUE(WaitForServerSocketGone());
        StopServer();
    }
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
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    rdma::RdmaHello msg = MakeValidV3HelloWithEce(0x02c9, 0x1, 0x0);
    std::string packet = MakeV3Packet(msg);
    ASSERT_EQ((ssize_t)packet.size(),
              write(sockfd, packet.data(), packet.size()));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));

    rdma::RdmaHello reply;
    ReadServerV3Reply(sockfd, &reply);

    // ACK `flags`=0 -> clean FALLBACK_TCP so the test ends without hardware.
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ((ssize_t)sizeof(flags), write(sockfd, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));

    sockfd.reset(-1);
    s.reset();
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
    SocketUniquePtr s = WaitForServerSocket();
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());
    StopServer();
}

// When ECE is enabled but there is no negotiated result (UT skips the real QP
// bring-up, so the server never fills `_outgoing_ece`), the server reply must
// still NOT advertise ECE (FillLocalRdmaHello degrade branch #2 ->
// degrade-safe).
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
    SocketUniquePtr s = WaitForServerSocket();
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
    s.reset();
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
    SocketUniquePtr s = WaitForServerSocket();
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// Build a valid v3 hello that also carries an MTU value.
rdma::RdmaHello MakeValidV3HelloWithMtu(uint32_t mtu) {
    rdma::RdmaHello msg = MakeValidV3Hello();
    msg.set_mtu(mtu);
    return msg;
}

// A client hello carrying MTU must not break the server handshake:
// the server still parses the hello and advances to S_ACK_WAIT.
TEST_F(RdmaTest, v3_server_accepts_client_hello_with_mtu) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    rdma::RdmaHello msg = MakeValidV3HelloWithMtu(IBV_MTU_4096);
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
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());
    StopServer();
}

// The server reply must carry a negotiated MTU when the client advertised one.
// The server computes min(local, client) at the start of BringUpQp (before the
// g_skip_rdma_init early-return), so _outgoing_mtu is set even in UT mode;
// FillLocalRdmaHello then includes it in the reply.
TEST_F(RdmaTest, v3_server_reply_has_negotiated_mtu) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    // Advertise a non-default MTU so we can verify negotiation.
    rdma::RdmaHello msg = MakeValidV3HelloWithMtu(IBV_MTU_4096);
    std::string packet = MakeV3Packet(msg);
    ASSERT_EQ((ssize_t)packet.size(),
              write(sockfd, packet.data(), packet.size()));

    rdma::RdmaHello reply;
    ReadServerV3Reply(sockfd, &reply);

    // The server should advertise a negotiated MTU in its reply.
    EXPECT_TRUE(reply.has_mtu());
    // Negotiated MTU = min(local_active_mtu, client_mtu). In UT mode
    // (g_skip_rdma_init=true) GetRdmaActiveMtu() returns the default
    // IBV_MTU_1024, so negotiated = min(1024, 4096) = 1024. With real
    // hardware (FLAGS_rdma_test_enable=true) the local active MTU may be
    // larger (e.g. 4096), so compare against min(local, advertised)
    // instead of a hard-coded value.
    uint32_t expected_mtu = std::min(rdma::GetRdmaActiveMtu(),
                                     static_cast<uint32_t>(IBV_MTU_4096));
    EXPECT_EQ(expected_mtu, reply.mtu());

    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ((ssize_t)sizeof(flags), write(sockfd, &flags, sizeof(flags)));

    sockfd.reset(-1);
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());
    StopServer();
}

// When the client does NOT advertise an MTU (e.g., v2 peer or older v3 peer),
// the server must NOT include an MTU in its reply (backward-compatible
// degradation to legacy IBV_MTU_1024 default).
TEST_F(RdmaTest, v3_server_reply_has_no_mtu_without_client_mtu) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    butil::fd_guard sockfd(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd >= 0);
    ASSERT_EQ(0, connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr)));
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    // Client hello without MTU field.
    rdma::RdmaHello msg = MakeValidV3Hello();
    std::string packet = MakeV3Packet(msg);
    ASSERT_EQ((ssize_t)packet.size(),
              write(sockfd, packet.data(), packet.size()));

    rdma::RdmaHello reply;
    ReadServerV3Reply(sockfd, &reply);

    // Server must not advertise MTU when client didn't.
    EXPECT_FALSE(reply.has_mtu());

    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ((ssize_t)sizeof(flags), write(sockfd, &flags, sizeof(flags)));

    sockfd.reset(-1);
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());
    StopServer();
}

// A client hello with an out-of-range MTU (e.g., 0 or > IBV_MTU_4096) must be
// rejected by ValidRdmaHello. Per the handshake-fallback design, the server
// does NOT close the connection; instead it degrades to TCP fallback: the
// endpoint enters S_ACK_WAIT with RDMA_OFF, the reply carries no MTU, and a
// subsequent ACK-without-RDMA finalizes FALLBACK_TCP.
TEST_F(RdmaTest, v3_server_rejects_invalid_mtu) {
    StartServer();

    sockaddr_in addr;
    bzero((char*)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    // Test with MTU = 0 (below IBV_MTU_256).
    butil::fd_guard sockfd1(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd1 >= 0);
    ASSERT_EQ(0, connect(sockfd1, (sockaddr*)&addr, sizeof(sockaddr)));
    SocketUniquePtr s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    rdma::RdmaHello msg = MakeValidV3HelloWithMtu(0);
    std::string packet = MakeV3Packet(msg);
    ASSERT_EQ((ssize_t)packet.size(),
              write(sockfd1, packet.data(), packet.size()));

    // The server rejects the hello but falls back to TCP instead of failing.
    s = GetSocketFromServer(0);
    ASSERT_TRUE(s != nullptr);
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    ASSERT_FALSE(s->Failed());

    // The fallback reply must not carry an MTU.
    rdma::RdmaHello reply;
    ReadServerV3Reply(sockfd1, &reply);
    EXPECT_FALSE(reply.has_mtu());

    // Ack without RDMA so the server finalizes FALLBACK_TCP.
    uint32_t flags = butil::HostToNet32(0);
    ASSERT_EQ(sizeof(flags), write(sockfd1, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    ASSERT_FALSE(s->Failed());

    sockfd1.reset(-1);
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    // Test with MTU = 6 (above IBV_MTU_4096).
    butil::fd_guard sockfd2(socket(AF_INET, SOCK_STREAM, 0));
    ASSERT_TRUE(sockfd2 >= 0);
    ASSERT_EQ(0, connect(sockfd2, (sockaddr*)&addr, sizeof(sockaddr)));
    s = WaitForServerSocket();
    ASSERT_TRUE(s != nullptr);

    msg = MakeValidV3HelloWithMtu(6);
    packet = MakeV3Packet(msg);
    ASSERT_EQ((ssize_t)packet.size(),
              write(sockfd2, packet.data(), packet.size()));

    s = GetSocketFromServer(0);
    ASSERT_TRUE(s != nullptr);
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::S_ACK_WAIT, RdmaTransportOf(s));
    ASSERT_EQ(RdmaTransport::RDMA_OFF, RdmaTransportOf(s)->_rdma_state);
    ASSERT_FALSE(s->Failed());

    ReadServerV3Reply(sockfd2, &reply);
    EXPECT_FALSE(reply.has_mtu());

    flags = butil::HostToNet32(0);
    ASSERT_EQ(sizeof(flags), write(sockfd2, &flags, sizeof(flags)));
    ASSERT_RDMA_STATE(rdma::RdmaEndpoint::FALLBACK_TCP, RdmaTransportOf(s));
    ASSERT_FALSE(s->Failed());

    sockfd2.reset(-1);
    s.reset();
    ASSERT_TRUE(WaitForServerSocketGone());

    StopServer();
}

// Verify the client includes its local MTU in the v3 hello.
TEST_F(RdmaTest, v3_client_hello_includes_mtu) {
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

    butil::fd_guard acc_fd(accept(sockfd, NULL, NULL));
    ASSERT_TRUE(acc_fd >= 0);

    // Read 4B magic + 4B pb_size + body from the client hello.
    uint8_t hdr[8];
    ASSERT_EQ(8, read(acc_fd, hdr, 8));
    ASSERT_EQ(0, memcmp(hdr, "RDM3", 4));
    uint32_t pb_size = butil::NetToHost32(*reinterpret_cast<uint32_t*>(hdr + 4));
    ASSERT_GT(pb_size, 0u);
    ASSERT_LE(pb_size, 4096u);
    std::string body(pb_size, '\0');
    ASSERT_EQ((ssize_t)pb_size, read(acc_fd, &body[0], pb_size));

    rdma::RdmaHello hello;
    ASSERT_TRUE(hello.ParseFromString(body));

    // The client hello must carry the locally queried active MTU.
    // In UT mode (g_skip_rdma_init=true), OpenDevice is never called so
    // GetRdmaActiveMtu() returns the default IBV_MTU_1024; with real
    // hardware (FLAGS_rdma_test_enable=true) it returns the port's actual
    // active MTU, so compare against it instead of a hard-coded value.
    EXPECT_TRUE(hello.has_mtu());
    EXPECT_EQ(rdma::GetRdmaActiveMtu(), hello.mtu());

    bthread_id_join(cntl.call_id());
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

    // Verify both peers actually use the negotiated RDMA mode, including v2
    // retaining private RQ when this test is run with `rdma_use_srq`=true.
    SocketUniquePtr client_socket;
    ASSERT_EQ(0, Socket::Address(channel._server_id, &client_socket));
    SocketUniquePtr server_socket = GetSocketFromServer(0);
    ASSERT_NE(nullptr, server_socket);
    const bool expect_srq = GetParam() == 3 && rdma::FLAGS_rdma_use_srq;
    for (Socket* socket : {client_socket.get(), server_socket.get()}) {
        auto* transport = RdmaTransportOf(socket);
        ASSERT_RDMA_STATE(rdma::RdmaEndpoint::ESTABLISHED, transport);
        ASSERT_EQ(RdmaTransport::RDMA_ON, transport->_rdma_state);
        EXPECT_EQ(GetParam(), transport->_rdma_ep->_handshake_version);
        const auto& connection = transport->_rdma_ep->_connection;
        ASSERT_TRUE(connection);
        ibv_qp* qp = nullptr;
        rdma::RdmaLocalInfo info;
        connection->GetLocalInfo(&info);
        if (expect_srq) {
            auto srq = std::dynamic_pointer_cast<rdma::SrqConnection>(connection);
            ASSERT_NE(nullptr, srq);
            qp = srq->qp();
            EXPECT_EQ(1u, info.srq_protocol);
            EXPECT_EQ(0u, info.rq_size);
            EXPECT_EQ(static_cast<uint32_t>(rdma::FLAGS_rdma_srq_window),
                      info.fc_window);
        } else {
            auto rq = std::dynamic_pointer_cast<rdma::RqConnection>(connection);
            ASSERT_NE(nullptr, rq);
            qp = rq->_resource->qp;
            EXPECT_EQ(0u, info.srq_protocol);
            EXPECT_GT(info.rq_size, 0u);
            EXPECT_EQ(0u, info.fc_window);
        }
        ibv_qp_attr attr{};
        ibv_qp_init_attr init{};
        ASSERT_EQ(0, rdma::IbvQueryQp(qp, &attr, IBV_QP_PATH_MTU, &init));
        uint32_t expected_mtu = rdma::GetRdmaActiveMtu();
        if (GetParam() == 2) {
            expected_mtu = std::min(expected_mtu,
                                    static_cast<uint32_t>(IBV_MTU_1024));
        }
        EXPECT_EQ(expected_mtu, static_cast<uint32_t>(attr.path_mtu));
    }

    server_socket.reset();
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
            SocketUniquePtr m = GetSocketFromServer(0);
            DumpRdmaEndpointInfo(s.get(), m.get());
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
            SocketUniquePtr m = GetSocketFromServer(0);
            DumpRdmaEndpointInfo(s.get(), m.get());
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
            SocketUniquePtr m = GetSocketFromServer(0);
            DumpRdmaEndpointInfo(s.get(), m.get());
        }
        ASSERT_TRUE(0 == cntl[i].ErrorCode() ||
                    EOVERCROWDED == cntl[i].ErrorCode()) << "req[" << i << "] " << berror(cntl[i].ErrorCode());
    }

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(cntl[0]._single_server_id, &s));
    SocketUniquePtr m = GetSocketFromServer(0);
    DumpRdmaEndpointInfo(s.get(), m.get());

    m.reset();
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
            SocketUniquePtr m = GetSocketFromServer(0);
            DumpRdmaEndpointInfo(s.get(), m.get());
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
            SocketUniquePtr m = GetSocketFromServer(0);
            DumpRdmaEndpointInfo(s.get(), m.get());
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
        const int before_round = g_echo_served.load(butil::memory_order_relaxed);
        auto cut = [round, before_round](Controller& cntl) {
            // Earlier rounds race freely with receive/parse. In the final
            // round also cover a cut after the peer has processed data. CPU
            // contention must not decide whether this case is exercised.
            if (round == CHURN_ROUND_NUM - 1) {
                EXPECT_TRUE(WaitUntil([before_round] {
                    return g_echo_served.load(butil::memory_order_relaxed) >
                        before_round;
                }));
            }
            ShutdownClientConnection(cntl);
        };
        succeeded += SendEchoRpcs(channel, CHURN_RPC_NUM, CHURN_ATTACH_SIZE,
                                  cut,
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
    auto connection = rdma_transport->_rdma_ep->_connection;
    if (auto srq = std::dynamic_pointer_cast<rdma::SrqConnection>(connection)) {
        rdma::SrqConnection::PostLock lock(srq.get());
        ibv_post_send(srq->qp(), &wr, &bad);
    } else {
        auto rq = std::dynamic_pointer_cast<rdma::RqConnection>(connection);
        ASSERT_TRUE(rq);
        ibv_post_send(rq->_resource->qp, &wr, &bad);
    }
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
    void* data = malloc(4096);
    ASSERT_NE(nullptr, data);
    ASSERT_EQ(0u, rdma::GetLKey(data));
    ASSERT_EQ(0, attach.append_user_data(data, 4096, nullptr));
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
    auto released = std::make_shared<butil::atomic<int>>(0);
    for (size_t i = 0; i < RPC_NUM; ++i) {
        for (size_t part = 0; part < 2; ++part) {
            const size_t index = 2 * i + part;
            mr[index] = static_cast<char*>(malloc(4096));
            ASSERT_NE(nullptr, mr[index]);
            memset(mr[index], i % 100, 4096);
            lkey[index] = rdma::RegisterMemoryForRdma(mr[index], 4096);
            ASSERT_NE(0u, lkey[index]);
            char* base = mr[index];
            ASSERT_EQ(0, cntl[i].request_attachment().append_user_data_with_meta(
                base + i, 4096 - i, [base, released](void*) {
                    rdma::DeregisterMemoryForRdma(base);
                    free(base);
                    released->fetch_add(1, butil::memory_order_release);
                }, lkey[index]));
        }
        req[i].set_message(__FUNCTION__);
        google::protobuf::Closure* done = DoNothing();
        ::test::EchoService::Stub(&channel).Echo(&cntl[i], &req[i], &res[i], done);
    }
    for (size_t i = 0; i < RPC_NUM; ++i) {
        bthread_id_join(cntl[i].call_id());
        ASSERT_EQ(0, cntl[i].ErrorCode()) << "req[" << i << "]";
        ASSERT_EQ(2 * (4096 - i), cntl[i].response_attachment().size());
        char tmp[8192];
        cntl[i].response_attachment().copy_to(tmp, 2 * (4096 - i));
        ASSERT_EQ(0, memcmp(mr[2 * i] + i, tmp, 4096 - i));
        ASSERT_EQ(0, memcmp(mr[2 * i + 1] + i, tmp + 4096 - i, 4096 - i));
        cntl[i].request_attachment().clear();
    }

    // The private-RQ implementation can retain an unsignaled tail. Close the
    // connection before requiring every user reference and MR to be released.
    SocketUniquePtr socket;
    if (Socket::Address(cntl[RPC_NUM - 1]._single_server_id, &socket) == 0) {
        socket->SetFailed();
        socket.reset();
    }
    StopServer();
    EXPECT_TRUE(WaitUntil([&] {
        return released->load(butil::memory_order_acquire) == 2 * RPC_NUM;
    }));
    for (char* base : mr) {
        EXPECT_EQ(0u, rdma::GetLKey(base));
    }
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
// covers both wire formats and SRQ negotiation when enabled.
INSTANTIATE_TEST_SUITE_P(
    HandshakeVersion, RdmaRpcTest,
    ::testing::Values(2, 3),
    [](const ::testing::TestParamInfo<int>& info) {
        return std::string("v") + std::to_string(info.param);
    });

static const uint32_t MELLANOX = 0x02c9;
static const uint32_t INTEL = 0x8086;
static const uint32_t ALIBABA = 0x1ded;
static const uint32_t OTHER = 0xffffff;  // e.g. rxe

TEST(RdmaMtuTest, connection_activation_applies_negotiated_mtu) {
    auto modify_qp = rdma::IbvModifyQp;
    BUTIL_SCOPE_EXIT {
        rdma::IbvModifyQp = modify_qp;
    };
    rdma::IbvModifyQp = [](ibv_qp* qp, ibv_qp_attr* attr,
                           ibv_qp_attr_mask mask) -> int {
        if (attr->qp_state == IBV_QPS_RTR) {
            EXPECT_NE(0, mask & IBV_QP_PATH_MTU);
            *static_cast<ibv_mtu*>(qp->qp_context) = attr->path_mtu;
        }
        return 0;
    };
    uint32_t local_mtu = rdma::GetRdmaActiveMtu();
    uint32_t legacy_mtu = std::min(local_mtu,
                                   static_cast<uint32_t>(IBV_MTU_1024));
    struct MtuCase {
        bool server;
        uint32_t advertised;
        uint32_t expected;
    } cases[] = {
        {false, 0, legacy_mtu},
        {true, 0, legacy_mtu},
        {false, IBV_MTU_256, IBV_MTU_256},
        {true, IBV_MTU_256, IBV_MTU_256},
        {false, IBV_MTU_4096, IBV_MTU_4096},
        {true, IBV_MTU_4096, local_mtu},
    };
    for (auto test_case : cases) {
        rdma::ParsedHello peer{};
        if (test_case.advertised != 0) {
            peer.path_mtu = test_case.advertised;
        }
        ibv_mtu applied{};
        ibv_qp qp{};
        qp.qp_context = &applied;
        EXPECT_EQ(rdma::QpActivationStage::RTS,
                  rdma::ConnectRcQp(&qp, peer, test_case.server, 0, 0));
        EXPECT_EQ(test_case.expected, static_cast<uint32_t>(applied));
    }
}

TEST(RdmaInlineDataTest, inline_data_to_request) {
    // First try: the project's 236 bytes, capped at the vendor's first
    // known limit.
    EXPECT_EQ(236u, rdma::InlineDataToRequest(MELLANOX, 236, false));
    EXPECT_EQ(216u, rdma::InlineDataToRequest(INTEL, 236, false));
    EXPECT_EQ(96u, rdma::InlineDataToRequest(ALIBABA, 236, false));
    EXPECT_EQ(236u, rdma::InlineDataToRequest(OTHER, 236, false));
    EXPECT_EQ(64u, rdma::InlineDataToRequest(INTEL, 64, false));
    // After a refusal: the vendor's next smaller known limit, else 16 less.
    EXPECT_EQ(101u, rdma::InlineDataToRequest(INTEL, 216, true));
    EXPECT_EQ(48u, rdma::InlineDataToRequest(INTEL, 101, true));
    EXPECT_EQ(32u, rdma::InlineDataToRequest(INTEL, 48, true));
    EXPECT_EQ(80u, rdma::InlineDataToRequest(ALIBABA, 96, true));
    EXPECT_EQ(220u, rdma::InlineDataToRequest(MELLANOX, 236, true));
    EXPECT_EQ(220u, rdma::InlineDataToRequest(OTHER, 236, true));
    EXPECT_EQ(0u, rdma::InlineDataToRequest(OTHER, 12, true));
    EXPECT_EQ(0u, rdma::InlineDataToRequest(OTHER, 0, true));
}

// Stub for IbvCreateQp: records every inline size requested, refuses a request
// above `g_stub_limit` (every request if `g_stub_limit` < 0) with
// `g_stub_errno`, and otherwise writes back `g_stub_grant` as the granted size
// (0: the request).
static std::vector<uint32_t> g_stub_requests;
static int64_t g_stub_limit = 0;
static int g_stub_errno = EINVAL;
static uint32_t g_stub_grant = 0;
static ibv_qp g_stub_qp;

static ibv_qp* StubCreateQp(ibv_pd*, ibv_qp_init_attr* attr) {
    g_stub_requests.push_back(attr->cap.max_inline_data);
    if (g_stub_limit < 0 || attr->cap.max_inline_data > g_stub_limit) {
        errno = g_stub_errno;
        return nullptr;
    }
    if (g_stub_grant != 0) {
        attr->cap.max_inline_data = g_stub_grant;
    }
    return &g_stub_qp;
}

// Runs CreateQpWithInlineData() against the stub. Returns whether a QP was
// created. `*limit` gets the inline limit, `*err` the `errno` left behind.
// Unless `keep_cached`, forgets the inline size cached by earlier QP creations
// first.
static bool CreateWithStub(uint32_t vendor_id, int64_t device_limit,
                           int refuse_errno, uint32_t grant, uint32_t* limit,
                           int* err, bool keep_cached = false) {
    if (!keep_cached) {
        rdma::g_inline_data_request.store(UINT32_MAX);
    }
    ibv_qp* (*saved)(ibv_pd*, ibv_qp_init_attr*) = rdma::IbvCreateQp;
    rdma::IbvCreateQp = StubCreateQp;
    g_stub_requests.clear();
    g_stub_limit = device_limit;
    g_stub_errno = refuse_errno;
    g_stub_grant = grant;
    ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(attr));
    *limit = 0;
    errno = 0;
    ibv_qp* qp = rdma::CreateQpWithInlineData(NULL, &attr, vendor_id, limit);
    *err = errno;
    rdma::IbvCreateQp = saved;
    return qp != nullptr;
}

TEST(RdmaInlineDataTest, create_qp_with_inline_data) {
    uint32_t limit = 0;
    int err = 0;
    typedef std::vector<uint32_t> Sizes;

    // Mellanox: 236 accepted on the first try. A larger grant (316) is capped
    // at 236 so that the inlined WQE fits the BlueFlame buffer.
    ASSERT_TRUE(CreateWithStub(MELLANOX, 1024, EINVAL, 316, &limit, &err));
    EXPECT_EQ(Sizes({236}), g_stub_requests);
    EXPECT_EQ(236u, limit);

    // Another device keeps a larger grant (rxe grants 16 bytes per send SGE).
    ASSERT_TRUE(CreateWithStub(OTHER, 1024, EINVAL, 512, &limit, &err));
    EXPECT_EQ(Sizes({236}), g_stub_requests);
    EXPECT_EQ(512u, limit);

    // Intel irdma, whose limit is 216, 101 or 48 depending on the generation.
    ASSERT_TRUE(CreateWithStub(INTEL, 216, EINVAL, 0, &limit, &err));
    EXPECT_EQ(Sizes({216}), g_stub_requests);
    EXPECT_EQ(216u, limit);
    ASSERT_TRUE(CreateWithStub(INTEL, 101, EINVAL, 0, &limit, &err));
    EXPECT_EQ(Sizes({216, 101}), g_stub_requests);
    EXPECT_EQ(101u, limit);
    ASSERT_TRUE(CreateWithStub(INTEL, 48, EINVAL, 0, &limit, &err));
    EXPECT_EQ(Sizes({216, 101, 48}), g_stub_requests);
    EXPECT_EQ(48u, limit);

    // Alibaba erdma (96).
    ASSERT_TRUE(CreateWithStub(ALIBABA, 96, EINVAL, 0, &limit, &err));
    EXPECT_EQ(Sizes({96}), g_stub_requests);
    EXPECT_EQ(96u, limit);

    // An unknown device that takes at most 101 bytes: 16 bytes less each time.
    ASSERT_TRUE(CreateWithStub(OTHER, 101, EINVAL, 0, &limit, &err));
    EXPECT_EQ(Sizes({236, 220, 204, 188, 172, 156, 140, 124, 108, 92}),
              g_stub_requests);
    EXPECT_EQ(92u, limit);

    // A failure that is not EINVAL stops at once and leaves its `errno`.
    ASSERT_FALSE(CreateWithStub(OTHER, -1, ENOMEM, 0, &limit, &err));
    EXPECT_EQ(Sizes({236}), g_stub_requests);
    EXPECT_EQ(ENOMEM, err);

    // A device that refuses every size with EINVAL ends with a request of 0.
    ASSERT_FALSE(CreateWithStub(OTHER, -1, EINVAL, 0, &limit, &err));
    EXPECT_EQ(Sizes({236, 220, 204, 188, 172, 156, 140, 124, 108, 92, 76, 60,
                     44, 28, 12, 0}),
              g_stub_requests);
    EXPECT_EQ(EINVAL, err);
}

// Later QPs start at the inline size the last QP was created with.
TEST(RdmaInlineDataTest, create_qp_reuses_inline_size) {
    uint32_t limit = 0;
    int err = 0;
    typedef std::vector<uint32_t> Sizes;

    // The first QP on an Intel device limited to 48 bytes steps down. The next
    // asks for 48 straight away.
    ASSERT_TRUE(CreateWithStub(INTEL, 48, EINVAL, 0, &limit, &err));
    EXPECT_EQ(Sizes({216, 101, 48}), g_stub_requests);
    ASSERT_TRUE(CreateWithStub(INTEL, 48, EINVAL, 0, &limit, &err, true));
    EXPECT_EQ(Sizes({48}), g_stub_requests);
    EXPECT_EQ(48u, limit);

    // If the cached size is refused (a QP whose other attributes leave less
    // room), the step-down continues from it, and the new size is kept.
    ASSERT_TRUE(CreateWithStub(INTEL, 40, EINVAL, 0, &limit, &err, true));
    EXPECT_EQ(Sizes({48, 32}), g_stub_requests);
    EXPECT_EQ(32u, limit);
    ASSERT_TRUE(CreateWithStub(INTEL, 40, EINVAL, 0, &limit, &err, true));
    EXPECT_EQ(Sizes({32}), g_stub_requests);

    // A failure other than EINVAL leaves the cached size as it was.
    ASSERT_FALSE(CreateWithStub(INTEL, -1, ENOMEM, 0, &limit, &err, true));
    EXPECT_EQ(Sizes({32}), g_stub_requests);
    ASSERT_TRUE(CreateWithStub(INTEL, 40, EINVAL, 0, &limit, &err, true));
    EXPECT_EQ(Sizes({32}), g_stub_requests);

    // Mellanox: 236 is accepted and asked for again. The grant is still capped.
    ASSERT_TRUE(CreateWithStub(MELLANOX, 1024, EINVAL, 316, &limit, &err));
    ASSERT_TRUE(CreateWithStub(MELLANOX, 1024, EINVAL, 316, &limit, &err,
                               true));
    EXPECT_EQ(Sizes({236}), g_stub_requests);
    EXPECT_EQ(236u, limit);
    rdma::g_inline_data_request.store(UINT32_MAX);
}

// The send-path decision of CutFromIOBufList(), which itself needs an
// initialized RDMA device.
TEST(RdmaInlineDataTest, should_post_inline) {
    const uint32_t granted = 236;
    // A message from the block pool at the granted size is inlined, one byte
    // more is not.
    EXPECT_TRUE(rdma::ShouldPostInline(true, granted, granted));
    EXPECT_FALSE(rdma::ShouldPostInline(true, granted + 1, granted));
    EXPECT_TRUE(rdma::ShouldPostInline(true, 1, granted));
    // A message in user registered memory, which may be device memory, is
    // never inlined, however small.
    EXPECT_FALSE(rdma::ShouldPostInline(false, 1, granted));
    EXPECT_FALSE(rdma::ShouldPostInline(false, granted, granted));
    // A QP granted no inline data inlines nothing.
    EXPECT_FALSE(rdma::ShouldPostInline(true, 1, 0));
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
