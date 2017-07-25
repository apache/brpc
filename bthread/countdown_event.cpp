// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: Zhangyi Chen (chenzhangyi01@baidu.com)
// Date: 2016/06/03 13:15:24

#include "base/atomicops.h"     // base::atomic<int>
#include "bthread/countdown_event.h"
#include "bthread/butex.h"      // butex_construct

namespace bthread {

CountdownEvent::CountdownEvent() {
    _butex = (int*)butex_construct(_butex_memory);
    ((base::atomic<int>*)_butex)->store(0, base::memory_order_relaxed);
    _wait_was_invoked = false;
}

CountdownEvent::~CountdownEvent() {
    butex_destruct(_butex_memory);
}

int CountdownEvent::init(int initial_count) {
    if (initial_count < 0) {
        return -1;
    }
    ((base::atomic<int>*)_butex)->store(initial_count, 
                                        base::memory_order_relaxed);
    _wait_was_invoked = false;
    return 0;
}

void CountdownEvent::signal(int sig) {
    butex_add_ref_before_wake(_butex);
    const int prev = ((base::atomic<int>*)_butex)
        ->fetch_sub(sig, base::memory_order_release);
    if (prev > sig) {
        butex_remove_ref(_butex);
        return;
    }
    LOG_IF(ERROR, prev < sig) << "Counter is over decreased";
    butex_wake_all_and_remove_ref(_butex);
}

void CountdownEvent::wait() {
    _wait_was_invoked = true;
    for (;;) {
        const int seen_counter = 
            ((base::atomic<int>*)_butex)->load(base::memory_order_acquire);
        if (seen_counter <= 0) {
            return;
        }
        butex_wait(_butex, seen_counter, NULL);
    }
}

void CountdownEvent::add_count(int v) {
    if (v <= 0) {
        LOG_IF(ERROR, v < 0) << "Negative v=" << v;
        return;
    }
    LOG_IF(ERROR, _wait_was_invoked) 
            << "Invoking add_count() after wait() was invoked";
    ((base::atomic<int>*)_butex)->fetch_add(v, base::memory_order_release);
}

void CountdownEvent::reset(int v) {
    const int prev_counter =
            ((base::atomic<int>*)_butex)
                ->exchange(v, base::memory_order_release);
    LOG_IF(ERROR, !prev_counter) << "Invoking reset() while the count is not 0";
    _wait_was_invoked = false;
}

int CountdownEvent::timed_wait(const timespec& duetime) {
    _wait_was_invoked = true;
    for (;;) {
        const int seen_counter = 
            ((base::atomic<int>*)_butex)->load(base::memory_order_acquire);
        if (seen_counter <= 0) {
            return 0;
        }
        const int rc = butex_wait(_butex, seen_counter, &duetime);
        if (rc < 0 && errno == ETIMEDOUT) {
            return ETIMEDOUT;
        }
    }
}

}  // namespace bthread
