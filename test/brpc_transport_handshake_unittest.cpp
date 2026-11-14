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

#include <gtest/gtest.h>

// GoogleTest 1.11 and earlier expose flags without getter/setter macros.
#ifndef GTEST_FLAG_GET
#define GTEST_FLAG_GET(name) ::testing::GTEST_FLAG(name)
#define GTEST_FLAG_SET(name, value) (::testing::GTEST_FLAG(name) = (value))
#endif

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>

#include "bthread/bthread.h"
#include "bthread/butex.h"
#include "bthread/countdown_event.h"
#include "bthread/unstable.h"
#include "butil/fd_guard.h"
#include "butil/sys_byteorder.h"
#include "butil/time.h"
#include "brpc/adapter_transport.h"
#include "brpc/global.h"
#include "brpc/handshake/handshake_io.h"
#include "brpc/handshake/handshake_adapter.h"
#include "brpc/input_messenger.h"
#include "brpc/policy/transport_handshake_protocol.h"
#include "brpc/protocol.h"
#if BRPC_WITH_RDMA
#include "brpc/rdma_transport.h"
#include "brpc/rdma/rdma_endpoint.h"
#endif
#if BRPC_WITH_UBRING
#include "brpc/ubshm/ub_endpoint.h"
#endif
#include "brpc/socket.h"
#include "brpc/tcp_transport.h"
#include "brpc/transport_handshake.h"

namespace brpc {
#if BRPC_WITH_RDMA
namespace rdma {
extern decltype(&bthread_start_background) StartPollingThread;
}
#endif
#if BRPC_WITH_UBRING
namespace ubring {
extern decltype(&bthread_start_background) StartPollingThread;
}
#endif
namespace handshake {

#if BRPC_WITH_RDMA || BRPC_WITH_UBRING
struct PollerStartFailure {
    int attempts = 0;
    int fail_on = 0;
    bthread::CountdownEvent failed;
    bthread::CountdownEvent resume;
};
static PollerStartFailure* g_poller_start_failure;

static int StartPollerWithFailure(bthread_t* tid, const bthread_attr_t* attr,
                                  void* (*fn)(void*), void* arg) {
    auto* failure = g_poller_start_failure;
    if (failure->attempts++ == failure->fail_on) {
        failure->failed.signal();
        failure->resume.wait();
        return EAGAIN;
    }
    return bthread_start_background(tid, attr, fn, arg);
}

template <typename Endpoint>
static void CheckPollerStartup(decltype(&bthread_start_background)& start) {
    auto saved_groups = std::move(Endpoint::_poller_groups);
    Endpoint::_poller_groups = std::vector<typename Endpoint::PollerGroup>(1);
    auto& group = Endpoint::_poller_groups[0];
    group.pollers = std::vector<typename Endpoint::Poller>(2);
    auto saved_start = start;
    start = StartPollerWithFailure;
    for (int fail_on : {0, 1}) {
        PollerStartFailure failure;
        failure.fail_on = fail_on;
        g_poller_start_failure = &failure;
        std::atomic<int> initialized{0};
        std::atomic<int> released{0};
        std::atomic<int> rejected_reentry{0};
        std::atomic<bool> pause_release{false};
        bthread::CountdownEvent release_entered(2), release_resume;
        auto initialize = [&] {
            return Endpoint::PollingModeInitialize(
                0, nullptr, [&] { ++initialized; }, [&] {
                    if (pause_release.load()) {
                        release_entered.signal();
                        release_resume.wait();
                    }
                    if (Endpoint::PollingModeInitialize(
                            0, nullptr, nullptr, nullptr) == -1 &&
                        errno == EBUSY) {
                        ++rejected_reentry;
                    }
                    ++released;
                });
        };
        int first_rc = 0;
        std::thread first([&] { first_rc = initialize(); });
        failure.failed.wait();
        // Another caller must wait until startup or rollback completes.
        const bool unlocked = group.mutex.try_lock();
        EXPECT_FALSE(unlocked);
        if (unlocked) {
            group.mutex.unlock();
        }
        failure.resume.signal();
        first.join();
        EXPECT_EQ(-1, first_rc);
        EXPECT_FALSE(group.running.load());
        EXPECT_EQ(fail_on, released.load());
        for (const auto& poller : group.pollers) {
            EXPECT_EQ(INVALID_BTHREAD, poller.tid);
        }
        EXPECT_EQ(0, initialize());
        EXPECT_EQ(fail_on + 3, failure.attempts);
        pause_release.store(true);
        std::thread first_release([] { Endpoint::PollingModeRelease(0); });
        release_entered.wait();
        EXPECT_EQ(-1, initialize());
        EXPECT_EQ(EBUSY, errno);
        std::thread second_release([] { Endpoint::PollingModeRelease(0); });
        release_resume.signal();
        first_release.join();
        second_release.join();
        EXPECT_EQ(fail_on + 2, initialized.load());
        EXPECT_EQ(initialized.load(), released.load());
        EXPECT_EQ(released.load(), rejected_reentry.load());
    }
    start = saved_start;
    Endpoint::_poller_groups = std::move(saved_groups);
    g_poller_start_failure = nullptr;
}
#endif

#if BRPC_WITH_RDMA
TEST(PollerStartupTest, rdma_rolls_back_and_retries) {
    const std::string saved = GTEST_FLAG_GET(death_test_style);
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT({
        alarm(5);
        rdma::FLAGS_rdma_use_polling = true;
        CheckPollerStartup<rdma::RdmaEndpoint>(rdma::StartPollingThread);
        _exit(::testing::Test::HasFailure() ? 1 : 0);
    }, ::testing::ExitedWithCode(0), "");
    GTEST_FLAG_SET(death_test_style, saved);
}
#endif

#if BRPC_WITH_UBRING
TEST(PollerStartupTest, ubshm_rolls_back_and_retries) {
    const std::string saved = GTEST_FLAG_GET(death_test_style);
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT({
        alarm(5);
        CheckPollerStartup<ubring::UBShmEndpoint>(ubring::StartPollingThread);
        _exit(::testing::Test::HasFailure() ? 1 : 0);
    }, ::testing::ExitedWithCode(0), "");
    GTEST_FLAG_SET(death_test_style, saved);
}
#endif

class HandshakeSocketPair {
public:
    void Init(SocketOptions options = SocketOptions()) {
        int fds[2];
        ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
        peer_fd.reset(fds[1]);
        options.fd = fds[0];
        SocketId id;
        ASSERT_EQ(0, Socket::Create(options, &id));
        ASSERT_EQ(0, Socket::Address(id, &socket));
    }

    ~HandshakeSocketPair() {
        if (socket) {
            socket->SetFailed();
        }
    }

