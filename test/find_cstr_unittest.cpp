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
#include <gtest/gtest.h>
#include "butil/find_cstr.h"
#include "butil/time.h"
#include "butil/logging.h"

namespace {
class FindCstrTest : public ::testing::Test{
protected:
    FindCstrTest(){
    };
    virtual ~FindCstrTest(){};
    virtual void SetUp() {
        srand(time(0));
    };
    virtual void TearDown() {
    };
};

TEST_F(FindCstrTest, sanity) {
    std::map<std::string, int> t1;
    ASSERT_EQ(0u, t1.size());
    ASSERT_TRUE(t1.empty());
    for (std::map<std::string, int>::iterator
             it = t1.begin(); it != t1.end(); ++it) {
        // nothing.
    }
    t1["hello"] = 0xdeadbeef;
    std::map<std::string, int>::iterator it = butil::find_cstr(t1, "hello");
    ASSERT_TRUE(it != t1.end());
    ASSERT_EQ("hello", it->first);
    ASSERT_EQ(0xdeadbeef, (unsigned int)it->second);
}

TEST_F(FindCstrTest, perf) {
    std::map<std::string, int> t1;
    std::string key_buf;
    std::vector<size_t> key_offsets;
    key_buf.reserve(8192);
    key_offsets.reserve(1000);
    for (size_t i = 0; i < 1000; ++i) {
        char tmp[16];
        int len = snprintf(tmp, sizeof(tmp), "hello%lu", i);
        t1[tmp] = i;
        key_offsets.push_back(key_buf.size());
        key_buf.append(tmp, len + 1);
    }
    ASSERT_EQ(1000u, t1.size());
    ASSERT_FALSE(t1.empty());

    const size_t N = 20000;
    std::vector<const char*> all_keys;
    all_keys.reserve(N);
    size_t j = 0;
    for (size_t i = 0; i < N; ++i) {
        all_keys.push_back(key_buf.data() + key_offsets[j]);
        if (++j >= key_offsets.size()) {
            j = 0;
        }
    }
    std::random_shuffle(all_keys.begin(), all_keys.end());
    int sum = 0;
    butil::Timer tm;
    tm.start();
    for (size_t i = 0; i < all_keys.size(); ++i) {
        sum += butil::find_cstr(t1, all_keys[i])->second;
    }
    tm.stop();
    int64_t elp1 = tm.n_elapsed();
    tm.start();
    for (size_t i = 0; i < all_keys.size(); ++i) {
        sum += t1.find(all_keys[i])->second;
    }
    tm.stop();
    int64_t elp2 = tm.n_elapsed();

    LOG(INFO) << "elp1=" << elp1 / N << " elp2=" << elp2 / N;
}

}
