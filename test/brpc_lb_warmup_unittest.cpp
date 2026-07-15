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

#include <map>
#include <sstream>
#include <vector>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <unistd.h>
#include "butil/fast_rand.h"
#include "butil/time.h"
#include "brpc/socket.h"
#include "brpc/load_balancer.h"
#include "brpc/policy/round_robin_load_balancer.h"
#include "brpc/policy/weighted_round_robin_load_balancer.h"
#include "brpc/policy/consistent_hashing_load_balancer.h"
#include "brpc/policy/locality_aware_load_balancer.h"
#include "brpc/policy/p2c_ewma_load_balancer.h"

namespace brpc {
DECLARE_double(lb_warmup_curve);
}

namespace {

class SaveRecycle : public brpc::SocketUser {
    void BeforeRecycle(brpc::Socket* s) { delete this; }
};

brpc::ServerId CreateServer(const char* addr, const char* tag = "") {
    butil::EndPoint point;
    EXPECT_EQ(0, str2endpoint(addr, &point));
    brpc::ServerId id(8888);
    brpc::SocketOptions options;
    options.remote_side = point;
    options.user = new SaveRecycle;
    EXPECT_EQ(0, brpc::Socket::Create(options, &id.id));
    id.tag = tag;
    return id;
}

// Select `count' times at `now_us' and return times each server was chosen.
// Feeds back immediately when the LB asks for it(la).
std::map<brpc::SocketId, int> CountShares(
    brpc::LoadBalancer* lb, int count, int64_t now_us,
    bool changable_weights = false, bool with_request_code = false) {
    std::map<brpc::SocketId, int> shares;
    for (int i = 0; i < count; ++i) {
        brpc::LoadBalancer::SelectIn in = {
            now_us, changable_weights, with_request_code,
            with_request_code ? butil::fast_rand() % UINT_MAX : 0u, NULL };
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectOut out(&ptr);
        if (lb->SelectServer(in, &out) != 0) {
            continue;
        }
        ++shares[ptr->id()];
        if (out.need_feedback) {
            brpc::LoadBalancer::CallInfo info;
            info.begin_time_us = now_us;
            info.server_id = ptr->id();
            info.error_code = 0;
            info.controller = NULL;
            lb->Feedback(info);
        }
    }
    return shares;
}

class LbWarmupTest : public ::testing::Test {
protected:
    void SetUp() override {
        _saved_warmup_ms = brpc::FLAGS_lb_warmup_ms;
        _saved_curve = brpc::FLAGS_lb_warmup_curve;
    }
    void TearDown() override {
        brpc::FLAGS_lb_warmup_ms = _saved_warmup_ms;
        brpc::FLAGS_lb_warmup_curve = _saved_curve;
    }

