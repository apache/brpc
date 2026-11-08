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

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#if BRPC_WITH_RDMA
#include <errno.h>
#include <netinet/in.h>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>
#include "butil/memory/scope_guard.h"
#include "brpc/errno.pb.h"
#include "brpc/policy/baidu_rpc_meta.pb.h"
#include "brpc/rdma/rdma_connection.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_handshake.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/srq_receive_pool.h"
#include "brpc/rdma/srq_worker.h"
#include "rdma_test_helper.h"

DECLARE_int32(task_group_ntags);

namespace brpc {
DECLARE_bool(usercode_in_pthread);
DECLARE_bool(log_idle_connection_close);
DEFINE_bool(rdma_test_enable, false, "Enable tests requiring RDMA hardware.");

namespace rdma {
DECLARE_bool(rdma_trace_verbose);
DECLARE_int32(rdma_memory_pool_max_regions);
DECLARE_int32(rdma_client_handshake_version);
DECLARE_bool(rdma_recv_zerocopy);
DECLARE_int32(rdma_zerocopy_min_size);
DECLARE_int32(rdma_srq_workers);
DECLARE_int32(rdma_srq_depth);
DECLARE_int32(rdma_srq_pool_blocks);
DECLARE_int32(rdma_srq_pool_grow_blocks);
DECLARE_int32(rdma_srq_tx_cq_size);
DECLARE_int32(rdma_srq_control_reserve);
extern int (*IbvPostSrqSend)(ibv_qp*, ibv_send_wr*, ibv_send_wr**);
extern int (*IbvPostSrqRecv)(ibv_srq*, ibv_recv_wr*, ibv_recv_wr**);
extern int (*IbvPollSrqCq)(ibv_cq*, int, ibv_wc*);
extern butil::atomic<void (*)(SrqWorker*, bool, butil::atomic<uint64_t>*, SrqReceivePool*)>
    SrqWorkerStopHookForTest;
extern butil::atomic<void (*)()> SrqPostLockRetryHookForTest;
extern int (*IbvQueryDevice)(ibv_context*, ibv_device_attr*);
extern bool ValidateSrqDevice(ibv_context*);
extern int (*IbvModifyQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask);
extern int (*IbvDestroyQp)(ibv_qp*);
extern int (*IbvDestroySrq)(ibv_srq*);
extern void (*IbvAckAsyncEvent)(ibv_async_event*);
extern butil::atomic<bool> g_rdma_available;
extern bool g_skip_rdma_init;
}  // namespace rdma
}  // namespace brpc

using namespace brpc;
using namespace brpc::rdma_test;

static std::shared_ptr<rdma::SrqConnection> SrqConnectionOf(Socket* socket) {
    return std::dynamic_pointer_cast<rdma::SrqConnection>(
        RdmaTransportOf(socket)->_rdma_ep->_connection);
}

static std::shared_ptr<rdma::SrqConnection> SrqConnectionOf(
        const SocketUniquePtr& socket) {
    return SrqConnectionOf(socket.get());
}

static uint32_t ConnectionQpn(const rdma::RdmaConnection& connection) {
    rdma::RdmaLocalInfo info;
    connection.GetLocalInfo(&info);
    return info.qp_num;
}

static int64_t SrqCounter(const std::shared_ptr<rdma::SrqConnection>& connection,
                          const char* name) {
    const std::string key = std::string(name) + '=';
    int64_t value = -1;
    // Debug omits receive counts while the worker is busy. Wait until they are
    // readable. A missing value does not mean pool exhaustion. Atomic capacity
    // counters remain readable even in tests that deliberately pause the
    // worker.
    EXPECT_TRUE(WaitUntil([&] {
        std::ostringstream out;
        connection->Debug(out);
        const std::string text = out.str();
        const size_t pos = text.find(key);
        if (pos == std::string::npos) {
            return false;
        }
        value = strtoll(text.c_str() + pos + key.size(), nullptr, 10);
        return true;
    }));
    return value;
}

TEST(RdmaSrqOptionsTest, requires_event_mode_and_valid_worker_count) {
    GFLAGS_NAMESPACE::FlagSaver flags;
    rdma::FLAGS_rdma_use_srq = true;
    rdma::FLAGS_rdma_use_polling = true;
    EXPECT_FALSE(rdma::ValidateSrqOptions());
    errno = 0;
    EXPECT_EQ(-1, rdma::RdmaEndpoint::GlobalInitialize());
    EXPECT_EQ(EINVAL, errno);

    rdma::FLAGS_rdma_use_polling = false;
    rdma::FLAGS_rdma_poller_num = 0;
    rdma::FLAGS_rdma_srq_workers = 1;
    EXPECT_TRUE(rdma::ValidateSrqOptions());
    rdma::FLAGS_rdma_srq_workers = 0;
    EXPECT_FALSE(rdma::ValidateSrqOptions());

    // Private RQ ignores SRQ-specific options.
    rdma::FLAGS_rdma_use_srq = false;
    rdma::FLAGS_rdma_use_polling = true;
    EXPECT_TRUE(rdma::ValidateSrqOptions());
}

namespace {
ibv_device_attr g_srq_device_attr;
int QuerySrqTestDevice(ibv_context*, ibv_device_attr* attr) {
    *attr = g_srq_device_attr;
    return 0;
}
}  // namespace

class RdmaSrqDeviceTest : public ::testing::Test {
protected:
    void SetUp() override {
        _query_device = rdma::IbvQueryDevice;
        rdma::IbvQueryDevice = QuerySrqTestDevice;
        g_srq_device_attr = {};
        g_srq_device_attr.max_srq = 1;
        g_srq_device_attr.max_srq_sge = 1;
        g_srq_device_attr.max_srq_wr = 32;
        g_srq_device_attr.max_cqe = 128;
        rdma::FLAGS_rdma_srq_depth = 32;
        rdma::FLAGS_rdma_srq_tx_cq_size = 128;
        _context.device = &_device;
    }

    void TearDown() override { rdma::IbvQueryDevice = _query_device; }
    GFLAGS_NAMESPACE::FlagSaver _flags;
    decltype(rdma::IbvQueryDevice) _query_device = nullptr;
    ibv_device _device = {};
    ibv_context _context = {};
};

TEST_F(RdmaSrqDeviceTest, accepts_capabilities_independent_of_vendor_and_name) {
    for (uint32_t vendor : {0u, 0x02c9u, 0x8086u}) {
        g_srq_device_attr.vendor_id = vendor;
        for (const char* name : {"mlx5_0", "rdma_0", "renamed_device"}) {
            SCOPED_TRACE(name);
            snprintf(_device.name, sizeof(_device.name), "%s", name);
            EXPECT_TRUE(rdma::ValidateSrqDevice(&_context));
        }
    }
}

TEST_F(RdmaSrqDeviceTest, rejects_missing_srq_or_receive_sge_support) {
    g_srq_device_attr.max_srq = 0;
    EXPECT_FALSE(rdma::ValidateSrqDevice(&_context));
    EXPECT_EQ(ENOTSUP, errno);
    g_srq_device_attr.max_srq = 1;
    g_srq_device_attr.max_srq_sge = 0;
    EXPECT_FALSE(rdma::ValidateSrqDevice(&_context));
    EXPECT_EQ(ENOTSUP, errno);
}

TEST_F(RdmaSrqDeviceTest, rejects_depths_exceeding_device_limits) {
    g_srq_device_attr.max_srq_wr = 31;
    EXPECT_FALSE(rdma::ValidateSrqDevice(&_context));
    EXPECT_EQ(EINVAL, errno);
    g_srq_device_attr.max_srq_wr = 32;
    g_srq_device_attr.max_cqe = 127;
    EXPECT_FALSE(rdma::ValidateSrqDevice(&_context));
    EXPECT_EQ(EINVAL, errno);
    g_srq_device_attr.max_cqe = 128;
    g_srq_device_attr.max_srq_wr = 129;
    rdma::FLAGS_rdma_srq_depth = 129;
    EXPECT_FALSE(rdma::ValidateSrqDevice(&_context));
    EXPECT_EQ(EINVAL, errno);
    rdma::FLAGS_rdma_srq_depth = 128;
    EXPECT_TRUE(rdma::ValidateSrqDevice(&_context));
}

// A small real registered pool makes overload and recovery observable without
// allocating gigabytes or mutating options while a worker is using them.
class RdmaSrqPoolTest : public RdmaTest {
protected:
    RdmaSrqPoolTest() : RdmaTest(8714) {}

    void SetUp() override {
        if (!FLAGS_rdma_test_enable) {
            GTEST_SKIP();
        }
        if (rdma::FLAGS_rdma_use_polling) {
            GTEST_SKIP() << "SRQ only supports event mode";
        }
        if (FLAGS_task_group_ntags != 1 || rdma::GetRdmaBlockSize() != 8192) {
            GTEST_SKIP() << "Pool pressure tests require one tag and 8 KiB blocks";
        }
        rdma::StopAllSrqWorkers();
        ASSERT_TRUE(WaitUntil([] { return rdma::SrqReceivePool::LivePools() == 0; }));
        _flags = std::make_unique<GFLAGS_NAMESPACE::FlagSaver>();
        rdma::FLAGS_rdma_use_srq = true;
        rdma::FLAGS_rdma_client_handshake_version = 3;
        rdma::FLAGS_rdma_srq_workers = 1;
        rdma::FLAGS_rdma_srq_depth = 32;
        rdma::FLAGS_rdma_srq_pool_blocks = 256;
        rdma::FLAGS_rdma_srq_pool_grow_blocks = 32;
        rdma::FLAGS_rdma_srq_window = 16;
        rdma::FLAGS_rdma_srq_sq_size = 32;
        rdma::FLAGS_rdma_srq_tx_cq_size = 128;
        rdma::FLAGS_rdma_srq_control_reserve = 8;
        rdma::FLAGS_rdma_recv_zerocopy = true;
        rdma::FLAGS_rdma_zerocopy_min_size = 1;
        ASSERT_TRUE(rdma::ValidateSrqOptions());
        StartServer();
    }

    void TearDown() override {
        if (_flags) {
            StopServer();
            rdma::StopAllSrqWorkers();
            EXPECT_TRUE(WaitUntil([] { return rdma::SrqReceivePool::LivePools() == 0; }));
            _flags.reset();
        }
        RdmaTest::TearDown();
    }

    void InitChannel(Channel* channel, const std::string& group) {
        ChannelOptions options;
        options.socket_mode = SOCKET_MODE_RDMA;
        options.connection_type = "single";
        options.connection_group = group;
        options.timeout_ms = 15000;
        options.max_retry = 0;
        ASSERT_EQ(0, channel->Init(g_ep, &options));
        ASSERT_EQ(1, SendEchoRpcs(*channel, 1, 0));
    }

    std::shared_ptr<rdma::SrqConnection> Connection(Channel& channel) {
        SocketUniquePtr socket;
        if (Socket::Address(channel._server_id, &socket) != 0) {
            return {};
        }
        return SrqConnectionOf(socket);
    }

    // Hold completed replies, independent of the transport/parser lifetime.
    void FillPool(Channel* channels, std::vector<butil::IOBuf>* retained) {
        auto connection = Connection(channels[0]);
        ASSERT_TRUE(connection);
        butil::IOBuf payload;
        ASSERT_EQ(0, payload.resize(64 * 1024, 'h'));
        for (int i = 0; i < 32; ++i) {
            Controller cntl;
            test::EchoRequest req;
            test::EchoResponse res;
            req.set_message("hold pool blocks");
            cntl.request_attachment().append(payload);
            ::test::EchoService::Stub(&channels[i % 4]).Echo(&cntl, &req, &res, nullptr);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            ASSERT_TRUE(cntl.response_attachment().equals(payload));
            retained->push_back(cntl.response_attachment());
            if (rdma::SrqReceivePool::RegisteredBytes() ==
                    rdma::GetRdmaBlockSize() * rdma::FLAGS_rdma_srq_pool_blocks &&
                SrqCounter(connection, "rdma_srq_pool_free") < 32) {
                break;
            }
        }
        ASSERT_TRUE(WaitUntil([&] {
            return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0;
        }));
        ASSERT_LT(SrqCounter(connection, "rdma_srq_pool_free"), 32);
    }

