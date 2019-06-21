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

#include <pthread.h>                                // pthread_*

#include <cstddef>
#include <memory>
#include <iostream>

#include "butil/time.h"
#include "butil/macros.h"

#include "bvar/detail/agent_group.h"
#include "butil/atomicops.h"

#include <gtest/gtest.h>

namespace {
using namespace bvar::detail;

struct Add {
    uint64_t operator()(const uint64_t lhs, const uint64_t rhs) const {
        return lhs + rhs;
    }
};

const size_t OPS_PER_THREAD = 2000000;

class AgentGroupTest : public testing::Test {
protected:
    typedef butil::atomic<uint64_t>                             agent_type;
    void SetUp() {}
    void TearDown() {}

    static void *thread_counter(void *arg) {
        int id = (int)((long)arg);
        agent_type *item = AgentGroup<agent_type>::get_or_create_tls_agent(id);
        if (item == NULL) {
            EXPECT_TRUE(false);
            return NULL;
        }
        butil::Timer timer;
        timer.start();
        for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
            agent_type *element = AgentGroup<agent_type>::get_or_create_tls_agent(id);
            uint64_t old_value = element->load(butil::memory_order_relaxed);
            uint64_t new_value;
            do {
                new_value = old_value + 2;
            } while (__builtin_expect(!element->compare_exchange_weak(old_value, new_value, 
                                                     butil::memory_order_relaxed,
                                                     butil::memory_order_relaxed), 0));
            //element->store(element->load(butil::memory_order_relaxed) + 2, 
            //               butil::memory_order_relaxed);
            //element->fetch_add(2, butil::memory_order_relaxed);
        }
        timer.stop();
        return (void *)(timer.n_elapsed());
    }
};

TEST_F(AgentGroupTest, test_sanity) {
    int id = AgentGroup<agent_type>::create_new_agent();
    ASSERT_TRUE(id >= 0) << id;
    agent_type *element = AgentGroup<agent_type>::get_or_create_tls_agent(id);
    ASSERT_TRUE(element != NULL);
    AgentGroup<agent_type>::destroy_agent(id);
}

butil::atomic<uint64_t> g_counter(0);

void *global_add(void *) {
    butil::Timer timer;
    timer.start();
    for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
        g_counter.fetch_add(2, butil::memory_order_relaxed);
    }
    timer.stop();
    return (void *)(timer.n_elapsed());
}

TEST_F(AgentGroupTest, test_perf) {
    size_t loops = 100000;
    size_t id_num = 512;
    int ids[id_num];
    for (size_t i = 0; i < id_num; ++i) {
        ids[i] = AgentGroup<agent_type>::create_new_agent();
        ASSERT_TRUE(ids[i] >= 0);
    }
    butil::Timer timer;
    timer.start();
    for (size_t i = 0; i < loops; ++i) {
        for (size_t j = 0; j < id_num; ++j) {
            agent_type *agent =
                AgentGroup<agent_type>::get_or_create_tls_agent(ids[j]);
            ASSERT_TRUE(agent != NULL) << ids[j];
        }
    }
    timer.stop();
    LOG(INFO) << "It takes " << timer.n_elapsed() / (loops * id_num)
              << " ns to get tls agent for " << id_num << " agents";
    for (size_t i = 0; i < id_num; ++i) {
        AgentGroup<agent_type>::destroy_agent(ids[i]);
    }

}

TEST_F(AgentGroupTest, test_all_perf) {
    long id = AgentGroup<agent_type>::create_new_agent();
    ASSERT_TRUE(id >= 0) << id;
    pthread_t threads[24];
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_create(&threads[i], NULL, &thread_counter, (void *)id);
    }
    long totol_time = 0;
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        void *ret; 
        pthread_join(threads[i], &ret);
        totol_time += (long)ret;
    }
    LOG(INFO) << "ThreadAgent takes " 
              << totol_time / (OPS_PER_THREAD * ARRAY_SIZE(threads));
    totol_time = 0;
    g_counter.store(0, butil::memory_order_relaxed);
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        pthread_create(&threads[i], NULL, global_add, (void *)id);
    }
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        void *ret; 
        pthread_join(threads[i], &ret);
        totol_time += (long)ret;
    }
    LOG(INFO) << "Global Atomic takes " 
              << totol_time / (OPS_PER_THREAD * ARRAY_SIZE(threads));
    AgentGroup<agent_type>::destroy_agent(id);
    //sleep(1000);
}
} // namespace