    butil::fd_guard peer_fd;
    SocketUniquePtr socket;
};

class MemoryHandshakeIO : public HandshakeIO {
public:
    explicit MemoryHandshakeIO(const std::string& input = std::string())
        : _input(input), _offset(0) {}

    int ReadExact(void* data, size_t len) override {
        if (_input.size() - _offset < len) {
            errno = EIO;
            return -1;
        }
        memcpy(data, _input.data() + _offset, len);
        _offset += len;
        return 0;
    }

    int WriteAll(const void* data, size_t len) override {
        _output.append(static_cast<const char*>(data), len);
        return 0;
    }

    int PushBack(const void* data, size_t len) override {
        _pushed_back.append(static_cast<const char*>(data), len);
        return 0;
    }

    const std::string& output() const { return _output; }
    const std::string& pushed_back() const { return _pushed_back; }

private:
    std::string _input;
    size_t _offset;
    std::string _output;
    std::string _pushed_back;
};

struct BlockingHandshakeRead {
    SocketHandshakeIO* io;
    bthread::CountdownEvent completed;
    int result = 0;
    int error = 0;

    explicit BlockingHandshakeRead(SocketHandshakeIO* value) : io(value) {}
};

static void* RunBlockingHandshakeRead(void* arg) {
    BlockingHandshakeRead* read =
        static_cast<BlockingHandshakeRead*>(arg);
    char byte = 0;
    read->result = read->io->ReadExact(&byte, sizeof(byte));
    read->error = errno;
    read->completed.signal();
    return nullptr;
}

struct BlockingHandshakeWrite {
    SocketHandshakeIO* io;
    bthread::CountdownEvent completed;
    int result = 0;
    int error = 0;

    explicit BlockingHandshakeWrite(SocketHandshakeIO* value) : io(value) {}
};

static void* RunBlockingHandshakeWrite(void* arg) {
    auto* write = static_cast<BlockingHandshakeWrite*>(arg);
    const char byte = 0;
    write->result = write->io->WriteAll(&byte, sizeof(byte));
    write->error = errno;
    write->completed.signal();
    return nullptr;
}

class TestAppConnect : public AppConnect {
public:
    void StartConnect(const Socket*, void (*done)(int, void*),
                      void* data) override {
        done(0, data);
    }

    void StopConnect(Socket*) override {}
};

class RecordingAppConnect : public TestAppConnect {
public:
    void StopConnect(Socket* socket) override {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            ++_stopped[socket->id()];
        }
        stopped.signal();
    }

    int StopCount(SocketId id) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _stopped.find(id);
        return it == _stopped.end() ? 0 : it->second;
    }

    bthread::CountdownEvent stopped;

private:
    std::mutex _mutex;
    std::map<SocketId, int> _stopped;
};

static int RejectHandshakeStart(bthread_t*, const bthread_attr_t*,
                                void* (*)(void*), void*) {
    errno = EINTR;
    return EAGAIN;
}

static FrameSpec FixedSpec(const char* magic, size_t magic_len,
                           size_t total_len) {
    return FrameSpec(magic, magic_len, total_len, total_len,
                     FrameSpec::FIXED);
}

class TestHandshakeProtocol : public HandshakeProtocol {
public:
    explicit TestHandshakeProtocol(std::string* calls = nullptr)
        : _calls(calls), _with_extension(false),
          _extension_size(0) {}

    int ProtocolVersion() const override { return 7; }
    const FrameSpec& HelloFrameSpec() const override {
        static const FrameSpec spec = FixedSpec("HS", 2, 4);
        return spec;
    }
    const FrameSpec& AckFrameSpec() const override {
        static const FrameSpec spec = FixedSpec(nullptr, 0, 1);
        return spec;
    }
    StepResult BuildHello(bool enabled, std::string* payload) override {
        Append("build ");
        *payload = enabled ? "LO" : "NO";
        return STEP_OK;
    }
    StepResult ParseHello(const std::string& payload) override {
        Append("parse ");
        return payload == "OK" ? STEP_OK : STEP_FALLBACK;
    }
    StepResult BuildAck(bool enabled, std::string* payload) override {
        Append(enabled ? "ack1 " : "ack0 ");
        *payload = enabled ? "1" : "0";
        return STEP_OK;
    }
    StepResult ParseAck(
        const std::string& payload, bool* enabled) override {
        Append("parse_ack ");
        *enabled = payload == "1";
        return payload == "1" || payload == "0" ? STEP_OK : STEP_ERROR;
    }
    bool HasExtension() const override { return _with_extension; }
    const FrameSpec& ExtensionFrameSpec() const override {
        return _extension_spec;
    }
    StepResult BuildExtension(
        bool enabled, std::string* payload) override {
        *payload = enabled
            ? std::string("E1", _extension_size)
            : std::string("E0", _extension_size);
        return STEP_OK;
    }
    StepResult ParseExtension(const std::string& payload) override {
        return payload == std::string("E1", _extension_size)
            ? STEP_OK : STEP_FALLBACK;
    }
    void EnableExtension(size_t size) {
        _with_extension = true;
        _extension_size = size;
        _extension_spec = FixedSpec(nullptr, 0, size);
    }

private:
    void Append(const char* value) {
        if (_calls != nullptr) {
            *_calls += value;
        }
    }

    std::string* _calls;
    bool _with_extension;
    size_t _extension_size;
    FrameSpec _extension_spec;
};

class TestHandshakeTransport : public HandshakeTransport {
public:
    explicit TestHandshakeTransport(std::string* calls = nullptr)
        : prepare_result(STEP_OK), negotiate_result(STEP_OK),
          validate_result(STEP_OK), tcp_active(false),
          high_speed_active(false), failed(false), _calls(calls) {}

    StepResult PrepareResources() override {
        Append("prepare ");
        return prepare_result;
    }
    StepResult NegotiateResources() override {
        Append("negotiate ");
        return negotiate_result;
    }
    void OnEstablished() override {
        high_speed_active = true;
        Append("activate");
    }
    void OnFallback() override { tcp_active = true; }
    void OnFailed() override { failed = true; }
    StepResult ValidateEstablished() override {
        Append("validate ");
        return validate_result;
    }

    StepResult prepare_result;
    StepResult negotiate_result;
    StepResult validate_result;
    bool tcp_active;
    bool high_speed_active;
    bool failed;

private:
    void Append(const char* value) {
        if (_calls != nullptr) {
            *_calls += value;
        }
    }
    std::string* _calls;
};

class TestServerHandshakeTransport : public TestHandshakeTransport {
public:
    butil::IOBuf* source = nullptr;
    StepResult ValidateEstablished() override {
        return source->empty() ? STEP_OK : STEP_ERROR;
    }
};

