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

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "butil/macros.h"
#include "brpc/socket.h"
#include "brpc/excluded_servers.h"
#include "brpc/policy/consistent_hashing_load_balancer.h"
#include "brpc/policy/hasher.h"

namespace {

brpc::ServerId CreateServer(const char* addr, const char* tag = "") {
    butil::EndPoint point;
    EXPECT_EQ(0, str2endpoint(addr, &point));
    brpc::ServerId id(8888);
    brpc::SocketOptions options;
    options.remote_side = point;
    EXPECT_EQ(0, brpc::Socket::Create(options, &id.id));
    id.tag = tag;
    return id;
}

void DestroyServers(const std::vector<brpc::ServerId>& ids) {
    for (size_t i = 0; i < ids.size(); ++i) {
        brpc::Socket::SetFailed(ids[i].id);
    }
}

brpc::LoadBalancer::SelectIn MakeInput(uint64_t code,
                                       bool changable_weights = true) {
    brpc::LoadBalancer::SelectIn in = { 0, changable_weights, true, code, nullptr };
    return in;
}

int64_t TotalInflightOf(brpc::LoadBalancer* lb) {
    std::ostringstream os;
    brpc::DescribeOptions opt;
    opt.verbose = true;
    lb->Describe(os, opt);
    const std::string desc = os.str();
    const size_t pos = desc.find("total_inflight=");
    EXPECT_NE(std::string::npos, pos) << desc;
    return strtoll(desc.c_str() + pos + strlen("total_inflight="), nullptr, 10);
}

class CHBoundedLoadTest : public testing::Test {};

TEST_F(CHBoundedLoadTest, load_factor_validation) {
    brpc::policy::ConsistentHashingBoundedLoadBalancer lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    ASSERT_EQ(nullptr, lb.New("load_factor=1.0"));
    ASSERT_EQ(nullptr, lb.New("load_factor=0.5"));
    ASSERT_EQ(nullptr, lb.New("load_factor=abc"));
    brpc::LoadBalancer* valid = lb.New("load_factor=1.5");
    ASSERT_TRUE(valid != nullptr);
    valid->Destroy();

    ASSERT_EQ("", GFLAGS_NAMESPACE::SetCommandLineOption(
        "chash_bounded_load_factor", "0.9"));
    ASSERT_EQ("", GFLAGS_NAMESPACE::SetCommandLineOption(
        "chash_bounded_load_factor", "1.0"));
    ASSERT_NE("", GFLAGS_NAMESPACE::SetCommandLineOption(
        "chash_bounded_load_factor", "1.25"));
}

TEST_F(CHBoundedLoadTest, replicas_parameter) {
    brpc::policy::ConsistentHashingLoadBalancer classic_lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    brpc::policy::ConsistentHashingBoundedLoadBalancer bounded_lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    const brpc::LoadBalancer* lbs[] = { &classic_lb, &bounded_lb };
    for (size_t i = 0; i < arraysize(lbs); ++i) {
        ASSERT_EQ(nullptr, lbs[i]->New("replicas=abc"));
        brpc::LoadBalancer* lb = lbs[i]->New("replicas=300");
        ASSERT_TRUE(lb != nullptr);
        std::ostringstream os;
        brpc::DescribeOptions opt;
        opt.verbose = true;
        lb->Describe(os, opt);
        ASSERT_NE(std::string::npos,
                  os.str().find("replica per host: 300")) << os.str();
        lb->Destroy();
    }
    brpc::LoadBalancer* lb = bounded_lb.New("replicas=200 load_factor=2");
    ASSERT_TRUE(lb != nullptr);
    lb->Destroy();
}

TEST_F(CHBoundedLoadTest, hot_key_is_capped) {
    const size_t N = 8;
    const size_t K = 200;
    const double FACTOR = 1.25;
    std::vector<brpc::ServerId> ids;
    brpc::policy::ConsistentHashingBoundedLoadBalancer lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    brpc::policy::ConsistentHashingLoadBalancer classic_lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    for (size_t i = 0; i < N; ++i) {
        char addr[32];
        snprintf(addr, sizeof(addr), "192.168.1.%d:8080", (int)i);
        ids.push_back(CreateServer(addr));
    }
    ASSERT_EQ(N, lb.AddServersInBatch(ids));
    ASSERT_EQ(N, classic_lb.AddServersInBatch(ids));

    const std::string hot_key = "hot_key";
    brpc::LoadBalancer::SelectIn in =
        MakeInput(brpc::policy::MurmurHash32(hot_key.data(), hot_key.size()));

    // Classic CH sends every request for the key to one server.
    std::map<brpc::SocketId, size_t> classic_counts;
    for (size_t i = 0; i < K; ++i) {
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectOut out(&ptr);
        ASSERT_EQ(0, classic_lb.SelectServer(in, &out));
        ++classic_counts[ptr->id()];
    }
    ASSERT_EQ(1UL, classic_counts.size());
    const brpc::SocketId primary = classic_counts.begin()->first;

    // Bounded-load CH spreads the same hot key once the primary server hits
    // its capacity: no server exceeds ceil(FACTOR * K / N) outstanding
    // requests(the cap of the last selection, when total load is largest).
    std::map<brpc::SocketId, size_t> counts;
    for (size_t i = 0; i < K; ++i) {
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectOut out(&ptr);
        ASSERT_EQ(0, lb.SelectServer(in, &out));
        ASSERT_TRUE(out.need_feedback);
        ++counts[ptr->id()];
    }
    const size_t cap = (size_t)std::ceil(FACTOR * K / N);
    size_t max_count = 0;
    for (std::map<brpc::SocketId, size_t>::iterator it = counts.begin();
         it != counts.end(); ++it) {
        max_count = std::max(max_count, it->second);
    }
    ASSERT_LE(max_count, cap);
    ASSERT_GT(counts.size(), 1UL);
    // The first selection matches classic CH: locality is unchanged until
    // the primary server is at capacity.
    ASSERT_GT(counts[primary], 0UL);
    ASSERT_EQ((int64_t)K, TotalInflightOf(&lb));

    DestroyServers(ids);
}

TEST_F(CHBoundedLoadTest, overflow_walks_to_ring_successor) {
    const size_t N = 8;
    const size_t K = 200;
    std::vector<brpc::ServerId> ids;
    brpc::policy::ConsistentHashingBoundedLoadBalancer lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    brpc::policy::ConsistentHashingLoadBalancer classic_lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    std::map<brpc::SocketId, brpc::ServerId> id_map;
    for (size_t i = 0; i < N; ++i) {
        char addr[32];
        snprintf(addr, sizeof(addr), "192.168.1.%d:8080", (int)i);
        ids.push_back(CreateServer(addr));
        id_map[ids.back().id] = ids.back();
    }
    ASSERT_EQ(N, lb.AddServersInBatch(ids));
    ASSERT_EQ(N, classic_lb.AddServersInBatch(ids));

    const std::string hot_key = "another_hot_key";
    brpc::LoadBalancer::SelectIn in =
        MakeInput(brpc::policy::MurmurHash32(hot_key.data(), hot_key.size()));

    // First-use order of servers under overflow.
    std::vector<brpc::SocketId> bounded_order;
    std::map<brpc::SocketId, size_t> seen;
    for (size_t i = 0; i < K; ++i) {
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectOut out(&ptr);
        ASSERT_EQ(0, lb.SelectServer(in, &out));
        if (++seen[ptr->id()] == 1) {
            bounded_order.push_back(ptr->id());
        }
    }
    ASSERT_GT(bounded_order.size(), 1UL);

    // Expected overflow targets are the ring successors: what classic CH
    // picks as servers are removed one by one.
    for (size_t i = 0; i < bounded_order.size(); ++i) {
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectOut out(&ptr);
        ASSERT_EQ(0, classic_lb.SelectServer(in, &out));
        ASSERT_EQ(bounded_order[i], ptr->id()) << "i=" << i;
        ASSERT_TRUE(classic_lb.RemoveServer(id_map[ptr->id()]));
    }

    DestroyServers(ids);
}

TEST_F(CHBoundedLoadTest, feedback_decrements_load) {
    const size_t N = 3;
    std::vector<brpc::ServerId> ids;
    brpc::policy::ConsistentHashingBoundedLoadBalancer lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    for (size_t i = 0; i < N; ++i) {
        char addr[32];
        snprintf(addr, sizeof(addr), "192.168.1.%d:8080", (int)i);
        ids.push_back(CreateServer(addr));
    }
    ASSERT_EQ(N, lb.AddServersInBatch(ids));

    const std::string key = "some_key";
    brpc::LoadBalancer::SelectIn in =
        MakeInput(brpc::policy::MurmurHash32(key.data(), key.size()));

    // With capacity ceil(1.25 * 1 / 3) = 1 an idle ring always routes the
    // key to its primary server, so select+feedback staying on one server
    // for many rounds proves the counters return to zero every round.
    brpc::SocketId primary = 0;
    for (size_t i = 0; i < 100; ++i) {
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectOut out(&ptr);
        ASSERT_EQ(0, lb.SelectServer(in, &out));
        ASSERT_TRUE(out.need_feedback);
        if (i == 0) {
            primary = ptr->id();
        } else {
            ASSERT_EQ(primary, ptr->id()) << "i=" << i;
        }
        const brpc::LoadBalancer::CallInfo info = { 0, ptr->id(), 0, nullptr };
        lb.Feedback(info);
    }
    ASSERT_EQ(0, TotalInflightOf(&lb));

    // Saturate the primary without feedback: the key overflows...
    std::vector<brpc::SocketId> outstanding;
    bool overflowed = false;
    for (size_t i = 0; i < 20; ++i) {
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectOut out(&ptr);
        ASSERT_EQ(0, lb.SelectServer(in, &out));
        outstanding.push_back(ptr->id());
        overflowed |= (ptr->id() != primary);
    }
    ASSERT_TRUE(overflowed);
    // ...and returns to the primary once the load drains.
    for (size_t i = 0; i < outstanding.size(); ++i) {
        const brpc::LoadBalancer::CallInfo info = { 0, outstanding[i], 0, nullptr };
        lb.Feedback(info);
    }
    ASSERT_EQ(0, TotalInflightOf(&lb));
    brpc::SocketUniquePtr ptr;
    brpc::LoadBalancer::SelectOut out(&ptr);
    ASSERT_EQ(0, lb.SelectServer(in, &out));
    ASSERT_EQ(primary, ptr->id());
    const brpc::LoadBalancer::CallInfo info = { 0, ptr->id(), 0, nullptr };
    lb.Feedback(info);

    DestroyServers(ids);
}

TEST_F(CHBoundedLoadTest, feedback_after_server_removed_keeps_total_consistent) {
    const size_t N = 3;
    std::vector<brpc::ServerId> ids;
    brpc::policy::ConsistentHashingBoundedLoadBalancer lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    for (size_t i = 0; i < N; ++i) {
        char addr[32];
        snprintf(addr, sizeof(addr), "192.168.1.%d:8080", (int)i);
        ids.push_back(CreateServer(addr));
    }
    ASSERT_EQ(N, lb.AddServersInBatch(ids));

    const std::string key = "some_key";
    brpc::LoadBalancer::SelectIn in =
        MakeInput(brpc::policy::MurmurHash32(key.data(), key.size()));
    brpc::SocketUniquePtr ptr;
    brpc::LoadBalancer::SelectOut out(&ptr);
    ASSERT_EQ(0, lb.SelectServer(in, &out));
    ASSERT_EQ(1, TotalInflightOf(&lb));

    brpc::ServerId selected;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i].id == ptr->id()) {
            selected = ids[i];
        }
    }
    ASSERT_TRUE(lb.RemoveServer(selected));
    const brpc::LoadBalancer::CallInfo info = { 0, ptr->id(), 0, nullptr };
    lb.Feedback(info);
    ASSERT_EQ(0, TotalInflightOf(&lb));
    ASSERT_TRUE(lb.AddServer(selected));

    DestroyServers(ids);
}

TEST_F(CHBoundedLoadTest, no_accounting_without_changable_weights) {
    const size_t N = 4;
    std::vector<brpc::ServerId> ids;
    brpc::policy::ConsistentHashingBoundedLoadBalancer lb(
        brpc::policy::CONS_HASH_LB_MURMUR3);
    for (size_t i = 0; i < N; ++i) {
        char addr[32];
        snprintf(addr, sizeof(addr), "192.168.1.%d:8080", (int)i);
        ids.push_back(CreateServer(addr));
    }
    ASSERT_EQ(N, lb.AddServersInBatch(ids));

    const std::string key = "some_key";
    brpc::LoadBalancer::SelectIn in = MakeInput(
        brpc::policy::MurmurHash32(key.data(), key.size()), false);
    brpc::SocketId first = 0;
    for (size_t i = 0; i < 50; ++i) {
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectOut out(&ptr);
        ASSERT_EQ(0, lb.SelectServer(in, &out));
        ASSERT_FALSE(out.need_feedback);
        if (i == 0) {
            first = ptr->id();
        } else {
            ASSERT_EQ(first, ptr->id());
        }
    }
    ASSERT_EQ(0, TotalInflightOf(&lb));

    DestroyServers(ids);
}

} // namespace