    struct PendingEcho : google::protobuf::Closure {
        Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        butil::IOBuf payload;
        butil::atomic<bool> done{false};
        bool started = false;
        void Start(Channel& channel, size_t size) {
            ASSERT_EQ(0, payload.resize(size, 'p'));
            req.set_message("partial under pressure");
            cntl.request_attachment().append(payload);
            started = true;
            ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, this);
        }
        void Run() override { done.store(true, butil::memory_order_release); }
        ~PendingEcho() {
            if (started) {
                bthread_id_join(cntl.call_id());
            }
        }
    };

    // Block the last response callback and verify that it no longer owns
    // consumption for this connection.
    struct BlockedReply : google::protobuf::Closure {
        Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        butil::atomic<bool> entered{false};
        butil::atomic<bool> release{false};
        bool started = false;
        int attr_error = -1;
        bthread_attr_t attr;
        void Start(Channel& channel) {
            req.set_message("hold one connection's callback");
            cntl.set_timeout_ms(5000);
            started = true;
            ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, this);
        }
        void Run() override {
            attr_error = bthread_getattr(bthread_self(), &attr);
            entered.store(true, butil::memory_order_release);
            while (!release.load(butil::memory_order_acquire)) {
                bthread_usleep(1000);
            }
            if (!attr_error) {
                bthread_attr_destroy(&attr);
            }
        }
        ~BlockedReply() {
            release.store(true, butil::memory_order_release);
            if (started) {
                bthread_id_join(cntl.call_id());
            }
        }
    };

    std::unique_ptr<GFLAGS_NAMESPACE::FlagSaver> _flags;
};

TEST_F(RdmaSrqPoolTest, zero_copy_attachment_outlives_worker) {
    rdma::FLAGS_rdma_srq_pool_blocks = 1024;
    butil::IOBuf received;
    butil::IOBuf expected;
    ASSERT_EQ(0, expected.resize(1048576, 'q'));
    {
        Channel channel;
        ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "attachment_outlives_worker"));
        Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message("hold SRQ attachment");
        cntl.request_attachment().append(expected);
        ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, nullptr);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        received = cntl.response_attachment();
        ASSERT_TRUE(received.equals(expected));
        ASSERT_NE(0u, rdma::SrqReceivePool::RegionId(received.fetch1()));
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(cntl._single_server_id, &socket));
        auto connection = SrqConnectionOf(socket);
        ASSERT_TRUE(connection);
        rdma::SrqConnection::PostLock lock(connection.get());
        ASSERT_EQ(static_cast<rdma::RdmaAsyncEventContext*>(connection.get()),
                  connection->qp()->qp_context);
    }
    StopServer();
    rdma::StopAllSrqWorkers();
    EXPECT_EQ(0u, rdma::SrqLiveConnections());
    EXPECT_GT(rdma::SrqReceivePool::LivePools(), 0u);
    EXPECT_TRUE(received.equals(expected));
    received.clear();
    EXPECT_TRUE(WaitUntil([] { return rdma::SrqReceivePool::LivePools() == 0; }));
}

TEST_F(RdmaSrqPoolTest, copied_receives_recover_full_posted_capacity) {
    // Copy even if concurrent RPCs are coalesced into one larger SEND, so all
    // completed receives exercise owner-local descriptor reuse.
    rdma::FLAGS_rdma_recv_zerocopy = false;
    Channel channel;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "copied_receive_reuse"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    for (int round = 0; round < 4; ++round) {
        ASSERT_EQ(128, SendEchoRpcs(channel, 128, 64));
    }
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0 &&
               SrqCounter(connection, "rdma_srq_posted") == 32 &&
               SrqCounter(connection, "rdma_srq_pool_free") ==
                   static_cast<int64_t>(rdma::SrqReceivePool::RegisteredBytes() /
                                        rdma::GetRdmaBlockSize()) - 32;
    }));
}

TEST_F(RdmaSrqPoolTest, held_attachments_release_and_resume) {
    Channel channels[4], blocked;
    for (int i = 0; i < 4; ++i) {
        ASSERT_NO_FATAL_FAILURE(InitChannel(&channels[i], "pool_hold_" + std::to_string(i)));
    }
    // Establish the connection first so connection-validation RPCs do not wait
    // on an exhausted receive pool.
    ASSERT_NO_FATAL_FAILURE(InitChannel(&blocked, "pool_resume"));
    auto connection = Connection(channels[0]);
    ASSERT_TRUE(connection);
    const size_t initial_bytes = rdma::SrqReceivePool::RegisteredBytes();
    std::vector<butil::IOBuf> retained;
    ASSERT_NO_FATAL_FAILURE(FillPool(channels, &retained));
    EXPECT_GT(rdma::SrqReceivePool::RegisteredBytes(), initial_bytes);
    PendingEcho call;
    ASSERT_NO_FATAL_FAILURE(call.Start(blocked, 512 * 1024));
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connection, "rdma_srq_pool_free") == 0 &&
            SrqCounter(connection, "rdma_srq_posted") == 0;
    }));
    EXPECT_FALSE(call.done.load(butil::memory_order_acquire));
    EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_closing"));
    butil::IOBuf expected;
    ASSERT_EQ(0, expected.resize(64 * 1024, 'h'));
    for (const auto& data : retained) {
        EXPECT_TRUE(data.equals(expected));
    }
    // With no posted receive WRs left, returning blocks must wake the
    // worker and refill the SRQ without another incoming CQ event.
    retained.clear();
    ASSERT_TRUE(WaitUntil([&] { return call.done.load(butil::memory_order_acquire); }));
    ASSERT_FALSE(call.cntl.Failed()) << call.cntl.ErrorText();
    EXPECT_TRUE(call.cntl.response_attachment().equals(call.payload));
    ASSERT_TRUE(WaitUntil([&] { return SrqCounter(connection, "rdma_srq_posted") == 32; }));
    EXPECT_FALSE(connection->worker_retired());
}

TEST_F(RdmaSrqPoolTest, quiet_one_way_send_releases_user_memory) {
    Channel channel;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "quiet_user_send"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0;
    }));
    constexpr size_t size = 64;
    std::unique_ptr<void, decltype(&free)> memory(malloc(size), free);
    ASSERT_TRUE(memory);
    memset(memory.get(), 'q', size);
    memcpy(memory.get(), "PRPC", 4);
    const uint32_t lengths[] = {htonl(4 * 1024 * 1024), 0};
    memcpy(static_cast<char*>(memory.get()) + 4, lengths, sizeof(lengths));
    const uint32_t lkey = rdma::RegisterMemoryForRdma(memory.get(), size);
    ASSERT_NE(0u, lkey);
    auto released = std::make_shared<butil::atomic<int64_t>>(0);
    butil::IOBuf data;
    ASSERT_EQ(0, data.append_user_data_with_meta(
        memory.get(), size, [connection, released](void* p) {
            (void)connection->IsWritable();
            rdma::DeregisterMemoryForRdma(p);
            free(p);
            released->store(butil::monotonic_time_us(), butil::memory_order_release);
        }, lkey));
    memory.release();
    // An incomplete RPC elicits no response or credit request at this window
    // position. No subsequent send or connection close may free the buffer.
    const int64_t started = butil::monotonic_time_us();
    butil::IOBuf* ptr = &data;
    ASSERT_EQ(static_cast<ssize_t>(size), connection->Send(&ptr, 1));
    ASSERT_TRUE(data.empty());
    ASSERT_TRUE(WaitUntil([&] {
        return released->load(butil::memory_order_acquire) != 0;
    }));
    LOG(INFO) << "Quiet SRQ user buffer released after "
              << released->load() - started << " us";
    EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_sq_outstanding"));
    EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_closing"));
    EXPECT_FALSE(connection->worker_retired());
}

TEST_F(RdmaSrqPoolTest, blocked_callback_does_not_stall_same_connection_or_other_qps) {
    if (rdma::FLAGS_rdma_disable_bthread || FLAGS_usercode_in_pthread) {
        GTEST_SKIP() << "Callback attribute checks require normal bthreads";
    }
    Channel channel, other;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "blocking_delivery_done"));
    ASSERT_NO_FATAL_FAILURE(InitChannel(&other, "beside_blocking_done"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    // Construct next first so failure cleanup unblocks call before waiting for
    // next to finish.
    PendingEcho next;
    BlockedReply call;
    call.Start(channel);
    ASSERT_TRUE(WaitUntil([&] { return call.entered.load(butil::memory_order_acquire); }));
    ASSERT_FALSE(call.cntl.Failed()) << call.cntl.ErrorText();
    ASSERT_EQ(0, call.attr_error);
    EXPECT_EQ(nullptr, call.attr.keytable_pool);
    EXPECT_EQ(0, call.attr.tag);
    ASSERT_EQ(0u, connection->_delivery_events.load(butil::memory_order_acquire));
    ASSERT_NO_FATAL_FAILURE(next.Start(channel, 64));
    // The previous callback is still blocked, but parsing has released consumer
    // ownership. A new response on the same connection must complete.
    ASSERT_TRUE(WaitUntil([&] {
        return next.done.load(butil::memory_order_acquire);
    }));
    EXPECT_FALSE(next.cntl.Failed()) << next.cntl.ErrorText();
    EXPECT_TRUE(next.cntl.response_attachment().equals(next.payload));
    EXPECT_FALSE(call.release.load(butil::memory_order_acquire));
    Controller cntl;
    test::EchoRequest req;
    test::EchoResponse res;
    cntl.set_timeout_ms(WAIT_TIMEOUT_US / 1000);
    req.set_message("progress while other callback is blocked");
    ASSERT_EQ(0, cntl.request_attachment().resize(8192, 'c'));
    ::test::EchoService::Stub(&other).Echo(&cntl, &req, &res, nullptr);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    EXPECT_TRUE(cntl.response_attachment().equals(cntl.request_attachment()));
    EXPECT_FALSE(connection->worker_retired());
    call.release.store(true, butil::memory_order_release);
}

TEST_F(RdmaSrqPoolTest, close_leaves_queued_data_to_the_single_consumer) {
    Channel channel;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "close_blocked_consumer"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    // Hold consumer ownership without dequeueing. Application callbacks no
    // longer own consumption and cannot be used to block the consumer.
    ASSERT_TRUE(WaitUntil([&] {
        uint64_t events = 0;
        return connection->_delivery_events.compare_exchange_strong(
            events, 1, butil::memory_order_acq_rel);
    }));
    auto consume = butil::MakeScopeGuard([&] { connection->Consume(nullptr); });

    auto released = std::make_shared<butil::atomic<int>>(0);
    butil::IOBuf received;
    auto* payload = malloc(64);
    ASSERT_NE(nullptr, payload);
    ASSERT_EQ(0, received.append_user_data(payload, 64, [released](void* data) {
        free(data);
        released->fetch_add(1, butil::memory_order_release);
    }));
    ASSERT_TRUE(connection->EnqueueReceived(received));
    received.clear();
    connection->StartDelivery();
    connection->Close();
    // Close stops submission but must not Dequeue concurrently. The current
    // consumer still owns the queue.
    EXPECT_EQ(0, released->load(butil::memory_order_acquire));
    connection->Consume(nullptr);
    consume.dismiss();
    ASSERT_TRUE(WaitUntil([&] {
        return connection->_delivery_events.load(butil::memory_order_acquire) == 0 &&
               released->load(butil::memory_order_acquire) == 1;
    }));
    EXPECT_FALSE(connection->EnqueueReceived(received));
}

TEST_F(RdmaSrqPoolTest, delivery_early_exit_releases_payloads_and_consumer) {
    for (bool socket_failed : {false, true}) {
        SCOPED_TRACE(socket_failed);
        SocketOptions options;
        SocketId sid;
        ASSERT_EQ(0, Socket::Create(options, &sid));
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(sid, &socket));
        BUTIL_SCOPE_EXIT {
            socket->SetFailed();
        };
        auto connection = rdma::SrqConnection::Create(
            socket.get(), BTHREAD_TAG_DEFAULT, rdma::FLAGS_rdma_srq_sq_size);
        ASSERT_TRUE(connection);
        ASSERT_EQ(0, connection->EnableReceive());

        auto released = std::make_shared<butil::atomic<int>>(0);
        for (int i = 0; i < 2; ++i) {
            butil::IOBuf data;
            auto* payload = malloc(64);
            ASSERT_NE(nullptr, payload);
            ASSERT_EQ(0, data.append_user_data(payload, 64, [released](void* p) {
                free(p);
                released->fetch_add(1, butil::memory_order_release);
            }));
            ASSERT_TRUE(connection->EnqueueReceived(data.movable()));
        }
        if (socket_failed) {
            // This Socket has no RDMA transport. Failure only makes Address
            // fail. StartDelivery cleanup must still close the connection
            // explicitly.
            socket->SetFailed();
            ASSERT_FALSE(connection->_closing.load(butil::memory_order_acquire));
        } else {
            connection->Close();
        }
        EXPECT_FALSE(connection->StartDelivery());
        // Cover both the prefetched first block and later queued blocks.
        // Release each once and return consumer ownership.
        EXPECT_EQ(2, released->load(butil::memory_order_acquire));
        EXPECT_EQ(0u, connection->_delivery_events.load(butil::memory_order_acquire));
        EXPECT_TRUE(connection->_closing.load(butil::memory_order_acquire));
        EXPECT_FALSE(connection->worker_retired());
        ASSERT_TRUE(WaitUntil([&] {
            rdma::SrqConnection::PostLock lock(connection.get());
            return connection->qp() == nullptr;
        }));
    }
}