class TestServerHandshakeAdapter : public HandshakeAdapter {
public:
    MemoryHandshakeIO io;
    mutable HandshakeSession session;
    TestHandshakeProtocol protocol;
    TestServerHandshakeTransport transport;

protected:
    StepResult RunServerStep(butil::IOBuf* source, Socket*) override {
        std::vector<HandshakeProtocol*> protocols(1, &protocol);
        transport.source = source;
        return session.RunServer(io, protocols, source, &transport, false);
    }
    HandshakeSession* GetSession(Socket*) const override {
        return &session;
    }
};

class TestUpgradeMessenger : public InputMessenger {
public:
    using InputMessenger::OnNewMessagesUntil;
    TestServerHandshakeAdapter adapter;
    int peer_fd = -1;
    int parse_calls = 0;
    int write_result = -1;

    static ParseResult Parse(butil::IOBuf* source, Socket* socket,
                             bool, const void*) {
        TestUpgradeMessenger* messenger =
            static_cast<TestUpgradeMessenger*>(socket->user());
        ++messenger->parse_calls;
        ParseResult result = messenger->adapter.ExecuteServerHandshake(
            source, socket);
        if (messenger->adapter.session.phase(butil::memory_order_acquire) ==
            ESTABLISHED) {
            // Make bytes readable after the ACK was consumed, but before the
            // outer event handler has regained control.
            messenger->write_result = write(messenger->peer_fd, "RPC", 3);
        }
        return result;
    }
    static void Process(InputMessageBase*) {}
    static bool StopReading(Socket* socket) {
        return static_cast<TestUpgradeMessenger*>(socket->user())
            ->adapter.session.phase(butil::memory_order_acquire) == ESTABLISHED;
    }
};

TEST(TransportHandshakeTest, rejected_handshake_start_releases_socket_reference) {
    SocketOptions options;
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));
    const int32_t references = socket->nref();
    int completions = 0;
    auto done = [](int, void* data) { ++*static_cast<int*>(data); };

    EXPECT_EQ(-1, AdapterTransport::StartClientUpgradeImpl(
        socket.get(), done, &completions, RejectHandshakeStart));
    EXPECT_EQ(EAGAIN, errno);
    EXPECT_EQ(references, socket->nref());
    // The caller owns the completion callback when startup fails.
    EXPECT_EQ(0, completions);
    socket->SetFailed();
}

TEST(TransportHandshakeTest, registered_protocol_uses_transport_name) {
    GlobalInitializeOrDie();
    EXPECT_EQ(1, PROTOCOL_TRANSPORT_HANDSHAKE);
    EXPECT_EQ(PROTOCOL_TRANSPORT_HANDSHAKE,
              StringToProtocolType("transport_handshake"));
    EXPECT_STREQ("transport_handshake",
                 ProtocolTypeToString(PROTOCOL_TRANSPORT_HANDSHAKE));

    const Protocol* protocol = FindProtocol(PROTOCOL_TRANSPORT_HANDSHAKE);
    ASSERT_NE(nullptr, protocol);
    EXPECT_EQ(policy::ParseTransportHandshake, protocol->parse);
    EXPECT_EQ(policy::ProcessTransportHandshake, protocol->process_request);
}

TEST(TransportHandshakeTest, adapter_retains_context_for_fragmented_extension) {
    HandshakeSocketPair pair;
    ASSERT_NO_FATAL_FAILURE(pair.Init());
    auto& socket = pair.socket;
    TestServerHandshakeAdapter adapter;
    adapter.protocol.EnableExtension(2);
    butil::IOBuf source;
    source.append("HSOK", 4);
    ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA,
              adapter.ExecuteServerHandshake(&source, socket.get()).error());
    ASSERT_NE(nullptr, socket->parsing_context());
    ServerHandshakeContext* context = static_cast<ServerHandshakeContext*>(
        socket->parsing_context());
    ASSERT_EQ(&adapter, context->adapter());
    source.append("E", 1);
    ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA,
              context->adapter()->ExecuteServerHandshake(&source, socket.get())
                  .error());
    source.append("11", 2);
    ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA,
              context->adapter()->ExecuteServerHandshake(&source, socket.get())
                  .error());
    EXPECT_EQ(ESTABLISHED, adapter.session.phase(butil::memory_order_acquire));
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(nullptr, socket->parsing_context());
    socket->SetFailed();
}

TEST(TransportHandshakeTest, adapter_rejects_tcp_data_coalesced_with_upgrade_ack) {
    HandshakeSocketPair pair;
    ASSERT_NO_FATAL_FAILURE(pair.Init());
    auto& socket = pair.socket;
    TestServerHandshakeAdapter adapter;
    butil::IOBuf source;
    source.append("HSOK1RPC", 8);
    EXPECT_EQ(PARSE_ERROR_ABSOLUTELY_WRONG,
              adapter.ExecuteServerHandshake(&source, socket.get()).error());
    EXPECT_EQ(FAILED, adapter.session.phase(butil::memory_order_acquire));
    EXPECT_FALSE(adapter.transport.high_speed_active);
    EXPECT_EQ(3UL, source.size());
    socket->SetFailed();
}

TEST(TransportHandshakeTest, tcp_read_loop_stops_when_upgrade_completes) {
    TestUpgradeMessenger messenger;
    InputMessageHandler handler{};
    handler.parse = TestUpgradeMessenger::Parse;
    handler.process = TestUpgradeMessenger::Process;
    handler.name = "test_upgrade";
    ASSERT_EQ(0, messenger.AddNonProtocolHandler(handler));
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    butil::fd_guard peer_fd(fds[1]);
    messenger.peer_fd = peer_fd;
    SocketOptions options;
    options.fd = fds[0];
    options.user = &messenger;
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));
    ASSERT_EQ(5, write(peer_fd, "HSOK1", 5));
    messenger.OnNewMessagesUntil(socket.get(), TestUpgradeMessenger::StopReading);
    EXPECT_EQ(1, messenger.parse_calls);
    EXPECT_EQ(3, messenger.write_result);
    EXPECT_EQ(ESTABLISHED,
              messenger.adapter.session.phase(butil::memory_order_acquire));
    char remaining[3];
    ASSERT_EQ(3, recv(socket->fd(), remaining, sizeof(remaining), MSG_DONTWAIT));
    EXPECT_EQ(0, memcmp(remaining, "RPC", 3));
    socket->SetFailed();
}

static std::string MakeUBShmHello() {
    std::string frame(64, '\0');
    memcpy(&frame[0], "UB", 2);
    const uint16_t msg_len = butil::HostToNet16(64);
    const uint16_t hello_ver = butil::HostToNet16(2);
    const uint16_t impl_ver = butil::HostToNet16(1);
    memcpy(&frame[2], &msg_len, sizeof(msg_len));
    memcpy(&frame[4], &hello_ver, sizeof(hello_ver));
    memcpy(&frame[6], &impl_ver, sizeof(impl_ver));
    return frame;
}

