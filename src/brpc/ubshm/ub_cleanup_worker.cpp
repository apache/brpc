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

#include <pthread.h>
#include <stdint.h>
#include "butil/atomicops.h"
#include "butil/containers/mpsc_queue.h"
#include "butil/logging.h"
#include "butil/scoped_lock.h"
#include "bthread/bthread.h"
#include "brpc/ubshm/ub_cleanup_worker.h"
#include "brpc/ubshm/ub_ring_manager.h"

namespace brpc {
namespace ubring {

// Test hook, never set in production: when non-zero, the lazy worker start
// inside Post() skips the real bthread_start_background call and behaves as
// if it had returned this value, so a transient start failure (e.g. ENOMEM)
// and its recovery can be exercised deterministically. It is only consulted
// while the worker is not started; read under g_worker_mtx, written by tests
// before posting.
butil::atomic<int> g_ub_test_worker_start_rc(0);
// Test hook, never set in production: when true, Post() fails immediately
// without starting the worker or enqueueing, so a failed post can be
// simulated regardless of whether the process-wide worker is already
// running (the start hook above is only consulted on the first post).
butil::atomic<bool> g_ub_test_fail_post(false);

namespace {

// Cleanup latency is tolerated in seconds (it only delays releasing memory of
// an already dead link), so the idle worker polls instead of spinning and the
// module needs no extra wakeup primitive.
constexpr uint64_t kCleanupIdlePollIntervalUs = 100 * 1000;

butil::atomic<bool> g_worker_started(false);
// Posted-but-not-finished jobs. A poster increments it before enqueueing and
// the worker decrements it after the job returned, so DrainAndWait can wait
// for quiescence without observing the queue itself.
butil::atomic<int> g_cleanup_inflight(0);
// The queue is never destroyed. The cleanup worker bthread outlives every
// orderly teardown -- the bthread runtime itself is not destructed at exit
// (see the note in task_control.cpp), so the worker can still be inside
// Dequeue() while static destructors run. MPSCQueue's destructor drains the
// queue, which would make that destruction a *second* concurrent consumer
// of a single-consumer queue (_cur_dequeue_node is a plain pointer) and
// double-free nodes; a timer callback firing during the destructor phase
// would additionally enqueue into a destroyed queue. Leaking the queue
// follows the brpc convention for globals entangled with threads that
// never stop (g_task_control itself); the kernel reclaims it at exit.
butil::MPSCQueue<UbrCleanupJob>* const g_cleanup_queue =
        new butil::MPSCQueue<UbrCleanupJob>();
// Serializes the posters (never taken by the worker) around the lazy start and
// the enqueue; see Post().
pthread_mutex_t g_worker_mtx = PTHREAD_MUTEX_INITIALIZER;

void RunTrxCleanup(const UbrCleanupJob& job) {
    UbrCleanupCtl* ctl = job.ctl;
    // The slot may have been released and reused while the job was queued; the
    // cleanup body must then be skipped, but the control object still has to
    // be settled (exactly as the timer callback did before the offload).
    if (UBRingManager::IsUbrTrxSlotUsed(job.trx->trx_mgr_index, job.ubr_id)) {
        job.work(job.trx, job.ubr_id);
    }
    ATOMIC_STORE(ctl->state, UBR_CLEANUP_DONE);
    ctl->ReleaseRef();                       // timer/callback reference
}

void* UbrCleanupWorkerMain(void*) {
    UbrCleanupJob job;
    for (;;) {
        if (!g_cleanup_queue->Dequeue(job)) {
            bthread_usleep(kCleanupIdlePollIntervalUs);
            continue;
        }
        if (job.type == UbrCleanupJob::SHM_DRAIN) {
            job.shm_step(job.shm_list);
        } else {
            RunTrxCleanup(job);
        }
        g_cleanup_inflight.fetch_sub(1);
    }
    return nullptr;
}

bool Post(const UbrCleanupJob& job) {
    // Test hook (never set in production): simulate a post that fails after
    // the worker is already running; see g_ub_test_fail_post.
    if (BAIDU_UNLIKELY(g_ub_test_fail_post.load(butil::memory_order_relaxed))) {
        return false;
    }
    // Serialize the posters (the worker itself never takes this mutex) so
    // that at most one cleanup worker is ever started: two posters seeing
    // "not started" at the same time would start two bthreads -- two
    // concurrent consumers of a single-consumer queue. The start attempt and
    // the enqueue also share one critical section, so a job is only ever
    // queued behind a worker whose start succeeded. Posting only happens on
    // cleanup paths, so the mutex is free in practice.
    BAIDU_SCOPED_LOCK(g_worker_mtx);
    if (!g_worker_started.load()) {
        // Retry the lazy start on every post instead of latching a failure:
        // bthread_start_background can fail transiently (ENOMEM), and a
        // permanent fallback would run every later SDK cleanup inline on the
        // global timer thread -- exactly what the worker exists to avoid,
        // and worst under the memory pressure that caused the failure. A
        // failed attempt leaves g_worker_started false and fails only THIS
        // post; the caller runs it inline as the safety net, and the next
        // post retries the start.
        bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
        bthread_attr_set_name(&attr, "UBCleanup");
        bthread_t tid;
        // A non-zero test hook overrides the start result without creating
        // a bthread; see g_ub_test_worker_start_rc.
        const int injected_rc = g_ub_test_worker_start_rc.load(
                butil::memory_order_relaxed);
        const int rc = injected_rc != 0 ? injected_rc
                : bthread_start_background(&tid, &attr,
                                           UbrCleanupWorkerMain, nullptr);
        if (BAIDU_UNLIKELY(rc != 0)) {
            LOG_EVERY_SECOND(ERROR) << "Fail to start the ubring cleanup "
                                    << "worker bthread, running the cleanup "
                                    << "inline.";
            return false;
        }
        g_worker_started.store(true);
    }
    // Increment before enqueueing so a concurrent DrainAndWait can never
    // observe quiescence while this job is still on its way to the queue.
    g_cleanup_inflight.fetch_add(1);
    g_cleanup_queue->Enqueue(job);
    return true;
}

}  // namespace

bool UbrCleanupWorker::PostShmDrain(ShmList* shm_list,
                                    void (*shm_step)(ShmList* shm_list)) {
    if (BAIDU_UNLIKELY(shm_list == nullptr || shm_step == nullptr)) {
        return false;
    }
    UbrCleanupJob job;
    job.type = UbrCleanupJob::SHM_DRAIN;
    job.shm_list = shm_list;
    job.shm_step = shm_step;
    return Post(job);
}

bool UbrCleanupWorker::PostTrxCleanup(UbrTrx* trx, uint64_t ubr_id,
                                      void (*work)(UbrTrx*, uint64_t),
                                      UbrCleanupCtl* ctl) {
    if (BAIDU_UNLIKELY(trx == nullptr || work == nullptr || ctl == nullptr)) {
        return false;
    }
    UbrCleanupJob job;
    job.type = UbrCleanupJob::TRX_CLEANUP;
    job.trx = trx;
    job.ubr_id = ubr_id;
    job.work = work;
    job.ctl = ctl;
    return Post(job);
}

void UbrCleanupWorker::DrainAndWait() {
    while (g_cleanup_inflight.load() > 0) {
        bthread_usleep(1000);
    }
}

}  // namespace ubring
}  // namespace brpc