// Verify receive delivery and fragmented RPC parsing.
class RdmaSrqReceiveTest : public RdmaSrqPoolTest {
protected:
    int PeerError(Socket* socket) {
        pthread_mutex_lock(&socket->_id_wait_list_mutex);
        const int error = socket->_error_code;
        pthread_mutex_unlock(&socket->_id_wait_list_mutex);
        return error;
    }

    void InitPeer(Channel* channel, SocketUniquePtr* peer) {
        ASSERT_NO_FATAL_FAILURE(InitChannel(channel, "partial_receive"));
        std::vector<SocketId> ids;
        _server._am->ListConnections(&ids);
        ASSERT_EQ(1u, ids.size());
        ASSERT_EQ(0, Socket::Address(ids[0], peer));
        ASSERT_TRUE(SrqConnectionOf(*peer));
    }

    int64_t BufferedBytes(Socket* peer) {
        auto connection = SrqConnectionOf(peer);
        // Temporarily acquire consumption on an idle connection so parser
        // inspection cannot race with a delivery task. Afterwards, run the
        // normal consumer to handle intervening notifications and return
        // ownership through CAS.
        uint64_t events = 0;
        if (!connection->_delivery_events.compare_exchange_strong(
                events, 1, butil::memory_order_acq_rel)) {
            return -1;
        }
        int64_t bytes = RdmaTransportOf(peer)->_rdma_ep->_input_processor.read_buf().size();
        connection->Consume(peer);
        return bytes;
    }

    butil::IOBuf Header(uint32_t body_size, uint32_t meta_size = 0) {
        const uint32_t lengths[] = {htonl(body_size), htonl(meta_size)};
        butil::IOBuf result;
        EXPECT_EQ(0, result.append("PRPC", 4));
        EXPECT_EQ(0, result.append(lengths, sizeof(lengths)));
        return result;
    }

    void MakeEcho(butil::IOBuf* header, butil::IOBuf* body) {
        policy::RpcMeta meta;
        meta.mutable_request()->set_service_name(test::EchoService::descriptor()->full_name());
        meta.mutable_request()->set_method_name("Echo");
        // Raw messages have no client RPC waiter. Replies are discarded by the
        // existing path.
        meta.set_correlation_id(0);
        test::EchoRequest request;
        request.set_message(std::string(256, 's'));
        std::string meta_bytes, request_bytes;
        ASSERT_TRUE(meta.SerializeToString(&meta_bytes));
        ASSERT_TRUE(request.SerializeToString(&request_bytes));
        ASSERT_EQ(0, body->append(meta_bytes));
        ASSERT_EQ(0, body->append(request_bytes));
        *header = Header(body->size(), meta_bytes.size());
    }

    bool SendBytes(const std::shared_ptr<rdma::SrqConnection>& connection,
                   butil::IOBuf data) {
        const int64_t deadline = butil::monotonic_time_us() + WAIT_TIMEOUT_US;
        while (!data.empty()) {
            butil::IOBuf* ptr = &data;
            const ssize_t bytes = connection->Send(&ptr, 1);
            if (bytes < 0 && errno != EAGAIN) {
                return false;
            }
            if (butil::monotonic_time_us() >= deadline) {
                return false;
            }
            if (bytes < 0) {
                usleep(WAIT_INTERVAL_US);
            }
        }
        return true;
    }
};

TEST_F(RdmaSrqReceiveTest, queued_data_waits_for_enable_receive) {
    Channel channel;
    SocketUniquePtr peer;
    ASSERT_NO_FATAL_FAILURE(InitPeer(&channel, &peer));
    auto receiver = SrqConnectionOf(peer);
    ASSERT_TRUE(WaitUntil([&] { return BufferedBytes(peer.get()) == 0; }));
    receiver->_delivery_enabled.store(false, butil::memory_order_release);
    butil::IOBuf header, body;
    ASSERT_NO_FATAL_FAILURE(MakeEcho(&header, &body));
    header.append(body);
    int served = g_echo_served.load(butil::memory_order_relaxed);
    ASSERT_TRUE(receiver->EnqueueReceived(header.movable()));
    EXPECT_TRUE(header.empty());
    EXPECT_FALSE(receiver->StartDelivery());
    ASSERT_TRUE(WaitUntil([&] {
        return receiver->_delivery_events.load(butil::memory_order_acquire) == 0;
    }));
    EXPECT_EQ(served, g_echo_served.load(butil::memory_order_relaxed));
    ASSERT_EQ(0, receiver->EnableReceive());
    ASSERT_TRUE(WaitUntil([&] {
        return g_echo_served.load(butil::memory_order_relaxed) == served + 1 &&
               BufferedBytes(peer.get()) == 0;
    }));
    EXPECT_EQ(0, PeerError(peer.get()));
}

TEST_F(RdmaSrqReceiveTest, concurrent_enqueue_and_consumer_handoff) {
    Channel channel;
    SocketUniquePtr peer;
    ASSERT_NO_FATAL_FAILURE(InitPeer(&channel, &peer));
    auto receiver = SrqConnectionOf(peer);
    butil::IOBuf header, body;
    ASSERT_NO_FATAL_FAILURE(MakeEcho(&header, &body));
    header.append(body);
    int served = g_echo_served.load(butil::memory_order_relaxed);
    butil::atomic<bool> failed{false};
    std::vector<std::thread> producers;
    // Each node contains a complete message. Concurrent producers must preserve
    // byte-stream message boundaries.
    for (int i = 0; i < 4; ++i) {
        producers.emplace_back([&] {
            for (int j = 0; j < 128; ++j) {
                if (!receiver->EnqueueReceived(header)) {
                    failed.store(true, butil::memory_order_relaxed);
                    return;
                }
                receiver->StartDelivery();
                if (j % 8 == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    EXPECT_FALSE(failed.load(butil::memory_order_relaxed));
    ASSERT_TRUE(WaitUntil([&] {
        return g_echo_served.load(butil::memory_order_relaxed) == served + 512 &&
               BufferedBytes(peer.get()) == 0;
    }));
    EXPECT_EQ(0, PeerError(peer.get()));
    EXPECT_EQ(1, SendEchoRpcs(channel, 1, 64));
}

TEST_F(RdmaSrqReceiveTest, protocol_reassembles_multiple_fragments) {
    Channel channel;
    SocketUniquePtr peer;
    ASSERT_NO_FATAL_FAILURE(InitPeer(&channel, &peer));
    auto sender = Connection(channel);
    auto receiver = SrqConnectionOf(peer);
    butil::IOBuf header, body;
    ASSERT_NO_FATAL_FAILURE(MakeEcho(&header, &body));
    ASSERT_TRUE(SendBytes(sender, header));
    ASSERT_TRUE(WaitUntil([&] {
        return BufferedBytes(peer.get()) == static_cast<int64_t>(header.size());
    }));
    const int served = g_echo_served.load(butil::memory_order_relaxed);
    size_t expected = header.size();
    for (int i = 0; i < 8; ++i) {
        bthread_usleep(10000);
        butil::IOBuf fragment;
        ASSERT_GT(body.cutn(&fragment, body.size() / (8 - i)), 0u);
        ASSERT_TRUE(SendBytes(sender, fragment));
        expected += fragment.size();
        ASSERT_TRUE(WaitUntil([&] {
            return BufferedBytes(peer.get()) ==
                (body.empty() ? 0 : static_cast<int64_t>(expected));
        }));
        ASSERT_EQ(0, PeerError(peer.get()));
        ASSERT_EQ(0, SrqCounter(receiver, "rdma_srq_closing"));
    }
    ASSERT_TRUE(WaitUntil([&] {
        return g_echo_served.load(butil::memory_order_relaxed) == served + 1;
    }));
    EXPECT_EQ(1, SendEchoRpcs(channel, 1, 8192));
    EXPECT_EQ(sender, Connection(channel));
}

namespace {
struct SrqPostFault {
    butil::atomic<int> after{-1};
    butil::atomic<bool> unknown{false};
    butil::atomic<int> hits{0};
    butil::atomic<int> attempted{0};
    butil::atomic<int> accepted{0};
    butil::atomic<int> real_error{0};
    void Arm(int prefix, bool invalid_bad_wr) {
        unknown.store(invalid_bad_wr, butil::memory_order_relaxed);
        hits.store(0, butil::memory_order_relaxed);
        attempted.store(0, butil::memory_order_relaxed);
        accepted.store(0, butil::memory_order_relaxed);
        real_error.store(0, butil::memory_order_relaxed);
        after.store(prefix, butil::memory_order_release);
    }
};
SrqPostFault g_send_post_fault;
SrqPostFault g_recv_post_fault;
struct SrqPostPause {
    butil::atomic<bool> reached{false};
    butil::atomic<bool> resume{false};
    std::thread submitter;
    ~SrqPostPause();
};
butil::atomic<SrqPostPause*> g_send_post_pause{nullptr};
butil::atomic<uint32_t> g_observe_tx_qpn{0};
butil::atomic<bool> g_observed_tx{false};
// Pause reads from a selected TX CQ to deterministically exhaust shared-CQ or
// per-connection SQ capacity.
butil::atomic<ibv_cq*> g_paused_tx_cq{nullptr};
butil::atomic<bool> g_tx_poll_paused{false};
SrqPostPause::~SrqPostPause() {
    auto* self = this;
    g_send_post_pause.compare_exchange_strong(self, nullptr);
    resume.store(true, butil::memory_order_release);
    if (submitter.joinable()) {
        submitter.join();
    }
}
// Pause only the worker-lock attempt simulated by the test thread. Real worker
// traffic remains concurrent.
thread_local SrqPostPause* g_post_lock_retry_pause = nullptr;
void PausePostLockRetry() {
    auto* pause = g_post_lock_retry_pause;
    if (!pause) {
        return;
    }
    pause->reached.store(true, butil::memory_order_release);
    while (!pause->resume.load(butil::memory_order_acquire)) {
        bthread_usleep(1000);
    }
}
butil::atomic<uint32_t> g_send_fault_qpn{0};
butil::atomic<bool> g_send_fault_control{false};
// Count real data WR attempts on the selected QP, across post_send batches.
butil::atomic<uint64_t> g_observed_send_wrs{0};
decltype(rdma::IbvPostSrqSend) g_real_srq_send = nullptr;
decltype(rdma::IbvPostSrqRecv) g_real_srq_recv = nullptr;
decltype(rdma::IbvPollSrqCq) g_real_srq_poll = nullptr;

// Submit a real prefix to the NIC, then simulate the provider rejecting the
// suffix. An invalid `bad_wr` exercises the conservative DMA teardown path.
template <typename Queue, typename Wr, typename Post>
int InjectSrqPostFailure(Queue* queue, Wr* first, Wr** bad,
                        SrqPostFault& fault, Post real_post) {
    int prefix = fault.after.load(butil::memory_order_acquire);
    if (prefix < 0) {
        return real_post(queue, first, bad);
    }
    int count = 0;
    Wr* split = nullptr;
    Wr* last = nullptr;
    for (Wr* wr = first; wr; wr = wr->next, ++count) {
        if (count == prefix) {
            split = wr;
        }
        if (count == prefix - 1) {
            last = wr;
        }
    }
    if (count < prefix || (count == prefix && !fault.unknown.load()) ||
        !fault.after.compare_exchange_strong(prefix, -1)) {
        return real_post(queue, first, bad);
    }
    fault.attempted.store(count);
    int rc = 0;
    if (last) {
        last->next = nullptr;
        rc = real_post(queue, first, bad);
        last->next = split;
    }
    fault.real_error.store(rc);
    if (rc == 0) {
        fault.accepted.store(prefix);
        *bad = fault.unknown.load() ? nullptr : split;
        rc = ENOMEM;
    }
    fault.hits.fetch_add(1, butil::memory_order_release);
    return rc;
}

int FaultedSrqSend(ibv_qp* qp, ibv_send_wr* wr, ibv_send_wr** bad) {
    if (qp->qp_num != g_send_fault_qpn.load() ||
        (wr->num_sge == 0) != g_send_fault_control.load()) {
        return g_real_srq_send(qp, wr, bad);
    }
    for (ibv_send_wr* current = wr; current; current = current->next) {
        if (current->num_sge != 0) {
            g_observed_send_wrs.fetch_add(1, butil::memory_order_relaxed);
        }
    }
    auto* pause = g_send_post_pause.exchange(nullptr);
    int rc = InjectSrqPostFailure(qp, wr, bad, g_send_post_fault, g_real_srq_send);
    if (pause) {
        pause->reached.store(true, butil::memory_order_release);
        while (!pause->resume.load(butil::memory_order_acquire)) {
            usleep(1000);
        }
    }
    return rc;
}

int ObservedSrqPoll(ibv_cq* cq, int count, ibv_wc* wc) {
    if (cq == g_paused_tx_cq.load(butil::memory_order_acquire)) {
        g_tx_poll_paused.store(true, butil::memory_order_release);
        return 0;
    }
    int n = g_real_srq_poll(cq, count, wc);
    for (int i = 0; i < n; ++i) {
        if (wc[i].status == IBV_WC_SUCCESS && wc[i].opcode == IBV_WC_SEND &&
            wc[i].qp_num == g_observe_tx_qpn.load()) {
            g_observed_tx.store(true, butil::memory_order_release);
        }
    }
    return n;
}

int FaultedSrqRecv(ibv_srq* srq, ibv_recv_wr* wr, ibv_recv_wr** bad) {
    return InjectSrqPostFailure(srq, wr, bad, g_recv_post_fault, g_real_srq_recv);
}
}  // namespace

class RdmaSrqPostTest : public RdmaSrqPoolTest {
protected:
    void SetUp() override {
        RdmaSrqPoolTest::SetUp();
        if (!_flags || HasFatalFailure()) {
            return;
        }
        g_send_post_fault.Arm(-1, false);
        g_recv_post_fault.Arm(-1, false);
        g_send_fault_qpn.store(0);
        g_send_fault_control.store(false);
        g_observed_send_wrs.store(0);
        g_send_post_pause.store(nullptr);
        g_observe_tx_qpn.store(0);
        g_observed_tx.store(false);
        g_paused_tx_cq.store(nullptr);
        g_tx_poll_paused.store(false);
        g_real_srq_send = rdma::IbvPostSrqSend;
        g_real_srq_recv = rdma::IbvPostSrqRecv;
        g_real_srq_poll = rdma::IbvPollSrqCq;
        rdma::IbvPostSrqSend = FaultedSrqSend;
        rdma::IbvPostSrqRecv = FaultedSrqRecv;
        rdma::IbvPollSrqCq = ObservedSrqPoll;
        _hooked = true;
    }

    void TearDown() override {
        if (_hooked) {
            g_send_post_fault.after.store(-1);
            g_recv_post_fault.after.store(-1);
            g_paused_tx_cq.store(nullptr);
        }
        RdmaSrqPoolTest::TearDown();
        if (_hooked) {
            rdma::IbvPostSrqSend = g_real_srq_send;
            rdma::IbvPostSrqRecv = g_real_srq_recv;
            rdma::IbvPollSrqCq = g_real_srq_poll;
        }
    }

    void CheckFault(const SrqPostFault& fault, int prefix) {
        EXPECT_EQ(1, fault.hits.load());
        EXPECT_EQ(0, fault.real_error.load());
        EXPECT_EQ(prefix, fault.accepted.load());
        if (fault.unknown.load()) {
            EXPECT_GE(fault.attempted.load(), prefix);
        } else {
            EXPECT_GT(fault.attempted.load(), prefix);
        }
    }

    void PauseTx(const std::shared_ptr<rdma::SrqConnection>& connection) {
        {
            rdma::SrqConnection::PostLock lock(connection.get());
            g_tx_poll_paused.store(false, butil::memory_order_relaxed);
            g_paused_tx_cq.store(connection->qp()->send_cq, butil::memory_order_release);
            connection->ScheduleProgressLocked(true);
        }
        ASSERT_TRUE(WaitUntil([] {
            return g_tx_poll_paused.load(butil::memory_order_acquire);
        }));
    }

    void ResumeTx(const std::shared_ptr<rdma::SrqConnection>& connection) {
        g_paused_tx_cq.store(nullptr, butil::memory_order_release);
        // CQ events may have been acknowledged while polling was paused. Kick
        // the owner to read CQEs already present.
        rdma::SrqConnection::PostLock lock(connection.get());
        connection->ScheduleProgressLocked(true);
    }

    void FailRefill(bool unknown) {
        Channel channel;
        const std::string group = unknown ? "recv_unknown" : "recv_prefix";
        ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, group));
        auto connection = Connection(channel);
        ASSERT_TRUE(connection);
        butil::IOBuf retained;
        butil::IOBuf expected;
        ASSERT_EQ(0, expected.resize(64 * 1024, 'r'));
        {
            Controller cntl;
            test::EchoRequest req;
            test::EchoResponse res;
            req.set_message("retain before refill fault");
            cntl.request_attachment().append(expected);
            ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, nullptr);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            retained = cntl.response_attachment();
            ASSERT_NE(0u, rdma::SrqReceivePool::RegionId(retained.fetch1()));
        }
        g_recv_post_fault.Arm(5, unknown);
        ASSERT_EQ(0, SendEchoRpcs(channel, 1, 256 * 1024));
        CheckFault(g_recv_post_fault, 5);
        ASSERT_TRUE(WaitUntil([&] { return connection->worker_retired(); }));
        ASSERT_TRUE(WaitUntil([] { return rdma::SrqLiveConnections() == 0; }));
        EXPECT_TRUE(retained.equals(expected));
        Channel recovered;
        ASSERT_NO_FATAL_FAILURE(InitChannel(&recovered, group + "_recovered"));
        ASSERT_TRUE(Connection(recovered));
        ASSERT_EQ(16, SendEchoRpcs(recovered, 16, 8192));
        EXPECT_TRUE(retained.equals(expected));
    }

    void FailGrant(bool unknown) {
        Channel channel;
        const std::string group = unknown ? "grant_unknown" : "grant_unposted";
        ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, group));
        auto peer = WaitForServerSocket(channel._server_id);
        ASSERT_NE(nullptr, peer);
        auto connection = SrqConnectionOf(peer);
        ASSERT_TRUE(connection);
        peer.reset();
        g_send_fault_qpn.store(ConnectionQpn(*connection));
        g_send_fault_control.store(true);
        // For unknown `bad_wr` the real zero-payload grant was already posted.
        // Its CQE must never outlive the reservation or an active TxRecord.
        const int prefix = unknown ? 1 : 0;
        g_send_post_fault.Arm(prefix, unknown);
        ASSERT_EQ(0, SendEchoRpcs(channel, 1, 256 * 1024));
        CheckFault(g_send_post_fault, prefix);
        ASSERT_TRUE(WaitUntil([] { return rdma::SrqLiveConnections() == 0; }));
        if (unknown) {
            // DestroyAll drops the connection count before releasing RX
            // buffers and publishing that the whole worker is retired.
            ASSERT_TRUE(WaitUntil([&] { return connection->worker_retired(); }));
        } else {
            EXPECT_FALSE(connection->worker_retired());
        }
        EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_tx_outstanding"));
        Channel recovered;
        ASSERT_NO_FATAL_FAILURE(InitChannel(&recovered, group + "_recovered"));
        ASSERT_TRUE(Connection(recovered));
        ASSERT_EQ(16, SendEchoRpcs(recovered, 16, 8192));
    }
    bool _hooked = false;
};