TEST(TransportHandshakeTest, socket_io_pushback_prepends_buffered_input) {
    for (const std::string& suffix : {std::string(), std::string("tail")}) {
        SocketOptions options;
        SocketId id;
        ASSERT_EQ(0, Socket::Create(options, &id));
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(id, &socket));
        auto& input = socket->fd_input_processor().read_buf();
        input.append(suffix);
        SocketHandshakeIO io(socket.get());

        EXPECT_EQ(0, io.PushBack("PRPC", 4));
        EXPECT_EQ("PRPC" + suffix, input.to_string());
        EXPECT_EQ(0, io.PushBack("\0", 1));
        const std::string expected = std::string(1, '\0') + "PRPC" + suffix;
        EXPECT_EQ(expected, input.to_string());
        EXPECT_EQ(0, io.PushBack(nullptr, 0));
        EXPECT_EQ(expected, input.to_string());
        socket->SetFailed();
    }
}

TEST(HandshakeFrameTest,
     socket_magic_mismatch_restores_prefix_before_buffered_input) {
    HandshakeSocketPair pair;
    ASSERT_NO_FATAL_FAILURE(pair.Init());
    auto& peer_fd = pair.peer_fd;
    auto& socket = pair.socket;
    socket->fd_input_processor().read_buf().append("tail");
    ASSERT_EQ(4, write(peer_fd, "PRPC", 4));

    SocketHandshakeIO io(socket.get());
    std::string payload;
    EXPECT_EQ(FRAME_NOT_MINE, FrameCodec::ReadFrame(
        &io, FixedSpec("URMA", 4, 4), true, &payload));
    EXPECT_EQ("PRPCtail", socket->fd_input_processor().read_buf().to_string());
    socket->SetFailed();
}

TEST(TransportHandshakeTest, socket_io_initializes_read_butex) {
    auto* reused = bthread::butex_create_checked<butil::atomic<int>>();
    reused->store(37, butil::memory_order_relaxed);
    bthread::butex_destroy(reused);
    SocketHandshakeIO io;
    EXPECT_EQ(0, io._read_butex->load(butil::memory_order_relaxed));
    io.NotifyReadable();
    EXPECT_EQ(1, io._read_butex->load(butil::memory_order_relaxed));
}

TEST(TransportHandshakeTest, blocked_write_stops_after_socket_failure) {
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    butil::fd_guard peer_fd(fds[1]);
    const int flags = fcntl(fds[0], F_GETFL, 0);
    ASSERT_GE(flags, 0);
    ASSERT_EQ(0, fcntl(fds[0], F_SETFL, flags | O_NONBLOCK));
    char data[4096] = {};
    ssize_t n;
    do {
        n = send(fds[0], data, sizeof(data), MSG_NOSIGNAL);
    } while (n > 0 || (n < 0 && errno == EINTR));
    ASSERT_EQ(-1, n);
    ASSERT_EQ(EAGAIN, errno);

    SocketOptions options;
    options.fd = fds[0];
    options.need_on_edge_trigger = true;
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));
    SocketHandshakeIO io(socket.get());
    BlockingHandshakeWrite write(&io);
    bthread_t tid;
    ASSERT_EQ(0, bthread_start_background(
        &tid, nullptr, RunBlockingHandshakeWrite, &write));
    bool was_waiting = false;
    const int64_t wait_deadline = butil::gettimeofday_us() + 1000000;
    while (butil::gettimeofday_us() < wait_deadline) {
        // A nonzero wake count proves WriteAll reached WaitEpollOut's butex.
        if (bthread::butex_wake(socket->_epollout_butex, true) > 0) {
            was_waiting = true;
            break;
        }
        bthread_usleep(1000);
    }
    socket->SetFailed(EFAILEDSOCKET, "cancel blocked handshake write");
    bthread_flush();
    const int completed = write.completed.timed_wait(
        butil::milliseconds_from_now(1000));
    if (completed != 0) {
        shutdown(socket->fd(), SHUT_RDWR);
    }
    ASSERT_EQ(0, bthread_join(tid, nullptr));
    EXPECT_TRUE(was_waiting);
    EXPECT_EQ(0, completed);
    EXPECT_EQ(-1, write.result);
    EXPECT_EQ(EFAILEDSOCKET, write.error);
}

TEST(TransportHandshakeTest, established_socket_does_not_restart_handshake) {
    const SocketMode modes[] = {
        SOCKET_MODE_TCP,
#if BRPC_WITH_RDMA
        SOCKET_MODE_RDMA,
#endif
#if BRPC_WITH_UBRING
        SOCKET_MODE_UBRING,
#endif
    };
    for (SocketMode mode : modes) {
        SocketOptions options;
        options.socket_mode = mode;
        SocketId id;
        ASSERT_EQ(0, Socket::Create(options, &id));
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(id, &socket));
        AdapterTransport* adapter = AdapterTransport::Get(socket.get());
        adapter->handshake_session()->set_phase(ESTABLISHED);

        const char* prefixes[] = {"RDMA", "RDM3", "UB"};
        for (const char* prefix : prefixes) {
            butil::IOBuf input;
            input.append(prefix);
            const ParseResult result = adapter->ProcessUpgradeReadable(&input);
            EXPECT_EQ(PARSE_ERROR_TRY_OTHERS, result.error());
            EXPECT_EQ(prefix, input.to_string());
            EXPECT_EQ(ESTABLISHED, adapter->handshake_phase());
            EXPECT_EQ(nullptr, socket->parsing_context());
        }
        socket->SetFailed();
    }
}

TEST(TransportHandshakeTest, failed_socket_read_preserves_buffered_data) {
    for (size_t prefix_len : {0u, 1u}) {
        SCOPED_TRACE(prefix_len);
        int fds[2];
        ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
        butil::fd_guard peer_fd(fds[1]);
        SocketOptions options;
        options.fd = fds[0];
        SocketId id;
        ASSERT_EQ(0, Socket::Create(options, &id));
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(id, &socket));
        ASSERT_EQ(2, write(peer_fd, "HS", 2));

        SocketHandshakeIO io(socket.get());
        char data[2] = {};
        if (prefix_len != 0) {
            ASSERT_EQ(0, io.ReadExact(data, prefix_len));
            EXPECT_EQ('H', data[0]);
        }
        ASSERT_EQ(0, socket->SetFailed(EFAILEDSOCKET,
                                      "cancel buffered handshake read"));
        errno = 0;
        EXPECT_EQ(-1, io.ReadExact(data, 2 - prefix_len));
        EXPECT_EQ(EFAILEDSOCKET, errno);

        const ssize_t remaining = recv(socket->fd(), data, sizeof(data),
                                       MSG_DONTWAIT);
        ASSERT_EQ(static_cast<ssize_t>(2 - prefix_len), remaining);
        EXPECT_EQ(0, memcmp(data, "HS" + prefix_len, remaining));
    }
}

