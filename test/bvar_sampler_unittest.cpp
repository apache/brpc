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

#include <limits>                           //std::numeric_limits
#include "bvar/detail/sampler.h"
#include "butil/time.h"
#include "butil/logging.h"
#include <gtest/gtest.h>

namespace {

TEST(SamplerTest, linked_list) {
    butil::LinkNode<bvar::detail::Sampler> n1, n2;
    n1.InsertBeforeAsList(&n2);
    ASSERT_EQ(n1.next(), &n2);
    ASSERT_EQ(n1.previous(), &n2);
    ASSERT_EQ(n2.next(), &n1);
    ASSERT_EQ(n2.previous(), &n1);

    butil::LinkNode<bvar::detail::Sampler> n3, n4;
    n3.InsertBeforeAsList(&n4);
    ASSERT_EQ(n3.next(), &n4);
    ASSERT_EQ(n3.previous(), &n4);
    ASSERT_EQ(n4.next(), &n3);
    ASSERT_EQ(n4.previous(), &n3);

    n1.InsertBeforeAsList(&n3);
    ASSERT_EQ(n1.next(), &n2);
    ASSERT_EQ(n2.next(), &n3);
    ASSERT_EQ(n3.next(), &n4);
    ASSERT_EQ(n4.next(), &n1);
    ASSERT_EQ(&n1, n2.previous());
    ASSERT_EQ(&n2, n3.previous());
    ASSERT_EQ(&n3, n4.previous());
    ASSERT_EQ(&n4, n1.previous());
}

class DebugSampler : public bvar::detail::Sampler {
public:
    DebugSampler() : _ncalled(0) {}
    ~DebugSampler() {
        ++_s_ndestroy;
    }
    void take_sample() {
        ++_ncalled;
    }
    int called_count() const { return _ncalled; }
private:
    int _ncalled;
    static int _s_ndestroy;
};
int DebugSampler::_s_ndestroy = 0;

TEST(SamplerTest, single_threaded) {
#if !BRPC_WITH_GLOG
    logging::StringSink log_str;
    logging::LogSink* old_sink = logging::SetLogSink(&log_str);
#endif
    const int N = 100;
    DebugSampler* s[N];
    for (int i = 0; i < N; ++i) {
        s[i] = new DebugSampler;
        s[i]->schedule();
    }
    usleep(1010000);
    for (int i = 0; i < N; ++i) {
        // LE: called once every second, may be called more than once
        ASSERT_LE(1, s[i]->called_count()) << "i=" << i;
    }
    EXPECT_EQ(0, DebugSampler::_s_ndestroy);
    for (int i = 0; i < N; ++i) {
        s[i]->destroy();
    }
    usleep(1010000);
    EXPECT_EQ(N, DebugSampler::_s_ndestroy);
#if !BRPC_WITH_GLOG
    ASSERT_EQ(&log_str, logging::SetLogSink(old_sink));
    if (log_str.find("Removed ") != std::string::npos) {
        ASSERT_NE(std::string::npos, log_str.find("Removed 0, sampled 100"));
        ASSERT_NE(std::string::npos, log_str.find("Removed 100, sampled 0"));
    }
#endif
}

static void* check(void*) {
    const int N = 100;
    DebugSampler* s[N];
    for (int i = 0; i < N; ++i) {
        s[i] = new DebugSampler;
        s[i]->schedule();
    }
    usleep(1010000);
    for (int i = 0; i < N; ++i) {
        EXPECT_LE(1, s[i]->called_count()) << "i=" << i;
    }
    for (int i = 0; i < N; ++i) {
        s[i]->destroy();
    }
    return NULL;
}

TEST(SamplerTest, multi_threaded) {
#if !BRPC_WITH_GLOG
    logging::StringSink log_str;
    logging::LogSink* old_sink = logging::SetLogSink(&log_str);
#endif
    pthread_t th[10];
    DebugSampler::_s_ndestroy = 0;
    for (size_t i = 0; i < arraysize(th); ++i) {
        ASSERT_EQ(0, pthread_create(&th[i], NULL, check, NULL));
    }
    for (size_t i = 0; i < arraysize(th); ++i) {
        ASSERT_EQ(0, pthread_join(th[i], NULL));
    }
    sleep(1);
    EXPECT_EQ(100 * arraysize(th), (size_t)DebugSampler::_s_ndestroy);
#if !BRPC_WITH_GLOG
    ASSERT_EQ(&log_str, logging::SetLogSink(old_sink));
    if (log_str.find("Removed ") != std::string::npos) {
        ASSERT_NE(std::string::npos, log_str.find("Removed 0, sampled 1000"));
        ASSERT_NE(std::string::npos, log_str.find("Removed 1000, sampled 0"));
    }
#endif
}
} // namespace
