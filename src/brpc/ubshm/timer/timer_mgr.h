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

// bthread based timer facade for the ubring module. Callbacks run on the
// process-wide bthread timer thread and must return quickly.

#ifndef BRPC_TIMER_MGR_H
#define BRPC_TIMER_MGR_H

#include <stdint.h>
#include "brpc/ubshm/common/common.h"

namespace brpc {
namespace ubring {

// Opaque timer handle. nullptr means "not started" (or already deleted /
// fired for one-shot timers).
typedef struct UbrTimerTask* UbrTimerId;

// Maps the current re-arm interval of a periodic timer to the next one.
// Runs on the timer thread only.
typedef uint64_t (*UbrTimerBackoffFn)(void* arg, uint64_t cur_interval_us);

// Schedule a one-shot `cb(arg, gen)' to run after `delay_us'. The handle slot
// is released before the callback runs, so the callback may free the object
// that stores the slot; the task object itself is released automatically.
// `slot' must point to a real butil::atomic<UbrTimerId> object, so start and
// delete can race on one RMW without reinterpreting plain storage as an
// atomic. `gen' is an opaque value carried by the task and passed back on the
// fire, so a callback armed for an object that was later released and reused
// can detect that it is stale. It is required: passing a wrong value silently
// disables the generation guard, so callers must pass the generation of the
// object `arg' points to.
RETURN_CODE UbrTimerStart(butil::atomic<UbrTimerId>* slot, uint64_t delay_us,
                          void* (*cb)(void*, uint64_t), void* arg, uint64_t gen);

// Schedule `cb(arg, gen)' after `delay_us' and re-arm it every `interval_us'
// until it is deleted. `interval_us' must be positive: a periodic timer without
// a period is a misconfiguration and is rejected here, instead of silently
// degrading to a one-shot (which is what the interval-based API used to do).
// Unlike a one-shot timer, a periodic one retires its handle slot only after
// the callback returned, so the callback must NOT free the object that stores
// the slot. `backoff' maps the interval the timer is currently using to the
// next one; returning 0 is ignored (the previous interval is kept) so a broken
// back-off cannot turn the global timer thread into a spin loop.
RETURN_CODE UbrTimerStartPeriodic(butil::atomic<UbrTimerId>* slot,
                                  uint64_t delay_us, uint64_t interval_us,
                                  void* (*cb)(void*, uint64_t), void* arg,
                                  uint64_t gen,
                                  UbrTimerBackoffFn backoff = nullptr);

// Non-blocking delete, safe to call from inside the timer callback itself.
// This function does not wait for an already running callback and does not
// protect resources reachable from `arg` on its own.
//
// Returns 0 when this call wins the handle-slot competition.
// - For a one-shot timer, the callback will not run.
// - For a periodic timer, future rearming is stopped, but an already
//   dispatched or running callback may still execute once more. Callers
//   must not reclaim resources reachable from `arg` based on this return
//   alone; use UbrTimerDelAndWait when teardown needs to wait for callbacks.
//   A return of 0 therefore never means "the handle is free again".
// A periodic callback that deletes its own timer falls into the periodic case
// as well, but only marks it stopped: the handle slot stays anchored until the
// callback returns, so a concurrent UbrTimerDelAndWait on the same slot really
// does wait for that callback.
//
// Returns 1 when this caller did not acquire timer ownership. The callback,
// another deleter, or the scheduling path is responsible for settling the
// timer resources, so this caller must not reclaim them.
int UbrTimerDel(butil::atomic<UbrTimerId>* slot);

// Delete and wait until a possibly running callback finished, so the
// caller can free resources reachable from `arg'. Safe to call from inside the
// callback's own dispatch: it then degrades to the non-blocking UbrTimerDel
// instead of joining itself, so callers never have to know which thread they
// run on.
//
// A one-shot callback that is already running holds the ownership of `arg' by
// itself (mirroring bthread_timer_del returning 1) and cannot be waited for
// through the slot. Callers of one-shot timers must therefore keep `arg' alive
// by their own reference (this is a hard requirement, not a convenience: the
// delayed-clear path relies on UbrCleanupCtl's reference count for exactly
// this reason). A periodic callback -- including one that deleted its own
// timer -- always can be waited for, because its task stays anchored until it
// returns. The wait polls with bthread_usleep, which degrades to ::usleep on
// plain pthread callers (e.g. process-exit paths).
void UbrTimerDelAndWait(butil::atomic<UbrTimerId>* slot);

}  // namespace ubring
}  // namespace brpc

#endif //BRPC_TIMER_MGR_H
