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
#if BRPC_WITH_RDMA
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <gflags/gflags.h>
#include "bthread/bthread.h"
#include "bthread/id.h"
#include "butil/memory/scope_guard.h"
#include "butil/time.h"
#include "brpc/socket.h"
#include "brpc/rdma/srq_receive_pool.h"
#include "brpc/rdma/srq_worker.h"

namespace brpc {
namespace rdma {
DECLARE_bool(rdma_recv_zerocopy);
extern ibv_mr* (*IbvRegMr)(ibv_pd*, void*, size_t, int);
extern int (*IbvDeregMr)(ibv_mr*);
extern ibv_qp* (*IbvCreateQp)(ibv_pd*, ibv_qp_init_attr*);
extern int (*IbvDestroyQp)(ibv_qp*);
extern int (*IbvDestroyCq)(ibv_cq*);
extern int (*IbvDestroySrq)(ibv_srq*);
extern int (*IbvPostSrqRecv)(ibv_srq*, ibv_recv_wr*, ibv_recv_wr**);
extern int (*IbvPollSrqCq)(ibv_cq*, int, ibv_wc*);
extern int (*IbvReqNotifySrqCq)(ibv_cq*, int);
extern butil::atomic<int (*)(bool, bthread_t*, const bthread_attr_t*,
                            void* (*)(void*), void*)> SrqStartTaskHookForTest;
extern std::shared_ptr<SrqWorker> CreateSrqWorkerForTest();
extern bool InitializeSrqWorkerForTest(const std::shared_ptr<SrqWorker>&,
    ibv_cq*, ibv_cq*, ibv_srq*, SrqReceivePool*, uint32_t);
extern bool AddSrqConnectionForTest(const std::shared_ptr<SrqWorker>&,
                                  const std::shared_ptr<SrqConnection>&, uint32_t);
extern uint32_t ReserveSrqTxForTest(const std::shared_ptr<SrqWorker>&, uint32_t);
extern bool SrqWorkerIdleForTest(const SrqWorker*);
extern void ScheduleSrqWorkerForTest(const std::shared_ptr<SrqWorker>&,
                                   std::function<void()>, bool);
extern void WakeSrqWorkerForTest(const std::shared_ptr<SrqWorker>&);
extern void StopSrqWorkerForTest(const std::shared_ptr<SrqWorker>&);
}  // namespace rdma
}  // namespace brpc

namespace {
using namespace brpc;

bool WaitFor(const std::function<bool()>& predicate) {
    int64_t deadline = butil::gettimeofday_us() + 5000000;
    while (!predicate()) {
        if (butil::gettimeofday_us() >= deadline) {
            return false;
        }
        usleep(1000);
    }
    return true;
}

struct FakeVerbs {
    struct Receive {
        uint64_t id;
        void* data;
    };
    std::mutex mutex;
    ibv_cq tx{};
    ibv_cq rx{};
    ibv_srq srq{};
    std::deque<ibv_wc> sends;
    std::deque<ibv_wc> receives;
    std::deque<Receive> posted;
    bool armed[2]{false, false};
    unsigned arms[2]{0, 0};
    unsigned polls[2]{0, 0};
    unsigned completions[2]{0, 0};
    uint32_t next_qpn{1};
    uint32_t receiving_qpn{0};
    uint32_t closing_qpn{0};
    unsigned drain_after{0};
    // Negative means continuous arrivals until the refill budget is spent.
    int short_after{-1};
    std::vector<int> drain_counts;
    std::vector<size_t> drain_depths;
    bool destroyed{false};

