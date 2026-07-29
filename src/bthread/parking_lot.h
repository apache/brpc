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
#include "bthread/types.h"    // bthread_tag_t
#include "bthread/sys_futex.h"

namespace bthread {

DECLARE_bool(parking_lot_no_signal_when_no_waiter);

// Forward declaration
class TaskControl;

// Out-of-line helpers that forward to TaskControl's per-tag waiter accounting.
// Defined in task_control.cpp (where TaskControl is complete), so that
// parking_lot.h can keep TaskControl as an incomplete type and avoid a
// circular include with task_control.h.
void parking_lot_waiter_add(TaskControl* tc, bthread_tag_t tag);
void parking_lot_waiter_sub(TaskControl* tc, bthread_tag_t tag);

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
        , _no_signal_when_no_waiter(FLAGS_parking_lot_no_signal_when_no_waiter)
        , _tc(nullptr), _tag(0) {}

    // Set TaskControl pointer + owning tag for waiter tracking (called once
    // during TaskControl::init after the ParkingLot array is constructed).
    void set_task_control(TaskControl* tc, bthread_tag_t tag) {
        _tc = tc;
        _tag = tag;
    }

    // Wake up at most `num_task' workers.
    // Returns #workers woken up.
    int signal(int num_task) {
        _pending_signal.fetch_add((num_task << 1), butil::memory_order_release);
        if (_no_signal_when_no_waiter && _waiter_num.load(butil::memory_order_relaxed) == 0) {
            return 0;
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
        // Track this waiter BEFORE checking state, so that a concurrent
        // ready_to_run() that calls has_waiting_workers() will see us
        // even if it races between our state check and futex_wait().
        // This eliminates the lost-wakeup window.
        if (_tc) {
            parking_lot_waiter_add(_tc, _tag);
        }
        if (_no_signal_when_no_waiter) {
            _waiter_num.fetch_add(1, butil::memory_order_relaxed);
        }
        if (get_state().val != expected_state.val) {
            // State changed since caller last checked — signal already sent.
            // Undo the tracking and return without sleeping.
            if (_no_signal_when_no_waiter) {
                _waiter_num.fetch_sub(1, butil::memory_order_relaxed);
            }
            if (_tc) {
                parking_lot_waiter_sub(_tc, _tag);
            }
            return;
        }
        futex_wait_private(&_pending_signal, expected_state.val, NULL);
        if (_no_signal_when_no_waiter) {
            _waiter_num.fetch_sub(1, butil::memory_order_relaxed);
        }
        if (_tc) {
            parking_lot_waiter_sub(_tc, _tag);
        }
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
    // TaskControl pointer for waiter tracking (may be NULL before init).
    TaskControl* _tc;
    // Owning tag, used to update the per-tag waiter counter in TaskControl.
    bthread_tag_t _tag;
};

}  // namespace bthread

#endif  // BTHREAD_PARKING_LOT_H
