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
#include <thread>
#include "butil/atomicops.h"
#include "brpc/rdma/srq_flow_control.h"

namespace {
using brpc::rdma::SrqFlowControl;

TEST(SrqFlowControlTest, sparse_requests_keep_initial_credit) {
    SrqFlowControl sender;
    SrqFlowControl receiver;
    ASSERT_TRUE(sender.Init(16));
    ASSERT_TRUE(receiver.Init(16));
    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(sender.can_send());
        const uint32_t flags = sender.PrepareData();
        EXPECT_EQ(0u, flags);
        sender.DataPosted(flags);
        ASSERT_TRUE(receiver.Receive(flags, 64));
        receiver.ApplyReceived();
        EXPECT_FALSE(receiver.grant_pending());
    }
    EXPECT_EQ(8u, sender.window());
    // Initial sparse sends require neither reverse-direction data nor a
    // burst-specific ACK.
}

TEST(SrqFlowControlTest, plain_data_preserves_pending_hard_request) {
    SrqFlowControl receiver;
    ASSERT_TRUE(receiver.Init(16));
    ASSERT_TRUE(receiver.Receive(SrqFlowControl::HARD_REQUEST, 64));
    receiver.ApplyReceived();
    for (int i = 0; i < 64; ++i) {
        ASSERT_TRUE(receiver.Receive(0, 64));
        receiver.ApplyReceived();
        EXPECT_EQ(16u, receiver.window());
        EXPECT_TRUE(receiver.NeedPureGrant());
    }
    EXPECT_FALSE(SrqFlowControl::ValidMessage(0, 0));
}

TEST(SrqFlowControlTest, unidirectional_stream_survives_delayed_grants) {
    for (uint32_t capacity : {4u, 5u, 16u, 127u, 512u}) {
        SrqFlowControl sender;
        SrqFlowControl receiver;
        ASSERT_TRUE(sender.Init(capacity));
        ASSERT_TRUE(receiver.Init(capacity));
        for (int cycle = 0; cycle < 20; ++cycle) {
            int soft = 0;
            int hard = 0;
            for (uint32_t i = 0; i < capacity; ++i) {
                ASSERT_TRUE(sender.can_send());
                const uint32_t flags = sender.PrepareData();
                soft += (flags & SrqFlowControl::SOFT_REQUEST) != 0;
                hard += (flags & SrqFlowControl::HARD_REQUEST) != 0;
                sender.DataPosted(flags);
                ASSERT_TRUE(receiver.Receive(flags, 8192));
                receiver.ApplyReceived();
                // Simulate temporarily unavailable send resources: keep the
                // request pending and delay the grant until the end of this
                // iteration.
                EXPECT_EQ(hard != 0, receiver.NeedPureGrant());
            }
            EXPECT_EQ(1, soft);
            EXPECT_EQ(1, hard);
            EXPECT_FALSE(sender.can_send());
            // A pure grant restores the send window even without
            // reverse-direction application data.
            ASSERT_TRUE(receiver.NeedPureGrant());
            receiver.PureGrantPosted();
            ASSERT_TRUE(sender.Receive(SrqFlowControl::PURE_GRANT, 0));
            sender.ApplyReceived();
            EXPECT_EQ(capacity, sender.window());
            EXPECT_FALSE(sender.grant_pending());
            EXPECT_FALSE(receiver.grant_pending());
        }
    }
}

TEST(SrqFlowControlTest, piggyback_grant_restores_instead_of_adding) {
    SrqFlowControl a;
    SrqFlowControl b;
    ASSERT_TRUE(a.Init(16));
    ASSERT_TRUE(b.Init(64));
    for (int i = 0; i < 9; ++i) {
        const uint32_t flags = a.PrepareData();
        a.DataPosted(flags);
        ASSERT_TRUE(b.Receive(flags, 8));
        b.ApplyReceived();
    }
    ASSERT_EQ(7u, a.window());
    ASSERT_TRUE(b.grant_pending());
    EXPECT_FALSE(b.NeedPureGrant());
    const uint32_t flags = b.PrepareData();
    ASSERT_EQ(SrqFlowControl::GRANT, flags);
    b.DataPosted(flags);
    ASSERT_TRUE(a.Receive(flags, 8));
    a.ApplyReceived();
    EXPECT_EQ(16u, a.window());
    EXPECT_EQ(63u, b.window());
    EXPECT_FALSE(b.grant_pending());
}

