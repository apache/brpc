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

// bthread - An M:N threading library to make applications more concurrent.

// Date: 2017/07/27 23:07:06

#ifndef BTHREAD_PARKING_LOT_H
#define BTHREAD_PARKING_LOT_H

#include <gflags/gflags.h>
#include "butil/atomicops.h"
#include "bthread/sys_futex.h"

namespace bthread {

DECLARE_bool(parking_lot_no_signal_when_no_waiter);

// Park idle workers.
class BAIDU_CACHELINE_ALIGNMENT ParkingLot {
public:
    class State {
    public:
        State(): val(0) {}
        bool stopped() const { return val & 1; }
    private:
    friend class ParkingLot;
        State(int val) : val(val) {}
        int val;
    };

    ParkingLot()
        : _pending_signal(0), _waiter_num(0)
        , _no_signal_when_no_waiter(FLAGS_parking_lot_no_signal_when_no_waiter) {}

    // Wake up at most `num_task' workers.
    // Returns #workers woken up.
    //
    // When _no_signal_when_no_waiter is enabled, signal() and wait() form a
    // Dekker-style synchronization. Both sides MUST preserve StoreLoad order:
    //
    //   signal(): _pending_signal += num_task * 2;  load(_waiter_num)
    //   wait():   _waiter_num += 1;                 load(_pending_signal)
    //
    // memory_order_release is NOT enough for the signal side, since it does not
    // prevent the following load from being reordered before the store. Once the
    // order is broken, each side may observe the other side's old value, which
    // ends up with a lost wakeup: signal() sees no waiter and skips futex_wake(),
    // while the waiter sees no new signal and goes to sleep.
    //
    // With the matching seq_cst fences below, the StoreLoad order is enforced
    // on both sides:
    //
    //   signaler                         waiter
    //   --------                         ------
    //   _pending_signal += num_task * 2
    //   seq_cst fence
    //                                    _waiter_num += 1
    //                                    seq_cst fence
    //   load(_waiter_num)
    //     |
    //     +-- > 0: futex_wake() -------> waiter is woken, or has not slept yet
    //     |
    //     +-- == 0: return
    //                                    load(_pending_signal)
    //                                    -> sees the new value; does not sleep
    //
    // Thus, signal() either observes a published waiter and wakes it, or the
    // waiter observes the published signal before it can sleep.
    int signal(int num_task) {
        _pending_signal.fetch_add((num_task << 1), butil::memory_order_release);
        if (_no_signal_when_no_waiter) {
            // Matching StoreLoad fence for the waiter-side fence below.
            butil::atomic_thread_fence(butil::memory_order_seq_cst);
            if (_waiter_num.load(butil::memory_order_relaxed) == 0) {
                return 0;
            }
        }
        return futex_wake_private(&_pending_signal, num_task);
    }

    // Get a state for later wait().
    State get_state() {
        return _pending_signal.load(butil::memory_order_acquire);
    }

    // Wait for tasks.
    // If the `expected_state' does not match, wait() may finish directly.
    void wait(const State& expected_state) {
        if (get_state().val != expected_state.val) {
            // Fast path, no need to futex_wait.
            return;
        }
        if (!_no_signal_when_no_waiter) {
            futex_wait_private(&_pending_signal, expected_state.val, NULL);
            return;
        }

        _waiter_num.fetch_add(1, butil::memory_order_relaxed);
        // Matching StoreLoad fence for the signal-side fence above. It publishes
        // this waiter before _pending_signal is checked below.
        butil::atomic_thread_fence(butil::memory_order_seq_cst);
        // Re-check _pending_signal with a real atomic load, which closes the Dekker
        // pattern at the C++ memory model level (the check inside futex_wait is done
        // by the kernel and is invisible to the compiler). It also saves a futex
        // syscall that would return EAGAIN immediately.
        if (_pending_signal.load(butil::memory_order_relaxed) == expected_state.val) {
            futex_wait_private(&_pending_signal, expected_state.val, nullptr);
        }
        _waiter_num.fetch_sub(1, butil::memory_order_relaxed);
    }

    // Wakeup suspended wait() and make them unwaitable ever. 
    void stop() {
        _pending_signal.fetch_or(1);
        futex_wake_private(&_pending_signal, 10000);
    }

private:
    // higher 31 bits for signalling, LSB for stopping.
    butil::atomic<int> _pending_signal;
    butil::atomic<int> _waiter_num;
    // Whether to signal when there is no waiter.
    // In busy worker scenarios, signal overhead
    // can be reduced.
    bool _no_signal_when_no_waiter;
};

}  // namespace bthread

#endif  // BTHREAD_PARKING_LOT_H