namespace {
struct SrqEventExitPause {
    // Fix the target worker and pause point while other workers continue
    // normally.
    std::shared_ptr<rdma::SrqConnection> connection;
    bool after_decision = false;
    // reached publishes the event-counter pointer and snapshot, allowing the
    // test thread to verify that Kick was coalesced.
    butil::atomic<uint64_t>* events = nullptr;
    // Valid while the paused owner retains its progress lock and pool reference.
    rdma::SrqReceivePool* pool{nullptr};
    uint64_t initial_events = 0;
    butil::atomic<bool> reached{false};
    butil::atomic<bool> resume{false};
    butil::atomic<bool> finished{false};
};
butil::atomic<SrqEventExitPause*> g_srq_event_exit_pause{nullptr};

void PauseSrqEventExit(rdma::SrqWorker* worker, bool after_decision,
                       butil::atomic<uint64_t>* events, rdma::SrqReceivePool* pool) {
    auto* pause = g_srq_event_exit_pause.load(butil::memory_order_acquire);
    if (!pause || pause->connection->_worker.get() != worker ||
        pause->after_decision != after_decision ||
        pause->reached.load(butil::memory_order_relaxed)) {
        return;
    }
    // The progress lock serializes hooks for this worker. Pause only the first
    // round reaching this point.
    pause->events = events;
    pause->pool = pool;
    pause->initial_events = events->load(butil::memory_order_acquire);
    pause->reached.store(true, butil::memory_order_release);
    while (!pause->resume.load(butil::memory_order_acquire)) {
        bthread_usleep(1000);
    }
    pause->finished.store(true, butil::memory_order_release);
}
}  // namespace

class RdmaSrqEventExitTest : public RdmaSrqPostTest {
protected:
    void TearDown() override {
        // Even an early assertion exit must release the progress lock before
        // Stop retries failure cleanup.
        _pause.resume.store(true, butil::memory_order_release);
        rdma::SrqWorkerStopHookForTest.store(nullptr, butil::memory_order_release);
        g_srq_event_exit_pause.store(nullptr, butil::memory_order_release);
        RdmaSrqPostTest::TearDown();
    }

    void CheckExternalFailure(bool after_decision) {
        Channel channel, other;
        ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "fail_at_event_exit"));
        ASSERT_NO_FATAL_FAILURE(InitChannel(&other, "beside_failed_event_exit"));
        auto connection = Connection(channel);
        auto neighbor = Connection(other);
        ASSERT_TRUE(connection);
        ASSERT_TRUE(neighbor);
        ASSERT_EQ(connection->_worker, neighbor->_worker);
        SocketUniquePtr socket, other_socket;
        ASSERT_EQ(0, Socket::Address(channel._server_id, &socket));
        ASSERT_EQ(0, Socket::Address(other._server_id, &other_socket));
        ASSERT_TRUE(WaitUntil([&] {
            return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0;
        }));
        _pause.connection = connection;
        _pause.after_decision = after_decision;
        g_srq_event_exit_pause.store(&_pause, butil::memory_order_release);
        rdma::SrqWorkerStopHookForTest.store(PauseSrqEventExit,
                                            butil::memory_order_release);
        {
            rdma::SrqConnection::PostLock lock(connection.get());
            connection->ScheduleProgressLocked(true);
        }
        ASSERT_TRUE(WaitUntil([&] {
            return _pause.reached.load(butil::memory_order_acquire);
        }));

        // The owner has completed its final ProgressLocked call. Trigger Fail
        // through the real Send path with an unrecognized `bad_wr`, without
        // submitting data to the NIC.
        g_send_fault_qpn.store(ConnectionQpn(*connection));
        g_send_post_fault.Arm(0, true);
        butil::IOBuf data;
        data.append("fail after progress");
        butil::IOBuf* from = &data;
        EXPECT_EQ(-1, connection->Send(&from, 1));
        EXPECT_EQ(ENOMEM, errno);
        CheckFault(g_send_post_fault, 0);
        ASSERT_TRUE(WaitUntil([&] {
            return _pause.events->load(butil::memory_order_acquire) >
                _pause.initial_events;
        }));
        EXPECT_EQ(1, SrqCounter(connection, "rdma_srq_worker_broken"));
        EXPECT_FALSE(connection->worker_retired());
        _pause.resume.store(true, butil::memory_order_release);

        // Without another request or Stop, the current owner or normal event
        // handoff must finish destruction and notify every connection outside
        // the lock, not only the connection whose Send failed.
        ASSERT_TRUE(WaitUntil([&] { return connection->worker_retired(); }));
        ASSERT_TRUE(WaitUntil([&] {
            return socket->Failed() && other_socket->Failed();
        }));
        EXPECT_TRUE(_pause.finished.load(butil::memory_order_acquire));
        EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_sq_outstanding"));
        EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_tx_outstanding"));
        EXPECT_TRUE(WaitUntil([] { return rdma::SrqLiveConnections() == 0; }));

        Channel recovered;
        ASSERT_NO_FATAL_FAILURE(InitChannel(&recovered, "after_event_exit_failure"));
        auto next = Connection(recovered);
        ASSERT_TRUE(next);
        EXPECT_NE(connection->_worker, next->_worker);
        EXPECT_EQ(16, SendEchoRpcs(recovered, 16, 8192));
    }

    void CheckStopNotifications(bool pause_event) {
        // These sockets have no TCP peer or RPC timeout to mask a lost SRQ
        // failure notification. Only deferred SetFailed can fail them.
        std::vector<SocketUniquePtr> sockets(8);
        std::vector<std::shared_ptr<rdma::SrqConnection>> connections;
        auto cleanup = butil::MakeScopeGuard([&] {
            _pause.resume.store(true, butil::memory_order_release);
            for (auto& socket : sockets) {
                if (socket != nullptr) {
                    socket->SetFailed();
                }
            }
        });
        for (auto& socket : sockets) {
            SocketOptions options;
            SocketId sid;
            ASSERT_EQ(0, Socket::Create(options, &sid));
            ASSERT_EQ(0, Socket::Address(sid, &socket));
            auto connection = rdma::SrqConnection::Create(
                socket.get(), BTHREAD_TAG_DEFAULT, rdma::FLAGS_rdma_srq_sq_size);
            ASSERT_NE(nullptr, connection);
            connections.push_back(std::move(connection));
            ASSERT_EQ(connections.front()->_worker, connections.back()->_worker);
        }

        if (pause_event) {
            _pause.connection = connections.front();
            _pause.after_decision = true;
            g_srq_event_exit_pause.store(&_pause, butil::memory_order_release);
            rdma::SrqWorkerStopHookForTest.store(PauseSrqEventExit,
                                                butil::memory_order_release);
            {
                rdma::SrqConnection::PostLock lock(_pause.connection.get());
                _pause.connection->ScheduleProgressLocked(true);
            }
            ASSERT_TRUE(WaitUntil([&] {
                return _pause.reached.load(butil::memory_order_acquire);
            }));
            // Queue a failure after the event owner's last progress check.
            // Stop will generate failures for the other connections while
            // both callers may finish their rounds outside the progress lock.
            _pause.connection->RequestClose(ERDMA);
            std::thread stopper([] { rdma::StopAllSrqWorkers(); });
            auto join = butil::MakeScopeGuard([&] {
                _pause.resume.store(true, butil::memory_order_release);
                stopper.join();
            });
            ASSERT_TRUE(WaitUntil([&] {
                return SrqCounter(_pause.connection, "rdma_srq_worker_broken") == 1;
            }));
            _pause.resume.store(true, butil::memory_order_release);
        } else {
            // No event task is needed. Stop must detach its own failure batch
            // even though it has already removed the event source.
            rdma::StopAllSrqWorkers();
        }

        ASSERT_TRUE(WaitUntil([&] {
            for (auto& socket : sockets) {
                if (!socket->Failed()) {
                    return false;
                }
            }
            return true;
        }));
        EXPECT_TRUE(connections.front()->worker_retired());
        EXPECT_EQ(0u, rdma::SrqLiveConnections());
    }

    SrqEventExitPause _pause;
};

