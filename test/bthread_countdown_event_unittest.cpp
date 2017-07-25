// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2016/06/03 13:25:44

#include <gtest/gtest.h>
#include <bthread/countdown_event.h>
#include "base/atomicops.h"
#include "base/time.h"

namespace {
struct Arg {
    bthread::CountdownEvent event;
    base::atomic<int> num_sig;
};

void *signaler(void *arg) {
    Arg* a = (Arg*)arg;
    a->num_sig.fetch_sub(1, base::memory_order_relaxed);
    LOG(INFO) << "Signal";
    a->event.signal();
    return NULL;
}

TEST(CountdonwEventTest, sanity) {
    for (int n = 1; n < 10; ++n) {
        Arg a;
        a.num_sig = n;
        a.event.init(n);
        for (int i = 0; i < n; ++i) {
            bthread_t tid;
            ASSERT_EQ(0, bthread_start_urgent(&tid, NULL, signaler, &a));
        }
        a.event.wait();
        ASSERT_EQ(0, a.num_sig.load(base::memory_order_relaxed));
        LOG(INFO) << "Wakeup";
    }
}

TEST(CountdonwEventTest, timed_wait) {
    bthread::CountdownEvent event;
    event.init(1);
    int rc = event.timed_wait(base::milliseconds_from_now(100));
    ASSERT_EQ(rc, ETIMEDOUT);
    event.signal();
    rc = event.timed_wait(base::milliseconds_from_now(100));
    ASSERT_EQ(rc, 0);
    bthread::CountdownEvent event1;
    event1.init(1);
    event1.signal();
    rc = event.timed_wait(base::milliseconds_from_now(1));
    ASSERT_EQ(rc, 0);
    LOG(INFO) << "sizeof(timespec)=" << sizeof(timespec);
}
} // namespace
