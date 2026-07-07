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

#include <cstdlib>
#include <map>
#include <sstream>
#include <vector>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>
#include "butil/macros.h"
#include "butil/time.h"
#include "brpc/socket.h"
#include "brpc/controller.h"
#include "brpc/excluded_servers.h"
#include "brpc/policy/p2c_ewma_load_balancer.h"

namespace brpc {
namespace policy {
DECLARE_int64(p2c_max_punish_ms);
}
}

namespace {

butil::atomic<size_t> nrecycle(0);
class SaveRecycle : public brpc::SocketUser {
    void BeforeRecycle(brpc::Socket* s) {
        nrecycle.fetch_add(1, butil::memory_order_relaxed);
        delete this;
    }
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

// Report a call that took `latency_us' back to the load balancer.
void FeedbackLatency(brpc::LoadBalancer* lb, brpc::SocketId server_id,
                     int64_t latency_us, int error_code = 0,
                     const brpc::Controller* cntl = NULL) {
    brpc::LoadBalancer::CallInfo info;
    info.begin_time_us = butil::gettimeofday_us() - latency_us;
    info.server_id = server_id;
    info.error_code = error_code;
    info.controller = cntl;
    lb->Feedback(info);
}

class P2CEwmaLoadBalancerTest : public ::testing::Test {
protected:
    void SetUp() override {
        _lb = new brpc::policy::P2CEwmaLoadBalancer;
    }
    void TearDown() override {
        _lb->Destroy();
    }

    int Select(brpc::SocketUniquePtr* ptr, bool* need_feedback = NULL,
               const brpc::ExcludedServers* excluded = NULL) {
        brpc::LoadBalancer::SelectIn in = {
            butil::gettimeofday_us(), true, false, 0u, excluded };
        brpc::LoadBalancer::SelectOut out(ptr);
        const int rc = _lb->SelectServer(in, &out);
        if (need_feedback != NULL) {
            *need_feedback = out.need_feedback;
        }
        return rc;
    }

    brpc::policy::P2CEwmaLoadBalancer* _lb;
};

TEST_F(P2CEwmaLoadBalancerTest, add_remove_servers) {
    brpc::SocketUniquePtr ptr;
    ASSERT_EQ(ENODATA, Select(&ptr));

    std::vector<brpc::ServerId> ids;
    ids.push_back(CreateServer("127.0.0.1:7777"));
    ids.push_back(CreateServer("127.0.0.1:7778"));
    ids.push_back(CreateServer("127.0.0.1:7779"));

    ASSERT_TRUE(_lb->AddServer(ids[0]));
    // Duplicated server is rejected.
    ASSERT_FALSE(_lb->AddServer(ids[0]));
    ASSERT_EQ(2u, _lb->AddServersInBatch(
        std::vector<brpc::ServerId>(ids.begin() + 1, ids.end())));

    ASSERT_EQ(0, Select(&ptr));

    ASSERT_TRUE(_lb->RemoveServer(ids[0]));
    ASSERT_FALSE(_lb->RemoveServer(ids[0]));
    ASSERT_EQ(2u, _lb->RemoveServersInBatch(
        std::vector<brpc::ServerId>(ids.begin() + 1, ids.end())));
    ASSERT_EQ(ENODATA, Select(&ptr));
}

TEST_F(P2CEwmaLoadBalancerTest, single_server) {
    const brpc::ServerId id = CreateServer("127.0.0.1:7780");
    ASSERT_TRUE(_lb->AddServer(id));
    for (int i = 0; i < 10; ++i) {
        brpc::SocketUniquePtr ptr;
        bool need_feedback = false;
        ASSERT_EQ(0, Select(&ptr, &need_feedback));
        ASSERT_EQ(id.id, ptr->id());
        ASSERT_TRUE(need_feedback);
        FeedbackLatency(_lb, id.id, 1000);
    }
}

TEST_F(P2CEwmaLoadBalancerTest, prefers_lower_latency) {
    const brpc::ServerId fast = CreateServer("127.0.0.1:7781");
    const brpc::ServerId slow = CreateServer("127.0.0.1:7782");
    ASSERT_TRUE(_lb->AddServer(fast));
    ASSERT_TRUE(_lb->AddServer(slow));

    // Servers respond with their characteristic latency; both get observed
    // within the first rounds because unobserved servers score best.
    std::map<brpc::SocketId, size_t> counts;
    const int kRounds = 1000;
    for (int i = 0; i < kRounds; ++i) {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, Select(&ptr));
        ++counts[ptr->id()];
        // Selected server responds with its own characteristic latency.
        FeedbackLatency(_lb, ptr->id(), ptr->id() == fast.id ? 1000 : 50000);
    }
    LOG(INFO) << "fast=" << counts[fast.id] << " slow=" << counts[slow.id];
    // The fast server should receive almost all traffic.
    ASSERT_GE(counts[fast.id], (size_t)(0.9 * kRounds));
}

TEST_F(P2CEwmaLoadBalancerTest, sheds_degraded_server) {
    const brpc::ServerId a = CreateServer("127.0.0.1:7783");
    const brpc::ServerId b = CreateServer("127.0.0.1:7784");
    ASSERT_TRUE(_lb->AddServer(a));
    ASSERT_TRUE(_lb->AddServer(b));
    // Warm up with `b' slightly faster so it is the preferred server.
    for (int i = 0; i < 20; ++i) {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, Select(&ptr));
        FeedbackLatency(_lb, ptr->id(), ptr->id() == a.id ? 1000 : 500);
    }

