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

#include <new>
#include "bthread/bthread.h"                     // bthread_usleep
#include "bthread/unstable.h"                    // bthread_timer_add/del
#include "butil/atomicops.h"
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

// Granularity of the two join waits in UbrTimerDelAndWait. They only run on
// teardown paths and only for the short window in which a timer is being
// started or a callback is finishing.
constexpr uint64_t kTimerPollIntervalUs = 100;

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
//
// A periodic callback that deletes its own timer does NOT take the task out
// of the slot: it only marks the task stopped, and the firing callback
// retires the slot right before it returns (see UbrTimerOnFire). Keeping the
// task anchored for the whole callback is what lets a concurrent
// UbrTimerDelAndWait still find it and wait for the callback to finish,
// instead of returning as if no callback were in flight.
struct UbrTimerTask {
    butil::atomic<UbrTimerId>* slot;
    butil::atomic<bthread_timer_t> id;
    void* (*cb)(void*, uint64_t);
    void* arg;
    uint64_t gen;                                // opaque, passed back to cb
    UbrTimerBackoffFn backoff;
    uint64_t interval_us;                        // timer thread only
    bool periodic;
    butil::atomic<int> state;                    // kStarting/kScheduled/kDead
    butil::atomic<bool> stopped;
    butil::atomic<int> ref;
    butil::atomic<bool> join_pending;            // a DelAndWait is waiting
    butil::atomic<bool> done;                    // refs hit zero, joiner frees
    // Set by whoever settles the handle slot: the one-shot wrapper (right
    // after its slot CAS, before it runs the user callback), a deleter whose
    // bthread_timer_del cancelled the task before dispatch, or the kDead
    // path. Once true, no facade code -- including the dispatched wrapper --
    // will ever touch `*slot` again, so the object storing the slot may be
    // freed. The scheduled wrapper is kept alive by its own schedule
    // reference while a deleter waits on this flag.
    butil::atomic<bool> slot_retired;
};