    void ReceiveOne() {
        CHECK(!posted.empty());
        auto slot = posted.front();
        posted.pop_front();
        *static_cast<char*>(slot.data) = 'x';
        ibv_wc wc{};
        wc.wr_id = slot.id;
        wc.qp_num = receiving_qpn;
        wc.status = IBV_WC_SUCCESS;
        wc.opcode = IBV_WC_RECV;
        wc.wc_flags = IBV_WC_WITH_IMM;
        wc.byte_len = 1;
        receives.push_back(wc);
    }
};

FakeVerbs* g_verbs{nullptr};
void (*g_previous_deallocate)(void*){nullptr};

ibv_mr* Register(ibv_pd*, void* address, size_t size, int) {
    auto* mr = new ibv_mr();
    mr->addr = address;
    mr->length = size;
    mr->lkey = 1;
    return mr;
}

int Deregister(ibv_mr* mr) {
    delete mr;
    return 0;
}

void Deallocate(void* memory) {
    if (!rdma::SrqReceivePool::Recycle(memory)) {
        g_previous_deallocate(memory);
    }
}

ibv_qp* CreateQp(ibv_pd*, ibv_qp_init_attr* attr) {
    auto* qp = new ibv_qp();
    qp->qp_num = g_verbs->next_qpn++;
    qp->send_cq = attr->send_cq;
    qp->recv_cq = attr->recv_cq;
    qp->srq = attr->srq;
    return qp;
}

int DestroyQp(ibv_qp* qp) {
    std::lock_guard<std::mutex> lock(g_verbs->mutex);
    if (qp->qp_num == g_verbs->closing_qpn) {
        g_verbs->destroyed = true;
    }
    delete qp;
    return 0;
}

int PostReceive(ibv_srq*, ibv_recv_wr* wr, ibv_recv_wr**) {
    std::lock_guard<std::mutex> lock(g_verbs->mutex);
    for (; wr != nullptr; wr = wr->next) {
        g_verbs->posted.push_back({wr->wr_id,
            reinterpret_cast<void*>(wr->sg_list[0].addr)});
    }
    return 0;
}

int Arm(ibv_cq* cq, int) {
    std::lock_guard<std::mutex> lock(g_verbs->mutex);
    int index = cq == &g_verbs->rx ? 1 : 0;
    ++g_verbs->arms[index];
    g_verbs->armed[index] = true;
    return 0;
}

int Poll(ibv_cq* cq, int count, ibv_wc* wc) {
    std::lock_guard<std::mutex> lock(g_verbs->mutex);
    bool receive = cq == &g_verbs->rx;
    int index = receive ? 1 : 0;
    ++g_verbs->polls[index];
    bool draining = receive && g_verbs->closing_qpn != 0 && !g_verbs->destroyed &&
        g_verbs->polls[index] > g_verbs->drain_after;
    if (draining) {
        g_verbs->drain_depths.push_back(g_verbs->posted.size());
        int arrivals = count;
        if (g_verbs->short_after >= 0 &&
            g_verbs->drain_counts.size() >= static_cast<size_t>(g_verbs->short_after)) {
            arrivals = 7;
        }
        // A missing production budget must fail the expected batch sequence,
        // not leave teardown waiting forever for an endlessly busy owner.
        if (g_verbs->drain_counts.size() >= 8) {
            arrivals = 0;
        }
        arrivals = std::min<int>(arrivals, g_verbs->posted.size());
        for (int i = 0; i < arrivals; ++i) {
            g_verbs->ReceiveOne();
        }
    }
    auto& ready = receive ? g_verbs->receives : g_verbs->sends;
    int n = 0;
    while (n < count && !ready.empty()) {
        wc[n++] = ready.front();
        ready.pop_front();
    }
    g_verbs->completions[index] += n;
    if (draining) {
        g_verbs->drain_counts.push_back(n);
    }
    return n;
}

struct PublishPause {
    butil::atomic<bool> entered{false};
    butil::atomic<bool> resume{false};
};
thread_local PublishPause* g_publish_pause{nullptr};

void PausePublication(const void*) {
    auto* pause = g_publish_pause;
    if (pause != nullptr) {
        pause->entered.store(true, butil::memory_order_release);
        while (!pause->resume.load(butil::memory_order_acquire)) {
            usleep(1000);
        }
    }
}

butil::atomic<unsigned> g_task_start_failures{0};
thread_local rdma::SrqConnection* g_delivery_to_close{nullptr};

int FailTaskStart(bool, bthread_t*, const bthread_attr_t*, void* (*)(void*), void*) {
    g_task_start_failures.fetch_add(1, butil::memory_order_relaxed);
    if (g_delivery_to_close != nullptr) {
        // Close after the prefetched input and Socket reference have moved
        // into the task. Inline consumption must discard this input once.
        g_delivery_to_close->_closing.store(true, butil::memory_order_release);
    }
    return ENOMEM;
}

struct NotificationBarrier {
    butil::atomic<int> entered{0};
    butil::atomic<int> finished{0};
    butil::atomic<bool> resume{false};