    // `b' degrades: its first slow response must shift traffic to `a'
    // immediately(peak-sensitivity), long before an averaging window would.
    std::map<brpc::SocketId, size_t> counts;
    for (int i = 0; i < 100; ++i) {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, Select(&ptr));
        ++counts[ptr->id()];
        FeedbackLatency(_lb, ptr->id(), ptr->id() == a.id ? 1000 : 100000);
    }
    ASSERT_GE(counts[a.id], 90u);
}

TEST_F(P2CEwmaLoadBalancerTest, inflight_breaks_ties) {
    const brpc::ServerId a = CreateServer("127.0.0.1:7785");
    const brpc::ServerId b = CreateServer("127.0.0.1:7786");
    ASSERT_TRUE(_lb->AddServer(a));
    ASSERT_TRUE(_lb->AddServer(b));

    // With no latency observations, scores only differ by in-flight count,
    // so two selections without feedback must go to different servers.
    brpc::SocketUniquePtr ptr1;
    brpc::SocketUniquePtr ptr2;
    ASSERT_EQ(0, Select(&ptr1));
    ASSERT_EQ(0, Select(&ptr2));
    ASSERT_NE(ptr1->id(), ptr2->id());

    // After feedback(decrementing in-flight), both are tied again and the
    // third selection must not crash or fail.
    FeedbackLatency(_lb, ptr1->id(), 1000);
    FeedbackLatency(_lb, ptr2->id(), 1000);
    brpc::SocketUniquePtr ptr3;
    ASSERT_EQ(0, Select(&ptr3));
}

TEST_F(P2CEwmaLoadBalancerTest, weighted_split_by_inflight) {
    // With equal latency, steady-state in-flight counts converge to the
    // 1:2:4 weight ratio because the score is (inflight+1)/weight.
    const brpc::ServerId w1 = CreateServer("127.0.0.1:7787", "1");
    const brpc::ServerId w2 = CreateServer("127.0.0.1:7788", "2");
    const brpc::ServerId w4 = CreateServer("127.0.0.1:7789", "4");
    brpc::policy::P2CEwmaLoadBalancer* lb =
        _lb->New(butil::StringPiece("choices=3"));
    ASSERT_TRUE(lb != NULL);
    ASSERT_TRUE(lb->AddServer(w1));
    ASSERT_TRUE(lb->AddServer(w2));
    ASSERT_TRUE(lb->AddServer(w4));

    std::map<brpc::SocketId, size_t> counts;
    const int kRounds = 700;
    for (int i = 0; i < kRounds; ++i) {
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectIn in = {
            butil::gettimeofday_us(), true, false, 0u, NULL };
        brpc::LoadBalancer::SelectOut out(&ptr);
        ASSERT_EQ(0, lb->SelectServer(in, &out));
        ++counts[ptr->id()];
        // No feedback: requests stay in flight.
    }
    LOG(INFO) << "w1=" << counts[w1.id] << " w2=" << counts[w2.id]
              << " w4=" << counts[w4.id];
    const double share1 = counts[w1.id] / (double)kRounds;
    const double share2 = counts[w2.id] / (double)kRounds;
    const double share4 = counts[w4.id] / (double)kRounds;
    ASSERT_NEAR(share1, 1.0 / 7, 0.05);
    ASSERT_NEAR(share2, 2.0 / 7, 0.05);
    ASSERT_NEAR(share4, 4.0 / 7, 0.05);
    lb->Destroy();
}