TEST(TransportHandshakeTest, blocked_read_stops_after_socket_failure) {
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    butil::fd_guard peer_fd(fds[1]);

    SocketOptions options;
    options.fd = fds[0];
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));

    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));

    SocketHandshakeIO io(socket.get());
    BlockingHandshakeRead read(&io);
    bthread_t tid;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    ASSERT_EQ(0, bthread_start_background(
        &tid, &attr, RunBlockingHandshakeRead, &read));

    bool was_waiting = false;
    const int64_t wait_deadline = butil::gettimeofday_us() + 1000000;
    while (butil::gettimeofday_us() < wait_deadline) {
        // A nonzero wake count proves ReadExact reached its read butex.
        if (bthread::butex_wake(io._read_butex, true) > 0) {
            was_waiting = true;
            break;
        }
        bthread_usleep(1000);
    }
    EXPECT_EQ(0, socket->SetFailed(
        EFAILEDSOCKET, "cancel blocked handshake read"));
    bthread_flush();
    const int completed = read.completed.timed_wait(
        butil::milliseconds_from_now(1000));
    if (completed != 0) {
        shutdown(socket->fd(), SHUT_RDWR);
        io.NotifyReadable();
    }
    ASSERT_EQ(0, bthread_join(tid, nullptr));

    EXPECT_TRUE(was_waiting);
    EXPECT_EQ(0, completed);
    EXPECT_EQ(-1, read.result);
    EXPECT_EQ(EFAILEDSOCKET, read.error);
}

TEST(HandshakeFrameTest, supports_two_byte_fixed_magic) {
    const FrameSpec spec = FixedSpec("UB", 2, 6);
    std::string frame;
    ASSERT_EQ(FRAME_OK, FrameCodec::Encode(spec, "data", &frame));
    ASSERT_EQ("UBdata", frame);
}

TEST(HandshakeFrameTest, encodes_u16_total_length) {
    const FrameSpec spec(
        "RDMA", 4, 8, 64, FrameSpec::U16_TOTAL_LENGTH);
    std::string frame;
    ASSERT_EQ(FRAME_OK, FrameCodec::Encode(spec, "xy", &frame));
    ASSERT_EQ(8UL, frame.size());
    uint16_t total_be = 0;
    memcpy(&total_be, frame.data() + 4, sizeof(total_be));
    ASSERT_EQ(8, butil::NetToHost16(total_be));
    ASSERT_EQ("xy", frame.substr(6));
}

TEST(HandshakeFrameTest, encodes_u32_body_length) {
    const FrameSpec spec(
        "RDM3", 4, 9, 32, FrameSpec::U32_BODY_LENGTH);
    std::string frame;
    ASSERT_EQ(FRAME_OK, FrameCodec::Encode(spec, "abc", &frame));
    uint32_t body_be = 0;
    memcpy(&body_be, frame.data() + 4, sizeof(body_be));
    ASSERT_EQ(3U, butil::NetToHost32(body_be));
    ASSERT_EQ("abc", frame.substr(8));
}

TEST(HandshakeFrameTest, buffered_partial_frame_is_not_consumed) {
    const FrameSpec spec(
        "RDMA", 4, 8, 64, FrameSpec::U16_TOTAL_LENGTH);
    std::string frame;
    ASSERT_EQ(FRAME_OK, FrameCodec::Encode(spec, "payload", &frame));
    butil::IOBuf source;
    source.append(frame.data(), frame.size() - 1);
    std::string payload;
    ASSERT_EQ(FRAME_NEED_MORE,
              FrameCodec::ParseBufferedFrame(&source, spec, &payload));
    ASSERT_EQ(frame.size() - 1, source.size());

    source.append(frame.data() + frame.size() - 1, 1);
    ASSERT_EQ(FRAME_OK,
              FrameCodec::ParseBufferedFrame(&source, spec, &payload));
    ASSERT_EQ("payload", payload);
    ASSERT_TRUE(source.empty());
}

TEST(HandshakeFrameTest, buffered_magic_mismatch_is_not_consumed) {
    const FrameSpec spec = FixedSpec("UB", 2, 6);
    butil::IOBuf source;
    source.append("XXdata", 6);
    std::string payload;
    ASSERT_EQ(FRAME_NOT_MINE,
              FrameCodec::ParseBufferedFrame(&source, spec, &payload));
    ASSERT_EQ(6UL, source.size());
}

TEST(HandshakeFrameTest, blocking_magic_mismatch_is_pushed_back) {
    MemoryHandshakeIO io("XX");
    const FrameSpec spec = FixedSpec("UB", 2, 6);
    std::string payload;
    ASSERT_EQ(FRAME_NOT_MINE,
              FrameCodec::ReadFrame(&io, spec, true, &payload));
    ASSERT_EQ("XX", io.pushed_back());
}

TEST(HandshakeFrameTest, rejects_lengths_outside_bounds) {
    const FrameSpec spec(
        "RDMA", 4, 8, 16, FrameSpec::U16_TOTAL_LENGTH);
    std::string header("RDMA", 4);
    const uint16_t total_be = butil::HostToNet16(17);
    header.append(reinterpret_cast<const char*>(&total_be), sizeof(total_be));
    butil::IOBuf source;
    source.append(header);
    std::string payload;
    ASSERT_EQ(FRAME_PROTOCOL_ERROR,
              FrameCodec::ParseBufferedFrame(&source, spec, &payload));
    ASSERT_EQ(header.size(), source.size());
}

#if BRPC_WITH_RDMA && BRPC_WITH_UBRING
TEST(TransportHandshakeTest, upgrade_capability_is_mode_specific) {
    SocketOptions options;
    SocketId id;
    SocketUniquePtr socket;

    options.socket_mode = SOCKET_MODE_RDMA;
    ASSERT_EQ(0, Socket::Create(options, &id));
    ASSERT_EQ(0, Socket::Address(id, &socket));
    AdapterTransport* rdma_adapter = AdapterTransport::Get(socket.get());
    ASSERT_TRUE(rdma_adapter->upgrade_capable(SOCKET_MODE_RDMA));
    ASSERT_FALSE(rdma_adapter->upgrade_capable(SOCKET_MODE_UBRING));
    socket->SetFailed();
    socket.reset();

    options.socket_mode = SOCKET_MODE_UBRING;
    ASSERT_EQ(0, Socket::Create(options, &id));
    ASSERT_EQ(0, Socket::Address(id, &socket));
    AdapterTransport* ubshm_adapter = AdapterTransport::Get(socket.get());
    ASSERT_TRUE(ubshm_adapter->upgrade_capable(SOCKET_MODE_UBRING));
    ASSERT_FALSE(ubshm_adapter->upgrade_capable(SOCKET_MODE_RDMA));
    socket->SetFailed();
}
#endif