    void ArriveAndWait() {
        entered.fetch_add(1, butil::memory_order_acq_rel);
        while (entered.load(butil::memory_order_acquire) != 2 &&
               !resume.load(butil::memory_order_acquire)) {
            bthread_usleep(1000);
        }
        finished.fetch_add(1, butil::memory_order_release);
    }
};

void NotifyOnFailure(Socket* socket, std::function<void()> callback) {
    auto task = std::make_unique<std::function<void()>>(std::move(callback));
    bthread_id_t id;
    ASSERT_EQ(0, bthread_id_create(&id, task.get(),
        [](bthread_id_t id, void* arg, int) {
            std::unique_ptr<std::function<void()>> callback(
                static_cast<std::function<void()>*>(arg));
            (*callback)();
            return bthread_id_unlock_and_destroy(id);
        }));
    task.release();
    socket->NotifyOnFailed(id);
}

// Run the actual worker with synthetic completions and registered-memory
// handles. No RDMA device, real QP, server or hardware-test flag is required.
class RdmaSrqProgressTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_verbs = &_verbs;
        _reg = rdma::IbvRegMr;
        _dereg = rdma::IbvDeregMr;
        _create_qp = rdma::IbvCreateQp;
        _destroy_qp = rdma::IbvDestroyQp;
        _destroy_cq = rdma::IbvDestroyCq;
        _destroy_srq = rdma::IbvDestroySrq;
        _post = rdma::IbvPostSrqRecv;
        _poll = rdma::IbvPollSrqCq;
        _arm = rdma::IbvReqNotifySrqCq;
        rdma::IbvRegMr = Register;
        rdma::IbvDeregMr = Deregister;
        rdma::IbvCreateQp = CreateQp;
        rdma::IbvDestroyQp = DestroyQp;
        rdma::IbvDestroyCq = [](ibv_cq*) { return 0; };
        rdma::IbvDestroySrq = [](ibv_srq*) { return 0; };
        rdma::IbvPostSrqRecv = PostReceive;
        rdma::IbvPollSrqCq = Poll;
        rdma::IbvReqNotifySrqCq = Arm;
        g_previous_deallocate = butil::iobuf::blockmem_deallocate;
        butil::iobuf::blockmem_deallocate = Deallocate;
        rdma::FLAGS_rdma_recv_zerocopy = false;
        _verbs.tx.cqe = 256;
        _worker = rdma::CreateSrqWorkerForTest();
        auto* pool = rdma::SrqReceivePool::Create(nullptr, 8192, 128);
        ASSERT_NE(nullptr, pool);
        ASSERT_TRUE(rdma::InitializeSrqWorkerForTest(
            _worker, &_verbs.tx, &_verbs.rx, &_verbs.srq, pool, 64));
        ASSERT_NO_FATAL_FAILURE(AddConnection());
        _verbs.receiving_qpn = _connections[0]->_qpn;
    }

    void TearDown() override {
        rdma::SrqStartTaskHookForTest.store(nullptr, butil::memory_order_release);
        butil::MPSCQueueEnqueueHookForTest().store(nullptr, butil::memory_order_release);
        if (_worker != nullptr) {
            rdma::StopSrqWorkerForTest(_worker);
        }
        _connections.clear();
        for (auto& socket : _sockets) {
            socket->SetFailed();
        }
        _sockets.clear();
        _worker.reset();
        EXPECT_TRUE(WaitFor([] { return rdma::SrqReceivePool::LivePools() == 0; }));
        butil::iobuf::blockmem_deallocate = g_previous_deallocate;
        rdma::IbvRegMr = _reg;
        rdma::IbvDeregMr = _dereg;
        rdma::IbvCreateQp = _create_qp;
        rdma::IbvDestroyQp = _destroy_qp;
        rdma::IbvDestroyCq = _destroy_cq;
        rdma::IbvDestroySrq = _destroy_srq;
        rdma::IbvPostSrqRecv = _post;
        rdma::IbvPollSrqCq = _poll;
        rdma::IbvReqNotifySrqCq = _arm;
        g_verbs = nullptr;
    }

    void AddConnection() {
        SocketOptions options;
        SocketId sid;
        ASSERT_EQ(0, Socket::Create(options, &sid));
        SocketUniquePtr socket;
        ASSERT_EQ(0, Socket::Address(sid, &socket));
        auto connection = std::shared_ptr<rdma::SrqConnection>(
            new rdma::SrqConnection(_worker, socket.get(), BTHREAD_TAG_DEFAULT));
        _sockets.push_back(std::move(socket));
        _connections.push_back(connection);
        ASSERT_TRUE(rdma::AddSrqConnectionForTest(_worker, connection, 128));
        ASSERT_TRUE(connection->_fc.Init(512));
    }

    void Queue(bool receive, unsigned count) {
        bool notify;
        {
            rdma::SrqConnection::PostLock post(_connections[0].get());
            if (!receive) {
                ASSERT_EQ(count, rdma::ReserveSrqTxForTest(_worker, count));
            }
            std::lock_guard<std::mutex> lock(_verbs.mutex);
            for (unsigned i = 0; i < count; ++i) {
                if (receive) {
                    ASSERT_FALSE(_verbs.posted.empty());
                    _verbs.ReceiveOne();
                } else {
                    uint32_t slot = _connections[0]->_sq_head++ % _connections[0]->_sq_size;
                    _connections[0]->_records[slot].active.store(true, butil::memory_order_release);
                    ibv_wc wc{};
                    wc.wr_id = slot + 1;
                    wc.qp_num = _connections[0]->_qpn;
                    wc.status = IBV_WC_SUCCESS;
                    wc.opcode = IBV_WC_SEND;
                    _verbs.sends.push_back(wc);
                }
            }
            int index = receive ? 1 : 0;
            notify = _verbs.armed[index];
            _verbs.armed[index] = false;
        }
        // Model one-shot notifications. An unarmed CQ cannot rescue an owner
        // that incorrectly exits after skipping arm.
        if (notify) {
            rdma::WakeSrqWorkerForTest(_worker);
        }
    }

    void WaitIdle(unsigned tx, unsigned rx) {
        ASSERT_TRUE(WaitFor([&] {
            std::lock_guard<std::mutex> lock(_verbs.mutex);
            return _verbs.completions[0] == tx && _verbs.completions[1] == rx &&
                rdma::SrqWorkerIdleForTest(_worker.get());
        }));
    }

    void CheckFullBatch(bool receive) {
        ASSERT_NO_FATAL_FAILURE(Queue(receive, 32));
        ASSERT_NO_FATAL_FAILURE(WaitIdle(receive ? 0 : 32, receive ? 32 : 0));
        {
            std::lock_guard<std::mutex> lock(_verbs.mutex);
            // Two passes without arm, followed by two passes with arm. There
            // was one initial arm and exactly one arm during this event task.
            EXPECT_EQ(4u, _verbs.polls[0]);
            EXPECT_EQ(4u, _verbs.polls[1]);
            EXPECT_EQ(2u, _verbs.arms[0]);
            EXPECT_EQ(2u, _verbs.arms[1]);
            EXPECT_TRUE(_verbs.armed[0]);
            EXPECT_TRUE(_verbs.armed[1]);
        }
        ASSERT_NO_FATAL_FAILURE(Queue(receive, 1));
        ASSERT_NO_FATAL_FAILURE(WaitIdle(receive ? 0 : 33, receive ? 33 : 0));
        unsigned arms;
        unsigned polls;
        {
            std::lock_guard<std::mutex> lock(_verbs.mutex);
            arms = _verbs.arms[0];
            polls = _verbs.polls[0];
        }
        // Two full batches retain the same four polls while requiring only
        // one arm per CQ, instead of arming in both iterations.
        ASSERT_NO_FATAL_FAILURE(Queue(receive, 64));
        ASSERT_NO_FATAL_FAILURE(WaitIdle(receive ? 0 : 97, receive ? 97 : 0));
        std::lock_guard<std::mutex> lock(_verbs.mutex);
        EXPECT_EQ(polls + 4, _verbs.polls[0]);
        EXPECT_EQ(polls + 4, _verbs.polls[1]);
        EXPECT_EQ(arms + 1, _verbs.arms[0]);
        EXPECT_EQ(arms + 1, _verbs.arms[1]);
    }

    void CheckRetirement(bool short_poll) {
        ASSERT_NO_FATAL_FAILURE(AddConnection());
        {
            std::lock_guard<std::mutex> lock(_verbs.mutex);
            _verbs.closing_qpn = _connections[1]->_qpn;
            _verbs.drain_after = _verbs.polls[1] + 1;
            _verbs.short_after = short_poll ? 1 : -1;
        }
        _connections[1]->Close();
        ASSERT_TRUE(WaitFor([&] {
            std::lock_guard<std::mutex> lock(_verbs.mutex);
            return _verbs.destroyed && rdma::SrqWorkerIdleForTest(_worker.get());
        }));
        {
            std::lock_guard<std::mutex> lock(_verbs.mutex);
            if (short_poll) {
                EXPECT_EQ((std::vector<int>{32, 7}), _verbs.drain_counts);
                EXPECT_EQ((std::vector<size_t>{64, 64}), _verbs.drain_depths);
            } else {
                EXPECT_EQ((std::vector<int>{32, 32, 32, 32, 0}), _verbs.drain_counts);
                EXPECT_EQ((std::vector<size_t>{64, 64, 64, 32, 0}), _verbs.drain_depths);
            }
            EXPECT_EQ(64u, _verbs.posted.size());
            // Full retirement batches must not cause normal progress to skip
            // arming. The second normal pass is empty, so this task exits now.
            EXPECT_EQ(2u, _verbs.polls[0]);
            EXPECT_EQ(2u, _verbs.arms[0]);
        }
        EXPECT_FALSE(_connections[0]->worker_retired());
    }

    GFLAGS_NAMESPACE::FlagSaver _flags;
    FakeVerbs _verbs;
    std::shared_ptr<rdma::SrqWorker> _worker;
    std::vector<SocketUniquePtr> _sockets;
    std::vector<std::shared_ptr<rdma::SrqConnection>> _connections;
    decltype(rdma::IbvRegMr) _reg{nullptr};
    decltype(rdma::IbvDeregMr) _dereg{nullptr};
    decltype(rdma::IbvCreateQp) _create_qp{nullptr};
    decltype(rdma::IbvDestroyQp) _destroy_qp{nullptr};
    decltype(rdma::IbvDestroyCq) _destroy_cq{nullptr};
    decltype(rdma::IbvDestroySrq) _destroy_srq{nullptr};
    decltype(rdma::IbvPostSrqRecv) _post{nullptr};
    decltype(rdma::IbvPollSrqCq) _poll{nullptr};
    decltype(rdma::IbvReqNotifySrqCq) _arm{nullptr};
};