TEST(SrqFlowControlTest, failed_post_does_not_lose_request_or_window) {
    SrqFlowControl fc;
    ASSERT_TRUE(fc.Init(16));
    ASSERT_TRUE(fc.Receive(SrqFlowControl::HARD_REQUEST, 8));
    fc.ApplyReceived();
    for (int retry = 0; retry < 5; ++retry) {
        EXPECT_EQ(SrqFlowControl::GRANT, fc.PrepareData());
        EXPECT_EQ(16u, fc.window());
        EXPECT_TRUE(fc.NeedPureGrant());
    }
    fc.DataPosted(fc.PrepareData());
    EXPECT_EQ(15u, fc.window());
    EXPECT_FALSE(fc.grant_pending());
    // A later soft request must not downgrade an already-pending hard request.
    ASSERT_TRUE(fc.Receive(SrqFlowControl::HARD_REQUEST, 8));
    fc.ApplyReceived();
    ASSERT_TRUE(fc.Receive(SrqFlowControl::SOFT_REQUEST, 8));
    fc.ApplyReceived();
    EXPECT_TRUE(fc.NeedPureGrant());
    fc.PureGrantPosted();
    EXPECT_EQ(15u, fc.window());
    EXPECT_FALSE(fc.grant_pending());
}

TEST(SrqFlowControlTest, malformed_control_does_not_mutate_state) {
    SrqFlowControl fc;
    ASSERT_TRUE(fc.Init(16));
    fc.DataPosted(fc.PrepareData());
    for (uint32_t invalid : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u, 0xffffffffu}) {
        EXPECT_FALSE(fc.Receive(invalid, 0));
        fc.ApplyReceived();
        EXPECT_EQ(15u, fc.window());
        EXPECT_FALSE(fc.grant_pending());
    }
    for (uint32_t invalid : {3u, 7u, 8u, 12u, 0xffffffffu}) {
        EXPECT_FALSE(fc.Receive(invalid, 1));
        fc.ApplyReceived();
        EXPECT_EQ(15u, fc.window());
        EXPECT_FALSE(fc.grant_pending());
    }
    EXPECT_FALSE(fc.Init(0));
    EXPECT_FALSE(fc.Init(3));
    EXPECT_FALSE(fc.Init(65536));
    EXPECT_EQ(15u, fc.window());
}

TEST(SrqFlowControlTest, bidirectional_request_and_grant_share_one_message) {
    SrqFlowControl fc;
    ASSERT_TRUE(fc.Init(4));
    fc.DataPosted(fc.PrepareData());
    fc.DataPosted(fc.PrepareData());
    ASSERT_TRUE(fc.Receive(SrqFlowControl::HARD_REQUEST, 8));
    fc.ApplyReceived();
    const uint32_t flags = fc.PrepareData();
    EXPECT_EQ(SrqFlowControl::SOFT_REQUEST | SrqFlowControl::GRANT, flags);
    fc.DataPosted(flags);
    EXPECT_EQ(1u, fc.window());
    EXPECT_FALSE(fc.grant_pending());
    ASSERT_TRUE(fc.Receive(SrqFlowControl::HARD_REQUEST | SrqFlowControl::GRANT, 8));
    fc.ApplyReceived();
    EXPECT_EQ(4u, fc.window());
    EXPECT_TRUE(fc.NeedPureGrant());
}

TEST(SrqFlowControlTest, received_grant_waits_for_successful_prefix_commit) {
    SrqFlowControl fc;
    ASSERT_TRUE(fc.Init(16));
    auto planned = fc.Snapshot();
    uint32_t flags[4];
    for (int i = 0; i < 4; ++i) {
        flags[i] = planned.PrepareData();
        planned.DataPosted(flags[i]);
    }
    // Simulate a grant arriving before post returns: publish the event without
    // changing the authoritative send window.
    ASSERT_TRUE(fc.Receive(SrqFlowControl::PURE_GRANT, 0));
    EXPECT_EQ(16u, fc.window());
    EXPECT_EQ(12u, planned.window());
    // Verbs accepted only the first two WRs. Charge those before restoring the
    // window.
    fc.DataPosted(flags[0]);
    fc.DataPosted(flags[1]);
    EXPECT_EQ(14u, fc.window());
    fc.ApplyReceived();
    EXPECT_EQ(16u, fc.window());
    EXPECT_FALSE(fc.grant_pending());
}