TEST(TransportHandshakeTest, tcp_keeps_last_message_for_inline_processing) {
    struct Message : InputMessageBase {
        void DestroyImpl() override {}
        ~Message() override = default;
    } message;
    TcpTransport transport;
    InputMessageClosure input;
    input.reset(&message);
    int queued = 0;
    transport.QueueMessage(input, &queued, true);
    EXPECT_EQ(&message, input.release());
    EXPECT_EQ(0, queued);
}

TEST(TransportHandshakeTest, debug_reads_protocol_version_during_negotiation) {
    HandshakeSocketPair pair;
    ASSERT_NO_FATAL_FAILURE(pair.Init());
    auto* adapter = AdapterTransport::Get(pair.socket.get());
    auto* session = adapter->handshake_session();
    session->set_protocol_version(2);
    session->set_phase(HELLO_WAIT);
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            session->set_protocol_version(2);
            session->set_protocol_version(3);
        }
    });
    for (int i = 0; i < 64; ++i) {
        std::ostringstream output;
        adapter->Debug(output);
        const std::string snapshot = output.str();
        EXPECT_TRUE(snapshot.find("handshake_version=2") != std::string::npos ||
                    snapshot.find("handshake_version=3") != std::string::npos);
    }
    stop.store(true, std::memory_order_relaxed);
    writer.join();
}

TEST(TransportHandshakeTest, publish_fallback_after_tcp_state) {
    HandshakeSession session;
    int tcp_active = 0;
    session.set_phase(NEGOTIATING);
    session.PublishFallback([&tcp_active]() { tcp_active = 1; });
    ASSERT_EQ(1, tcp_active);
    ASSERT_EQ(FALLBACK_TCP, session.phase(butil::memory_order_acquire));
}

TEST(TransportHandshakeTest, failed_handshake_preserves_io_error_after_cleanup) {
    class CleanupChangesErrno : public TestHandshakeTransport {
    public:
        void OnFailed() override {
            TestHandshakeTransport::OnFailed();
            errno = ENOENT;
        }
    } transport;
    MemoryHandshakeIO io;
    HandshakeSession session;
    TestHandshakeProtocol protocol;

    EXPECT_EQ(STEP_ERROR, session.RunClient(io, &protocol, &transport));
    EXPECT_EQ(EIO, errno);
    EXPECT_TRUE(transport.failed);
    EXPECT_EQ(FAILED, session.phase(butil::memory_order_acquire));
}

class HandshakeSessionTest : public ::testing::Test {
protected:
    HandshakeSessionTest() : io("HSOK"), protocol(&calls), transport(&calls) {}
    MemoryHandshakeIO io;
    HandshakeSession session;
    std::string calls;
    TestHandshakeProtocol protocol;
    TestHandshakeTransport transport;
    butil::IOBuf source;
    std::vector<HandshakeProtocol*> protocols{&protocol};
};

TEST_F(HandshakeSessionTest, client_runs_codec_and_resource_sequence) {

    ASSERT_EQ(STEP_OK, session.RunClient(io, &protocol, &transport));
    ASSERT_EQ("prepare build parse negotiate ack1 activate", calls);
    ASSERT_EQ("HSLO1", io.output());
    ASSERT_EQ(ESTABLISHED, session.phase(butil::memory_order_acquire));
    ASSERT_EQ(7, session.protocol_version());
}

TEST_F(HandshakeSessionTest, client_exchanges_extension_before_ack) {
    io = MemoryHandshakeIO("HSOKE");
    protocol.EnableExtension(1);

    ASSERT_EQ(STEP_OK, session.RunClient(io, &protocol, &transport));
    EXPECT_EQ("HSLOE1", io.output());
    EXPECT_EQ(ESTABLISHED, session.phase(butil::memory_order_acquire));
}

TEST_F(HandshakeSessionTest, server_resumes_fragmented_extension_then_ack) {
    source.append("HSOK", 4);
    protocol.EnableExtension(2);

    ASSERT_EQ(STEP_NEED_MORE,
              session.RunServer(io, protocols, &source, &transport, false));
    EXPECT_EQ(EXTENSION_WAIT, session.phase(butil::memory_order_acquire));
    EXPECT_EQ("HSLO", io.output());
    source.append("E", 1);
    ASSERT_EQ(STEP_NEED_MORE,
              session.RunServer(io, protocols, &source, &transport, false));
    EXPECT_EQ(EXTENSION_WAIT, session.phase(butil::memory_order_acquire));
    source.append("11", 2);  // Remainder of extension, then ACK.
    ASSERT_EQ(STEP_OK,
              session.RunServer(io, protocols, &source, &transport, false));
    EXPECT_EQ("HSLOE1", io.output());
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(ESTABLISHED, session.phase(butil::memory_order_acquire));
}

TEST_F(HandshakeSessionTest, client_resource_failure_falls_back_before_io) {
    transport.prepare_result = STEP_FALLBACK;
    transport.negotiate_result = STEP_ERROR;

    ASSERT_EQ(STEP_FALLBACK, session.RunClient(io, &protocol, &transport));
    ASSERT_TRUE(transport.tcp_active);
    ASSERT_TRUE(io.output().empty());
    ASSERT_EQ(FALLBACK_TCP, session.phase(butil::memory_order_acquire));
}

TEST_F(HandshakeSessionTest, server_resumes_at_buffered_ack) {
    source.append("HSOK", 4);


    ASSERT_EQ(STEP_NEED_MORE,
              session.RunServer(io, protocols, &source, &transport, false));
    ASSERT_EQ(ACK_WAIT, session.phase(butil::memory_order_acquire));
    ASSERT_EQ("HSLO", io.output());
    ASSERT_TRUE(source.empty());

    source.append("1", 1);
    ASSERT_EQ(STEP_OK,
              session.RunServer(io, protocols, &source, &transport, false));
    ASSERT_EQ("parse prepare negotiate build parse_ack validate activate",
              calls);
    ASSERT_EQ(ESTABLISHED, session.phase(butil::memory_order_acquire));
}

TEST_F(HandshakeSessionTest, server_resource_failure_falls_back_after_ack) {
    source.append("HSOK0", 5);
    transport.prepare_result = STEP_FALLBACK;
    transport.negotiate_result = STEP_ERROR;

    ASSERT_EQ(STEP_FALLBACK,
              session.RunServer(io, protocols, &source, &transport, false));
    ASSERT_EQ("HSNO", io.output());
    ASSERT_TRUE(source.empty());
    ASSERT_TRUE(transport.tcp_active);
    ASSERT_EQ(FALLBACK_TCP, session.phase(butil::memory_order_acquire));
}