TEST_F(RdmaSrqProgressTest, full_tx_batch_rearms_before_sleeping) {
    CheckFullBatch(false);
}

TEST_F(RdmaSrqProgressTest, full_rx_batch_rearms_before_sleeping) {
    CheckFullBatch(true);
}

TEST_F(RdmaSrqProgressTest, retirement_refills_and_accepts_a_short_rx_poll) {
    CheckRetirement(true);
}

TEST_F(RdmaSrqProgressTest, retirement_bounds_refill_under_continuous_rx) {
    CheckRetirement(false);
}

TEST_F(RdmaSrqProgressTest, unlinked_command_does_not_block_cqs_or_refill) {
    butil::atomic<int> executed{0};
    int expected = 2;
    rdma::ScheduleSrqWorkerForTest(_worker, [&] { ++executed; }, false);
    PublishPause pause;
    butil::MPSCQueueEnqueueHookForTest().store(PausePublication, butil::memory_order_release);
    std::thread producer([&] {
        g_publish_pause = &pause;
        rdma::ScheduleSrqWorkerForTest(_worker, [&] { ++executed; }, true);
        g_publish_pause = nullptr;
    });
    BUTIL_SCOPE_EXIT {
        pause.resume.store(true, butil::memory_order_release);
        if (producer.joinable()) {
            producer.join();
        }
        butil::MPSCQueueEnqueueHookForTest().store(nullptr, butil::memory_order_release);
        // Commands capture stack state. Drain before returning, including a
        // failed assertion while the producer was paused.
        EXPECT_TRUE(WaitFor([&] {
            return executed.load() >= expected && rdma::SrqWorkerIdleForTest(_worker.get());
        }));
        rdma::StopSrqWorkerForTest(_worker);
    };
    ASSERT_TRUE(WaitFor([&] { return pause.entered.load(butil::memory_order_acquire); }));
    rdma::ScheduleSrqWorkerForTest(_worker, [&] { ++executed; }, true);
    expected = 3;
    ASSERT_NO_FATAL_FAILURE(Queue(false, 1));
    ASSERT_NO_FATAL_FAILURE(Queue(true, 32));
    // Subsequent CQ progress and refill must complete before publication is
    // released. This catches both a spin and partial mutation of the queue.
    ASSERT_NO_FATAL_FAILURE(WaitIdle(1, 32));
    EXPECT_EQ(0, executed.load());
    {
        std::lock_guard<std::mutex> lock(_verbs.mutex);
        EXPECT_EQ(64u, _verbs.posted.size());
    }
    pause.resume.store(true, butil::memory_order_release);
    producer.join();
    ASSERT_TRUE(WaitFor([&] { return executed.load() == 3; }));
    rdma::ScheduleSrqWorkerForTest(_worker, [&] { ++executed; }, true);
    expected = 4;
    ASSERT_TRUE(WaitFor([&] { return executed.load() == 4; }));
}

