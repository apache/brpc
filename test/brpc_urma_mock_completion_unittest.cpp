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

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#if BRPC_WITH_URMA
#include "urma_api.h"

namespace {

class UrmaMockCompletionTest : public testing::Test {
   protected:
    void SetUp() override {
        urma_init_attr_t attr{};
        const auto status = urma_init(&attr);
        ASSERT_TRUE(status == URMA_SUCCESS || status == URMA_EEXIST);
        _owns_init = status == URMA_SUCCESS;
        int count = 0;
        auto** devices = urma_get_device_list(&count);
        const bool mock =
            devices && count > 0 &&
            std::strcmp(devices[0]->name, "mock_urma_device") == 0;
        if (mock) {
            _context = urma_create_context(devices[0], 0);
        }
        urma_free_device_list(devices);
        if (!mock) {
            GTEST_SKIP() << "Completion ordering test uses the link-time mock";
        }
        ASSERT_NE(nullptr, _context);
        urma_jfc_cfg_t cq{};
        cq.depth = 8;
        _send_cq = urma_create_jfc(_context, &cq);
        _recv_cq = urma_create_jfc(_context, &cq);
        ASSERT_NE(nullptr, _send_cq);
        ASSERT_NE(nullptr, _recv_cq);
        urma_jfr_cfg_t rq{};
        rq.depth = 8;
        rq.jfc = _recv_cq;
        _recv_queue = urma_create_jfr(_context, &rq);
        ASSERT_NE(nullptr, _recv_queue);
        urma_jetty_cfg_t cfg{};
        cfg.jfs_cfg.jfc = _send_cq;
        cfg.shared.jfr = _recv_queue;
        _sender = urma_create_jetty(_context, &cfg);
        _receiver = urma_create_jetty(_context, &cfg);
        ASSERT_NE(nullptr, _sender);
        ASSERT_NE(nullptr, _receiver);
        urma_rjetty_t remote{};
        remote.jetty_id = _receiver->jetty_id;
        urma_token_t token{};
        _target = urma_import_jetty(_context, &remote, &token);
        ASSERT_NE(nullptr, _target);
    }

    void TearDown() override {
        if (_target) urma_unimport_jetty(_target);
        if (_receiver) urma_delete_jetty(_receiver);
        if (_sender) urma_delete_jetty(_sender);
        if (_recv_queue) urma_delete_jfr(_recv_queue);
        if (_recv_cq) urma_delete_jfc(_recv_cq);
        if (_send_cq) urma_delete_jfc(_send_cq);
        if (_context) urma_delete_context(_context);
        if (_owns_init) urma_uninit();
    }

    bool _owns_init = false;
    urma_context_t* _context = nullptr;
    urma_jfc_t* _send_cq = nullptr;
    urma_jfc_t* _recv_cq = nullptr;
    urma_jfr_t* _recv_queue = nullptr;
    urma_jetty_t* _sender = nullptr;
    urma_jetty_t* _receiver = nullptr;
    urma_target_jetty_t* _target = nullptr;
};

TEST_F(UrmaMockCompletionTest, send_completion_follows_payload_delivery) {
    // Keep the copy large enough for a concurrent CQ consumer to run while
    // the mock is still using the source buffer.
    const std::string payload(16 * 1024 * 1024, 'p');
    std::string received(payload.size(), '\0');
    urma_sge_t recv_sge{};
    recv_sge.addr = reinterpret_cast<uint64_t>(&received[0]);
    recv_sge.len = received.size();
    urma_jfr_wr_t recv_wr{};
    recv_wr.src.sge = &recv_sge;
    recv_wr.src.num_sge = 1;
    recv_wr.user_ctx = 2;
    urma_jfr_wr_t* bad_recv = nullptr;
    ASSERT_EQ(URMA_SUCCESS, urma_post_jfr_wr(_recv_queue, &recv_wr, &bad_recv));

    urma_sge_t send_sge{};
    send_sge.addr = reinterpret_cast<uint64_t>(payload.data());
    send_sge.len = payload.size();
    urma_jfs_wr_t send_wr{};
    send_wr.opcode = URMA_OPC_SEND;
    send_wr.flag.bs.complete_enable = 1;
    send_wr.tjetty = _target;
    send_wr.send.src.sge = &send_sge;
    send_wr.send.src.num_sge = 1;
    send_wr.user_ctx = 1;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    urma_status_t posted = URMA_EINVAL;
    std::thread writer([&] {
        while (!started.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        urma_jfs_wr_t* bad_send = nullptr;
        posted = urma_post_jetty_send_wr(_sender, &send_wr, &bad_send);
        finished.store(true, std::memory_order_release);
    });
    started.store(true, std::memory_order_release);
    urma_cr_t send_cr{};
    int send_count = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (send_count == 0 && std::chrono::steady_clock::now() < deadline) {
        send_count = urma_poll_jfc(_send_cq, 1, &send_cr);
        if (finished.load(std::memory_order_acquire) && send_count == 0) {
            send_count = urma_poll_jfc(_send_cq, 1, &send_cr);
            break;
        }
    }
    urma_cr_t recv_cr{};
    const int recv_count = urma_poll_jfc(_recv_cq, 1, &recv_cr);
    // Join before assertions so a failed check still releases the worker.
    writer.join();
    ASSERT_EQ(URMA_SUCCESS, posted);
    ASSERT_EQ(1, send_count);
    EXPECT_EQ(URMA_CR_SUCCESS, send_cr.status);
    EXPECT_EQ(1u, send_cr.user_ctx);
    ASSERT_EQ(1, recv_count)
        << "Mock SEND completed before delivering its source payload";
    EXPECT_EQ(URMA_CR_SUCCESS, recv_cr.status);
    EXPECT_EQ(2u, recv_cr.user_ctx);
    EXPECT_EQ(payload.size(), recv_cr.completion_len);
    EXPECT_EQ(payload, received);
}

}  // namespace
#endif  // BRPC_WITH_URMA
