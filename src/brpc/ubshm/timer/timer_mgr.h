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

// Schedule `cb(arg)' to run after `delay_us' and, when `interval_us' > 0,
// re-arm itself after every run until deleted. One-shot timers release
// their handle slot before running the callback, so the callback may free
// the object that stores the slot; the task object itself is released
// automatically.
RETURN_CODE UbrTimerStart(UbrTimerId* slot, uint64_t delay_us,
                          uint64_t interval_us, void* (*cb)(void*),
                          void* arg,
                          UbrTimerBackoffFn backoff = nullptr);

// Non-blocking delete, safe from inside the timer callback itself. Does
// not wait for a running callback and does not protect `arg' on its own.
// Returns 0 when the call won the slot competition: a one-shot callback
// is guaranteed never to run, and the caller consumes any per-task
// resources it tracks for this timer (ownership of them transfers to the
// caller); for a periodic timer an already-started callback is not
// interrupted. Returns 1 when the callback has been dispatched (it
// consumes those resources itself on every exit) or its fate is still
// being settled by the scheduler -- the caller must not consume anything
// then.
int UbrTimerDel(UbrTimerId* slot);

// Delete and wait until a possibly running callback finished, so the
// caller can free resources reachable from `arg'. Never call this on the
// callback's own task. A one-shot callback that is already running holds
// the ownership of `arg' by itself (mirroring bthread_timer_del returning
// 1) and cannot be waited for through the slot. The wait polls with
// bthread_usleep, which degrades to ::usleep on plain pthread callers
// (e.g. process-exit paths).
void UbrTimerDelAndWait(UbrTimerId* slot);

}  // namespace ubring
}  // namespace brpc

#endif //BRPC_TIMER_MGR_H
