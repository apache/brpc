// Copyright (c) 2014 Baidu, Inc.

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2016/06/03 13:25:44

#include <gtest/gtest.h>
#include <bthread/countdown_event.h>
#include "butil/atomicops.h"
#include "butil/time.h"

namespace {
struct Arg {
    bthread::CountdownEvent event;
    butil::atomic<int> num_sig;
};

void *signaler(void *arg) {
    Arg* a = (Arg*)arg;
    a->num_sig.fetch_sub(1, butil::memory_order_relaxed);
    a->event.signal();
    return NULL;
}

TEST(CountdonwEventTest, sanity) {
    for (int n = 1; n < 10; ++n) {
        Arg a;
        a.num_sig = n;
        a.event.reset(n);
        for (int i = 0; i < n; ++i) {
            bthread_t tid;
            ASSERT_EQ(0, bthread_start_urgent(&tid, NULL, signaler, &a));
        }
        a.event.wait();
        ASSERT_EQ(0, a.num_sig.load(butil::memory_order_relaxed));
    }
}

TEST(CountdonwEventTest, timed_wait) {
    bthread::CountdownEvent event;
    int rc = event.timed_wait(butil::milliseconds_from_now(100));
    ASSERT_EQ(rc, ETIMEDOUT);
    event.signal();
    rc = event.timed_wait(butil::milliseconds_from_now(100));
    ASSERT_EQ(rc, 0);
    bthread::CountdownEvent event1;
    event1.signal();
    rc = event.timed_wait(butil::milliseconds_from_now(1));
    ASSERT_EQ(rc, 0);
}
} // namespace