TEST_F(HandshakeSessionTest, server_falls_back_without_consuming_other_magic) {
    source.append("XXok", 4);
    transport.prepare_result = STEP_ERROR;
    transport.negotiate_result = STEP_ERROR;

    ASSERT_EQ(STEP_FALLBACK,
              session.RunServer(io, protocols, &source, &transport, true));
    ASSERT_TRUE(transport.tcp_active);
    ASSERT_EQ(4UL, source.size());
    ASSERT_EQ(FALLBACK_TCP, session.phase(butil::memory_order_acquire));

    ASSERT_EQ(STEP_NOT_MINE,
              session.RunServer(io, protocols, &source, &transport, true));
    ASSERT_EQ(4UL, source.size());
    ASSERT_EQ(FALLBACK_TCP, session.phase(butil::memory_order_acquire));
}

TEST_F(HandshakeSessionTest, server_enters_hello_phase_after_magic_matches) {

    source.append("H", 1);
    ASSERT_EQ(STEP_NEED_MORE,
              session.RunServer(io, protocols, &source, &transport, false));
    ASSERT_EQ(UNINITIALIZED, session.phase(butil::memory_order_acquire));

    source.append("S", 1);
    ASSERT_EQ(STEP_NEED_MORE,
              session.RunServer(io, protocols, &source, &transport, false));
    ASSERT_EQ(HELLO_WAIT, session.phase(butil::memory_order_acquire));
    ASSERT_EQ(7, session.protocol_version());
    ASSERT_EQ(2UL, source.size());
}

TEST(TransportHandshakeTest,
     impossible_magic_prefixes_try_other_protocols_without_consuming) {
    HandshakeSocketPair pair;
    ASSERT_NO_FATAL_FAILURE(pair.Init());
    auto& socket = pair.socket;

    const char* const invalid_prefixes[] = {
        "X", "UX", "RX", "RDN", "RDMB",
    };
    for (size_t i = 0; i < arraysize(invalid_prefixes); ++i) {
        butil::IOBuf source;
        source.append(invalid_prefixes[i]);
        const size_t original_size = source.size();
        const ParseResult result = policy::ParseTransportHandshake(
            &source, socket.get(), false, nullptr);
        ASSERT_FALSE(result.is_ok());
        EXPECT_EQ(PARSE_ERROR_TRY_OTHERS, result.error())
            << invalid_prefixes[i];
        EXPECT_EQ(original_size, source.size())
            << invalid_prefixes[i];
        EXPECT_EQ(UNINITIALIZED,
                  AdapterTransport::Get(socket.get())->handshake_phase());
        EXPECT_EQ(nullptr, socket->parsing_context());
    }
    socket->SetFailed();
}

TEST(TransportHandshakeTest,
     partial_rdma_magic_waits_for_more_data_without_consuming) {
    HandshakeSocketPair pair;
    ASSERT_NO_FATAL_FAILURE(pair.Init());
    auto& socket = pair.socket;

    const char* const partial_prefixes[] = {
        "R", "RD", "RDM",
    };
    for (size_t i = 0; i < arraysize(partial_prefixes); ++i) {
        butil::IOBuf source;
        source.append(partial_prefixes[i]);
        const size_t original_size = source.size();
        const ParseResult result = policy::ParseTransportHandshake(
            &source, socket.get(), false, nullptr);
        ASSERT_FALSE(result.is_ok());
        EXPECT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA, result.error())
            << partial_prefixes[i];
        EXPECT_EQ(original_size, source.size())
            << partial_prefixes[i];
        EXPECT_EQ(UNINITIALIZED,
                  AdapterTransport::Get(socket.get())->handshake_phase());
        EXPECT_EQ(nullptr, socket->parsing_context());
    }
    socket->SetFailed();
}

TEST(TransportHandshakeTest,
     plain_tcp_server_incrementally_rejects_ubshm_upgrade) {
    HandshakeSocketPair pair;
    ASSERT_NO_FATAL_FAILURE(pair.Init());
    auto& peer_fd = pair.peer_fd;
    auto& socket = pair.socket;

    const std::string hello = MakeUBShmHello();
    butil::IOBuf source;
    source.append(hello.data(), 1);
    ParseResult result = policy::ParseTransportHandshake(
        &source, socket.get(), false, nullptr);
    ASSERT_FALSE(result.is_ok());
    ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA, result.error());
    ASSERT_EQ(1UL, source.size());
    ASSERT_EQ(UNINITIALIZED,
              AdapterTransport::Get(socket.get())->handshake_phase());

    source.append(hello.data() + 1, hello.size() - 1);
    result = policy::ParseTransportHandshake(
        &source, socket.get(), false, nullptr);
    ASSERT_FALSE(result.is_ok());
    ASSERT_EQ(PARSE_ERROR_NOT_ENOUGH_DATA, result.error());
    ASSERT_TRUE(source.empty());
    ASSERT_NE(nullptr, socket->parsing_context());

    char reply[64];
    ASSERT_EQ(sizeof(reply), read(peer_fd, reply, sizeof(reply)));
    EXPECT_EQ(0, memcmp(reply, "UB", 2));
    uint16_t reply_len = 0;
    memcpy(&reply_len, reply + 2, sizeof(reply_len));
    EXPECT_EQ(64, butil::NetToHost16(reply_len));
    EXPECT_EQ(0, reply[4]);
    EXPECT_EQ(0, reply[5]);
    EXPECT_EQ(0, reply[6]);
    EXPECT_EQ(0, reply[7]);

    const uint32_t ack = 0;
    source.append(&ack, sizeof(ack));
    result = policy::ParseTransportHandshake(
        &source, socket.get(), false, nullptr);
    ASSERT_FALSE(result.is_ok());
    ASSERT_EQ(PARSE_ERROR_TRY_OTHERS, result.error());
    ASSERT_TRUE(source.empty());
    ASSERT_EQ(FALLBACK_TCP,
              AdapterTransport::Get(socket.get())->handshake_phase());
    ASSERT_EQ(nullptr, socket->parsing_context());
    socket->SetFailed();
}

