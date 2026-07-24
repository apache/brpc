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

// Unit tests for TcpTransport, focusing on the io_uring integration added
// in tcp_transport.{h,cpp}: io_uring is NOT a distinct SocketMode/Transport
// (unlike RDMA/UBRING); it is an optional, opt-in I/O backend owned directly
// by TcpTransport. These tests verify:
//   1. The default (io_uring globally disabled) code path is unaffected --
//      this is the most important regression check, since the overwhelming
//      majority of TCP sockets never touch io_uring.
//   2. When an IouringEndpoint happens to be attached to a TcpTransport,
//      TcpTransport correctly exposes/releases it and dispatches Debug() to
//      it, without requiring a real io_uring ring (no kernel io_uring
//      support or Poller bthread is needed for these tests).

#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sstream>

#include <butil/fd_guard.h>
#include "brpc/socket.h"
#include "brpc/tcp_transport.h"

#if BRPC_WITH_IOURING
#include "brpc/iouring/iouring_helper.h"
#include "brpc/iouring/iouring_endpoint.h"
#endif  // BRPC_WITH_IOURING

namespace brpc {

class TcpTransportTest : public ::testing::Test {
protected:
    TcpTransportTest() {}
    ~TcpTransportTest() override {}
};

// ---------------------------------------------------------------------------
// Helper: create a Socket (and therefore its TcpTransport) backed by a real
// fd, following the same pattern used throughout brpc_socket_unittest.cpp.
// ---------------------------------------------------------------------------
static int CreateSocketPairAndSocket(SocketId* id, int* peer_fd) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return -1;
    }
    *peer_fd = fds[0];
    SocketOptions options;
    options.fd = fds[1];
    return Socket::Create(options, id);
}

// ===========================================================================
// Default path: io_uring is not globally enabled (this is the default and
// the state of every process that never calls
// iouring::GlobalIouringInitializeOrDie()/InitPollingModeWithTag()).
//
// TcpTransport must behave exactly like a plain synchronous-fd transport:
// no IouringEndpoint is ever created, and every I/O method uses the
// classic read(2)/write(2)-via-fd path.
// ===========================================================================

TEST_F(TcpTransportTest, no_iouring_endpoint_by_default) {
#if BRPC_WITH_IOURING
    // Sanity check: unless some earlier test in this binary explicitly
    // brought up the global io_uring context, it must be off by default.
    ASSERT_FALSE(iouring::IsIouringAvailable());
#endif

    SocketId id;
    int peer_fd = -1;
    ASSERT_EQ(0, CreateSocketPairAndSocket(&id, &peer_fd));
    butil::fd_guard peer_guard(peer_fd);

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(id, &s));

    // TcpTransport is created via TransportFactory::CreateTransport() for
    // SOCKET_MODE_TCP, which is the default socket_mode.
    TcpTransport* transport = dynamic_cast<TcpTransport*>(s->_transport.get());
    ASSERT_NE(nullptr, transport);

#if BRPC_WITH_IOURING
    // Since io_uring was never globally enabled, Init() must not have
    // created an IouringEndpoint.
    EXPECT_EQ(nullptr, transport->GetIouringEp());
#endif

    // Debug() must not crash when there's no IouringEndpoint attached.
    std::ostringstream oss;
    transport->Debug(oss);

    ASSERT_EQ(0, s->SetFailed());
}

TEST_F(TcpTransportTest, cut_from_iobuf_uses_sync_fd_path_by_default) {
    SocketId id;
    int peer_fd = -1;
    ASSERT_EQ(0, CreateSocketPairAndSocket(&id, &peer_fd));
    butil::fd_guard peer_guard(peer_fd);

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(id, &s));

    TcpTransport* transport = dynamic_cast<TcpTransport*>(s->_transport.get());
    ASSERT_NE(nullptr, transport);

    // Write via TcpTransport::CutFromIOBuf() and confirm the bytes are
    // observed on the other end of the socketpair -- exercising the
    // synchronous cut_into_file_descriptor() fallback path. CutFromIOBuf()
    // returns the number of bytes written (as cut_into_file_descriptor()
    // does), not a plain 0/-1 status.
    butil::IOBuf buf;
    buf.append("hello tcp_transport");
    const size_t buf_len = buf.length();
    ASSERT_EQ(static_cast<int>(buf_len), transport->CutFromIOBuf(&buf));
    EXPECT_TRUE(buf.empty());

    char received[64] = {0};
    ssize_t n = read(peer_fd, received, sizeof(received) - 1);
    ASSERT_EQ(static_cast<ssize_t>(buf_len), n);
    EXPECT_STREQ("hello tcp_transport", received);

    ASSERT_EQ(0, s->SetFailed());
}

