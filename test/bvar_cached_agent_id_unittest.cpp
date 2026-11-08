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

#include <algorithm>
#include <limits>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "bvar/recorder.h"
#include "bvar/reducer.h"

namespace {

struct SelectExtremum {
    explicit SelectExtremum(bool select_min) : minimum(select_min) {}
    void operator()(int& lhs, int rhs) const {
        lhs = minimum ? std::min(lhs, rhs) : std::max(lhs, rhs);
    }
    bool minimum;
};

TEST(CachedAgentIdTest, unbound_slot_and_stateful_operator) {
    typedef bvar::Reducer<int, SelectExtremum> Reducer;
    typedef Reducer::combiner_type Combiner;
    Reducer first(std::numeric_limits<int>::max(), SelectExtremum(true));
    Reducer second(std::numeric_limits<int>::lowest(), SelectExtremum(false));
    const bvar::detail::AgentId id = first.share_combiner()->agent_id();
    ASSERT_EQ(nullptr, Combiner::get_bound_tls_agent(-1));
    ASSERT_NE(nullptr, Combiner::AgentGroup::get_or_create_tls_agent(id));
    ASSERT_EQ(nullptr, Combiner::get_bound_tls_agent(id));
    first << 12 << 42;
    second << 12 << 42;
    ASSERT_NE(nullptr, Combiner::get_bound_tls_agent(id));
    ASSERT_EQ(12, first.get_value());
    ASSERT_EQ(42, second.get_value());
    ASSERT_EQ(12, first.reset());
    first << 3;
    ASSERT_EQ(3, first.get_value());
}

TEST(CachedAgentIdTest, reducer_reuses_expired_slot) {
    typedef bvar::Reducer<int, SelectExtremum> Reducer;
    typedef Reducer::combiner_type Combiner;
    bvar::detail::AgentId id;
    {
        Reducer old(std::numeric_limits<int>::lowest(), SelectExtremum(false));
        id = old.share_combiner()->agent_id();
        old << 80;
        ASSERT_EQ(80, old.get_value());
    }
    Reducer fresh(std::numeric_limits<int>::max(), SelectExtremum(true));
    ASSERT_EQ(id, fresh.share_combiner()->agent_id());
    ASSERT_NE(nullptr, Combiner::AgentGroup::get_tls_agent(id));
    ASSERT_EQ(nullptr, Combiner::get_bound_tls_agent(id));
    fresh << 100;
    ASSERT_EQ(100, fresh.get_value());
    fresh << -20;
    ASSERT_EQ(-20, fresh.get_value());
}

TEST(CachedAgentIdTest, retained_reducer_combiner_keeps_id_alive) {
    typedef bvar::Reducer<int, SelectExtremum> Reducer;
    typedef Reducer::combiner_type Combiner;
    Combiner::self_shared_type held;
    bvar::detail::AgentId id;
    {
        Reducer old(std::numeric_limits<int>::lowest(), SelectExtremum(false));
        held = old.share_combiner();
        id = held->agent_id();
        old << 80;
    }
    Reducer other(std::numeric_limits<int>::max(), SelectExtremum(true));
    ASSERT_NE(id, other.share_combiner()->agent_id());
    other << 5;
    ASSERT_EQ(80, held->combine_agents());
    ASSERT_EQ(5, other.get_value());
    held.reset();
    Reducer fresh(std::numeric_limits<int>::lowest(), SelectExtremum(false));
    ASSERT_EQ(id, fresh.share_combiner()->agent_id());
    fresh << 6;
    ASSERT_EQ(6, fresh.get_value());
}

#if !WITH_BABYLON_COUNTER
TEST(CachedAgentIdTest, recorder_reuses_slot_after_last_owner_releases) {
    typedef bvar::IntRecorder::combiner_type Combiner;
    Combiner::self_shared_type held;
    bvar::detail::AgentId id;
    {
        bvar::IntRecorder old;
        held = old.share_combiner();
        id = held->agent_id();
        old << 80;
    }
    bvar::IntRecorder other;
    ASSERT_NE(id, other.share_combiner()->agent_id());
    other << 5;
    ASSERT_EQ(80, held->combine_agents().sum);
    ASSERT_EQ(1, held->combine_agents().num);
    held.reset();
    bvar::IntRecorder fresh;
    ASSERT_EQ(id, fresh.share_combiner()->agent_id());
    ASSERT_NE(nullptr, Combiner::AgentGroup::get_tls_agent(id));
    ASSERT_EQ(nullptr, Combiner::get_bound_tls_agent(id));
    fresh << 6 << 7;
    ASSERT_EQ(13, fresh.get_value().sum);
    ASSERT_EQ(2, fresh.get_value().num);
    ASSERT_EQ(13, fresh.reset().sum);
    fresh << -4;
    ASSERT_EQ(-4, fresh.get_value().sum);
    ASSERT_EQ(1, fresh.get_value().num);
}

TEST(CachedAgentIdTest, fresh_threads_initialize_and_commit_on_exit) {
    typedef bvar::IntRecorder::combiner_type RecorderCombiner;
    typedef bvar::Adder<int64_t>::combiner_type AdderCombiner;
    bvar::IntRecorder recorder;
    bvar::Adder<int64_t> adder;
    const auto recorder_id = recorder.share_combiner()->agent_id();
    const auto adder_id = adder.share_combiner()->agent_id();
    std::thread workers[8];
    for (auto& worker : workers) {
        worker = std::thread([&] {
            EXPECT_EQ(nullptr,
                      RecorderCombiner::get_bound_tls_agent(recorder_id));
            EXPECT_EQ(nullptr,
                      AdderCombiner::get_bound_tls_agent(adder_id));
            for (int i = 0; i < 10000; ++i) {
                recorder << 3;
                adder << 2;
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    ASSERT_EQ(80000, recorder.get_value().num);
    ASSERT_EQ(240000, recorder.get_value().sum);
    ASSERT_EQ(160000, adder.get_value());
}
#endif  // !WITH_BABYLON_COUNTER

}  // namespace