TEST_F(P2CEwmaLoadBalancerTest, error_feedback_punishes_server) {
    const brpc::ServerId good = CreateServer("127.0.0.1:7790");
    const brpc::ServerId bad = CreateServer("127.0.0.1:7791");
    ASSERT_TRUE(_lb->AddServer(good));
    ASSERT_TRUE(_lb->AddServer(bad));
    // Warm up with `bad' slightly faster so it is the preferred server.
    for (int i = 0; i < 20; ++i) {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, Select(&ptr));
        FeedbackLatency(_lb, ptr->id(), ptr->id() == good.id ? 1000 : 500);
    }

    // `bad' starts failing fast(1ms), but failures are punished with at
    // least the RPC timeout so it keeps losing selections.
    brpc::Controller cntl;
    cntl.set_timeout_ms(100);
    std::map<brpc::SocketId, size_t> counts;
    for (int i = 0; i < 100; ++i) {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, Select(&ptr));
        ++counts[ptr->id()];
        if (ptr->id() == good.id) {
            FeedbackLatency(_lb, good.id, 1000);
        } else {
            FeedbackLatency(_lb, bad.id, 1000, ETIMEDOUT, &cntl);
        }
    }
    ASSERT_GE(counts[good.id], 90u);
}

TEST_F(P2CEwmaLoadBalancerTest, excluded_servers) {
    const brpc::ServerId a = CreateServer("127.0.0.1:7792");
    const brpc::ServerId b = CreateServer("127.0.0.1:7793");
    ASSERT_TRUE(_lb->AddServer(a));
    ASSERT_TRUE(_lb->AddServer(b));

    brpc::ExcludedServers* excluded = brpc::ExcludedServers::Create(2);
    excluded->Add(a.id);
    for (int i = 0; i < 20; ++i) {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, Select(&ptr, NULL, excluded));
        ASSERT_EQ(b.id, ptr->id());
        FeedbackLatency(_lb, b.id, 1000);
    }
    // All servers excluded: still take the last chance instead of failing.
    excluded->Add(b.id);
    brpc::SocketUniquePtr ptr;
    ASSERT_EQ(0, Select(&ptr, NULL, excluded));
    brpc::ExcludedServers::Destroy(excluded);
}

TEST_F(P2CEwmaLoadBalancerTest, invalid_parameters) {
    brpc::LoadBalancer* lb = _lb->New(butil::StringPiece(""));
    ASSERT_TRUE(lb != NULL);
    lb->Destroy();
    lb = _lb->New(butil::StringPiece("choices=4 tau_ms=5000"));
    ASSERT_TRUE(lb != NULL);
    lb->Destroy();
    ASSERT_TRUE(_lb->New(butil::StringPiece("choices=1")) == NULL);
    ASSERT_TRUE(_lb->New(butil::StringPiece("choices=abc")) == NULL);
    ASSERT_TRUE(_lb->New(butil::StringPiece("tau_ms=0")) == NULL);
    ASSERT_TRUE(_lb->New(butil::StringPiece("unknown=1")) == NULL);
}

struct ChurnArg {
    brpc::policy::P2CEwmaLoadBalancer* lb;
    butil::atomic<bool> stop;
    butil::atomic<size_t> nselected;
};

void* SelectAndFeedback(void* void_arg) {
    ChurnArg* arg = static_cast<ChurnArg*>(void_arg);
    while (!arg->stop.load(butil::memory_order_relaxed)) {
        brpc::SocketUniquePtr ptr;
        brpc::LoadBalancer::SelectIn in = {
            butil::gettimeofday_us(), true, false, 0u, NULL };
        brpc::LoadBalancer::SelectOut out(&ptr);
        if (arg->lb->SelectServer(in, &out) == 0) {
            arg->nselected.fetch_add(1, butil::memory_order_relaxed);
            if (out.need_feedback) {
                FeedbackLatency(arg->lb, ptr->id(), 1000);
            }
        }
    }
    return NULL;
}