// ===========================================================================
// ContextInitOrDie() must be a safe no-op: bringing up the global io_uring
// context is an explicit, opt-in action left to the caller (see
// example/iouring_echo_c++), not something every plain-TCP Server/Channel
// should pay for. Calling it repeatedly must never abort the process, even
// when the machine's kernel does not support io_uring.
// ===========================================================================

TEST_F(TcpTransportTest, context_init_or_die_is_noop) {
    EXPECT_EQ(0, TcpTransport::ContextInitOrDie(true, nullptr));
    EXPECT_EQ(0, TcpTransport::ContextInitOrDie(false, nullptr));
#if BRPC_WITH_IOURING
    // Must not have flipped on io_uring as a side effect.
    EXPECT_FALSE(iouring::IsIouringAvailable());
#endif
}

#if BRPC_WITH_IOURING

// ===========================================================================
// IouringEndpoint-attached path.
//
// These tests do NOT call iouring::GlobalIouringInitializeOrDie(): doing so
// would probe real kernel io_uring opcodes and exit(1) the whole test
// binary on machines/kernels that lack io_uring support (< 5.1), which
// would be an unacceptable CI/portability regression.
//
// Instead they call the lightweight, side-effect-free
// IouringEndpoint::GlobalInitialize() (just resizes an internal vector) to
// make PollerAddSid()/PollerRemoveSid() safe to call, then manipulate
// TcpTransport's private _iouring_ep field directly (test config compiles
// with -fno-access-control) to simulate "an endpoint is attached" without
// needing a real ring or Poller bthread.
// ===========================================================================

class TcpTransportIouringTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Idempotent, side-effect-free: only resizes _poller_groups so that
        // PollerAddSid()/PollerRemoveSid() (called from
        // AllocateResources()/DeallocateResources()/the IouringEndpoint
        // destructor) don't index into an empty vector.
        iouring::IouringEndpoint::GlobalInitialize();
    }
};

TEST_F(TcpTransportIouringTest, get_and_release_attached_endpoint) {
    SocketId id;
    int peer_fd = -1;
    ASSERT_EQ(0, CreateSocketPairAndSocket(&id, &peer_fd));
    butil::fd_guard peer_guard(peer_fd);

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(id, &s));

    TcpTransport* transport = dynamic_cast<TcpTransport*>(s->_transport.get());
    ASSERT_NE(nullptr, transport);
    ASSERT_EQ(nullptr, transport->GetIouringEp());

    // Simulate what TcpTransport::Init() does when
    // iouring::IsIouringAvailable() is true: attach an IouringEndpoint.
    transport->_iouring_ep = new iouring::IouringEndpoint(s.get());
    ASSERT_EQ(0, transport->_iouring_ep->AllocateResources());
    ASSERT_EQ(transport->_iouring_ep, transport->GetIouringEp());

    // Debug() must dispatch to IouringEndpoint::DebugInfo() without
    // touching any real ring state.
    std::ostringstream oss;
    transport->Debug(oss);
    EXPECT_NE(std::string::npos, oss.str().find("iouring_writable"));

    // Release() must delete the endpoint and null out the pointer, mirroring
    // what happens when the owning Socket is recycled.
    transport->Release();
    EXPECT_EQ(nullptr, transport->GetIouringEp());

    ASSERT_EQ(0, s->SetFailed());
}

TEST_F(TcpTransportIouringTest, reset_forwards_to_endpoint) {
    SocketId id;
    int peer_fd = -1;
    ASSERT_EQ(0, CreateSocketPairAndSocket(&id, &peer_fd));
    butil::fd_guard peer_guard(peer_fd);

    SocketUniquePtr s;
    ASSERT_EQ(0, Socket::Address(id, &s));

    TcpTransport* transport = dynamic_cast<TcpTransport*>(s->_transport.get());
    ASSERT_NE(nullptr, transport);

    transport->_iouring_ep = new iouring::IouringEndpoint(s.get());
    ASSERT_EQ(0, transport->_iouring_ep->AllocateResources());

    // TcpTransport::Reset() must not crash and must return 0 even though no
    // real ring/Poller backs this endpoint.
    EXPECT_EQ(0, transport->Reset(1));

    transport->Release();
    ASSERT_EQ(0, s->SetFailed());
}

#endif  // BRPC_WITH_IOURING

}  // namespace brpc