namespace {

// The task whose callback is running on this thread, if any. It lets
// UbrTimerDel recognize a delete issued by the callback's own dispatch and
// leave the task anchored until the callback returns.
thread_local UbrTimerTask* t_running_task = nullptr;

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
            // Publish the running task so an UbrTimerDel issued by this very
            // callback leaves the slot anchored for a concurrent waiter.
            UbrTimerTask* prev_running = t_running_task;
            t_running_task = task;
            task->cb(task->arg, task->gen);
            t_running_task = prev_running;
        }
        // Claim the next schedule's ref before re-reading `stopped' so a
        // racing delete can neither free the task nor orphan a re-arm.
        task->ref.fetch_add(1);
        if (task->stopped.load()) {
            // Retire the slot here, on the callback's own exit: a self-delete
            // deliberately left the task anchored. A deleter that took the
            // slot instead has already consumed the owner reference.
            UbrTimerId anchored = task;
            if (task->slot->compare_exchange_strong(anchored, nullptr)) {
                ReleaseRef(task);                // owner
            }
            ReleaseRef(task);                    // claimed next schedule
        } else {
            uint64_t interval = task->interval_us;
            if (task->backoff != nullptr) {
                const uint64_t next = task->backoff(task->arg, interval);
                if (BAIDU_UNLIKELY(next == 0)) {
                    // Re-arming for "now" would let this one timer monopolize
                    // the process-wide timer thread. A back-off (or a flag it
                    // reads) returning 0 is a bug, so keep the previous
                    // interval instead of honouring it. Guarded here rather
                    // than in each back-off function, because any of them can
                    // make this mistake.
                    LOG_EVERY_SECOND(ERROR) << "Ubr timer back-off returned 0, keeping interval_us="
                                            << interval;
                } else {
                    interval = next;
                }
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
    const bool owned = task->slot->compare_exchange_strong(expected, nullptr);
    // The slot storage is now retired: whatever the competition above
    // decided, this wrapper will not dereference `slot' again. A one-shot
    // UbrTimerDel that won the exchange (or that cancelled a task which had
    // not been dispatched at all) is waiting for this store before it lets
    // the caller free the storage, so it must happen before the potentially
    // long user callback.
    task->slot_retired.store(true);
    if (owned) {
        task->cb(task->arg, task->gen);
    }
    ReleaseRef(task);                            // schedule
    if (owned) {
        ReleaseRef(task);                        // owner
    }
}

UbrTimerTask* TakeOutTask(butil::atomic<UbrTimerId>* slot) {
    return slot->exchange(nullptr);
}

RETURN_CODE TimerStartInternal(butil::atomic<UbrTimerId>* slot, uint64_t delay_us,
                               uint64_t interval_us, bool periodic,
                               void* (*cb)(void*, uint64_t),
                               void* arg, uint64_t gen,
                               UbrTimerBackoffFn backoff) {
    if (BAIDU_UNLIKELY(slot == nullptr || cb == nullptr)) {
        LOG(ERROR) << "Ubr timer start invalid argument, slot=" << slot;
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY(periodic && interval_us == 0)) {
        // Rejecting beats silently arming a one-shot: a caller that asks for a
        // periodic timer and gets a single fire loses whatever the timer was
        // watching for the rest of the connection's life.
        LOG(ERROR) << "Ubr periodic timer start requires a positive interval.";
        return UBRING_ERR;
    }

    UbrTimerTask* task = new (std::nothrow) UbrTimerTask();
    if (BAIDU_UNLIKELY(task == nullptr)) {
        LOG(ERROR) << "Fail to malloc ubring timer task.";
        return UBRING_ERR;
    }
    task->slot = slot;
    task->id.store(0);
    task->cb = cb;
    task->arg = arg;
    task->gen = gen;
    task->backoff = backoff;
    task->interval_us = interval_us;
    task->periodic = periodic;
    task->state.store(kStarting);
    task->stopped.store(false);
    task->ref.store(3);                          // owner + schedule + starter
    task->join_pending.store(false);
    task->done.store(false);
    task->slot_retired.store(false);

    // Publish the real task before scheduling so a delete or a DelAndWait
    // racing the start always has an object to act on or wait for.
    UbrTimerId expected = nullptr;
    if (!slot->compare_exchange_strong(expected, task)) {
        LOG(ERROR) << "Ubr timer start refused, slot already occupied";
        delete task;                             // never published
        return UBRING_ERR;
    }

    bthread_timer_t id = 0;
    if (BAIDU_UNLIKELY(bthread_timer_add(
            &id, butil::microseconds_from_now((int64_t)delay_us),
            UbrTimerOnFire, task) != 0)) {
        LOG(ERROR) << "Fail to add ubring timer";
        task->state.store(kDead);                // wake DelAndWait waiters
        expected = task;
        const bool owned = slot->compare_exchange_strong(expected, nullptr);
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

RETURN_CODE UbrTimerStart(butil::atomic<UbrTimerId>* slot, uint64_t delay_us,
                          void* (*cb)(void*, uint64_t), void* arg, uint64_t gen) {
    return TimerStartInternal(slot, delay_us, 0, false, cb, arg, gen, nullptr);
}

RETURN_CODE UbrTimerStartPeriodic(butil::atomic<UbrTimerId>* slot,
                                  uint64_t delay_us, uint64_t interval_us,
                                  void* (*cb)(void*, uint64_t), void* arg,
                                  uint64_t gen, UbrTimerBackoffFn backoff) {
    return TimerStartInternal(slot, delay_us, interval_us, true, cb, arg, gen,
                              backoff);
}

int UbrTimerDel(butil::atomic<UbrTimerId>* slot) {
    if (slot == nullptr) {
        return 1;
    }
    // A periodic callback deleting its own timer must not pull the task out of
    // the slot: the anchored task is what lets a concurrent UbrTimerDelAndWait
    // find the running callback and wait for it. Mark it stopped instead and
    // let UbrTimerOnFire retire the slot when the callback returns. Deleting a
    // sibling timer from inside a callback is unaffected: the slots differ, so
    // the pointer comparison below fails and the normal path runs.
    if (t_running_task != nullptr && slot->load() == t_running_task) {
        t_running_task->stopped.store(true);
        // 0 means "the timer is stopped", not "the handle slot is now free":
        // the slot is retired by UbrTimerOnFire when this callback returns, and
        // a concurrent UbrTimerDelAndWait may still be joining the task.
        return 0;
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
        // The wrapper never ran and never will: this call is the one that
        // settles the slot storage.
        task->slot_retired.store(true);
        ReleaseRef(task);                        // owner; schedule/starter are
        return 1;                                // settled by the kDead path
    }
    bthread_timer_t id = task->id.load();
    if (id != 0 && bthread_timer_del(id) == 0) {
        // Cancelled before dispatch: the timer thread will never call the
        // wrapper, so nothing else can retire the slot storage.
        task->slot_retired.store(true);
        ReleaseRef(task);                        // schedule: cancelled before dispatch
    } else if (!task->periodic) {
        // One-shot already dispatched: the wrapper may be between its dispatch
        // (which made bthread_timer_del return 1) and its slot CAS, so it
        // still has to touch the storage before it returns. Wait it out here,
        // while this caller still owns the task through the owner reference,
        // so that returning 0 really means "the storage is not used anymore".
        // The wrapper retires the slot before calling any user code and takes
        // no lock doing so, hence this wait cannot deadlock, and it is bounded
        // by the OS scheduling of the timer thread. A periodic task needs no
        // such wait: its slot storage is the pooled UbrTrx, which the callers
        // of the non-blocking delete never free (only UbrMgrFini does, after
        // UbrTimerDelAndWait), and its retire-slot load happens after the user
        // callback -- which may take the very lock this caller holds.
        while (!task->slot_retired.load()) {
            bthread_usleep(kTimerPollIntervalUs);
        }
    }                                            // ==1 periodic: dispatched/running;
                                                 // OnFire releases the schedule ref
    ReleaseRef(task);                            // owner
    return 0;       // This call won the slot competition. For a one-shot timer,
                    // the callback will not run and the slot storage is unused.
                    // For a periodic timer, future rearming is stopped, but an
                    // already dispatched or running callback may still complete.
}

void UbrTimerDelAndWait(butil::atomic<UbrTimerId>* slot) {
    if (slot == nullptr) {
        return;
    }
    // Called by the callback of that very timer: joining it would wait for
    // ourselves, so degrade to the non-blocking delete (UbrTimerDel marks the
    // task stopped and lets UbrTimerOnFire retire the slot on callback exit).
    // Callers therefore do not need to know whether they run on the timer
    // thread.
    if (t_running_task != nullptr && slot->load() == t_running_task) {
        UbrTimerDel(slot);
        return;
    }
    UbrTimerTask* task = TakeOutTask(slot);
    if (task == nullptr) {
        return;
    }
    task->join_pending.store(true);
    task->stopped.store(true);
    // A start still in flight cannot be cancelled yet; wait for the
    // starter to schedule it or mark it dead, polling at
    // kTimerPollIntervalUs. DelAndWait runs on teardown paths (force close,
    // UbrMgrFini, connect failure) where a sub-millisecond delay is
    // irrelevant, so polling keeps the facade free of a second wait primitive
    // and works identically from bthreads and plain pthreads.
    while (task->state.load() == kStarting) {
        bthread_usleep(kTimerPollIntervalUs);
    }
    if (task->state.load() == kScheduled) {
        bthread_timer_t id = task->id.load();
        if (id != 0 && bthread_timer_del(id) == 0) {
            ReleaseRef(task);                    // cancelled before run
        }
    }
    ReleaseRef(task);                            // owner reference
    while (!task->done.load()) {
        bthread_usleep(kTimerPollIntervalUs);
    }
    task->join_pending.store(false);
    delete task;
}

}  // namespace ubring
}  // namespace brpc
