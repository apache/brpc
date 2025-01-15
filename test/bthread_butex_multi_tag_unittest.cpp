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

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "bthread/bthread.h"
#include "bthread/condition_variable.h"
#include "bthread/countdown_event.h"
#include "bthread/mutex.h"

DECLARE_int32(task_group_ntags);

int main(int argc, char* argv[]) {
    FLAGS_task_group_ntags = 3;
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}

namespace {

std::vector<bthread_tag_t> butex_wake_return(2, 0);

void* butex_wake_func(void* arg) {
    auto mutex = static_cast<bthread::Mutex*>(arg);
    butex_wake_return.push_back(bthread_self_tag());
    mutex->lock();
    butex_wake_return.push_back(bthread_self_tag());
    mutex->unlock();
    return nullptr;
}

TEST(BthreadButexMultiTest, butex_wake) {
    bthread::Mutex mutex;
    mutex.lock();
    bthread_t tid1;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.tag = 1;
    bthread_start_urgent(&tid1, &attr, butex_wake_func, &mutex);
    mutex.unlock();
    bthread_join(tid1, nullptr);
    ASSERT_EQ(butex_wake_return[0], butex_wake_return[1]);
}

std::vector<bthread_tag_t> butex_wake_all_return1(2, 0);
std::vector<bthread_tag_t> butex_wake_all_return2(2, 0);

struct ButexWakeAllArgs {
    bthread::CountdownEvent* ev;
    bthread::CountdownEvent* ack;
};

void* butex_wake_all_func1(void* arg) {
    auto p = static_cast<ButexWakeAllArgs*>(arg);
    auto ev = p->ev;
    auto ack = p->ack;
    butex_wake_all_return1.push_back(bthread_self_tag());
    ack->signal();
    ev->wait();
    butex_wake_all_return1.push_back(bthread_self_tag());
    return nullptr;
}

void* butex_wake_all_func2(void* arg) {
    auto p = static_cast<ButexWakeAllArgs*>(arg);
    auto ev = p->ev;
    auto ack = p->ack;
    butex_wake_all_return2.push_back(bthread_self_tag());
    ack->signal();
    ev->wait();
    butex_wake_all_return2.push_back(bthread_self_tag());
    return nullptr;
}

TEST(BthreadButexMultiTest, butex_wake_all) {
    bthread::CountdownEvent ev(2);
    bthread::CountdownEvent ack(2);
    ButexWakeAllArgs args{&ev, &ack};
    bthread_t tid1, tid2;
    bthread_attr_t attr1 = BTHREAD_ATTR_NORMAL;
    attr1.tag = 1;
    bthread_start_background(&tid1, &attr1, butex_wake_all_func1, &args);
    bthread_attr_t attr2 = BTHREAD_ATTR_NORMAL;
    attr2.tag = 2;
    bthread_start_background(&tid2, &attr2, butex_wake_all_func2, &args);
    ack.wait();
    ev.signal(2);
    bthread_join(tid1, nullptr);
    bthread_join(tid2, nullptr);
    ASSERT_EQ(butex_wake_all_return1[0], butex_wake_all_return1[1]);
    ASSERT_EQ(butex_wake_all_return2[0], butex_wake_all_return2[1]);
}

std::vector<bthread_tag_t> butex_requeue_return1(2, 0);
std::vector<bthread_tag_t> butex_requeue_return2(2, 0);

struct ButexRequeueArgs {
    bthread::Mutex* mutex;
    bthread::ConditionVariable* cond;
    bthread::CountdownEvent* ack;
};

void* butex_requeue_func1(void* arg) {
    auto p = static_cast<ButexRequeueArgs*>(arg);
    auto mutex = p->mutex;
    auto cond = p->cond;
    auto ack = p->ack;
    butex_wake_all_return1.push_back(bthread_self_tag());
    std::unique_lock<bthread::Mutex> lk(*mutex);
    ack->signal();
    cond->wait(lk);
    butex_wake_all_return1.push_back(bthread_self_tag());
    return nullptr;
}

void* butex_requeue_func2(void* arg) {
    auto p = static_cast<ButexRequeueArgs*>(arg);
    auto mutex = p->mutex;
    auto cond = p->cond;
    auto ack = p->ack;
    butex_wake_all_return2.push_back(bthread_self_tag());
    std::unique_lock<bthread::Mutex> lk(*mutex);
    ack->signal();
    cond->wait(lk);
    butex_wake_all_return2.push_back(bthread_self_tag());
    return nullptr;
}

TEST(BthreadButexMultiTest, butex_requeue) {
    bthread::Mutex mutex;
    bthread::ConditionVariable cond;
    bthread::CountdownEvent ack(2);
    ButexRequeueArgs args{&mutex, &cond, &ack};

    bthread_t tid1, tid2;
    bthread_attr_t attr1 = BTHREAD_ATTR_NORMAL;
    attr1.tag = 1;
    bthread_start_background(&tid1, &attr1, butex_requeue_func1, &args);
    bthread_attr_t attr2 = BTHREAD_ATTR_NORMAL;
    attr2.tag = 2;
    bthread_start_background(&tid2, &attr2, butex_requeue_func2, &args);
    ack.wait();
    {
        std::unique_lock<bthread::Mutex> lk(mutex);
        cond.notify_all();
    }
    {
        std::unique_lock<bthread::Mutex> lk(mutex);
        cond.notify_all();
    }
    bthread_join(tid1, nullptr);
    bthread_join(tid2, nullptr);
    ASSERT_EQ(butex_wake_all_return1[0], butex_wake_all_return1[1]);
    ASSERT_EQ(butex_wake_all_return2[0], butex_wake_all_return2[1]);
}

}  // namespace