TEST_F(RdmaSrqProgressTest, payload_release_and_failure_notify_progress_independently) {
    ASSERT_NO_FATAL_FAILURE(AddConnection());
    auto barrier = std::make_shared<NotificationBarrier>();
    auto dma_stopped = std::make_shared<butil::atomic<bool>>(false);
    std::thread stopper;
    BUTIL_SCOPE_EXIT {
        // Break the dependency on failure so a regression cannot hang CI.
        barrier->resume.store(true, butil::memory_order_release);
        if (stopper.joinable()) {
            stopper.join();
        }
    };
    ASSERT_NO_FATAL_FAILURE(NotifyOnFailure(_sockets[1].get(), [barrier] {
        barrier->ArriveAndWait();
    }));

    auto connection = _connections[0];
    {
        rdma::SrqConnection::PostLock lock(connection.get());
        auto* record = &connection->_records[0];
        std::unique_ptr<void, decltype(&free)> memory(malloc(64), free);
        ASSERT_NE(nullptr, memory);
        ASSERT_EQ(0, record->payload.append_user_data(memory.get(), 64,
            [barrier, dma_stopped,
             weak = std::weak_ptr<rdma::SrqConnection>(connection)](void* data) {
                auto connection = weak.lock();
                dma_stopped->store(connection != nullptr && connection->worker_retired(),
                                   butil::memory_order_release);
                barrier->ArriveAndWait();
                free(data);
            }));
        memory.release();
        ASSERT_EQ(1u, rdma::ReserveSrqTxForTest(_worker, 1));
        record->has_user_data = true;
        ++connection->_sq_head;
        record->active.store(true, butil::memory_order_release);
    }
    // Shutdown collects the outstanding payload and both Socket failures in
    // one round. The deleter and one failure callback wait for each other to
    // enter, so neither possible sequential ordering can pass this test.
    stopper = std::thread([worker = _worker] { rdma::StopSrqWorkerForTest(worker); });
    ASSERT_TRUE(WaitFor([&] { return barrier->finished.load(butil::memory_order_acquire) == 2; }));
    EXPECT_TRUE(dma_stopped->load(butil::memory_order_acquire));
    EXPECT_TRUE(_sockets[0]->Failed());
    EXPECT_TRUE(_sockets[1]->Failed());
}

