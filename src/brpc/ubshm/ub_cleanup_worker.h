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

#ifndef BRPC_UB_CLEANUP_WORKER_H
#define BRPC_UB_CLEANUP_WORKER_H

#include <stdint.h>
#include "brpc/ubshm/common/common.h"
#include "brpc/ubshm/shm/shm_def.h"

namespace brpc {
namespace ubring {

struct TagUbrTrx;
typedef struct TagUbrTrx UbrTrx;
struct UbrCleanupCtl;

// One unit of ubring cleanup work.
//
// The process-wide bthread timer thread runs every timer in the process (RPC
// timeouts, backup requests, bthread_usleep wakeups, ubring heartbeats and
// close checks), so a callback that calls into the UBS SDK -- which may wait
// on a network round trip to the SDK daemon -- delays all of them. The timer
// callbacks therefore only arbitrate ownership and post a job here; the
// blocking part runs on the dedicated cleanup worker.
struct UbrCleanupJob {
    enum Type {
        SHM_DRAIN = 0,      // release one pending shared-memory unmap
        TRX_CLEANUP = 1     // run the delayed cleanup of one trx generation
    };

    Type type = SHM_DRAIN;

    // SHM_DRAIN: `shm_step' performs the blocking SDK calls and settles the
    // producer's retry bookkeeping. It is a function pointer so this worker
    // stays independent of the shm backend, and so its scheduling can be
    // unit-tested without the SDK.
    void (*shm_step)(ShmList* shm_list) = nullptr;
    ShmList* shm_list = nullptr;

    // TRX_CLEANUP: the job takes over the timer/callback reference the
    // scheduling callback held on `ctl' and releases it when done. `work' is
    // the cleanup body (used by UbrDoAsynClearWork / UbrDoPassiveClearWork)
    // and only runs if the slot still belongs to `ubr_id'.
    UbrTrx* trx = nullptr;
    uint64_t ubr_id = 0;
    void (*work)(UbrTrx* trx, uint64_t ubr_id) = nullptr;
    UbrCleanupCtl* ctl = nullptr;
};

// A lazily started, process-wide bthread that drains UbrCleanupJob's. It is
// never stopped during a normal run: after a ShmMgrFini cycle the worker only
// needs to be quiescent (DrainAndWait) before the shared memory it may touch
// is finalized, and an idle worker performs no SDK call at all. Posting is
// serialized, so a job can never be queued behind a worker start that failed.
class UbrCleanupWorker {
public:
    // Post a shared-memory drain step. Returns false when the worker cannot be
    // started, in which case the caller must run `shm_step' inline.
    static bool PostShmDrain(ShmList* shm_list, void (*shm_step)(ShmList* shm_list));

    // Post a delayed trx cleanup, transferring the timer/callback reference of
    // `ctl'. Returns false when the worker cannot be started, in which case
    // the caller keeps the reference and must run the cleanup inline.
    static bool PostTrxCleanup(UbrTrx* trx, uint64_t ubr_id,
                               void (*work)(UbrTrx* trx, uint64_t ubr_id),
                               UbrCleanupCtl* ctl);

    // Wait until every posted job finished. Bounded by one in-flight SDK call;
    // returns immediately when the worker never started.
    static void DrainAndWait();
};

}  // namespace ubring
}  // namespace brpc

#endif //BRPC_UB_CLEANUP_WORKER_H