TEST_F(P2CEwmaLoadBalancerTest, concurrent_select_with_churn) {
    std::vector<brpc::ServerId> ids;
    for (int i = 0; i < 8; ++i) {
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", 7800 + i);
        ids.push_back(CreateServer(addr));
        ASSERT_TRUE(_lb->AddServer(ids.back()));
    }

    ChurnArg arg;
    arg.lb = _lb;
    arg.stop.store(false);
    arg.nselected.store(0);
    pthread_t threads[4];
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        ASSERT_EQ(0, pthread_create(
            &threads[i], NULL, SelectAndFeedback, &arg));
    }
    // Churn membership while selections are running.
    const int64_t stop_at_us = butil::gettimeofday_us() + 1000000L;
    while (butil::gettimeofday_us() < stop_at_us) {
        ASSERT_TRUE(_lb->RemoveServer(ids[0]));
        ASSERT_TRUE(_lb->AddServer(ids[0]));
    }
    arg.stop.store(true);
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        ASSERT_EQ(0, pthread_join(threads[i], NULL));
    }
    LOG(INFO) << "selected " << arg.nselected.load() << " times";
    ASSERT_GT(arg.nselected.load(), 0u);
}

TEST_F(P2CEwmaLoadBalancerTest, error_punish_is_capped) {
    const brpc::ServerId id = CreateServer("127.0.0.1:7900");
    ASSERT_TRUE(_lb->AddServer(id));
    brpc::Controller cntl;
    cntl.set_timeout_ms(100);
    // Persistent failures double the punished EWMA per sample; without the
    // cap 64 rounds would push it past 2^64.
    for (int i = 0; i < 64; ++i) {
        brpc::SocketUniquePtr ptr;
        ASSERT_EQ(0, Select(&ptr));
        FeedbackLatency(_lb, id.id, 1000, ETIMEDOUT, &cntl);
    }
    std::ostringstream os;
    brpc::DescribeOptions options;
    options.verbose = true;
    _lb->Describe(os, options);
    const std::string desc = os.str();
    const size_t pos = desc.find("ewma_us=");
    ASSERT_NE(std::string::npos, pos) << desc;
    const int64_t ewma_us = strtoll(desc.c_str() + pos + 8, NULL, 10);
    ASSERT_LE(ewma_us, brpc::policy::FLAGS_p2c_max_punish_ms * 1000L) << desc;
}

struct FeedbackBenchArg {
    brpc::policy::P2CEwmaLoadBalancer* lb;
    brpc::SocketId server_id;
    butil::atomic<bool> stop;
    butil::atomic<size_t> nfeedback;
};

void* FeedbackHammer(void* void_arg) {
    FeedbackBenchArg* arg = static_cast<FeedbackBenchArg*>(void_arg);
    size_t n = 0;
    while (!arg->stop.load(butil::memory_order_relaxed)) {
        FeedbackLatency(arg->lb, arg->server_id, 1000);
        ++n;
    }
    arg->nfeedback.fetch_add(n, butil::memory_order_relaxed);
    return NULL;
}

TEST_F(P2CEwmaLoadBalancerTest, feedback_lock_overhead) {
    // All threads feed back to a single server so update_mutex sees
    // worst-case contention.
    const brpc::ServerId id = CreateServer("127.0.0.1:7901");
    ASSERT_TRUE(_lb->AddServer(id));
    for (size_t nthread = 1; nthread <= 4; nthread *= 2) {
        FeedbackBenchArg arg;
        arg.lb = _lb;
        arg.server_id = id.id;
        arg.stop.store(false);
        arg.nfeedback.store(0);
        pthread_t threads[4];
        const int64_t begin_us = butil::gettimeofday_us();
        for (size_t i = 0; i < nthread; ++i) {
            ASSERT_EQ(0, pthread_create(
                &threads[i], NULL, FeedbackHammer, &arg));
        }
        usleep(500 * 1000);
        arg.stop.store(true);
        for (size_t i = 0; i < nthread; ++i) {
            ASSERT_EQ(0, pthread_join(threads[i], NULL));
        }
        const int64_t elapsed_us = butil::gettimeofday_us() - begin_us;
        const size_t n = arg.nfeedback.load();
        ASSERT_GT(n, 0u);
        LOG(INFO) << "Feedback on 1 shared server, " << nthread
                  << " thread(s): " << (elapsed_us * 1000L * nthread) / n
                  << "ns/op (" << n << " ops in " << elapsed_us / 1000 << "ms)";
    }
}

} // namespace