TEST_F(RdmaSrqEventExitTest, returned_zero_copy_blocks_do_not_notify_full_srq) {
    // The initial region reaches the limit, so growth cannot notify this fd.
    rdma::FLAGS_rdma_srq_pool_blocks = 64;
    Channel channel;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "full_srq_return"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    butil::IOBuf held;
    {
        Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message("hold receive block until the owner is ready to sleep");
        ASSERT_EQ(0, cntl.request_attachment().resize(8192, 'r'));
        ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, nullptr);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        held.swap(cntl.response_attachment());
        ASSERT_NE(0u, rdma::SrqReceivePool::RegionId(held.fetch1()));
    }
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0 &&
            SrqCounter(connection, "rdma_srq_posted") == 32;
    }));
    BUTIL_SCOPE_EXIT { _pause.resume.store(true, butil::memory_order_release); };
    _pause.connection = connection;
    g_srq_event_exit_pause.store(&_pause, butil::memory_order_release);
    rdma::SrqWorkerStopHookForTest.store(PauseSrqEventExit, butil::memory_order_release);
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        connection->ScheduleProgressLocked(true);
    }
    ASSERT_TRUE(WaitUntil([&] {
        return _pause.reached.load(butil::memory_order_acquire);
    }));
    ASSERT_NE(nullptr, _pause.pool);
    EXPECT_FALSE(_pause.pool->_notify.load(butil::memory_order_acquire));
    held.clear();
    // The parser may release its last reference after the RPC callback returns.
    ASSERT_TRUE(WaitUntil([&] {
        return _pause.pool->_returned.load(butil::memory_order_acquire) != nullptr;
    }));
    uint64_t events;
    EXPECT_EQ(-1, read(_pause.pool->recycle_fd(), &events, sizeof(events)));
    EXPECT_EQ(EAGAIN, errno);
}

TEST_F(RdmaSrqEventExitTest, failure_after_progress_is_destroyed_before_exit) {
    ASSERT_NO_FATAL_FAILURE(CheckExternalFailure(false));
}

TEST_F(RdmaSrqEventExitTest, failure_after_stop_decision_is_not_lost) {
    ASSERT_NO_FATAL_FAILURE(CheckExternalFailure(true));
}

TEST_F(RdmaSrqEventExitTest, stop_delivers_notifications_without_event_owner) {
    ASSERT_NO_FATAL_FAILURE(CheckStopNotifications(false));
}

TEST_F(RdmaSrqEventExitTest, stop_and_event_finish_deliver_all_notifications) {
    ASSERT_NO_FATAL_FAILURE(CheckStopNotifications(true));
}

TEST_F(RdmaSrqPostTest, close_waits_for_tx_without_stopping_shared_worker) {
    Channel channel, other;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "close_waiting_for_tx"));
    ASSERT_NO_FATAL_FAILURE(InitChannel(&other, "beside_close_waiting_for_tx"));
    auto connection = Connection(channel);
    auto neighbor = Connection(other);
    ASSERT_TRUE(connection);
    ASSERT_TRUE(neighbor);
    ASSERT_EQ(connection->_worker, neighbor->_worker);
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0;
    }));
    ASSERT_NO_FATAL_FAILURE(PauseTx(connection));

    butil::IOBuf expected;
    butil::IOBuf retained;
    ASSERT_EQ(0, expected.resize(8192, 'c'));
    {
        Controller cntl;
        test::EchoRequest req;
        test::EchoResponse res;
        req.set_message("retain across ordinary QP close");
        cntl.request_attachment().append(expected);
        test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, nullptr);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        retained = cntl.response_attachment();
        ASSERT_NE(0u, rdma::SrqReceivePool::RegionId(retained.fetch1()));
    }
    ASSERT_GT(SrqCounter(connection, "rdma_srq_sq_outstanding"), 0);
    connection->Close();
    ASSERT_TRUE(WaitUntil([&] {
        return connection->_last_wqe.load(butil::memory_order_acquire);
    }));
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        // LAST_WQE arrived, but send references and shared-CQ reservations are
        // still outstanding. The QP must remain alive.
        ASSERT_NE(nullptr, connection->qp());
        EXPECT_EQ(IBV_QPS_ERR, connection->qp()->state);
    }
    // Waiting for close must not occupy the worker. Other connections on this
    // SRQ/CQ continue sending and receiving.
    EXPECT_EQ(1, SendEchoRpcs(other, 1, 64));
    EXPECT_TRUE(retained.equals(expected));
    ResumeTx(connection);
    ASSERT_TRUE(WaitUntil([&] {
        rdma::SrqConnection::PostLock lock(connection.get());
        return connection->qp() == nullptr;
    }));
    connection->Close();  // A destroyed QP must not reschedule progress.
    EXPECT_FALSE(connection->worker_retired());
    EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_sq_outstanding"));
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(neighbor, "rdma_srq_tx_outstanding") == 0 &&
            SrqCounter(neighbor, "rdma_srq_posted") == rdma::FLAGS_rdma_srq_depth;
    }));
    EXPECT_TRUE(retained.equals(expected));
    retained.clear();
    EXPECT_EQ(16, SendEchoRpcs(other, 16, 8192));
}

namespace {
struct SrqCloseObservation {
    // Do not modify the observer after installation. A retained connection
    // reference protects it during async callbacks.
    std::shared_ptr<rdma::SrqConnection> connection;
    ibv_cq* rx_cq = nullptr;
    uint32_t qpn = 0;
    // Pause after the actual ACK so destroy_qp waiting for acknowledgement
    // cannot hide premature destruction.
    butil::atomic<bool> last_acked{false};
    butil::atomic<bool> resume_last{false};
    butil::atomic<bool> ack_hook_finished{false};
    // Publish LAST_WQE after an RX poll reports empty but before the worker
    // checks close readiness.
    butil::atomic<bool> publish_last_on_empty{false};
    butil::atomic<bool> published_on_empty{false};
    butil::atomic<bool> fresh_rx_empty{false};
    butil::atomic<bool> destroyed_after_fresh_rx{false};
};
butil::atomic<SrqCloseObservation*> g_srq_close_observation{nullptr};
decltype(rdma::IbvAckAsyncEvent) g_close_real_ack = nullptr;
decltype(rdma::IbvPollSrqCq) g_close_real_poll = nullptr;
decltype(rdma::IbvDestroyQp) g_close_real_destroy = nullptr;

void ObservedCloseAck(ibv_async_event* event) {
    auto* observation = g_srq_close_observation.load(butil::memory_order_acquire);
    bool pause = observation && event->event_type == IBV_EVENT_QP_LAST_WQE_REACHED &&
        event->element.qp->qp_num == observation->qpn;
    g_close_real_ack(event);
    if (pause) {
        observation->last_acked.store(true, butil::memory_order_release);
        while (!observation->resume_last.load(butil::memory_order_acquire)) {
            usleep(1000);
        }
        observation->ack_hook_finished.store(true, butil::memory_order_release);
    }
}

int ObservedClosePoll(ibv_cq* cq, int count, ibv_wc* wc) {
    int n = g_close_real_poll(cq, count, wc);
    auto* observation = g_srq_close_observation.load(butil::memory_order_acquire);
    if (observation && cq == observation->rx_cq && n == 0) {
        if (observation->publish_last_on_empty.exchange(false)) {
            observation->resume_last.store(true, butil::memory_order_release);
            bool published = WaitUntil([&] {
                return observation->connection->_last_wqe.load(butil::memory_order_acquire);
            });
            observation->published_on_empty.store(published, butil::memory_order_release);
        } else if (observation->published_on_empty.load(butil::memory_order_acquire)) {
            observation->fresh_rx_empty.store(true, butil::memory_order_release);
        }
    }
    return n;
}

int ObservedCloseDestroy(ibv_qp* qp) {
    auto* observation = g_srq_close_observation.load(butil::memory_order_acquire);
    if (observation && qp->qp_num == observation->qpn) {
        observation->destroyed_after_fresh_rx.store(
            observation->fresh_rx_empty.load(butil::memory_order_acquire),
            butil::memory_order_release);
    }
    return g_close_real_destroy(qp);
}
}  // namespace

class RdmaSrqCloseTest : public RdmaSrqPoolTest {
protected:
    void SetUp() override {
        RdmaSrqPoolTest::SetUp();
        if (!_flags || HasFatalFailure()) {
            return;
        }
        g_close_real_ack = rdma::IbvAckAsyncEvent;
        g_close_real_poll = rdma::IbvPollSrqCq;
        g_close_real_destroy = rdma::IbvDestroyQp;
        rdma::IbvAckAsyncEvent = ObservedCloseAck;
        rdma::IbvPollSrqCq = ObservedClosePoll;
        rdma::IbvDestroyQp = ObservedCloseDestroy;
        _hooked = true;
    }

    void TearDown() override {
        // Release the async event thread before destroying the QP, including
        // early assertion exits.
        _observation.resume_last.store(true, butil::memory_order_release);
        RdmaSrqPoolTest::TearDown();
        if (_hooked) {
            if (_observation.last_acked.load(butil::memory_order_acquire)) {
                EXPECT_TRUE(WaitUntil([&] {
                    return _observation.ack_hook_finished.load(butil::memory_order_acquire);
                }));
            }
            g_srq_close_observation.store(nullptr, butil::memory_order_release);
            rdma::IbvAckAsyncEvent = g_close_real_ack;
            rdma::IbvPollSrqCq = g_close_real_poll;
            rdma::IbvDestroyQp = g_close_real_destroy;
        }
    }
    SrqCloseObservation _observation;
    bool _hooked = false;
};