TEST_F(RdmaSrqProgressTest, socket_failure_callbacks_progress_independently) {
    ASSERT_NO_FATAL_FAILURE(AddConnection());
    auto barrier = std::make_shared<NotificationBarrier>();
    std::thread stopper;
    BUTIL_SCOPE_EXIT {
        barrier->resume.store(true, butil::memory_order_release);
        if (stopper.joinable()) {
            stopper.join();
        }
    };
    for (auto& socket : _sockets) {
        ASSERT_NO_FATAL_FAILURE(NotifyOnFailure(socket.get(), [barrier] {
            barrier->ArriveAndWait();
        }));
    }
    // Either Socket may appear first in the worker's connection map. The
    // first callback must not prevent the second Socket from being failed.
    stopper = std::thread([worker = _worker] { rdma::StopSrqWorkerForTest(worker); });
    ASSERT_TRUE(WaitFor([&] { return barrier->finished.load(butil::memory_order_acquire) == 2; }));
    for (auto& socket : _sockets) {
        EXPECT_TRUE(socket->Failed());
    }
}

TEST_F(RdmaSrqProgressTest, event_and_notification_start_failures_run_inline) {
    auto released = std::make_shared<butil::atomic<int>>(0);
    {
        rdma::SrqConnection::PostLock lock(_connections[0].get());
        auto* record = &_connections[0]->_records[0];
        std::unique_ptr<void, decltype(&free)> memory(malloc(64), free);
        ASSERT_NE(nullptr, memory);
        ASSERT_EQ(0, record->payload.append_user_data(memory.get(), 64,
            [released](void* data) {
                free(data);
                released->fetch_add(1, butil::memory_order_release);
            }));
        memory.release();
        record->has_user_data = true;
    }
    g_task_start_failures.store(0, butil::memory_order_relaxed);
    rdma::SrqStartTaskHookForTest.store(FailTaskStart, butil::memory_order_release);
    BUTIL_SCOPE_EXIT {
        rdma::SrqStartTaskHookForTest.store(nullptr, butil::memory_order_release);
    };
    ASSERT_NO_FATAL_FAILURE(Queue(false, 1));
    ASSERT_NO_FATAL_FAILURE(WaitIdle(1, 0));
    EXPECT_EQ(1, released->load(butil::memory_order_acquire));
    EXPECT_FALSE(_connections[0]->worker_retired());
    EXPECT_FALSE(_sockets[0]->Failed());

    // Event progress and payload release both completed despite failed starts.
    // Stop produces a Socket failure that must also execute inline once.
    rdma::StopSrqWorkerForTest(_worker);
    EXPECT_TRUE(_sockets[0]->Failed());
    EXPECT_TRUE(_connections[0]->worker_retired());
    EXPECT_EQ(1, released->load(butil::memory_order_acquire));
    EXPECT_GE(g_task_start_failures.load(butil::memory_order_acquire), 3u);
}

