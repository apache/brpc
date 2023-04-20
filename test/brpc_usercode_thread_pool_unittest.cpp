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
#include "brpc/usercode_thread_pool.h"

TEST(UserCodeThreadPoolTest, GetOrCreatePool) {
    brpc::UserCodeThreadRandomAssignPolicy policy;

    brpc::UserCodeThreadPoolConf conf1{
        "p1", 2, []() { LOG(INFO) << "hello world"; }, &policy};
    auto p1 = brpc::UserCodeThreadPool::GetOrCreatePool(conf1);
    ASSERT_NE(p1, nullptr);

    auto p2 = brpc::UserCodeThreadPool::GetOrCreatePool(conf1);
    ASSERT_EQ(p1, p2);

    brpc::UserCodeThreadPoolConf conf2{
        "p2", 2, []() { LOG(INFO) << "hello world"; }, &policy};
    auto p3 = brpc::UserCodeThreadPool::GetOrCreatePool(conf2);
    ASSERT_NE(p1, p3);

    brpc::UserCodeThreadPoolConf conf3{
        "p3", 0, []() { LOG(INFO) << "hello world"; }, &policy};
    auto p4 = brpc::UserCodeThreadPool::GetOrCreatePool(conf3);
    ASSERT_EQ(p4, nullptr);
}

TEST(UserCodeThreadPoolTest, SetPoolThreads) {
    brpc::UserCodeThreadRandomAssignPolicy policy;
    brpc::UserCodeThreadPoolConf conf1{
        "p1", 2, []() { LOG(INFO) << "hello world"; }, &policy};
    auto p1 = brpc::UserCodeThreadPool::GetOrCreatePool(conf1);
    ASSERT_NE(p1, nullptr);

    auto rc = brpc::UserCodeThreadPool::SetPoolThreads("p1", 1);
    ASSERT_EQ(rc, false);

    rc = brpc::UserCodeThreadPool::SetPoolThreads("p1", 3);
    ASSERT_EQ(rc, true);

    rc = brpc::UserCodeThreadPool::SetPoolThreads("p4", 3);
    ASSERT_EQ(rc, false);
}

void my_usercode(void* arg) {
    LOG(INFO) << "usercode process " << *static_cast<std::string*>(arg);
}

TEST(UserCodeThreadPoolTest, SetNumThreads) {
    brpc::UserCodeThreadRandomAssignPolicy policy;
    brpc::UserCodeThreadPool pool{
        "p1", []() { LOG(INFO) << "usercode thread start"; }, &policy};
    auto rc = pool.Init(2);
    ASSERT_EQ(rc, true);

    std::string arg("i am running");
    pool.RunUserCode(my_usercode, &arg);

    sleep(2);
}