TEST_F(RdmaSrqCloseTest, last_wqe_after_empty_requires_fresh_rx_poll) {
    Channel channel;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "last_wqe_after_empty"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0;
    }));
    _observation.connection = connection;
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        _observation.qpn = connection->qp()->qp_num;
        _observation.rx_cq = connection->qp()->recv_cq;
    }
    g_srq_close_observation.store(&_observation, butil::memory_order_release);
    connection->Close();
    ASSERT_TRUE(WaitUntil([&] {
        return _observation.last_acked.load(butil::memory_order_acquire);
    }));
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        ASSERT_NE(nullptr, connection->qp());
        EXPECT_FALSE(connection->_last_wqe.load(butil::memory_order_acquire));
        _observation.publish_last_on_empty.store(true, butil::memory_order_release);
        connection->ScheduleProgressLocked(true);
    }
    ASSERT_TRUE(WaitUntil([&] {
        rdma::SrqConnection::PostLock lock(connection.get());
        return connection->qp() == nullptr;
    }));
    EXPECT_TRUE(_observation.published_on_empty.load());
    EXPECT_TRUE(_observation.destroyed_after_fresh_rx.load());
    EXPECT_FALSE(connection->worker_retired());
    EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_tx_outstanding"));
}

TEST_F(RdmaSrqPoolTest, unactivated_connection_closes_without_last_wqe) {
    Channel other;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&other, "beside_unactivated_close"));
    auto neighbor = Connection(other);
    ASSERT_TRUE(neighbor);
    size_t live = rdma::SrqLiveConnections();
    SocketOptions options;
    SocketId sid;
    ASSERT_EQ(0, Socket::Create(options, &sid));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(sid, &socket));
    auto cleanup = butil::MakeScopeGuard([&] { socket->SetFailed(); });
    auto connection = rdma::SrqConnection::Create(
        socket.get(), BTHREAD_TAG_DEFAULT, rdma::FLAGS_rdma_srq_sq_size);
    ASSERT_TRUE(connection);
    ASSERT_EQ(neighbor->_worker, connection->_worker);
    EXPECT_EQ(rdma::QpActivationStage::NONE, connection->_activation_stage);
    connection->Close();
    connection->Close();
    ASSERT_TRUE(WaitUntil([&] {
        rdma::SrqConnection::PostLock lock(connection.get());
        return connection->qp() == nullptr;
    }));
    EXPECT_FALSE(connection->_last_wqe.load(butil::memory_order_acquire));
    ASSERT_TRUE(WaitUntil([&] { return rdma::SrqLiveConnections() == live; }));
    EXPECT_FALSE(connection->worker_retired());
    EXPECT_EQ(16, SendEchoRpcs(other, 16, 8192));
}

TEST_F(RdmaSrqPoolTest, late_failure_does_not_reopen_retired_worker) {
    Channel channel;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "late_worker_failure"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    std::function<void()> fail;
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        EXPECT_EQ(rdma::QpActivationStage::RTS, connection->_activation_stage);
        // Simulate an event that captured the worker before destruction but
        // invokes its callback after verbs resources are gone.
        auto* context = static_cast<rdma::RdmaAsyncEventContext*>(
            connection->qp()->send_cq->cq_context);
        fail = context->PrepareAsyncEvent(IBV_EVENT_CQ_ERR);
    }
    ASSERT_TRUE(fail);
    fail();
    ASSERT_TRUE(WaitUntil([&] { return connection->worker_retired(); }));
    for (int i = 0; i < 3; ++i) {
        fail();
        EXPECT_TRUE(connection->worker_retired());
        EXPECT_EQ(1, SrqCounter(connection, "rdma_srq_worker_broken"));
    }
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        EXPECT_EQ(nullptr, connection->qp());
        EXPECT_EQ(rdma::QpActivationStage::RTS, connection->_activation_stage);
    }
    // Close preserves activation history, but that history must not permit
    // further sends.
    butil::IOBuf data;
    data.append("closed connection");
    butil::IOBuf* from = &data;
    EXPECT_EQ(-1, connection->Send(&from, 1));
    EXPECT_EQ(ENOTCONN, errno);
    EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_tx_outstanding"));

    Channel recovered;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&recovered, "after_late_worker_failure"));
    auto next = Connection(recovered);
    ASSERT_TRUE(next);
    EXPECT_NE(connection->_worker, next->_worker);
    fail();
    EXPECT_TRUE(connection->worker_retired());
    EXPECT_EQ(16, SendEchoRpcs(recovered, 16, 8192));
}

TEST_F(RdmaSrqPostTest, early_completion_does_not_wait_for_post_return) {
    Channel channel, other;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "early_send_completion"));
    ASSERT_NO_FATAL_FAILURE(InitChannel(&other, "beside_early_send_completion"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0;
    }));
    uint64_t completed = connection->_sq_completed.load(butil::memory_order_acquire);
    size_t size = 64;
    std::unique_ptr<void, decltype(&free)> memory(malloc(size), free);
    ASSERT_TRUE(memory);
    memset(memory.get(), 'e', size);
    memcpy(memory.get(), "PRPC", 4);
    uint32_t lengths[] = {htonl(4 * 1024 * 1024), 0};
    memcpy(static_cast<char*>(memory.get()) + 4, lengths, sizeof(lengths));
    uint32_t lkey = rdma::RegisterMemoryForRdma(memory.get(), size);
    ASSERT_NE(0u, lkey);
    auto released = std::make_shared<butil::atomic<int>>(0);
    butil::IOBuf data;
    ASSERT_EQ(0, data.append_user_data_with_meta(
        memory.get(), size, [released](void* p) {
            rdma::DeregisterMemoryForRdma(p);
            free(p);
            released->fetch_add(1, butil::memory_order_release);
        }, lkey));
    memory.release();
    butil::atomic<ssize_t> result{-1};
    SrqPostPause pause;
    g_send_fault_qpn.store(ConnectionQpn(*connection));
    g_observe_tx_qpn.store(ConnectionQpn(*connection));
    g_send_post_pause.store(&pause);
    pause.submitter = std::thread([&] {
        butil::IOBuf* ptr = &data;
        result.store(connection->Send(&ptr, 1));
    });
    ASSERT_TRUE(WaitUntil([&] { return pause.reached.load(); }));
    // post_send still holds `_post_mutex` and has not returned. Real CQEs must
    // already reclaim slots and permit releasing zero-copy user memory without
    // requiring another send or close.
    ASSERT_TRUE(WaitUntil([&] {
        return connection->_sq_completed.load(butil::memory_order_acquire) ==
            completed + 1;
    }));
    EXPECT_TRUE(g_observed_tx.load());
    ASSERT_TRUE(WaitUntil([&] { return released->load() == 1; }));
    EXPECT_EQ(1, SendEchoRpcs(other, 1, 64));
    pause.resume.store(true, butil::memory_order_release);
    pause.submitter.join();
    EXPECT_EQ(static_cast<ssize_t>(size), result.load());
    EXPECT_TRUE(data.empty());
    EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_sq_outstanding"));
    EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_closing"));
    EXPECT_FALSE(connection->worker_retired());
}

TEST_F(RdmaSrqPostTest, grant_receipt_does_not_wait_for_post_return) {
    Channel channel, other;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "grant_during_post"));
    auto connection = Connection(channel);
    Socket* peer_socket = WaitForServerSocket();
    ASSERT_NE(nullptr, peer_socket);
    auto peer = SrqConnectionOf(peer_socket);
    ASSERT_TRUE(connection);
    ASSERT_TRUE(peer);
    ASSERT_NO_FATAL_FAILURE(InitChannel(&other, "beside_grant_during_post"));
    // Make the next real RPC response carry a grant to the client while it
    // still holds `_post_mutex`.
    ASSERT_TRUE(peer->_fc.Receive(rdma::SrqFlowControl::SOFT_REQUEST, 1));
    butil::atomic<int> result{0};
    SrqPostPause pause;
    g_send_fault_qpn.store(ConnectionQpn(*connection));
    g_send_post_pause.store(&pause);
    pause.submitter = std::thread([&] {
        result.store(SendEchoRpcs(channel, 1, 64));
    });
    ASSERT_TRUE(WaitUntil([&] { return pause.reached.load(); }));
    ASSERT_TRUE(WaitUntil([&] {
        return connection->_fc._received_events.load(butil::memory_order_acquire) &
            rdma::SrqFlowControl::GRANT;
    }));
    // Grant reception must not block the shared CQ owner. Another connection
    // can complete an RPC.
    EXPECT_EQ(1, SendEchoRpcs(other, 1, 64));
    pause.resume.store(true, butil::memory_order_release);
    pause.submitter.join();
    EXPECT_EQ(1, result.load());
    rdma::SrqConnection::PostLock lock(connection.get());
    // Charge this send before applying the grant. The final window must not
    // remain at capacity - 1.
    EXPECT_EQ(connection->_fc.capacity(), connection->_fc.window());
}

TEST_F(RdmaSrqPostTest, unlock_before_retry_registration_still_acquires) {
    SocketOptions options;
    SocketId sid;
    ASSERT_EQ(0, Socket::Create(options, &sid));
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(sid, &socket));
    BUTIL_SCOPE_EXIT { socket->SetFailed(); };
    // A new, unactivated connection has no CQEs or pending callbacks. The test
    // thread exclusively simulates its worker try_lock path.
    auto connection = rdma::SrqConnection::Create(
        socket.get(), BTHREAD_TAG_DEFAULT, rdma::FLAGS_rdma_srq_sq_size);
    ASSERT_TRUE(connection);
    BUTIL_SCOPE_EXIT { connection->Close(); };
    rdma::SrqPostLockRetryHookForTest.store(PausePostLockRetry,
                                          butil::memory_order_release);
    BUTIL_SCOPE_EXIT {
        rdma::SrqPostLockRetryHookForTest.store(nullptr, butil::memory_order_release);
    };
    butil::atomic<bool> acquired{false};
    SrqPostPause pause;
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        pause.submitter = std::thread([&] {
            g_post_lock_retry_pause = &pause;
            rdma::SrqConnection::PostLock retry(connection.get(), std::try_to_lock);
            acquired.store(retry.owns_lock(), butil::memory_order_release);
            g_post_lock_retry_pause = nullptr;
        });
        ASSERT_TRUE(WaitUntil([&] {
            return pause.reached.load(butil::memory_order_acquire);
        }));
        // The holder exits before notification registration, so it cannot be
        // expected to issue another Kick.
        EXPECT_FALSE(connection->_post_retry.load(butil::memory_order_acquire));
    }
    pause.resume.store(true, butil::memory_order_release);
    pause.submitter.join();
    EXPECT_TRUE(acquired.load(butil::memory_order_acquire));
    // Successful retry must cancel the notification so releasing the guard does
    // not spuriously wake itself while resources remain unavailable.
    EXPECT_FALSE(connection->_post_retry.load(butil::memory_order_acquire));
}

TEST_F(RdmaSrqPostTest, hard_request_during_post_is_tracked_and_granted) {
    Channel channel, other;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "hard_during_post"));
    auto connection = Connection(channel);
    Socket* peer_socket = WaitForServerSocket();
    ASSERT_NE(nullptr, peer_socket);
    auto peer = SrqConnectionOf(peer_socket);
    ASSERT_TRUE(connection);
    ASSERT_TRUE(peer);
    ASSERT_NO_FATAL_FAILURE(InitChannel(&other, "beside_hard_during_post"));
    ASSERT_EQ(connection->_worker, Connection(other)->_worker);
    {
        rdma::SrqConnection::PostLock lock(peer.get());
        // On an idle connection, leave only the hard-threshold credits so the
        // next response actually carries a hard request.
        ASSERT_TRUE(peer->_fc.Init(4));
        for (int i = 0; i < 3; ++i) {
            peer->_fc.DataPosted(peer->_fc.PrepareData());
        }
    }
    butil::atomic<int> result{0};
    PendingEcho neighbor;
    SrqPostPause pause;
    g_send_fault_qpn.store(ConnectionQpn(*connection));
    g_send_post_pause.store(&pause);
    pause.submitter = std::thread([&] {
        result.store(SendEchoRpcs(channel, 1, 64));
    });
    ASSERT_TRUE(WaitUntil([&] { return pause.reached.load(); }));
    ASSERT_TRUE(WaitUntil([&] {
        return (connection->_fc._received_events.load(butil::memory_order_acquire) &
                rdma::SrqFlowControl::HARD_REQUEST) &&
            connection->_progress_scheduled.load(butil::memory_order_acquire) &&
            connection->_post_retry.load(butil::memory_order_acquire);
    }));
    // A hard request schedules progress. While the paused sender still owns the
    // submit lock, another connection must complete a real RPC. Blocking the
    // owner on this lock would stall the entire shared worker.
    neighbor.Start(other, 64);
    bool independent = WaitUntil([&] {
        return neighbor.done.load(butil::memory_order_acquire);
    });
    pause.resume.store(true, butil::memory_order_release);
    pause.submitter.join();
    EXPECT_TRUE(independent);
    ASSERT_TRUE(WaitUntil([&] {
        return neighbor.done.load(butil::memory_order_acquire);
    }));
    EXPECT_FALSE(neighbor.cntl.Failed()) << neighbor.cntl.ErrorText();
    LOG(INFO) << "RPC beside contended hard request: "
              << neighbor.cntl.latency_us() << " us";
    EXPECT_EQ(1, result.load());
    // Send no new requests after unlocking. The unlock notification must wake
    // the owner to run the queued grant callback.
    ASSERT_TRUE(WaitUntil([&] { return peer->IsWritable(); }));
    ASSERT_TRUE(WaitUntil([&] {
        return !connection->_progress_scheduled.load(butil::memory_order_acquire);
    }));
    rdma::SrqConnection::PostLock lock(peer.get());
    EXPECT_EQ(4u, peer->_fc.window());
}