TEST(SrqFlowControlTest, new_hard_request_survives_old_grant_commit) {
    for (bool pure : {false, true}) {
        SrqFlowControl fc;
        ASSERT_TRUE(fc.Init(16));
        ASSERT_TRUE(fc.Receive(SrqFlowControl::HARD_REQUEST, 64));
        fc.ApplyReceived();
        uint32_t flags = fc.PrepareData();
        ASSERT_TRUE(flags & SrqFlowControl::GRANT);
        // A new request arrives between preparing the old grant and learning
        // its post result.
        ASSERT_TRUE(fc.Receive(SrqFlowControl::HARD_REQUEST, 64));
        if (pure) {
            fc.PureGrantPosted();
        } else {
            fc.DataPosted(flags);
        }
        EXPECT_FALSE(fc.grant_pending());
        fc.ApplyReceived();
        EXPECT_TRUE(fc.NeedPureGrant());
        EXPECT_EQ(pure ? 16u : 15u, fc.window());
    }
}

TEST(SrqFlowControlTest, coalesced_events_preserve_hard_and_decode_pure_grant) {
    SrqFlowControl fc;
    ASSERT_TRUE(fc.Init(16));
    fc.DataPosted(fc.PrepareData());
    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(fc.Receive(SrqFlowControl::SOFT_REQUEST, 64));
        ASSERT_TRUE(fc.Receive(SrqFlowControl::HARD_REQUEST, 64));
        ASSERT_TRUE(fc.Receive(SrqFlowControl::PURE_GRANT, 0));
    }
    EXPECT_EQ(15u, fc.window());
    EXPECT_FALSE(fc.grant_pending());
    // Merged internal events also equal 0x7, but must retain the hard request
    // actually received.
    fc.ApplyReceived();
    EXPECT_EQ(16u, fc.window());
    ASSERT_TRUE(fc.NeedPureGrant());
    fc.PureGrantPosted();
    ASSERT_TRUE(fc.Receive(SrqFlowControl::PURE_GRANT, 0));
    fc.ApplyReceived();
    EXPECT_FALSE(fc.grant_pending());
    // Applying again must not replay events already consumed.
    fc.DataPosted(fc.PrepareData());
    fc.ApplyReceived();
    EXPECT_EQ(15u, fc.window());
}

TEST(SrqFlowControlTest, concurrent_publication_and_exchange_do_not_lose_events) {
    SrqFlowControl fc;
    ASSERT_TRUE(fc.Init(16));
    butil::atomic<int> started{0};
    butil::atomic<int> published{0};
    int rounds = 10000;
    std::thread receiver([&] {
        for (int i = 1; i <= rounds; ++i) {
            while (started.load(butil::memory_order_acquire) != i) {
                std::this_thread::yield();
            }
            EXPECT_TRUE(fc.Receive(SrqFlowControl::SOFT_REQUEST, 64));
            EXPECT_TRUE(fc.Receive(SrqFlowControl::HARD_REQUEST, 64));
            EXPECT_TRUE(fc.Receive(SrqFlowControl::PURE_GRANT, 0));
            published.store(i, butil::memory_order_release);
        }
    });
    for (int i = 1; i <= rounds; ++i) {
        fc.DataPosted(fc.PrepareData());
        started.store(i, butil::memory_order_release);
        // exchange races with event publication. Apply once more afterwards to
        // include late arrivals.
        do {
            fc.ApplyReceived();
        } while (published.load(butil::memory_order_acquire) != i);
        fc.ApplyReceived();
        EXPECT_EQ(16u, fc.window());
        EXPECT_TRUE(fc.NeedPureGrant());
        if (fc.NeedPureGrant()) {
            fc.PureGrantPosted();
        }
    }
    receiver.join();
}
}  // namespace
