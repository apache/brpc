// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2017 Baidu.com, Inc. All Rights Reserved

// Author: chenzhangyi01@baidu.com, gejun@baidu.com
// Date: 2017/07/27 23:07:06

#ifndef  PUBLIC_BTHREAD_PARKING_LOT_H
#define  PUBLIC_BTHREAD_PARKING_LOT_H

#include "base/atomicops.h"
#include "bthread/sys_futex.h"

namespace bthread {

// Park idle workers.
class BAIDU_CACHELINE_ALIGNMENT ParkingLot {
public:
    struct State {
        bool stopped() const { return val & 1; }
        int val;
    };

    ParkingLot() : _pending_signal(0) {}

    // Wake up at most `num_task' workers.
    // Returns #workers woken up.
    int signal(int num_task) {
        _pending_signal.fetch_add((num_task << 1), base::memory_order_release);
        return futex_wake_private(&_pending_signal, num_task);
    }

    // Get a state for later wait().
    State get_state() {
        const State st = {_pending_signal.load(base::memory_order_acquire)};
        return st;
    }

    // Wait for tasks.
    // If the `expected_state' does not match, wait() may finish directly.
    void wait(const State& expected_state) {
        futex_wait_private(&_pending_signal, expected_state.val, NULL);
    }

    // Wakeup suspended wait() and make them unwaitable ever. 
    void stop() {
        _pending_signal.fetch_or(1);
        futex_wake_private(&_pending_signal, 10000);
    }
private:
    // higher 31 bits for signalling, MLB for stopping.
    base::atomic<int> _pending_signal;
};

}  // namespace bthread

#endif  //PUBLIC_BTHREAD_PARKING_LOT_H