TEST_F(RdmaSrqPostTest, received_grant_wakes_zero_window_writer) {
    Channel channel;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "grant_wakes_writer"));
    auto connection = Connection(channel);
    Socket* peer_socket = WaitForServerSocket();
    ASSERT_NE(nullptr, peer_socket);
    auto peer = SrqConnectionOf(peer_socket);
    ASSERT_TRUE(connection);
    ASSERT_TRUE(peer);
    SocketUniquePtr socket;
    ASSERT_EQ(0, Socket::Address(channel._server_id, &socket));
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0;
    }));
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        connection->_fc.ApplyReceived();
        // Exhaust only FC credits, leaving SQ/CQ capacity free so a grant is
        // required to end this wait.
        while (connection->_fc.can_send()) {
            connection->_fc.DataPosted(connection->_fc.PrepareData());
        }
    }
    ASSERT_FALSE(connection->IsWritable());
    ASSERT_TRUE(peer->_fc.Receive(rdma::SrqFlowControl::SOFT_REQUEST, 1));
    butil::IOBuf data;
    uint32_t lengths[] = {htonl(4 * 1024 * 1024), 0};
    ASSERT_EQ(0, data.append("PRPC", 4));
    ASSERT_EQ(0, data.append(lengths, sizeof(lengths)));
    butil::atomic<bool> entered{false};
    butil::atomic<bool> finished{false};
    butil::atomic<bool> writable{false};
    std::thread waiter([&] {
        timespec deadline = butil::microseconds_from_now(WAIT_TIMEOUT_US);
        int64_t until = butil::monotonic_time_us() + WAIT_TIMEOUT_US;
        entered.store(true, butil::memory_order_release);
        while (!connection->IsWritable() && butil::monotonic_time_us() < until) {
            socket->_transport->WaitEpollOut(socket->_epollout_butex, false, deadline);
        }
        writable.store(connection->IsWritable(), butil::memory_order_release);
        finished.store(true, butil::memory_order_release);
    });
    // Avoid ASSERT after starting the thread so failure paths still join it.
    EXPECT_TRUE(WaitUntil([&] { return entered.load(butil::memory_order_acquire); }));
    butil::IOBuf* ptr = &data;
    EXPECT_EQ(12, peer->Send(&ptr, 1));
    EXPECT_TRUE(WaitUntil([&] {
        return finished.load(butil::memory_order_acquire);
    }));
    waiter.join();
    EXPECT_TRUE(writable.load(butil::memory_order_acquire));
}

TEST_F(RdmaSrqPostTest, partial_post_return_after_prefix_completed) {
    for (bool unknown : {false, true}) {
        SCOPED_TRACE(unknown ? "unknown bad_wr" : "known bad_wr");
        Channel channel;
        ASSERT_NO_FATAL_FAILURE(InitChannel(&channel,
            unknown ? "early_partial_unknown" : "early_partial_known"));
        auto connection = Connection(channel);
        ASSERT_TRUE(connection);
        {
            rdma::SrqConnection::PostLock lock(connection.get());
            connection->_remote_block_size = 1024;
        }
        ASSERT_TRUE(WaitUntil([&] {
            return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0;
        }));
        uint64_t completed = connection->_sq_completed.load(butil::memory_order_acquire);
        int64_t window = SrqCounter(connection, "rdma_srq_window");
        butil::IOBuf data;
        uint32_t lengths[] = {htonl(4 * 1024 * 1024), 0};
        ASSERT_EQ(0, data.append("PRPC", 4));
        ASSERT_EQ(0, data.append(lengths, sizeof(lengths)));
        ASSERT_EQ(0, data.resize(8 * 1024, 'f'));
        butil::atomic<ssize_t> result{0};
        SrqPostPause pause;
        g_send_fault_qpn.store(ConnectionQpn(*connection));
        g_send_post_fault.Arm(3, unknown);
        g_send_post_pause.store(&pause);
        pause.submitter = std::thread([&] {
            butil::IOBuf* ptr = &data;
            result.store(connection->Send(&ptr, 1));
        });
        ASSERT_TRUE(WaitUntil([&] { return pause.reached.load(); }));
        CheckFault(g_send_post_fault, 3);
        ASSERT_EQ(8, g_send_post_fault.attempted.load());
        // The accepted prefix completes before the sender learns that the
        // suffix failed. Rollback must not return prefix credits twice. With an
        // unrecognized `bad_wr`, retain the uncertain suffix until worker-wide
        // DMA shutdown.
        ASSERT_TRUE(WaitUntil([&] {
            return connection->_sq_completed.load(butil::memory_order_acquire) ==
                completed + 3;
        }));
        pause.resume.store(true, butil::memory_order_release);
        pause.submitter.join();
        EXPECT_EQ(-1, result.load());
        ASSERT_TRUE(WaitUntil([] { return rdma::SrqLiveConnections() == 0; }));
        if (unknown) {
            ASSERT_TRUE(WaitUntil([&] { return connection->worker_retired(); }));
        } else {
            EXPECT_FALSE(connection->worker_retired());
            EXPECT_EQ(window - 3, SrqCounter(connection, "rdma_srq_window"));
        }
        EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_sq_outstanding"));
        EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_tx_outstanding"));
        {
            rdma::SrqConnection::PostLock lock(connection.get());
            EXPECT_EQ(completed + (unknown ? 8 : 3), connection->_sq_head);
        }
        rdma::StopAllSrqWorkers();
        ASSERT_TRUE(WaitUntil([] { return rdma::SrqReceivePool::LivePools() == 0; }));
    }
}

TEST_F(RdmaSrqPostTest, shared_cq_budget_blocks_and_recovers) {
    rdma::FLAGS_rdma_srq_depth = 256;
    rdma::FLAGS_rdma_srq_pool_blocks = 4096;
    rdma::FLAGS_rdma_srq_tx_cq_size = 64;
    std::vector<std::unique_ptr<Channel>> channels;
    std::vector<std::shared_ptr<rdma::SrqConnection>> connections;
    channels.push_back(std::make_unique<Channel>());
    ASSERT_NO_FATAL_FAILURE(InitChannel(channels.back().get(), "budget_paused"));
    connections.push_back(Connection(*channels.back()));
    ASSERT_TRUE(connections[0]);
    int64_t capacity = SrqCounter(connections[0], "rdma_srq_tx_capacity");
    int64_t limit = capacity - rdma::FLAGS_rdma_srq_control_reserve;
    ASSERT_GT(limit, 0);
    // Four WRs per connection avoid the W=16 credit thresholds and local SQ
    // limit. With TX CQ reclamation paused, only shared data-CQ capacity can
    // stop these sends.
    size_t peers = 1 + (limit + 3) / 4;
    for (size_t i = 1; i < peers; ++i) {
        channels.push_back(std::make_unique<Channel>());
        ASSERT_NO_FATAL_FAILURE(InitChannel(
            channels.back().get(), "budget_peer_" + std::to_string(i)));
        connections.push_back(Connection(*channels.back()));
        ASSERT_TRUE(connections.back());
    }
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connections[0], "rdma_srq_tx_outstanding") == 0;
    }));
    butil::IOBuf raw;
    uint32_t lengths[] = {htonl(4 * 1024 * 1024), 0};
    ASSERT_EQ(0, raw.append("PRPC", 4));
    ASSERT_EQ(0, raw.append(lengths, sizeof(lengths)));
    ASSERT_EQ(0, raw.resize(64, 'b'));
    ASSERT_NO_FATAL_FAILURE(PauseTx(connections[0]));
    int64_t posted = 0;
    std::shared_ptr<rdma::SrqConnection> blocked;
    butil::IOBuf pending;
    for (size_t i = 0; i < connections.size() && !blocked; ++i) {
        for (int n = 0; n < 4; ++n) {
            butil::IOBuf data = raw;
            butil::IOBuf* ptr = &data;
            ssize_t bytes = connections[i]->Send(&ptr, 1);
            if (bytes < 0) {
                ASSERT_EQ(EAGAIN, errno);
                ASSERT_EQ(raw.size(), data.size());
                blocked = connections[i];
                pending.swap(data);
                break;
            }
            ASSERT_EQ(static_cast<ssize_t>(raw.size()), bytes);
            ++posted;
        }
    }
    ASSERT_TRUE(blocked);
    EXPECT_EQ(limit, posted);
    EXPECT_EQ(limit, SrqCounter(blocked, "rdma_srq_tx_outstanding"));
    EXPECT_LT(SrqCounter(blocked, "rdma_srq_sq_outstanding"), 5);
    EXPECT_GT(SrqCounter(blocked, "rdma_srq_window"), 8);
    ResumeTx(blocked);
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(blocked, "rdma_srq_tx_outstanding") == 0;
    }));
    ASSERT_TRUE(blocked->IsWritable());
    butil::IOBuf* ptr = &pending;
    ASSERT_EQ(static_cast<ssize_t>(raw.size()), blocked->Send(&ptr, 1));
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(blocked, "rdma_srq_tx_outstanding") == 0;
    }));
    for (auto& connection : connections) {
        EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_closing"));
        EXPECT_FALSE(connection->worker_retired());
    }
    Channel recovered;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&recovered, "after_cq_budget_recovery"));
    EXPECT_EQ(16, SendEchoRpcs(recovered, 16, 8192));
}

TEST_F(RdmaSrqPostTest, send_spans_verbs_batches_until_sq_is_full) {
    rdma::FLAGS_rdma_srq_window = 512;
    rdma::FLAGS_rdma_srq_sq_size = 128;
    rdma::FLAGS_rdma_srq_tx_cq_size = 256;
    // This case retains 256 incomplete message fragments. Capacity must also
    // accommodate subsequent normal RPCs.
    rdma::FLAGS_rdma_srq_pool_blocks = 1024;
    Channel channel;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "send_sq_capacity"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        connection->_remote_block_size = 1024;
    }
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connection, "rdma_srq_tx_outstanding") == 0;
    }));
    g_send_fault_qpn.store(ConnectionQpn(*connection));
    // An incomplete message permits sending multiple raw batches without
    // parsing the test bytes as new RPCs.
    butil::IOBuf data;
    uint32_t lengths[] = {htonl(4 * 1024 * 1024), 0};
    ASSERT_EQ(0, data.append("PRPC", 4));
    ASSERT_EQ(0, data.append(lengths, sizeof(lengths)));
    ASSERT_EQ(0, data.resize(256 * 1024, 'b'));
    butil::IOBuf expected = data;
    butil::IOBuf* ptr = &data;
    uint64_t before = g_observed_send_wrs.load();
    // The 128-entry SQ reserves two control slots. One Send spans multiple
    // 32-WR verbs batches. Pause concurrent reclamation so it returns a
    // successful prefix only when usable data-SQ capacity is exhausted.
    ASSERT_NO_FATAL_FAILURE(PauseTx(connection));
    ssize_t bytes = connection->Send(&ptr, 1);
    ASSERT_EQ(126 * 1024, bytes);
    EXPECT_EQ(126u, g_observed_send_wrs.load() - before);
    expected.pop_front(bytes);
    EXPECT_TRUE(data.equals(expected));
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        ASSERT_TRUE(connection->_fc.Receive(rdma::SrqFlowControl::HARD_REQUEST, 1));
        connection->ScheduleProgressLocked(true);
    }
    ASSERT_TRUE(WaitUntil([&] {
        return SrqCounter(connection, "rdma_srq_sq_outstanding") == 127;
    }));
    // A pure grant already occupies the first control slot. Batched
    // data-capacity calculation must neither underflow nor consume the last
    // control slot, and must leave unsubmitted caller input untouched.
    EXPECT_EQ(-1, connection->Send(&ptr, 1));
    EXPECT_EQ(EAGAIN, errno);
    EXPECT_TRUE(data.equals(expected));
    ResumeTx(connection);
    ASSERT_TRUE(WaitUntil([&] {
        if (data.empty()) {
            return true;
        }
        ssize_t n = connection->Send(&ptr, 1);
        EXPECT_TRUE(n > 0 || (n < 0 && errno == EAGAIN));
        return data.empty();
    }));
    EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_closing"));
    Channel other;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&other, "after_send_sq_capacity"));
    EXPECT_EQ(4, SendEchoRpcs(other, 4, 64 * 1024));
}

