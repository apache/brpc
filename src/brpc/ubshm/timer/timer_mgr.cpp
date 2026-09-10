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

#include <atomic>
#include <new>
#include "bthread/bthread.h"                     // bthread_usleep
#include "bthread/unstable.h"                    // bthread_timer_add/del
#include "butil/time.h"
#include "brpc/ubshm/timer/timer_mgr.h"

namespace brpc {
namespace ubring {

namespace {

enum UbrTimerState {
    kStarting = 0,                               // published, not scheduled yet
    kScheduled = 1,
    kDead = 2                                    // scheduling failed
};

}  // namespace

// Reference rules: one "owner" ref for the handle slot, one "schedule" ref
// per pending/running bthread schedule, plus one ref held by the starter
// until its post-schedule bookkeeping is done. The schedule ref is
// consumed by the firing callback or by the deleter whose
// bthread_timer_del returned 0 (cancelled before run); the owner ref is
// consumed by whoever takes the task out of *slot -- a deleter, or the
// one-shot firing callback itself, which exits the slot BEFORE running
// the callback so that the callback may free the object storing the slot.
// All atomics are seq_cst so no interleaving can release a ref twice or
// free the task while a callback or the starter still touches it.
struct UbrTimerTask {
    UbrTimerId* slot;
    std::atomic<bthread_timer_t> id;
    void* (*cb)(void*);
    void* arg;
    UbrTimerBackoffFn backoff;
    uint64_t interval_us;                        // timer thread only
    bool periodic;
    std::atomic<int> state;                      // kStarting/kScheduled/kDead
    std::atomic<bool> stopped;
    std::atomic<int> ref;
    std::atomic<bool> join_pending;              // a DelAndWait is waiting
    std::atomic<bool> done;                      // refs hit zero, joiner frees
};

namespace {

void ReleaseRef(UbrTimerTask* task) {
    if (task->ref.fetch_sub(1) == 1) {
        if (task->join_pending.load()) {
            task->done.store(true);              // joiner frees the task
        } else {
            delete task;
        }
    }
}

void UbrTimerOnFire(void* p) {
    UbrTimerTask* task = (UbrTimerTask*)p;

    if (task->periodic) {
        if (!task->stopped.load()) {
            task->cb(task->arg);
        }
        // Claim the next schedule's ref before re-reading `stopped' so a
        // racing delete can neither free the task nor orphan a re-arm.
        task->ref.fetch_add(1);
        if (task->stopped.load()) {
            ReleaseRef(task);
        } else {
            uint64_t interval = task->interval_us;
            if (task->backoff != nullptr) {
                interval = task->backoff(task->arg, interval);
                task->interval_us = interval;
            }
            bthread_timer_t id = 0;
            if (bthread_timer_add(
                    &id, butil::microseconds_from_now((int64_t)interval),
                    UbrTimerOnFire, task) == 0) {
                task->id.store(id);
                if (task->stopped.load() && bthread_timer_del(id) == 0) {
                    ReleaseRef(task);
                }
            } else {
                LOG(ERROR) << "Fail to re-arm ubring timer";
                ReleaseRef(task);
            }
        }
        ReleaseRef(task);
        return;
    }

    // One-shot: exit the handle slot first -- after this the wrapper never
    // touches the storage again, so the callback may release the object
    // that holds it. Whether the callback runs is decided solely by this
    // slot competition: every UbrTimerDel that wants the callback
    // suppressed has to win this exchange first, so owned==true guarantees
    // no UbrTimerDel is pending. Do not consult `stopped' here: its store
    // (del thread) and this load (timer thread) are separated by the slot
    // RMW and seq_cst does not order the store-buffer case -- ownership of
    // the slot is the single arbiter.
    UbrTimerId expected = task;
    const bool owned =
        __atomic_compare_exchange_n(task->slot, &expected, (UbrTimerId) nullptr,
                                    false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (owned) {
        task->cb(task->arg);
    }
    ReleaseRef(task);                            // schedule
    if (owned) {
        ReleaseRef(task);                        // owner
    }
}

UbrTimerTask* TakeOutTask(UbrTimerId* slot) {
    return __atomic_exchange_n(slot, (UbrTimerId) nullptr, __ATOMIC_SEQ_CST);
}

RETURN_CODE TimerStartInternal(UbrTimerId* slot, uint64_t delay_us,
                               uint64_t interval_us, void* (*cb)(void*),
                               void* arg, UbrTimerBackoffFn backoff) {
    if (UNLIKELY(slot == nullptr || cb == nullptr)) {
        LOG(ERROR) << "Ubr timer start invalid argument, slot=" << slot;
        return UBRING_ERR;
    }

    UbrTimerTask* task = new (std::nothrow) UbrTimerTask();
    if (UNLIKELY(task == nullptr)) {
        LOG(ERROR) << "Fail to malloc ubring timer task.";
        return UBRING_ERR;
    }
    task->slot = slot;
    task->id.store(0);
    task->cb = cb;
    task->arg = arg;
    task->backoff = backoff;
    task->interval_us = interval_us;
    task->periodic = (interval_us > 0);
    task->state.store(kStarting);
    task->stopped.store(false);
    task->ref.store(3);                          // owner + schedule + starter
    task->join_pending.store(false);
    task->done.store(false);

    // Publish the real task before scheduling so a delete or a DelAndWait
    // racing the start always has an object to act on or wait for.
    UbrTimerId expected = nullptr;
    if (!__atomic_compare_exchange_n(slot, &expected, (UbrTimerId) task, false,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        LOG(ERROR) << "Ubr timer start refused, slot already occupied";
        delete task;                             // never published
        return UBRING_ERR;
    }

    bthread_timer_t id = 0;
    if (UNLIKELY(bthread_timer_add(
            &id, butil::microseconds_from_now((int64_t)delay_us),
            UbrTimerOnFire, task) != 0)) {
        LOG(ERROR) << "Fail to add ubring timer";
        task->state.store(kDead);                // wake DelAndWait waiters
        expected = task;
        const bool owned =
            __atomic_compare_exchange_n(slot, &expected, (UbrTimerId) nullptr,
                                        false, __ATOMIC_SEQ_CST,
                                        __ATOMIC_SEQ_CST);
        ReleaseRef(task);                        // schedule, never ran
        if (owned) {
            ReleaseRef(task);                    // owner
        }
        ReleaseRef(task);                        // starter
        return UBRING_ERR;
    }
    // A zero-delay task may have fired and re-armed already; keep a newer
    // id if so.
    bthread_timer_t expected_id = 0;
    task->id.compare_exchange_strong(expected_id, id);
    task->state.store(kScheduled);
    // No post-add stopped check here: a UbrTimerDel racing the start
    // returns 1 without consuming the per-task resources, and the armed
    // timer must fire so that OnFire settles the ownership protocol.
    ReleaseRef(task);                            // starter
    return UBRING_OK;
}

}  // namespace

RETURN_CODE UbrTimerStart(UbrTimerId* slot, uint64_t delay_us,
                          uint64_t interval_us, void* (*cb)(void*),
                          void* arg, UbrTimerBackoffFn backoff) {
    return TimerStartInternal(slot, delay_us, interval_us, cb, arg, backoff);
}

int UbrTimerDel(UbrTimerId* slot) {
    if (slot == nullptr) {
        return 1;
    }
    // Take the ownership of the slot first: after this exchange every
    // dereference below is safe (the task cannot be freed while we hold
    // the owner reference the slot used to anchor).
    UbrTimerTask* task = TakeOutTask(slot);
    if (task == nullptr) {
        return 1;        // fired and cleared its slot (callback side consumed)
                         // or another del won the exchange (it consumes)
    }
    task->stopped.store(true);                   // meaningful for periodic only
    // A start still in flight cannot be cancelled nor dispatched yet; wait
    // for the starter to settle the fate (kScheduled/kDead). Bounded: the
    // starter stores the state before taking any lock our caller holds.
    while (task->state.load() == kStarting) {
        bthread_usleep(1000);
    }
    if (task->state.load() == kDead) {
        ReleaseRef(task);                        // owner; schedule/starter are
        return 1;                                // settled by the kDead path
    }
    bthread_timer_t id = task->id.load();
    if (id != 0 && bthread_timer_del(id) == 0) {
        ReleaseRef(task);                        // schedule: cancelled before dispatch
    }                                            // ==1: dispatched, OnFire (owned==false)
                                                 // releases it
    ReleaseRef(task);                            // owner
    return 0;       // This call won the slot competition. For a one-shot timer,
                    // the callback will not run. For a periodic timer, future
                    // rearming is stopped, but an already dispatched or running
                    // callback may still complete.
}

void UbrTimerDelAndWait(UbrTimerId* slot) {
    if (slot == nullptr) {
        return;
    }
    UbrTimerTask* task = TakeOutTask(slot);
    if (task == nullptr) {
        return;
    }
    task->join_pending.store(true);
    task->stopped.store(true);
    // A start still in flight cannot be cancelled yet; wait for the
    // starter to schedule it or mark it dead.
    while (task->state.load() == kStarting) {
        bthread_usleep(1000);
    }
    if (task->state.load() == kScheduled) {
        bthread_timer_t id = task->id.load();
        if (id != 0 && bthread_timer_del(id) == 0) {
            ReleaseRef(task);                    // cancelled before run
        }
    }
    ReleaseRef(task);                            // owner reference
    while (!task->done.load()) {
        bthread_usleep(1000);
    }
    task->join_pending.store(false);
    delete task;
}

}  // namespace ubring
}  // namespace brpc