    int64_t _saved_warmup_ms;
    double _saved_curve;
};

TEST_F(LbWarmupTest, disabled_by_default) {
    ASSERT_EQ(0, brpc::FLAGS_lb_warmup_ms);
    // Any stamp maps to full weight when disabled.
    ASSERT_DOUBLE_EQ(1.0, brpc::WarmupMultiplier(butil::gettimeofday_us(), 0));

    // A just-added server gets its full share right away.
    brpc::policy::RoundRobinLoadBalancer lb;
    const brpc::ServerId a = CreateServer("127.0.0.1:8101");
    const brpc::ServerId b = CreateServer("127.0.0.1:8102");
    ASSERT_TRUE(lb.AddServer(a));
    ASSERT_TRUE(lb.AddServer(b));
    std::map<brpc::SocketId, int> shares =
        CountShares(&lb, 2000, butil::gettimeofday_us());
    ASSERT_GT(shares[a.id], 600);
    ASSERT_GT(shares[b.id], 600);
}

TEST_F(LbWarmupTest, multiplier_math) {
    brpc::FLAGS_lb_warmup_ms = 10000;
    const int64_t join_us = 1000000;

    // Unstamped server is never ramped.
    ASSERT_DOUBLE_EQ(1.0, brpc::WarmupMultiplier(0, join_us));
    // Ramp floor right after joining and on backward clock jumps.
    ASSERT_DOUBLE_EQ(0.1, brpc::WarmupMultiplier(join_us, join_us));
    ASSERT_DOUBLE_EQ(0.1, brpc::WarmupMultiplier(join_us + 5000000, join_us));
    // Linear ramp.
    ASSERT_DOUBLE_EQ(0.1, brpc::WarmupMultiplier(join_us, join_us + 500000));
    ASSERT_DOUBLE_EQ(0.3, brpc::WarmupMultiplier(join_us, join_us + 3000000));
    ASSERT_DOUBLE_EQ(0.5, brpc::WarmupMultiplier(join_us, join_us + 5000000));
    ASSERT_DOUBLE_EQ(1.0, brpc::WarmupMultiplier(join_us, join_us + 10000000));
    ASSERT_DOUBLE_EQ(1.0, brpc::WarmupMultiplier(join_us, join_us + 60000000));

    // Curve shaping: >1 is more conservative early, <1 more aggressive.
    brpc::FLAGS_lb_warmup_curve = 2.0;
    ASSERT_DOUBLE_EQ(0.25, brpc::WarmupMultiplier(join_us, join_us + 5000000));
    brpc::FLAGS_lb_warmup_curve = 0.5;
    ASSERT_DOUBLE_EQ(0.5, brpc::WarmupMultiplier(join_us, join_us + 2500000));

    brpc::FLAGS_lb_warmup_curve = 1.0;
    brpc::FLAGS_lb_warmup_ms = 0;
    ASSERT_DOUBLE_EQ(1.0, brpc::WarmupMultiplier(join_us, join_us));
}

TEST_F(LbWarmupTest, accept_probability_follows_multiplier) {
    brpc::FLAGS_lb_warmup_ms = 10000;
    const int64_t join_us = butil::gettimeofday_us();
    int accepted = 0;
    const int N = 10000;
    for (int i = 0; i < N; ++i) {
        accepted += brpc::WarmupAccept(join_us, join_us + 5000000);
    }
    // ~N/2 accepts at multiplier 0.5.
    ASSERT_GT(accepted, N * 4 / 10);
    ASSERT_LT(accepted, N * 6 / 10);
}

TEST_F(LbWarmupTest, rr_ramp_and_rejoin) {
    brpc::FLAGS_lb_warmup_ms = 300;
    brpc::policy::RoundRobinLoadBalancer lb;
    const brpc::ServerId a = CreateServer("127.0.0.1:8111");
    ASSERT_TRUE(lb.AddServer(a));
    usleep(400 * 1000);
    const brpc::ServerId b = CreateServer("127.0.0.1:8112");
    ASSERT_TRUE(lb.AddServer(b));

    const int N = 4000;
    // Server b is still cold, its share stays well below the even 50%.
    std::map<brpc::SocketId, int> shares =
        CountShares(&lb, N, butil::gettimeofday_us());
    ASSERT_LT(shares[b.id], N / 4) << shares[b.id];
    ASSERT_GT(shares[b.id], 0);

    // Past the window(simulated by a future timestamp) shares even out.
    shares = CountShares(&lb, N, butil::gettimeofday_us() + 1000000);
    ASSERT_GT(shares[b.id], N * 35 / 100);
    ASSERT_LT(shares[b.id], N * 65 / 100);

    // Removing and re-adding restarts the ramp.
    ASSERT_TRUE(lb.RemoveServer(b));
    ASSERT_TRUE(lb.AddServer(b));
    shares = CountShares(&lb, N, butil::gettimeofday_us());
    ASSERT_LT(shares[b.id], N / 4) << shares[b.id];
}

TEST_F(LbWarmupTest, wrr_ramp) {
    brpc::FLAGS_lb_warmup_ms = 300;
    brpc::policy::WeightedRoundRobinLoadBalancer lb;
    const brpc::ServerId a = CreateServer("127.0.0.1:8121", "2");
    ASSERT_TRUE(lb.AddServer(a));
    usleep(400 * 1000);
    const brpc::ServerId b = CreateServer("127.0.0.1:8122", "2");
    ASSERT_TRUE(lb.AddServer(b));

    const int N = 4000;
    std::map<brpc::SocketId, int> shares =
        CountShares(&lb, N, butil::gettimeofday_us());
    ASSERT_LT(shares[b.id], N / 4) << shares[b.id];

    shares = CountShares(&lb, N, butil::gettimeofday_us() + 1000000);
    ASSERT_GT(shares[b.id], N * 35 / 100);
    ASSERT_LT(shares[b.id], N * 65 / 100);
}

TEST_F(LbWarmupTest, chash_ramp) {
    brpc::FLAGS_lb_warmup_ms = 300;
    brpc::policy::ConsistentHashingLoadBalancer lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    const brpc::ServerId a = CreateServer("127.0.0.1:8131");
    ASSERT_TRUE(lb.AddServer(a));
    usleep(400 * 1000);
    const brpc::ServerId b = CreateServer("127.0.0.1:8132");
    ASSERT_TRUE(lb.AddServer(b));

    const int N = 4000;
    // Requests hashed onto cold b are mostly diverted along the ring.
    std::map<brpc::SocketId, int> shares =
        CountShares(&lb, N, butil::gettimeofday_us(), false, true);
    ASSERT_LT(shares[b.id], N * 35 / 100) << shares[b.id];

    shares = CountShares(&lb, N, butil::gettimeofday_us() + 1000000,
                         false, true);
    ASSERT_GT(shares[b.id], N * 25 / 100);
    ASSERT_LT(shares[b.id], N * 75 / 100);
}

TEST_F(LbWarmupTest, la_ramp) {
    brpc::FLAGS_lb_warmup_ms = 300;
    brpc::policy::LocalityAwareLoadBalancer lb;
    const brpc::ServerId a = CreateServer("127.0.0.1:8141");
    ASSERT_TRUE(lb.AddServer(a));
    usleep(400 * 1000);
    const brpc::ServerId b = CreateServer("127.0.0.1:8142");
    ASSERT_TRUE(lb.AddServer(b));

    // A cold server gets a reduced share of the weight tree.
    const int N = 4000;
    std::map<brpc::SocketId, int> shares =
        CountShares(&lb, N, butil::gettimeofday_us(), true);
    ASSERT_LT(shares[b.id], N * 30 / 100) << shares[b.id];
    ASSERT_GT(shares[b.id], 0);

    // While warming, b's weight lags its base weight...
    brpc::DescribeOptions opt;
    opt.verbose = true;
    std::ostringstream cold_desc;
    lb.Describe(cold_desc, opt);
    ASSERT_NE(std::string::npos, cold_desc.str().find("(base="))
        << cold_desc.str();

    // ...and catches up with it once the window has passed. Remove a so
    // that selections must touch b and refresh its weight; share-based
    // assertions do not work here: with identical synthetic latencies
    // LALB's qps/latency weights are degenerate.
    usleep(400 * 1000);
    ASSERT_TRUE(lb.RemoveServer(a));
    CountShares(&lb, 100, butil::gettimeofday_us(), true);
    std::ostringstream warm_desc;
    lb.Describe(warm_desc, opt);
    ASSERT_EQ(std::string::npos, warm_desc.str().find("(base="))
        << warm_desc.str();
}

TEST_F(LbWarmupTest, p2c_ramp) {
    brpc::FLAGS_lb_warmup_ms = 300;
    brpc::policy::P2CEwmaLoadBalancer lb;
    const brpc::ServerId a = CreateServer("127.0.0.1:8151");
    ASSERT_TRUE(lb.AddServer(a));
    usleep(400 * 1000);
    const brpc::ServerId b = CreateServer("127.0.0.1:8152");
    ASSERT_TRUE(lb.AddServer(b));

    const int N = 4000;
    // The discounted weight lifts b's score, both sampled servers being
    // otherwise equal, so b loses (nearly) every comparison while cold.
    std::map<brpc::SocketId, int> shares =
        CountShares(&lb, N, butil::gettimeofday_us());
    ASSERT_LT(shares[b.id], N * 5 / 100) << shares[b.id];

    shares = CountShares(&lb, N, butil::gettimeofday_us() + 1000000);
    ASSERT_GT(shares[b.id], N * 30 / 100);
    ASSERT_LT(shares[b.id], N * 70 / 100);
}

} // namespace