TEST_F(RdmaSrqPostTest, preparation_failure_rolls_back_staged_wr) {
    Channel channel;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "prepare_failure"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    const int64_t window = SrqCounter(connection, "rdma_srq_window");
    Controller cntl;
    // Several valid WRs fit in the same batch before an unregistered suffix
    // fails SGE conversion. None of that batch has been posted to the NIC.
    ASSERT_EQ(0, cntl.request_attachment().resize(64 * 1024, 'v'));
    void* memory = malloc(4096);
    ASSERT_NE(nullptr, memory);
    ASSERT_EQ(0u, rdma::GetLKey(memory));
    ASSERT_EQ(0, cntl.request_attachment().append_user_data(memory, 4096, nullptr));
    test::EchoRequest req;
    test::EchoResponse res;
    req.set_message("reject unregistered suffix");
    ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, nullptr);
    EXPECT_EQ(ERDMAMEM, cntl.ErrorCode()) << cntl.ErrorText();
    ASSERT_TRUE(WaitUntil([] { return rdma::SrqLiveConnections() == 0; }));
    EXPECT_EQ(window, SrqCounter(connection, "rdma_srq_window"));
    EXPECT_EQ(0, SrqCounter(connection, "rdma_srq_tx_outstanding"));
    EXPECT_FALSE(connection->worker_retired());
    Channel recovered;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&recovered, "prepare_failure_recovered"));
    ASSERT_TRUE(Connection(recovered));
    ASSERT_EQ(16, SendEchoRpcs(recovered, 16, 8192));
}

TEST_F(RdmaSrqPostTest, initial_receive_partial_failure_falls_back) {
    for (int prefix : {0, 7, 31}) {
        g_recv_post_fault.Arm(prefix, false);
        Channel channel;
        ASSERT_NO_FATAL_FAILURE(InitChannel(
            &channel, "initial_recv_fault_" + std::to_string(prefix)));
        CheckFault(g_recv_post_fault, prefix);
        ASSERT_FALSE(Connection(channel));
        rdma::StopAllSrqWorkers();
        EXPECT_EQ(0u, rdma::SrqLiveConnections());
        EXPECT_EQ(0u, rdma::SrqReceivePool::RegisteredBytes());
    }
    Channel recovered;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&recovered, "initial_recv_recovered"));
    ASSERT_TRUE(Connection(recovered));
}

TEST_F(RdmaSrqPostTest, receive_refill_failure_preserves_held_data) {
    for (bool unknown : {false, true}) {
        SCOPED_TRACE(unknown);
        ASSERT_NO_FATAL_FAILURE(FailRefill(unknown));
    }
}

TEST_F(RdmaSrqPostTest, pure_grant_post_failure_accounts_for_possible_dma) {
    for (bool unknown : {false, true}) {
        SCOPED_TRACE(unknown);
        ASSERT_NO_FATAL_FAILURE(FailGrant(unknown));
    }
}

namespace {
enum SrqResourceFault : uint32_t {
    FAIL_QP_DESTROY = 1,
    FAIL_SRQ_DESTROY = 2,
    FAIL_QP_RTR = 4,
    FAIL_QP_RTS = 8,
};
butil::atomic<uint32_t> g_srq_resource_faults{0};
butil::atomic<int> g_srq_resource_failures{0};
struct SrqResourceFunctions {
    decltype(rdma::IbvModifyQp) modify_qp;
    decltype(rdma::IbvDestroyQp) destroy_qp;
    decltype(rdma::IbvDestroySrq) destroy_srq;
} g_real_srq_resources;

bool FailSrqResource(uint32_t operation) {
    if (!(g_srq_resource_faults.load() & operation)) {
        return false;
    }
    g_srq_resource_failures.fetch_add(1);
    errno = EBUSY;
    return true;
}

int FaultModifyQp(ibv_qp* qp, ibv_qp_attr* attr, ibv_qp_attr_mask mask) {
    if ((mask & IBV_QP_STATE) &&
        ((attr->qp_state == IBV_QPS_RTR && FailSrqResource(FAIL_QP_RTR)) ||
         (attr->qp_state == IBV_QPS_RTS && FailSrqResource(FAIL_QP_RTS)))) {
        return EBUSY;
    }
    return g_real_srq_resources.modify_qp(qp, attr, mask);
}

int FaultDestroyQp(ibv_qp* qp) {
    return FailSrqResource(FAIL_QP_DESTROY) ? EBUSY :
        g_real_srq_resources.destroy_qp(qp);
}

int FaultDestroySrq(ibv_srq* srq) {
    return FailSrqResource(FAIL_SRQ_DESTROY) ? EBUSY :
        g_real_srq_resources.destroy_srq(srq);
}

}  // namespace

class RdmaSrqResourceTest : public RdmaSrqPoolTest {
protected:
    void SetUp() override {
        RdmaSrqPoolTest::SetUp();
        if (!_flags || HasFatalFailure()) {
            return;
        }
        g_srq_resource_faults.store(0);
        g_srq_resource_failures.store(0);
        g_real_srq_resources = {
            rdma::IbvModifyQp, rdma::IbvDestroyQp, rdma::IbvDestroySrq};
        rdma::IbvModifyQp = FaultModifyQp;
        rdma::IbvDestroyQp = FaultDestroyQp;
        rdma::IbvDestroySrq = FaultDestroySrq;
        _hooked = true;
    }

    void TearDown() override {
        g_srq_resource_faults.store(0);
        RdmaSrqPoolTest::TearDown();
        if (_hooked) {
            EXPECT_EQ(0u, rdma::SrqReceivePool::RetryDeregistrations());
            EXPECT_EQ(0u, rdma::SrqReceivePool::RegisteredBytes());
            rdma::IbvModifyQp = g_real_srq_resources.modify_qp;
            rdma::IbvDestroyQp = g_real_srq_resources.destroy_qp;
            rdma::IbvDestroySrq = g_real_srq_resources.destroy_srq;
        }
    }

    void CheckActivationFailure(uint32_t fault, rdma::QpActivationStage stage) {
        Channel other;
        ASSERT_NO_FATAL_FAILURE(InitChannel(&other, "beside_activation_failure"));
        auto neighbor = Connection(other);
        ASSERT_TRUE(neighbor);
        size_t live = rdma::SrqLiveConnections();
        SocketOptions options;
        SocketId sid;
        ASSERT_EQ(0, Socket::Create(options, &sid));
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(sid, &socket));
        auto cleanup = butil::MakeScopeGuard([&] { socket->SetFailed(); });
        auto connection = rdma::SrqConnection::Create(
            socket.get(), BTHREAD_TAG_DEFAULT, rdma::FLAGS_rdma_srq_sq_size);
        ASSERT_TRUE(connection);
        ASSERT_EQ(neighbor->_worker, connection->_worker);
        rdma::ParsedHello peer = {};
        peer.block_size = rdma::GetRdmaBlockSize();
        peer.fc_window = rdma::FLAGS_rdma_srq_window;
        peer.gid = rdma::GetRdmaGid();
        peer.lid = rdma::GetRdmaLid();
        peer.qp_num = ConnectionQpn(*neighbor);
        butil::optional<ibv_ece> outgoing_ece;
        g_srq_resource_faults.store(fault);
        EXPECT_EQ(-1, connection->Activate(peer, false, &outgoing_ece));
        EXPECT_EQ(EBUSY, errno);
        EXPECT_EQ(1, g_srq_resource_failures.load());
        g_srq_resource_faults.store(0);
        {
            rdma::SrqConnection::PostLock lock(connection.get());
            EXPECT_EQ(stage, connection->_activation_stage);
        }
        EXPECT_FALSE(connection->IsWritable());
        butil::IOBuf data;
        data.append("not activated");
        butil::IOBuf* from = &data;
        EXPECT_EQ(-1, connection->Send(&from, 1));
        EXPECT_EQ(ENOTCONN, errno);

        // If RTR succeeds but RTS fails, transition to ERR and wait for
        // LAST_WQE before reclaiming the QP. If RTR itself fails, destroy
        // directly rather than wait for an event that will never arrive.
        connection->Close();
        ASSERT_TRUE(WaitUntil([&] {
            rdma::SrqConnection::PostLock lock(connection.get());
            return connection->qp() == nullptr;
        }));
        EXPECT_EQ(stage != rdma::QpActivationStage::NONE,
                  connection->_last_wqe.load(butil::memory_order_acquire));
        {
            rdma::SrqConnection::PostLock lock(connection.get());
            EXPECT_EQ(stage, connection->_activation_stage);
        }
        EXPECT_FALSE(connection->worker_retired());
        EXPECT_TRUE(WaitUntil([&] { return rdma::SrqLiveConnections() == live; }));
        EXPECT_EQ(16, SendEchoRpcs(other, 16, 8192));
    }

    bool _hooked = false;
};

TEST_F(RdmaSrqResourceTest, rtr_failure_closes_without_last_wqe) {
    ASSERT_NO_FATAL_FAILURE(CheckActivationFailure(
        FAIL_QP_RTR, rdma::QpActivationStage::NONE));
}

TEST_F(RdmaSrqResourceTest, rts_failure_preserves_receive_retirement) {
    ASSERT_NO_FATAL_FAILURE(CheckActivationFailure(
        FAIL_QP_RTS, rdma::QpActivationStage::RTR));
}

TEST_F(RdmaSrqResourceTest, connection_close_failure_retains_worker_until_retry) {
    Channel channel;
    ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "ordinary_close_destroy_failure"));
    auto connection = Connection(channel);
    ASSERT_TRUE(connection);
    g_srq_resource_faults.store(FAIL_QP_DESTROY);
    connection->Close();
    ASSERT_TRUE(WaitUntil([] { return g_srq_resource_failures.load() != 0; }));
    EXPECT_FALSE(connection->worker_retired());
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        EXPECT_NE(nullptr, connection->qp());
    }
    EXPECT_GT(rdma::SrqReceivePool::RegisteredBytes(), 0u);
    // The local retirement list is gone, but the QPN table still retains the
    // connection and DMA references. Explicit Stop can retry.
    g_srq_resource_faults.store(0);
    rdma::StopAllSrqWorkers();
    EXPECT_TRUE(connection->worker_retired());
    EXPECT_EQ(0u, rdma::SrqLiveConnections());
    EXPECT_EQ(0u, rdma::SrqReceivePool::RegisteredBytes());
}

TEST_F(RdmaSrqResourceTest, failed_destruction_retains_memory_until_retry) {
    for (uint32_t fault : {FAIL_QP_DESTROY, FAIL_SRQ_DESTROY}) {
        Channel channel;
        ASSERT_NO_FATAL_FAILURE(InitChannel(&channel, "destroy_fault_" + std::to_string(fault)));
        auto connection = Connection(channel);
        ASSERT_TRUE(connection);
        butil::IOBuf expected;
        butil::IOBuf retained;
        ASSERT_EQ(0, expected.resize(64 * 1024, 'd'));
        {
            Controller cntl;
            test::EchoRequest req;
            test::EchoResponse res;
            req.set_message("hold across failed destruction");
            cntl.request_attachment().append(expected);
            ::test::EchoService::Stub(&channel).Echo(&cntl, &req, &res, nullptr);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            retained = cntl.response_attachment();
            ASSERT_NE(0u, rdma::SrqReceivePool::RegionId(retained.fetch1()));
        }
        const size_t bytes = rdma::SrqReceivePool::RegisteredBytes();
        g_srq_resource_failures.store(0);
        g_srq_resource_faults.store(fault);
        rdma::StopAllSrqWorkers();
        EXPECT_GT(g_srq_resource_failures.load(), 0);
        EXPECT_FALSE(connection->worker_retired());
        EXPECT_EQ(bytes, rdma::SrqReceivePool::RegisteredBytes());
        EXPECT_TRUE(retained.equals(expected));
        // Retrying shutdown after the provider recovers drains quarantine.
        g_srq_resource_faults.store(0);
        rdma::StopAllSrqWorkers();
        EXPECT_TRUE(connection->worker_retired());
        EXPECT_EQ(0u, rdma::SrqLiveConnections());
        EXPECT_EQ(bytes, rdma::SrqReceivePool::RegisteredBytes());
        EXPECT_TRUE(retained.equals(expected));
        retained.clear();
        ASSERT_TRUE(WaitUntil([] { return rdma::SrqReceivePool::RegisteredBytes() == 0; }));
    }
}

#endif  // BRPC_WITH_RDMA

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
