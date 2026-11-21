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

#if BRPC_WITH_URMA

#include <atomic>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include "brpc/urma/urma_recovery_policy.h"

namespace brpc {
namespace urma {
namespace {

std::atomic<int64_t> now_us(1000000);
int64_t FakeClock() { return now_us.load(); }

class UrmaRecoveryTest : public testing::Test {
protected:
    UrmaRecoveryTest() : policy(FakeClock, 2) {}
    void SetUp() override { now_us = 1000000; }
    UrmaRecoveryPolicy policy;
};

TEST_F(UrmaRecoveryTest, legacy_preserves_admission) {
    uint64_t epoch;
    EXPECT_TRUE(policy.AllowNewConnection("a", &epoch));
    policy.ReportPeerFailure("a", epoch);
    EXPECT_TRUE(policy.AllowNewConnection("a"));
    EXPECT_TRUE(policy.SetMode(UrmaRecoveryMode::AUTO));
    EXPECT_TRUE(policy.AllowNewConnection("a"));
}

TEST_F(UrmaRecoveryTest, peer_failure_only_cools_that_peer) {
    policy.SetMode(UrmaRecoveryMode::AUTO);
    policy.SetCooldownMs(10);
    uint64_t epoch;
    ASSERT_TRUE(policy.AllowNewConnection("a", &epoch));
    policy.ReportPeerFailure("a", epoch);
    EXPECT_FALSE(policy.AllowNewConnection("a"));
    EXPECT_TRUE(policy.AllowNewConnection("b"));
    EXPECT_TRUE(policy.AllowNewConnection(""));
    now_us += 9999;
    EXPECT_FALSE(policy.AllowNewConnection("a"));
    ++now_us;
    EXPECT_TRUE(policy.AllowNewConnection("a"));
}

TEST_F(UrmaRecoveryTest, tcp_only_and_stale_failure_after_mode_change) {
    policy.SetMode(UrmaRecoveryMode::AUTO);
    uint64_t old_epoch;
    ASSERT_TRUE(policy.AllowNewConnection("a", &old_epoch));
    policy.SetMode(UrmaRecoveryMode::TCP_ONLY);
    EXPECT_FALSE(policy.AllowNewConnection("a"));
    EXPECT_FALSE(policy.AllowNewConnection(""));
    policy.SetMode(UrmaRecoveryMode::AUTO);
    policy.ReportPeerFailure("a", old_epoch);
    EXPECT_TRUE(policy.AllowNewConnection("a"));
    uint64_t epoch;
    ASSERT_TRUE(policy.AllowNewConnection("a", &epoch));
    EXPECT_NE(old_epoch, epoch);
    // Reapplying AUTO does not clear existing quarantine.
    policy.ReportPeerFailure("a", epoch);
    policy.SetMode(UrmaRecoveryMode::AUTO);
    EXPECT_FALSE(policy.AllowNewConnection("a"));
}

TEST_F(UrmaRecoveryTest, bounded_peer_cache_evicts_earliest_expiry) {
    policy.SetMode(UrmaRecoveryMode::AUTO);
    uint64_t epoch;
    policy.AllowNewConnection("a", &epoch);
    policy.ReportPeerFailure("a", epoch);
    ++now_us;
    policy.ReportPeerFailure("b", epoch);
    ++now_us;
    policy.ReportPeerFailure("c", epoch);
    EXPECT_TRUE(policy.AllowNewConnection("a"));
    EXPECT_FALSE(policy.AllowNewConnection("b"));
    EXPECT_FALSE(policy.AllowNewConnection("c"));
    now_us += 5000000;
    policy.ReportPeerFailure("d", epoch);
    EXPECT_TRUE(policy.AllowNewConnection("b"));
    EXPECT_TRUE(policy.AllowNewConnection("c"));
    EXPECT_FALSE(policy.AllowNewConnection("d"));
}

TEST_F(UrmaRecoveryTest, validates_configuration) {
    EXPECT_FALSE(policy.SetMode(static_cast<UrmaRecoveryMode>(-1)));
    EXPECT_EQ(UrmaRecoveryMode::LEGACY, policy.GetMode());
    EXPECT_FALSE(policy.SetCooldownMs(0));
    EXPECT_FALSE(policy.SetCooldownMs(-1));
    EXPECT_FALSE(policy.SetCooldownMs(static_cast<int64_t>(INT32_MAX) + 1));
    EXPECT_TRUE(policy.SetCooldownMs(INT32_MAX));
    policy.SetMode(UrmaRecoveryMode::AUTO);
    uint64_t epoch;
    policy.AllowNewConnection("a", &epoch);
    policy.ReportPeerFailure("a", epoch);
    EXPECT_FALSE(policy.AllowNewConnection("a"));
}

TEST_F(UrmaRecoveryTest, concurrent_admission_failure_and_mode_change) {
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([this, i] {
            for (int j = 0; j < 200; ++j) {
                uint64_t epoch;
                const std::string peer = std::to_string(i);
                policy.SetMode(j % 2 ? UrmaRecoveryMode::AUTO
                                     : UrmaRecoveryMode::TCP_ONLY);
                policy.AllowNewConnection(peer, &epoch);
                policy.ReportPeerFailure(peer, epoch);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    policy.SetMode(UrmaRecoveryMode::TCP_ONLY);
    EXPECT_FALSE(policy.AllowNewConnection("a"));
    EXPECT_FALSE(policy.AllowNewConnection(""));
}

TEST_F(UrmaRecoveryTest, reports_reasons_and_admission_counters) {
    policy.SetMode(UrmaRecoveryMode::AUTO);
    uint64_t epoch;
    policy.AllowNewConnection("a", &epoch);
    policy.RecordNegotiatedConnection(epoch);
    policy.ReportPeerFailure("a", epoch, UrmaFailureReason::QP_FATAL);
    EXPECT_FALSE(policy.AllowNewConnection("a"));
    auto status = policy.GetStatus();
    EXPECT_EQ(1u, status.cooled_peers);
    EXPECT_EQ(1u, status.negotiated_connections);
    EXPECT_EQ(1u, status.peer_cooldown_admissions);
    EXPECT_EQ(
        1u, status.failures[static_cast<size_t>(UrmaFailureReason::QP_FATAL)]);
    policy.ReportPeerFailure("b", epoch, UrmaFailureReason::ACCESS_ERROR);
    EXPECT_TRUE(policy.AllowNewConnection("b"));
    now_us += 5000000;
    EXPECT_EQ(0u, policy.GetStatus().cooled_peers);
    policy.SetMode(UrmaRecoveryMode::TCP_ONLY);
    EXPECT_FALSE(policy.AllowNewConnection("b"));
    EXPECT_EQ(1u, policy.GetStatus().tcp_only_admissions);
    policy.ReportPeerFailure("b", epoch, UrmaFailureReason::CQ_ERROR);
    EXPECT_EQ(0u,
              policy.GetStatus()
                  .failures[static_cast<size_t>(UrmaFailureReason::CQ_ERROR)]);
}

}  // namespace
}  // namespace urma
}  // namespace brpc

#endif  // BRPC_WITH_URMA