TEST(TransportHandshakeTest,
     plain_tcp_server_preserves_data_coalesced_behind_ubshm_ack) {
    HandshakeSocketPair pair;
    ASSERT_NO_FATAL_FAILURE(pair.Init());
    auto& socket = pair.socket;

    butil::IOBuf source;
    source.append(MakeUBShmHello());
    const uint32_t ack = 0;
    source.append(&ack, sizeof(ack));
    const char application_data[] = "application-data";
    source.append(application_data, sizeof(application_data) - 1);

    const ParseResult result = policy::ParseTransportHandshake(
        &source, socket.get(), false, nullptr);
    ASSERT_FALSE(result.is_ok());
    ASSERT_EQ(PARSE_ERROR_TRY_OTHERS, result.error());
    ASSERT_EQ(sizeof(application_data) - 1, source.size());

    char remaining[sizeof(application_data) - 1] = {};
    ASSERT_EQ(sizeof(remaining),
              source.copy_to(remaining, sizeof(remaining)));
    EXPECT_EQ(0, memcmp(application_data, remaining, sizeof(remaining)));
    ASSERT_EQ(FALLBACK_TCP,
              AdapterTransport::Get(socket.get())->handshake_phase());
    ASSERT_EQ(nullptr, socket->parsing_context());
    socket->SetFailed();
}

#if BRPC_WITH_URMA
TEST(TransportHandshakeTest, urma_socket_skips_adapter_handshake_parser) {
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    butil::fd_guard peer_fd(fds[1]);

    SocketOptions options;
    options.fd = fds[0];
    options.socket_mode = SOCKET_MODE_URMA;
    SocketId id;
    ASSERT_EQ(0, Socket::Create(options, &id));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(id, &socket));

    butil::IOBuf source;
    source.append("PRPC", 4);
    const ParseResult result = policy::ParseTransportHandshake(
        &source, socket.get(), false, nullptr);
    ASSERT_FALSE(result.is_ok());
    EXPECT_EQ(PARSE_ERROR_TRY_OTHERS, result.error());
    EXPECT_EQ(4UL, source.size());
    EXPECT_EQ(nullptr, socket->parsing_context());
    socket->SetFailed();
}
#endif

#if BRPC_WITH_RDMA || BRPC_WITH_UBRING
TEST(TransportHandshakeTest, shared_app_connect_receives_each_socket_stop) {
    const SocketMode modes[] = {
#if BRPC_WITH_RDMA
        SOCKET_MODE_RDMA,
#endif
#if BRPC_WITH_UBRING
        SOCKET_MODE_UBRING,
#endif
    };
    for (SocketMode mode : modes) {
        const auto original = std::make_shared<RecordingAppConnect>();
        SocketOptions options;
        options.user =
            static_cast<SocketUser*>(get_or_new_client_side_messenger());
        options.socket_mode = mode;
        options.app_connect = original;
        SocketId first_id;
        ASSERT_EQ(0, Socket::Create(options, &first_id));
        SocketUniquePtr first;
        ASSERT_EQ(0, Socket::Address(first_id, &first));
        ASSERT_NE(nullptr, first->_app_connect);
        ASSERT_NE(original.get(), first->_app_connect.get());

        options.app_connect = first->_app_connect;
        SocketId second_id;
        ASSERT_EQ(0, Socket::Create(options, &second_id));
        SocketUniquePtr second;
        ASSERT_EQ(0, Socket::Address(second_id, &second));
        ASSERT_EQ(first->_app_connect.get(), second->_app_connect.get());

        AdapterTransport::Get(first.get())->FallbackToTcp();
        ASSERT_EQ(0, first->SetFailed());
        first.reset();
        ASSERT_EQ(0, original->stopped.timed_wait(
            butil::milliseconds_from_now(1000)));
        EXPECT_EQ(1, original->StopCount(first_id));
        EXPECT_EQ(0, original->StopCount(second_id));

        original->stopped.reset(1);
        AdapterTransport::Get(second.get())->FallbackToTcp();
        ASSERT_EQ(0, second->SetFailed());
        second.reset();
        ASSERT_EQ(0, original->stopped.timed_wait(
            butil::milliseconds_from_now(1000)));
        EXPECT_EQ(1, original->StopCount(first_id));
        EXPECT_EQ(1, original->StopCount(second_id));
    }
}
#endif

#if BRPC_WITH_RDMA
TEST(TransportHandshakeTest, inherited_adapter_connect_is_not_wrapped_twice) {
    auto original = std::make_shared<TestAppConnect>();
    SocketOptions options;
    options.user = get_or_new_client_side_messenger();
    options.need_on_edge_trigger = true;
    options.socket_mode = SOCKET_MODE_RDMA;
    options.app_connect = original;
    HandshakeSocketPair first;
    ASSERT_NO_FATAL_FAILURE(first.Init(options));
    const auto wrapped = AdapterTransport::Get(first.socket.get())->Connect();
    ASSERT_NE(nullptr, wrapped);
    EXPECT_NE(original.get(), wrapped.get());

    options.app_connect = wrapped;
    HandshakeSocketPair second;
    ASSERT_NO_FATAL_FAILURE(second.Init(options));
    EXPECT_EQ(wrapped.get(), AdapterTransport::Get(second.socket.get())->Connect().get());
}

#endif

#if BRPC_WITH_RDMA || BRPC_WITH_UBRING
TEST(TransportHandshakeTest, unavailable_upgrade_keeps_tcp_read_callback_after_reset) {
    const SocketMode modes[] = {
#if BRPC_WITH_RDMA
        SOCKET_MODE_RDMA,
#endif
#if BRPC_WITH_UBRING
        SOCKET_MODE_UBRING,
#endif
    };
    auto custom_read = [](Socket*) {};
    for (SocketMode mode : modes) {
        for (bool custom : {false, true}) {
            SCOPED_TRACE(::testing::Message() << mode << "/" << custom);
            auto original = std::make_shared<TestAppConnect>();
            SocketOptions options;
            options.user = get_or_new_client_side_messenger();
            options.need_on_edge_trigger = true;
            options.socket_mode = mode;
            options.app_connect = original;
            if (custom) {
                options.on_edge_triggered_events = +custom_read;
            }
            HandshakeSocketPair pair;
            ASSERT_NO_FATAL_FAILURE(pair.Init(options));
            auto* adapter = AdapterTransport::Get(pair.socket.get());
            adapter->high_speed_transport()->Release();
            EXPECT_EQ(original.get(), adapter->Connect().get());
            EXPECT_EQ(FALLBACK_TCP, adapter->handshake_phase());
            auto expected = custom ? +custom_read : InputMessenger::OnNewMessages;
            ASSERT_NE(nullptr, adapter->_on_edge_trigger.target<void (*)(Socket*)>());
            EXPECT_EQ(expected, *adapter->_on_edge_trigger.target<void (*)(Socket*)>());
            ASSERT_EQ(0, adapter->Reset(2));
            ASSERT_NE(nullptr, adapter->_on_edge_trigger.target<void (*)(Socket*)>());
            EXPECT_EQ(expected, *adapter->_on_edge_trigger.target<void (*)(Socket*)>());
        }
    }
}

#endif

}  // namespace handshake
}  // namespace brpc