TEST_F(RdmaSrqProgressTest, delivery_start_failure_consumes_inline) {
    g_task_start_failures.store(0, butil::memory_order_relaxed);
    rdma::SrqStartTaskHookForTest.store(FailTaskStart, butil::memory_order_release);
    BUTIL_SCOPE_EXIT {
        g_delivery_to_close = nullptr;
        rdma::SrqStartTaskHookForTest.store(nullptr, butil::memory_order_release);
    };
    for (bool urgent : {false, true}) {
        SCOPED_TRACE(urgent);
        if (urgent) {
            ASSERT_NO_FATAL_FAILURE(AddConnection());
        }
        auto connection = _connections.back();
        auto released = std::make_shared<butil::atomic<int>>(0);
        butil::IOBuf data;
        std::unique_ptr<void, decltype(&free)> memory(malloc(64), free);
        ASSERT_NE(nullptr, memory);
        ASSERT_EQ(0, data.append_user_data(memory.get(), 64, [released](void* data) {
            free(data);
            released->fetch_add(1, butil::memory_order_release);
        }));
        memory.release();
        ASSERT_TRUE(connection->EnqueueReceived(data.movable()));
        connection->_delivery_enabled.store(true, butil::memory_order_release);
        g_delivery_to_close = connection.get();
        EXPECT_TRUE(connection->StartDelivery(urgent));
        g_delivery_to_close = nullptr;
        EXPECT_EQ(1, released->load(butil::memory_order_acquire));
        EXPECT_EQ(0u, connection->_delivery_events.load(butil::memory_order_acquire));
        EXPECT_FALSE(connection->worker_retired());
        EXPECT_FALSE(_sockets.back()->Failed());
    }
    EXPECT_EQ(2u, g_task_start_failures.load(butil::memory_order_acquire));
}
}  // namespace
#endif  // BRPC_WITH_RDMA
