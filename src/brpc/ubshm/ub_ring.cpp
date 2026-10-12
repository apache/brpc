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

#include <errno.h>
#include <gflags/gflags.h>
#include <unistd.h>
#include <ctime>
#include <new>
#include "bthread/bthread.h"
#include "butil/logging.h"
#include "brpc/ubshm/ub_ring.h"
#include "brpc/ubshm/ub_cleanup_worker.h"
#include "brpc/ubshm/ub_ring_manager.h"
#include "brpc/ubshm/shm/shm_ipc.h"

namespace brpc {
namespace ubring {
uint32_t g_sleep_time[UBR_TASK_STEP_NUM] = {0};
DEFINE_int32(ub_disconnect_timeout_s, 5,
             "UBRing disconnection timeout in seconds.");
DEFINE_int32(ub_connect_timeout_s, 1,
             "UBRing connection timeout in seconds.");
DEFINE_int32(ub_hb_timer_interval_s, 5,
             "UBRing heartbeat timer interval in seconds.");
DEFINE_int32(ub_hb_retry_cnt, 10,
             "UBRing heartbeat retry count.");
DEFINE_int32(ub_event_queue_timer_interval_us, 100,
             "UBRing disconnection check interval in microseconds.");
DEFINE_int32(ub_event_queue_timer_interval_max_us, 10000,
             "UBRing upper bound of the close-check polling interval in "
             "microseconds while the link is idle; the interval backs off "
             "from ub_event_queue_timer_interval_us up to this value. "
             "Set to 0 to keep the interval steady (back-off disabled).");

// Exponential back-off multiplier of the close-check polling interval.
constexpr uint64_t kCloseCheckBackoffFactor = 2;

UBRing::UBRing()
{}
UBRing::~UBRing()
{}

RETURN_CODE UBRing::UbrTrxMapShm(SHM *local_shm, SHM *remote_shm)
{
    RETURN_CODE rc = UbrTrxMapLocalShm(local_shm);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx map local shared memory failed.";
        return rc;
    }
    rc = UbrTrxMapRemoteShm(remote_shm);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx map remote shared memory failed.";
        return rc;
    }
    return UBRING_OK;
}

// Stop a per-trx timer before its slot becomes reusable. UbrTimerDelAndWait
// returns only after a callback that was already dispatched has left the trx,
// which is what lets the caller clear the trx and free its shared memory
// afterwards. When the caller *is* the callback of that very timer, the facade
// recognizes it and degrades to the non-blocking delete (waiting would join the
// calling callback), so no caller has to know whether it runs on the timer
// thread. A sibling timer callback cannot be running concurrently either,
// because bthread dispatches every timer callback from one global timer thread
// (TimerThread in bthread/timer_thread.{h,cpp} is created by a single
// pthread_once).
static void UbrStopTrxTimer(butil::atomic<UbrTimerId>* slot) {
    UbrTimerDelAndWait(slot);
}

static void UbrDoAsynClearWork(UbrTrx *trx, uint64_t expect_ubr_id) {
    if (BAIDU_UNLIKELY(UBRing::UbrTrxFreeShm(trx) != UBRING_OK)) {
        LOG(ERROR) << "Trx close, wait for local shm " << trx->local_shm.name << " free fail.";
    }
    if (BAIDU_UNLIKELY(UBRingManager::ReleaseUbrTrxFromMgr(trx, expect_ubr_id) != UBRING_OK)) {
        LOG(ERROR) << "Trx close, release shm " << trx->local_shm.name << " trx failed.";
    }
}

static void UbrDoPassiveClearWork(UbrTrx *trx, uint64_t expect_ubr_id) {
    int rc = ShmLocalFree(&trx->remote_shm);
    if (rc != UBRING_OK) {
        LOG(ERROR) << "Trx passive clear, delete remote shm " << trx->remote_shm.name
                   << " failed. ret=" << rc;
    }
    rc = ShmLocalFree(&trx->local_shm);
    if (rc != UBRING_OK) {
        LOG(ERROR) << "Trx passive clear, delete local shm " << trx->local_shm.name
                   << " failed. ret=" << rc;
    }
    if (BAIDU_UNLIKELY(UBRingManager::ReleaseUbrTrxFromMgr(trx, expect_ubr_id) != UBRING_OK)) {
        LOG(ERROR) << "Trx passive clear, release shm " << trx->local_shm.name << " trx failed.";
    }
}

// Schedule the delayed cleanup of `trx'. The cleanup ownership lives in the
// per-acquisition control object, so exactly one of the delayed-clear
// callback and a force close ever runs the cleanup. `work' is the cleanup
// body, used directly when the timer cannot be started.
static RETURN_CODE UbrScheduleClearTimer(UbrTrx *trx, uint64_t expect_ubr_id,
                                         void* (*cb)(void*, uint64_t),
                                         void (*work)(UbrTrx*, uint64_t)) {
    if (BAIDU_UNLIKELY(trx == nullptr || trx->local_shm.addr == nullptr)) {
        return UBRING_OK;                    // released trx, stale event
    }
    // A callback that outlived its generation must not capture the id of the
    // slot's new occupant, nor schedule cleanup for that new transaction.
    if (BAIDU_UNLIKELY(ATOMIC_LOAD(trx->ubr_id) != expect_ubr_id)) {
        return UBRING_OK;                    // stale event on a reused slot
    }
    if (trx->cleanup_ctl.load() != nullptr) {
        return UBRING_OK;                    // cleanup already scheduled
    }
    auto* ctl = new (std::nothrow) UbrCleanupCtl();
    if (BAIDU_UNLIKELY(ctl == nullptr)) {
        LOG(ERROR) << "Fail to malloc ubr cleanup ctl.";
        return UBRING_ERR;
    }
    ctl->trx = trx;
    ctl->ubr_id = expect_ubr_id;
    ctl->state.store(UBR_CLEANUP_PENDING);
    ctl->timer = nullptr;
    ctl->ref.store(2);                       // timer/callback + starter; the
                                             // manager anchor is taken by
                                             // TryPublishUnitCleanupCtl

    // The manager publishes the ctl on the trx and anchors it in the pool slot
    // in one critical section, so a concurrent force close can never observe
    // the trx-side publication without its anchor.
    if (!UBRingManager::TryPublishUnitCleanupCtl(trx->trx_mgr_index,
                                                 ctl->ubr_id, ctl)) {
        // Another schedule won the slot, or the slot was released (and
        // possibly reused) before we could anchor: force close or the new
        // occupant owns it now. Nothing was armed or published -- drop our
        // two references.
        ctl->ReleaseRef();                   // timer/callback reference, never armed
        ctl->ReleaseRef();                   // starter reference
        return UBRING_OK;
    }
    // One-shot on purpose: this only delays the cleanup so in-flight IO can
    // drain, and FLAGS_ub_flying_io_timeout_s == 0 legitimately means "do not
    // delay".
    RETURN_CODE rc = UbrTimerStart(&ctl->timer,
            (uint64_t)FLAGS_ub_flying_io_timeout_s * SEC_TO_USEC, cb, ctl,
            expect_ubr_id);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        // The timer was never scheduled: this path owns the manager,
        // timer/callback and starter references. Hand the cleanup to the
        // worker -- the caller may be the timer thread -- so the trx does not
        // end up with neither timers nor a queued cleanup, and run it inline
        // only when the worker cannot be started.
        int state_expected = UBR_CLEANUP_PENDING;
        if (ATOMIC_COMPARE_EXCHANGE_STRONG(ctl->state, state_expected, UBR_CLEANUP_RUNNING)) {
            if (!UbrCleanupWorker::PostTrxCleanup(trx, ctl->ubr_id, work, ctl)) {
                // Gate on the slot still being used by this generation: force
                // close may have claimed the cleanup and released the slot
                // (which already freed the trx resources) after we were
                // anchored.
                if (UBRingManager::IsUbrTrxSlotUsed(trx->trx_mgr_index, ctl->ubr_id)) {
                    work(trx, ctl->ubr_id);
                }
                ATOMIC_STORE(ctl->state, UBR_CLEANUP_DONE);
                ctl->ReleaseRef();           // timer/callback reference
            }
            // When the job was posted, the worker owns the timer/callback
            // reference and settles state/anchor exactly like the timer-fired
            // path; the manager anchor keeps the control object alive until
            // the slot is acquired again or UbrMgrFini retires it.
        } else {
            // A force close already owns the cleanup. It could not take the
            // reference through the never-armed timer, so release it here.
            ctl->ReleaseRef();               // timer/callback reference
        }
        ctl->ReleaseRef();                   // starter reference
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY(ATOMIC_LOAD(trx->ubr_id) != ctl->ubr_id)) {
        // Published onto a slot that was released and reused meanwhile.
        if (UbrTimerDel(&ctl->timer) == 0) {
            ctl->ReleaseRef();               // timer/callback reference
        }
        UBRingManager::DetachUnitCleanupCtl(trx->trx_mgr_index, ctl);
        ctl->ReleaseRef();                   // starter reference
        return UBRING_OK;
    }
    ctl->ReleaseRef();                       // starter reference
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrTrxClose() {
    // _trx is still nullptr when the setup failed: UbrAllocateLocalShm and
    // UbrAllocateServerShm reset it on their error paths while the endpoint
    // keeps the non-null _ub_ring and calls UbrTrxClose from
    // DeallocateResources. Reject before the generation load, which would
    // dereference the null _trx.
    if (BAIDU_UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "Trx close failed, client trx is null.";
        return UBRING_ERR;
    }
    const uint64_t expect_ubr_id = ATOMIC_LOAD(_trx->ubr_id);
    RETURN_CODE close_check_rc = UbrTrxCloseCheck(_trx, expect_ubr_id);
    if (BAIDU_UNLIKELY(close_check_rc != UBRING_OK)) {
        if (close_check_rc == UBRING_REENTRY) {
            LOG(INFO) << "Trx close skipped, already closing, local name=" << _trx->local_shm.name;
            return UBRING_OK;
        }
        return UBRING_ERR;
    }
    if (_trx->ubr_rx.remote_tx_event_q.addr != nullptr) {
        ((UbrEventQMsg *)_trx->ubr_rx.remote_tx_event_q.addr)->flag = UBR_STATE_CLOSING;
    }

    const uint32_t disconnect_timeout_s = FLAGS_ub_disconnect_timeout_s;
    uint64_t start_time = GetCurNanoSeconds();

    if (_trx->ubr_tx.local_tx_event_q.addr != nullptr && ((UbrEventQMsg *)_trx->ubr_tx.local_tx_event_q.addr)->flag == UBR_STATE_CONNECTED) {
        ((UbrEventQMsg *)_trx->ubr_tx.local_tx_event_q.addr)->flag = UBR_STATE_CLOSED;
        _trx->ubr_tx.trx_state = UBR_STATE_CLOSED;
    }

    if (_trx->ubr_tx.remote_rx_event_q.addr != nullptr) {
        ((UbrEventQMsg *)_trx->ubr_tx.remote_rx_event_q.addr)->flag = UBR_STATE_CLOSED;
    }
    while (_trx->ubr_rx.local_rx_event_q.addr != nullptr && ((UbrEventQMsg *)_trx->ubr_rx.local_rx_event_q.addr)->flag != UBR_STATE_CLOSED) {
        UbrSetSleepTask(UBR_TASK_CLOSE);
        if (HasTimedOut(start_time, disconnect_timeout_s) != UBRING_OK) {
            LOG(WARNING) << "Local shm " << _trx->local_shm.name
            << " wait for the peer to close timed out, force cleanup.";
            _trx->ubr_rx.trx_state = UBR_STATE_CLOSED;
            // Wait out the close/heartbeat callbacks, which may schedule a
            // delayed cleanup, then settle the cleanup ownership: force
            // runs the cleanup itself when it can claim it, and leaves it
            // to an already running delayed-clear callback otherwise.
            UbrTimerDelAndWait(&_trx->close_timer);
            UbrTimerDelAndWait(&_trx->hb_timer);
            // Snapshot the delayed cleanup and, when there is none, claim the
            // cleanup for this force close in one critical section. Splitting
            // the two would let a concurrent SDK-fault callback publish a new
            // cleanup after our empty snapshot, leaving two paths running the
            // cleanup of the same shared memory.
            UbrCleanupCtl* ctl = nullptr;
            const UbrCleanupClaim claim = UBRingManager::ClaimTrxCleanupForced(
                    _trx->trx_mgr_index, expect_ubr_id, &ctl);
            bool cleanup_owned = false;
            if (claim == UBR_CLEANUP_CLAIM_HAS_CTL) {
                int expected = UBR_CLEANUP_PENDING;
                if (ATOMIC_COMPARE_EXCHANGE_STRONG(ctl->state, expected, UBR_CLEANUP_RUNNING)) {
                    cleanup_owned = true;
                    if (UbrTimerDel(&ctl->timer) == 0) {
                        ctl->ReleaseRef();   // timer/callback reference
                    }
                }
            } else if (claim == UBR_CLEANUP_CLAIM_OWNED_NULL) {
                cleanup_owned = true;
            }
            if (cleanup_owned) {
                if (_trx->ubr_tx.remote_rx_event_q.addr != nullptr) {
                    ((UbrEventQMsg *)_trx->ubr_tx.remote_rx_event_q.addr)->flag = UBR_STATE_CLOSED;
                }
                if (BAIDU_UNLIKELY(UbrTrxFreeShm(_trx) != UBRING_OK)) {
                    LOG(WARNING) << "Force close, local shm " << _trx->local_shm.name << " free failed.";
                }
                if (BAIDU_UNLIKELY(UBRingManager::ReleaseUbrTrxFromMgr(_trx, expect_ubr_id) != UBRING_OK)) {
                    LOG(WARNING) << "Force close, release trx " << _trx->local_shm.name << " failed.";
                }
                if (ctl != nullptr) {
                    ATOMIC_STORE(ctl->state, UBR_CLEANUP_DONE);
                }
            }
            if (ctl != nullptr) {
                ctl->ReleaseRef();               // snapshot reference
            }
            return UBRING_ERR_TIMEOUT;
        }
        bthread_usleep(1000);  // 1ms, yield to other bthreads
    }
    _trx->ubr_rx.trx_state = UBR_STATE_CLOSED;
    RETURN_CODE rc;
    if (BAIDU_UNLIKELY((rc = ClearTrxResource(_trx, expect_ubr_id)) != UBRING_OK)) {
        if (rc == UBRING_REENTRY) {
            LOG(INFO) << "Trx close, peer is closing, trx local name=" << _trx->local_shm.name;
            return UBRING_OK;
        }
        LOG(ERROR) << "Trx close, clear trx resource failed, trx local name=" << _trx->local_shm.name;
        return UBRING_ERR;
    }
    // Unlink local shm name immediately so process exit does not leave visible leftovers.
    RETURN_CODE unlink_rc = ShmFree(&_trx->local_shm);
    if (unlink_rc != UBRING_OK && unlink_rc != SHM_ERR_NOT_FOUND && unlink_rc != SHM_ERR_RESOURCE_ATTACHED) {
        LOG(WARNING) << "Trx close, unlink local shm failed, trx local name=" << _trx->local_shm.name
                     << ", rc=" << unlink_rc;
    }
    return UBRING_OK;
}

// Back-off policy of the close-check timer: fast while there is traffic or
// a close in progress, doubling up to the cap while idle.
static uint64_t UbrCloseTimerBackoff(void* arg, uint64_t cur_interval_us) {
    auto* trx = (UbrTrx*)arg;
    auto* local_rx_event_q = (UbrEventQMsg *)trx->ubr_rx.local_rx_event_q.addr;
    auto* local_tx_event_q = (UbrEventQMsg *)trx->ubr_tx.local_tx_event_q.addr;
    const uint64_t in_io_id = ATOMIC_LOAD(trx->ubr_rx.in_io_id);
    const uint64_t out_io_id = ATOMIC_LOAD(trx->ubr_tx.out_io_id);
    if (BAIDU_UNLIKELY(local_rx_event_q == nullptr)) {
        return (uint64_t)FLAGS_ub_event_queue_timer_interval_us;
    }
    const bool has_traffic = (in_io_id != trx->close_chk_in_io_id) ||
                             (out_io_id != trx->close_chk_out_io_id);
    const bool closing = (local_rx_event_q->flag != UBR_STATE_CONNECTED);
    trx->close_chk_in_io_id = in_io_id;
    trx->close_chk_out_io_id = out_io_id;
    if (has_traffic || closing || local_tx_event_q == nullptr) {
        return (uint64_t)FLAGS_ub_event_queue_timer_interval_us;
    }
    uint64_t next = cur_interval_us * kCloseCheckBackoffFactor;
    const uint64_t max_us = (uint64_t)FLAGS_ub_event_queue_timer_interval_max_us;
    if (max_us > 0 && next > max_us) {
        next = max_us;
    } else if (max_us == 0) {
        next = cur_interval_us;
    }
    return next;
}

RETURN_CODE UBRing::UbrAddCloseTimer() {
    if (BAIDU_UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "Trx add close timer failed, trx is null.";
        return UBRING_ERR;
    }

    // Name the offending flag: the facade would also reject a zero interval,
    // but only this log can point at the configuration that produced it.
    if (BAIDU_UNLIKELY(FLAGS_ub_event_queue_timer_interval_us <= 0)) {
        LOG(ERROR) << "Start ubr close timer failed, ub_event_queue_timer_interval_us="
                   << FLAGS_ub_event_queue_timer_interval_us << " must be positive.";
        return UBRING_ERR;
    }
    const uint32_t interval_us = FLAGS_ub_event_queue_timer_interval_us;
    _trx->close_chk_in_io_id = ATOMIC_LOAD(_trx->ubr_rx.in_io_id);
    _trx->close_chk_out_io_id = ATOMIC_LOAD(_trx->ubr_tx.out_io_id);
    RETURN_CODE rc = UbrTimerStartPeriodic(&_trx->close_timer, 0, interval_us,
                                           UbrTrxCloseCallback, (void*)_trx,
                                           ATOMIC_LOAD(_trx->ubr_id),
                                           UbrCloseTimerBackoff);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Start ubr close timer failed, trx local name=" << _trx->local_shm.name;
        return UBRING_ERR;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrAddTimer() {
    // The failure branch below reports _trx->local_shm.name, so a null _trx
    // would crash before the guards in UbrAddCloseTimer/UbrAddHBTimer get to
    // return their error. No current caller reaches this with a null _trx;
    // reject it anyway for the same reason those two do.
    if (BAIDU_UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "Trx add timer failed, trx is null.";
        return UBRING_ERR;
    }
    // Arm both timers with the manager lock held and only while the manager is
    // not shutting down: UbrMgrFini then knows that no per-trx timer can appear
    // after it took the shutdown flag, so waiting once for the timers it
    // snapshotted is enough. Nothing here may block on a timer callback -- the
    // callbacks take the manager lock themselves.
    bool armed_close = false;
    bool armed_hb = false;
    const bool allowed = UBRingManager::ArmTimersExclusive(
        [this, &armed_close, &armed_hb]() {
            armed_close = (UbrAddCloseTimer() == UBRING_OK);
            if (armed_close) {
                armed_hb = (UbrAddHBTimer() == UBRING_OK);
            }
            return armed_close && armed_hb;
        });
    if (BAIDU_UNLIKELY(!allowed || !armed_close || !armed_hb)) {
        // Disarm outside the arming exclusion: waiting for a dispatched
        // callback while holding the manager lock would deadlock against the
        // callbacks that take it.
        UbrTimerDelAndWait(&_trx->close_timer);
        UbrTimerDelAndWait(&_trx->hb_timer);
        if (!allowed) {
            // A shutdown refusal is expected, not a failure to report as one.
            LOG(WARNING) << "Ubr " << _trx->local_shm.name
                         << " timer not armed, ubr manager is shutting down.";
        } else {
            LOG(ERROR) << "Ubr " << _trx->local_shm.name << " add timer failed, close="
                       << armed_close << ", hb=" << armed_hb;
        }
        return UBRING_ERR;
    }
    return UBRING_OK;
}

// Per-trx timer callbacks run on the process-wide bthread timer thread and can
// still be dispatched after the UBRing that armed them started tearing down.
// Every access to the pooled UbrTrx is therefore gated on the generation the
// timer was armed with (the `gen' argument): the callbacks reject a stale fire
// here, and every scheduling path re-checks expect_ubr_id under
// g_ubr_trx_mgr_mtx (TryPublishUnitCleanupCtl) and again before running the
// cleanup work (IsUbrTrxSlotUsed).
//
// A slot cannot be released and reused underneath a callback that already
// passed that check, and the guarantee is a synchronization one, not a timing
// one. bthread dispatches all timer callbacks from a single global timer
// thread, so two per-trx callbacks never run concurrently, and the delayed
// clear arbitration (the ctl->state compare-exchange in the clear callbacks)
// is serialized after every per-trx callback that was dispatched before it.
// The cleanup body itself no longer runs on the timer thread but on the
// cleanup worker (UbrCleanupWorker): it is kept away from a released or reused
// slot by the generation gate it re-applies under g_ubr_trx_mgr_mtx right
// before running the work (IsUbrTrxSlotUsed), and by the scheduling paths that
// wait for the per-trx callbacks through UbrStopTrxTimer -> UbrTimerDelAndWait
// before they may clear the trx or hand its slot on. Entering a close claims
// the slot for the generation under g_ubr_trx_mgr_mtx
// (UBRingManager::TryClaimTrxClose), so a caller working from a stale snapshot
// -- the faulty-shm event, which looks the slot up before the claim -- cannot
// touch the next occupant. Teardown that happens on any other thread then waits
// for a dispatched callback through UbrStopTrxTimer -> UbrTimerDelAndWait
// before it clears the trx or frees its shared memory (UbrClearResourceCheck,
// UbrPassiveClearTrx), and the force close and UbrMgrFini paths do the same.
// That wait also covers a callback that stopped its own periodic timer from
// inside: the timer facade keeps such a task anchored until the callback
// returns precisely so these teardown paths cannot mistake a running callback
// for an idle trx. FLAGS_ub_flying_io_timeout_s only delays the delayed-clear
// work so that in-flight IO can drain; the callback side is already ordered by
// the timer thread. The UbrTrx pool itself is freed only by UbrMgrFini, which
// waits for these timers first.
void* UBRing::UbrTrxCloseCallback(void* args, uint64_t gen) {
    auto* trx = (UbrTrx*) args;
    // UbrTrxCallbackCheck rejects a null trx (and cleared queues) before we
    // dereference trx->ubr_id for the stale-generation check below.
    if (BAIDU_UNLIKELY(UBRing::UbrTrxCallbackCheck(trx) != UBRING_OK)) {
        return nullptr;
    }
    // Reject a stale fire whose trx slot was released and reused: it must not
    // touch the slot's new occupant.
    if (BAIDU_UNLIKELY(ATOMIC_LOAD(trx->ubr_id) != gen)) {
        return nullptr;
    }

    auto* local_rx_event_q = (UbrEventQMsg *)trx->ubr_rx.local_rx_event_q.addr;
    auto* local_tx_event_q = (UbrEventQMsg *)trx->ubr_tx.local_tx_event_q.addr;
    // UbrTrxCallbackCheck validated these before the generation check; repeat
    // it here because a concurrent release may have cleared them since, and the
    // dereference below is unconditional. UbrTrxHBCallback carries the same
    // guard.
    if (BAIDU_UNLIKELY(local_rx_event_q == nullptr || local_tx_event_q == nullptr)) {
        return nullptr;
    }
    if (local_rx_event_q->flag != UBR_STATE_CLOSED || local_tx_event_q->flag == UBR_STATE_CLOSED) {
        return nullptr;
    }
    trx->ubr_rx.trx_state = UBR_STATE_CLOSED;
    do {
        if (ATOMIC_LOAD(trx->close_cnt) == 0) {
            break;
        }
        ATOMIC_SUB(trx->close_cnt, 1);

        if (local_tx_event_q->flag == UBR_STATE_CONNECTED || ATOMIC_LOAD(trx->close_cnt) == 1) {
            local_tx_event_q->flag = UBR_STATE_CLOSED;
            trx->ubr_tx.trx_state = UBR_STATE_CLOSED;
        }
        UbrEventQMsg* remote_rx_event_q = (UbrEventQMsg *)trx->ubr_tx.remote_rx_event_q.addr;
        if (remote_rx_event_q == nullptr) {
            LOG(ERROR) << "Trx close callback failed, " << trx->local_shm.name << " remote_rx_event_q is NULL.";
            break;
        }
        remote_rx_event_q->flag = UBR_STATE_CLOSED;
        RETURN_CODE clear_rc = ClearTrxResource(trx, gen);
        if (BAIDU_UNLIKELY(clear_rc != UBRING_OK && clear_rc != UBRING_REENTRY)) {
            LOG(ERROR) << "Trx close callback failed, " << trx->local_shm.name << " clear trx resource failed.";
            break;
        }
    } while (0);
    return nullptr;
}

RETURN_CODE UBRing::UbrAddHBTimer() {
    if (BAIDU_UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "Trx add heartbeat timer failed, trx is null.";
        return UBRING_ERR;
    }

    // A zero or negative ub_hb_timer_interval_s would make the heartbeat fire
    // once (or, after the unsigned conversion, effectively never), so the link
    // would silently lose its liveness detection. UbrTimerStartPeriodic
    // rejects the zero interval; reject the negative one here as well.
    if (BAIDU_UNLIKELY(FLAGS_ub_hb_timer_interval_s <= 0)) {
        LOG(ERROR) << "Start ubr heartbeat timer failed, ub_hb_timer_interval_s="
                   << FLAGS_ub_hb_timer_interval_s << " must be positive.";
        return UBRING_ERR;
    }
    const uint64_t interval_us = (uint64_t)FLAGS_ub_hb_timer_interval_s * SEC_TO_USEC;
    RETURN_CODE rc = UbrTimerStartPeriodic(&_trx->hb_timer, 0, interval_us,
                                           UbrTrxHBCallback, (void*)_trx,
                                           ATOMIC_LOAD(_trx->ubr_id));
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Start ubr heartbeat timer failed.";
        return UBRING_ERR;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrPassiveClearTrx(UbrTrx *trx, uint64_t expect_ubr_id) {
    RETURN_CODE passive_close_check_rc = UbrTrxCloseCheck(trx, expect_ubr_id);
    if (BAIDU_UNLIKELY(passive_close_check_rc != UBRING_OK)) {
        if (passive_close_check_rc == UBRING_REENTRY) {
            LOG(INFO) << "Passive close skipped, active close in progress, name=" << trx->local_shm.name;
            return ClearTrxResource(trx, expect_ubr_id);
        }
        // UBRING_ERR means this generation no longer owns the slot: the event
        // belongs to a trx that was released (and possibly reused) meanwhile,
        // so its state and timers must not be touched at all.
        return UBRING_ERR;
    }
    trx->ubr_tx.trx_state = UBR_STATE_CLOSED;
    trx->ubr_rx.trx_state = UBR_STATE_CLOSED;
    UbrStopTrxTimer(&trx->close_timer);
    UbrStopTrxTimer(&trx->hb_timer);
    // Wait for in-flight IO on a one-shot timer instead of sleeping on the
    // timer thread.
    return UbrScheduleClearTimer(trx, expect_ubr_id,
                                 UbrPassiveClearCallback, UbrDoPassiveClearWork);
}

void* UBRing::UbrPassiveClearCallback(void* args, uint64_t) {
    auto* ctl = (UbrCleanupCtl*)args;
    if (BAIDU_UNLIKELY(ctl == nullptr)) {
        LOG(ERROR) << "Trx passive clear callback failed, ctl is null.";
        return nullptr;
    }
    int expected = UBR_CLEANUP_PENDING;
    if (!ATOMIC_COMPARE_EXCHANGE_STRONG(ctl->state,
                                        expected, UBR_CLEANUP_RUNNING)) {
        // Force close owns the cleanup; this fire still holds the
        // timer/callback reference inherited from the schedule.
        ctl->ReleaseRef();
        return nullptr;
    }
    // The ownership arbitration above is settled; hand the blocking work to
    // the cleanup worker instead of running it on the timer thread, which
    // serves every timer in the process. The job takes over the timer/callback
    // reference.
    UbrTrx* trx = ctl->trx;
    if (!UbrCleanupWorker::PostTrxCleanup(trx, ctl->ubr_id,
                                          UbrDoPassiveClearWork, ctl)) {
        if (BAIDU_UNLIKELY(UBRingManager::IsUbrTrxSlotUsed(trx->trx_mgr_index, ctl->ubr_id))) {
            UbrDoPassiveClearWork(trx, ctl->ubr_id);
        }
        ATOMIC_STORE(ctl->state, UBR_CLEANUP_DONE);
        ctl->ReleaseRef();                   // timer/callback reference
    }
    return nullptr;
}

void* UBRing::UbrTrxHBCallback(void* args, uint64_t gen) {
    auto* trx = (UbrTrx*) args;
    // UbrTrxCallbackCheck rejects a null trx (and cleared queues) before we
    // dereference trx->ubr_id for the stale-generation check below.
    if (BAIDU_UNLIKELY(UbrTrxCallbackCheck(trx) != UBRING_OK)) {
        return nullptr;
    }
    // Reject a stale fire whose trx slot was released and reused: it must not
    // touch the slot's new occupant.
    if (BAIDU_UNLIKELY(ATOMIC_LOAD(trx->ubr_id) != gen)) {
        return nullptr;
    }

    auto* local_data_status = (UbrDataStatusQMsg *)trx->ubr_tx.local_data_status_q.addr;
    auto* remote_data_status = (UbrDataStatusQMsg *)trx->ubr_rx.remote_data_status_q.addr;
    if (BAIDU_UNLIKELY(local_data_status == nullptr || remote_data_status == nullptr)) {
        LOG(ERROR) << "Heartbeat error, datastatus is NULL.";
        return nullptr;
    }

    if (trx->ubr_tx.trx_state != UBR_STATE_CONNECTED || trx->ubr_rx.trx_state != UBR_STATE_CONNECTED) {
        LOG_EVERY_SECOND(INFO) << "Heartbeat cannot be started, wait connected state.";
        return nullptr;
    }

    remote_data_status->heart_beat = 1;
    if (local_data_status->heart_beat == 1) {
        local_data_status->heart_beat = 0;
        trx->ubr_tx.hb_retry_cnt = 0;
        return nullptr;
    }

    ++trx->ubr_tx.hb_retry_cnt;
    if (trx->ubr_tx.hb_retry_cnt <= FLAGS_ub_hb_retry_cnt) {
        return nullptr;
    }

    int fd = (int)trx->local_shm.fd;
    LOG(INFO) << "Ubr heartbeat, start to clear trx resource. shm_fd=" << fd << ", shm_name=" << trx->local_shm.name;
    UbrPassiveClearTrx(trx, gen);
    LOG(INFO) << "Ubr heartbeat clear trx resource finish.";
    return nullptr;
}

RETURN_CODE UBRing::UbrAddAsynClearTimer(UbrTrx *trx, uint64_t expect_ubr_id) {
    if (BAIDU_UNLIKELY(trx == nullptr)) {
        LOG(ERROR) << "Trx add close timer failed, trx is null.";
        return UBRING_ERR;
    }
    return UbrScheduleClearTimer(trx, expect_ubr_id,
                                 UbrAsynClearCallback, UbrDoAsynClearWork);
}

void *UBRing::UbrAsynClearCallback(void *args, uint64_t)
{
    auto* ctl = (UbrCleanupCtl*) args;
    if (BAIDU_UNLIKELY(ctl == nullptr)) {
        LOG(ERROR) << "Trx close, ctl is null.";
        return nullptr;
    }
    int expected = UBR_CLEANUP_PENDING;
    if (!ATOMIC_COMPARE_EXCHANGE_STRONG(ctl->state,
                                        expected, UBR_CLEANUP_RUNNING)) {
        // Force close owns the cleanup; this fire still holds the
        // timer/callback reference inherited from the schedule.
        ctl->ReleaseRef();
        return nullptr;
    }
    // Same offload as the passive path: the timer thread only arbitrates
    // ownership, the worker does the SDK-backed cleanup.
    UbrTrx* trx = ctl->trx;
    if (!UbrCleanupWorker::PostTrxCleanup(trx, ctl->ubr_id,
                                          UbrDoAsynClearWork, ctl)) {
        if (BAIDU_UNLIKELY(UBRingManager::IsUbrTrxSlotUsed(trx->trx_mgr_index, ctl->ubr_id))) {
            UbrDoAsynClearWork(trx, ctl->ubr_id);
        }
        ATOMIC_STORE(ctl->state, UBR_CLEANUP_DONE);
        ctl->ReleaseRef();                   // timer/callback reference
    }
    return nullptr;
}

int UBRing::UbrTrxSend(const void *buf, uint32_t buf_len)
{
    if (BAIDU_UNLIKELY(CheckTrxSendPreCheck(_trx) != UBRING_OK)) {
        return UBRING_ERR;
    }
    // 1.2 Calculate space
    auto *data_status_msg = (UbrDataStatusQMsg *)_trx->ubr_tx.local_data_status_q.addr;
    auto *data_msg = (UbrMsgFormat *)_trx->ubr_tx.remote_data_q.addr;
    uint32_t cap = _trx->ubr_tx.capacity;
    uint32_t tail = data_status_msg->tail;
    uint32_t remain_chunk_num =
        (_trx->ubr_tx.write_pos > tail) ? (tail + cap - _trx->ubr_tx.write_pos) : (tail - _trx->ubr_tx.write_pos);
    uint32_t need_msg_chunk_num = CalcUbrMsgChunkCnt(buf_len);
    if (need_msg_chunk_num >= cap) {
        LOG(ERROR) << "Ubr send failed, payload length=" << buf_len
                   << " needs " << need_msg_chunk_num << " chunks, capacity=" << cap << ".";
        errno = EMSGSIZE;
        return UBRING_ERR;
    }
    if (remain_chunk_num < need_msg_chunk_num) {
        return UBRING_RETRY;
    }
    UbrMsgFormat *msg = &(_trx->ubr_tx.local_msg_space);
    uint32_t total_send_len = 0;
    uint32_t remain_buf_len = buf_len;
    uint8_t is_last_pkt = 0;
    const uint64_t io_seq = ATOMIC_ADD(_trx->ubr_tx.out_io_id, 1) + 1;
    ((UbrEventQMsg *)_trx->ubr_tx.remote_rx_event_q.addr)->io_id = io_seq;
    while (remain_buf_len > 0) {
        is_last_pkt = (uint8_t)(remain_buf_len <= UBR_MSG_PAYLOAD_LEN);
        msg->header[UBR_MSG_FLAG_INDEX] = is_last_pkt ? UBR_MSG_CHUNK_EOF : UBR_MSG_CHUNK_EXIST;
        msg->header[UBR_MSG_LEN_INDEX] = is_last_pkt ? (uint8_t)remain_buf_len : UBR_MSG_PAYLOAD_LEN;
        msg->header[UBR_MSG_CUR_INDEX] = 0;
        memcpy(msg->payload.inner, (const uint8_t *)buf + total_send_len, msg->header[UBR_MSG_LEN_INDEX]);
        Copy64Byte((int8_t *)&data_msg[_trx->ubr_tx.write_pos], (int8_t *)msg);
        _trx->ubr_tx.write_pos = (_trx->ubr_tx.write_pos + 1) % cap;
        total_send_len += msg->header[UBR_MSG_LEN_INDEX];
        remain_buf_len -= msg->header[UBR_MSG_LEN_INDEX];
    }
    return (int)total_send_len;
}

int UBRing::UbrTrxRecv(void *buf, uint32_t buf_len)
{
    RETURN_CODE rc = UBRING_OK;
    if (BAIDU_UNLIKELY((rc = CheckTrxRecvParam(_trx, buf, buf_len)) != UBRING_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }
    UbrMsgFormat *data_msg = (UbrMsgFormat *)_trx->ubr_rx.local_data_q.addr;
    uint32_t read_pos_end = _trx->ubr_rx.read_pos;
    uint8_t flag = data_msg[read_pos_end].header[UBR_MSG_FLAG_INDEX];
    if (flag == UBR_MSG_CHUNK_NONE) {
        return UBRING_RETRY;
    }
    return UbrTrxRecvBlockMode(static_cast<uint8_t *>(buf), buf_len);
}

int UBRing::UbrTrxRecvBlockMode(uint8_t *dest, uint32_t buf_len)
{
    RETURN_CODE rc = UBRING_OK;
    if (BAIDU_UNLIKELY((rc = CheckTrxRecvParam(_trx, dest, buf_len)) != UBRING_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }

    int32_t total_copied = 0;
    int32_t remaining_len = (int32_t)buf_len;
    bool not_eof_encountered = true;

    UbrRx *ubr_rx = &_trx->ubr_rx;
    UbrMsgFormat *data_msg = (UbrMsgFormat *)ubr_rx->local_data_q.addr;
    bool need_update_epoll_eof_pos = ubr_rx->read_pos == ubr_rx->ep_eof_pos;

    while (not_eof_encountered && remaining_len > 0) {
        if (BAIDU_UNLIKELY(CheckTrxRecvPreCheck(_trx) != UBRING_OK)) {
            return UBRING_ERR;
        }
        UbrMsgFormat *current_chunk = &data_msg[ubr_rx->read_pos];
        uint8_t flag = current_chunk->header[UBR_MSG_FLAG_INDEX];
        if (flag == UBR_MSG_CHUNK_NONE) {
            if (total_copied > 0) {
                break;
            }
            errno = EAGAIN;
            return -1;
        }
        if (flag == UBR_MSG_CHUNK_EOF) {
            not_eof_encountered = false;
        }
        uint8_t chunk_msg_len = current_chunk->header[UBR_MSG_LEN_INDEX];
        uint8_t cur_index = current_chunk->header[UBR_MSG_CUR_INDEX];
        if (BAIDU_UNLIKELY(!IsRecvChunkHeaderValid(chunk_msg_len, cur_index))) {
            LOG(ERROR) << "Trx recv failed, invalid chunk header msg_len="
                       << (uint32_t)chunk_msg_len << " cur_index=" << (uint32_t)cur_index;
            errno = EBADMSG;
            return UBRING_ERR;
        }
        uint8_t available_data = chunk_msg_len - cur_index;

        int32_t copy_len = (remaining_len < available_data) ? remaining_len : available_data;
        memcpy(dest + total_copied, data_msg[ubr_rx->read_pos].payload.inner + cur_index, (size_t)copy_len);
        total_copied += copy_len;
        remaining_len -= copy_len;
        current_chunk->header[UBR_MSG_CUR_INDEX] += (uint8_t)copy_len;
        if (BAIDU_LIKELY(current_chunk->header[UBR_MSG_CUR_INDEX] == chunk_msg_len)) {
            current_chunk->header[UBR_MSG_FLAG_INDEX] = UBR_MSG_CHUNK_NONE;
            UpdateDataQTail(_trx);
            ubr_rx->read_pos = (ubr_rx->read_pos + 1) % ubr_rx->capacity;
        }
    }
    if (need_update_epoll_eof_pos) {
        ubr_rx->ep_eof_pos = ubr_rx->read_pos;
    }
    return (int)total_copied;
}

ssize_t UBRing::UbrTrxWritev(const struct iovec *iov, int iovcnt)
{
    if (BAIDU_UNLIKELY(CheckTrxSendPreCheck(_trx) != UBRING_OK)) {
        return UBRING_ERR;
    }

    size_t buf_len = 0;
    for (int i = 0; i < iovcnt; i++) {
        buf_len += iov[i].iov_len;
    }
    RETURN_CODE rc = WritevHasEnoughSpace(buf_len);
    if (rc != UBRING_OK) {
        return rc;
    }

    UbrMsgFormat *data_msg = (UbrMsgFormat *)_trx->ubr_tx.remote_data_q.addr;
    UbrMsgFormat *msg = &(_trx->ubr_tx.local_msg_space);
    int cur_iov = 0;
    size_t cur_iov_pos = 0;
    ssize_t total_send_len = 0;
    size_t pkt_remain_n = 0;
    size_t iov_remain = 0;
    size_t fulled = 0;
    uint8_t is_last_pkt = 0;
    uint8_t cur_pkt_len = 0;
    const uint64_t io_seq = ATOMIC_ADD(_trx->ubr_tx.out_io_id, 1) + 1;
    ((UbrEventQMsg *)_trx->ubr_tx.remote_rx_event_q.addr)->io_id = io_seq;
    while (buf_len > 0) {
        is_last_pkt = (uint8_t)(buf_len <= UBR_MSG_PAYLOAD_LEN);
        cur_pkt_len = is_last_pkt ? (uint8_t)buf_len : UBR_MSG_PAYLOAD_LEN;
        msg->header[UBR_MSG_FLAG_INDEX] = is_last_pkt ? UBR_MSG_CHUNK_EOF : UBR_MSG_CHUNK_EXIST;
        msg->header[UBR_MSG_LEN_INDEX] = cur_pkt_len;
        msg->header[UBR_MSG_CUR_INDEX] = 0;
        pkt_remain_n = cur_pkt_len;
        while (cur_iov < iovcnt && pkt_remain_n > 0) {
            iov_remain = (iov[cur_iov].iov_len - cur_iov_pos);
            fulled = iov_remain > pkt_remain_n ? pkt_remain_n : iov_remain;
            memcpy((msg->payload.inner + (cur_pkt_len - (uint8_t)pkt_remain_n)),
                (uint8_t *)(iov[cur_iov].iov_base) + cur_iov_pos,
                fulled);
            pkt_remain_n -= fulled;
            cur_iov_pos += fulled;
            if (cur_iov_pos == iov[cur_iov].iov_len) {
                cur_iov++;
                cur_iov_pos = 0;
            }
        }

        Copy64Byte((int8_t *)&data_msg[_trx->ubr_tx.write_pos], (int8_t *)msg);
        _trx->ubr_tx.write_pos = (_trx->ubr_tx.write_pos + 1) % _trx->ubr_tx.capacity;
        total_send_len += (ssize_t)cur_pkt_len;
        buf_len -= (int)cur_pkt_len;
    }
    return total_send_len;
}

ssize_t UBRing::UbrTrxReadv(const struct iovec *iov, int iovcnt)
{
    RETURN_CODE rc = UBRING_OK;
    if (BAIDU_UNLIKELY((rc = CheckTrxRecvParam(_trx, iov, (uint32_t)iovcnt)) != UBRING_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }
    UbrMsgFormat *data_msg = (UbrMsgFormat *)_trx->ubr_rx.local_data_q.addr;
    uint32_t read_pos_end = _trx->ubr_rx.read_pos;
    uint8_t flag = data_msg[read_pos_end].header[UBR_MSG_FLAG_INDEX];
    if (flag == UBR_MSG_CHUNK_NONE) {
        errno = EAGAIN;
        return -1;
    }
    ssize_t nr = UbrTrxReadvBlockMode(iov, iovcnt);
    if (BAIDU_UNLIKELY(nr == -1)) {
        LOG(ERROR) << "Non-blocking readv msg in failed, connection has been closed.";
        errno = EPIPE;
        return -1;
    }
    return nr;
}

ssize_t UBRing::UbrTrxReadvBlockMode(const struct iovec *iov, int iovcnt)
{
    RETURN_CODE rc = UBRING_OK;
    if (BAIDU_UNLIKELY((rc = CheckTrxRecvParam(_trx, iov, (uint32_t)iovcnt)) != UBRING_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }

    size_t remain_buf_len = 0;
    for (int i = 0; i < iovcnt; i++) {
        remain_buf_len += iov[i].iov_len;
    }

    bool need_update_epoll_eof_pos = _trx->ubr_rx.read_pos == _trx->ubr_rx.ep_eof_pos;
    ssize_t total_recv_len = StartReadv(_trx, iov, iovcnt, remain_buf_len);

    if (need_update_epoll_eof_pos) {
        _trx->ubr_rx.ep_eof_pos = _trx->ubr_rx.read_pos;
    }
    return total_recv_len;
}

RETURN_CODE UBRing::IsUbrTrxReadable(uint32_t ep_event)
{
    if (BAIDU_UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "The trx to be checked is NULL.";
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY(_trx->local_shm.addr == nullptr)) {
        LOG(ERROR) << "The trx local_shm to be checked is NULL.";
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY(_trx->ubr_tx.trx_state != UBR_STATE_CONNECTED)) {
        return UBRING_ERR;
    }

    uint64_t io_id = ((UbrEventQMsg *)_trx->ubr_rx.local_rx_event_q.addr)->io_id;
    if ((ep_event & EPOLLET) && io_id == ATOMIC_LOAD(_trx->ubr_rx.in_io_id)) {
        return MPA_MUXER_NOT_READY;
    }

    uint32_t read_pos_end = _trx->ubr_rx.read_pos;
    if (ep_event & EPOLLET) {
        read_pos_end = _trx->ubr_rx.ep_eof_pos;
    }

    UbrMsgFormat *data_msg = (UbrMsgFormat *)_trx->ubr_rx.local_data_q.addr;
    uint8_t flag = data_msg[read_pos_end].header[UBR_MSG_FLAG_INDEX];
    if (flag == UBR_MSG_CHUNK_NONE) {
        return MPA_MUXER_NOT_READY;
    }
    if (ep_event & EPOLLET) {
        ATOMIC_STORE(_trx->ubr_rx.in_io_id, io_id);
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::IsUbrTrxWriteable(uint32_t ep_event)
{
    if (BAIDU_UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "The trx to be checked is NULL.";
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY(_trx->local_shm.addr == nullptr)) {
        LOG(ERROR) << "The trx local_shm to be checked is NULL.";
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY((UbrEventQMsg *)_trx->ubr_tx.local_tx_event_q.addr == nullptr)) {
        LOG(ERROR) << "The trx local_tx_event_q addr is NULL.";
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY((UbrEventQMsg *)_trx->ubr_tx.local_data_status_q.addr == nullptr)) {
        LOG(ERROR) << "The trx local_data_status_q addr is NULL.";
        return UBRING_ERR;
    }

    if (BAIDU_UNLIKELY(_trx->ubr_tx.trx_state != UBR_STATE_CONNECTED)) {
        LOG(ERROR) << "The trx is not connected state.";
        return UBRING_ERR;
    }

    UbrDataStatusQMsg *data_status_msg = (UbrDataStatusQMsg *)_trx->ubr_tx.local_data_status_q.addr;
    uint32_t cap = _trx->ubr_tx.capacity;
    uint32_t tail = data_status_msg->tail;
    uint32_t remain_chunk_num =
        (_trx->ubr_tx.write_pos > tail) ? (tail + cap - _trx->ubr_tx.write_pos) : (tail - _trx->ubr_tx.write_pos);
    if (remain_chunk_num == 0) {
        _trx->ubr_tx.ep_last_cap = remain_chunk_num;
        return MPA_MUXER_NOT_READY;
    }

    if ((ep_event & EPOLLET) && (_trx->ubr_tx.ep_last_cap >= remain_chunk_num)) {
        _trx->ubr_tx.ep_last_cap = remain_chunk_num;
        return MPA_MUXER_NOT_READY;
    }
    _trx->ubr_tx.ep_last_cap = remain_chunk_num;
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrSetTimeout(UbrTaskStep task_type, int timeout)
{
    if (task_type >= UBR_TASK_STEP_NUM || timeout < 0) {
        LOG(ERROR) << "Set timeout failed, invalid task type.";
        return UBRING_ERR;
    }

    g_sleep_time[task_type] = (uint32_t)timeout;
    LOG(INFO) << "Set timeout success, task_type=" << task_type << ", timeout=" << timeout;
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrTrxFreeShm(UbrTrx *trx)
{
    if (trx == nullptr) {
        LOG(ERROR) << "Trx is NULL.";
        return UBRING_ERR;
    }

    RETURN_CODE rc = UBRING_OK;
    rc = ShmMunmap(&trx->local_shm);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx close, local unmap " << trx->local_shm.name << " shm fail.";
        return UBRING_ERR;
    }

    rc = ShmFree(&trx->local_shm);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        if (rc != SHM_ERR_RESOURCE_ATTACHED && rc != SHM_ERR_NOT_FOUND) {
            LOG(ERROR) << "Wait for " << trx->local_shm.name << " local shm free fail.";
            return UBRING_ERR;
        }
        LOG(INFO) << "Local shm " << trx->local_shm.name << " already freed, continue to free remote shm.";
    }

    RETURN_CODE remote_rc = UBRING_OK;
    if (trx->remote_shm.addr != nullptr) {
        remote_rc = ShmRemoteFree(&trx->remote_shm);
    }
    if (remote_rc != UBRING_OK) {
        LOG(WARNING) << "Free remote shm " << trx->remote_shm.name << " failed, rc=" << remote_rc;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::UbrUnlinkLocalShm()
{
    if (BAIDU_UNLIKELY(_trx == nullptr)) {
        return UBRING_ERR;
    }
    RETURN_CODE rc = ShmFree(&_trx->local_shm);
    if (rc != UBRING_OK && rc != SHM_ERR_NOT_FOUND && rc != SHM_ERR_RESOURCE_ATTACHED) {
        LOG(WARNING) << "Unlink local shm " << _trx->local_shm.name << " failed, rc=" << rc;
        return rc;
    }
    return UBRING_OK;
}

void UBRing::PreWriteAddr(uint8_t *addr, size_t len)
{
    if (addr == nullptr) {
        return;
    }

    size_t i = 0;
    while (i < len) {
        if (i + sizeof(uint64_t) <= len) {
            *(uint64_t *)(addr + i) = (uint64_t)0;
            i += sizeof(uint64_t);
        } else if (i + sizeof(uint32_t) < len) {
            *(uint32_t *)(addr + i) = (uint32_t)0;
            i += sizeof(uint32_t);
        } else if (i + sizeof(uint16_t) < len) {
            *(uint16_t *)(addr + i) = (uint16_t)0;
            i += sizeof(uint16_t);
        } else {
            *(addr + i) = (uint8_t)0;
            i += sizeof(uint8_t);
        }
    }
}

void UBRing::PrewriteUbrTx(UbrTx *tx)
{
    if (tx == nullptr) {
        return;
    }
    PreWriteAddr(tx->remote_data_q.addr, tx->capacity * sizeof(UbrMsgFormat));
}

void UBRing::PrewriteUbrRx(UbrRx *rx)
{
    if (rx == nullptr) {
        return;
    }
    PreWriteAddr(rx->local_data_q.addr, rx->capacity * sizeof(UbrMsgFormat));
}

RETURN_CODE UBRing::UbrTrxMapLocalShm(SHM *local_shm)
{
    if (BAIDU_UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null.";
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY(local_shm == nullptr || local_shm->addr == nullptr)) {
        LOG(ERROR) << "Trx map Shared memory failed, local_shm is null or addr is NULL.";
        return UBRING_ERR;
    }
    _trx->local_shm = *local_shm;
    _trx->ubr_tx.local_tx_event_q.addr = local_shm->addr + TX_EVENTQ_ADDR_OFFSET;
    _trx->ubr_tx.local_tx_event_q.len = UBR_EVENTQ_LEN;
    _trx->ubr_rx.local_rx_event_q.addr = local_shm->addr + RX_EVENTQ_ADDR_OFFSET;
    _trx->ubr_rx.local_rx_event_q.len = UBR_EVENTQ_LEN;
    _trx->ubr_tx.local_data_status_q.addr = local_shm->addr + DATASTATUSQ_ADDR_OFFSET;
    _trx->ubr_tx.local_data_status_q.len = UBR_DATASTATUSQ_LEN;
    size_t addr_aligned_offset = Aligned64Offset(local_shm->addr + DATAQ_ADDR_OFFSET);
    _trx->ubr_rx.local_data_q.addr = local_shm->addr + DATAQ_ADDR_OFFSET + addr_aligned_offset;
    _trx->ubr_rx.local_data_q.len = local_shm->len - DATAQ_ADDR_OFFSET - addr_aligned_offset;
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrTrxMapRemoteShm(SHM *remote_shm)
{
    if (BAIDU_UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null.";
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY(remote_shm == nullptr || remote_shm->addr == nullptr)) {
        LOG(ERROR) << "Trx map Shared memory failed, remote_shm is null or addr is NULL.";
        return UBRING_ERR;
    }
    _trx->remote_shm = *remote_shm;
    _trx->ubr_rx.remote_tx_event_q.addr = remote_shm->addr + TX_EVENTQ_ADDR_OFFSET;
    _trx->ubr_rx.remote_tx_event_q.len = UBR_EVENTQ_LEN;
    _trx->ubr_tx.remote_rx_event_q.addr = remote_shm->addr + RX_EVENTQ_ADDR_OFFSET;
    _trx->ubr_tx.remote_rx_event_q.len = UBR_EVENTQ_LEN;
    _trx->ubr_rx.remote_data_status_q.addr = remote_shm->addr + DATASTATUSQ_ADDR_OFFSET;
    _trx->ubr_rx.remote_data_status_q.len = UBR_DATASTATUSQ_LEN;
    size_t addr_aligned_offset = Aligned64Offset(remote_shm->addr + DATAQ_ADDR_OFFSET);
    _trx->ubr_tx.remote_data_q.addr = remote_shm->addr + DATAQ_ADDR_OFFSET + addr_aligned_offset;
    _trx->ubr_tx.remote_data_q.len = remote_shm->len - DATAQ_ADDR_OFFSET - addr_aligned_offset;
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrServerTrxInit(SHM *local_shm, SHM *remote_shm)
{
    RETURN_CODE rc = UbrTrxMapShm(local_shm, remote_shm);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) <<"Trx map shared memory failed.";
        return rc;
    }

    uint32_t local_data_msg_cap = (uint32_t)(_trx->ubr_rx.local_data_q.len / UBR_MSG_LEN);
    uint32_t remote_data_msg_cap = (uint32_t)(_trx->ubr_tx.remote_data_q.len / UBR_MSG_LEN);
    _trx->ubr_rx.capacity = local_data_msg_cap;
    _trx->ubr_tx.capacity = remote_data_msg_cap;
    rc = UBRingManager::GetUbrDealMsgMaxCnt(_trx->ubr_rx.capacity, &_trx->ubr_rx.deal_msg_max_cnt);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Get ubring deal msg max cnt.";
        return rc;
    }
    PrewriteUbrRx(&_trx->ubr_rx);
    PrewriteUbrTx(&_trx->ubr_tx);

    ((UbrDataStatusQMsg *)(_trx->ubr_tx.local_data_status_q.addr))->tail = remote_data_msg_cap - 1;
    ((UbrDataStatusQMsg *)(_trx->ubr_rx.remote_data_status_q.addr))->tail = local_data_msg_cap - 1;

    if (BAIDU_UNLIKELY(UbrAddTimer() != UBRING_OK)) {
        LOG(ERROR) << "Ubr add timer failed, local_name=" << local_shm->name;
        return UBRING_ERR;
    }

    ((UbrDataStatusQMsg *)(_trx->ubr_tx.local_data_status_q.addr))->timeout =
        FLAGS_ub_connect_timeout_s;
    ((UbrDataStatusQMsg *)(_trx->ubr_rx.remote_data_status_q.addr))->timeout =
        FLAGS_ub_connect_timeout_s;

    ((UbrEventQMsg *)_trx->ubr_tx.remote_rx_event_q.addr)->flag = UBR_STATE_CONNECTED;
    ((UbrEventQMsg *)_trx->ubr_rx.local_rx_event_q.addr)->flag = UBR_STATE_CONNECTED;
    _trx->ubr_tx.trx_state = UBR_STATE_CONNECTED;
    _trx->ubr_rx.trx_state = UBR_STATE_CONNECTED;
    return UBRING_OK;
}

int UBRing::UbrAllocateServerShm(SHM* remote_trx_shm, SHM* local_trx_shm) {
    UbrSetSleepTask(UBR_TASK_ACCEPT_MAP_FRONT);
    if (BAIDU_UNLIKELY((ShmRemoteMalloc(remote_trx_shm)) != UBRING_OK)) {
        LOG(ERROR) << "Trx apply remote shared memory failed.";
        return -1;
    }

    if (BAIDU_UNLIKELY((ShmLocalCalloc(local_trx_shm)) != UBRING_OK)) {
        LOG(ERROR) << "Trx apply local shared memory failed.";
        ShmRemoteFree(remote_trx_shm);
        return -1;
    }

    UbrTrx **ubr_trx_ptr = &_trx;
    if (BAIDU_UNLIKELY((UBRingManager::AcquireUbrTrxFromMgr(ubr_trx_ptr)) != UBRING_OK)) {
        LOG(ERROR) << "Acquire ubrtrx failed.";
        ShmRemoteFree(remote_trx_shm);
        ShmLocalFree(local_trx_shm);
        return -1;
    }
    _trx->type = TCP_TRX;
    if (BAIDU_UNLIKELY((UbrServerTrxInit(local_trx_shm, remote_trx_shm)) != UBRING_OK)) {
        LOG(ERROR) << "Server trx init failed.";
        UbrTrxFreeShm(_trx);
        UBRingManager::ReleaseUbrTrxFromMgr(_trx, ATOMIC_LOAD(_trx->ubr_id));
        _trx = nullptr;
        return -1;
    }
    return 0;
}

int UBRing::UbrAllocateLocalShm(SHM *local_trx_shm, const char *shm_name)
{
    if (BAIDU_UNLIKELY((UBRingManager::AcquireUbrTrxFromMgr(&(_trx))) != UBRING_OK)) {
        LOG(ERROR) << "Acquire ubrtrx failed, local_name=" << shm_name;
        return -1;
    }

    _trx->type = TCP_TRX;
    if (BAIDU_UNLIKELY((ApplyAndMapLocalShm(local_trx_shm, shm_name)) != UBRING_OK)) {
        LOG(ERROR) << "Trx apply or map local shared memory failed, local_name=" << shm_name;
        _trx = nullptr;
        return -1;
    }
    return 0;
}

int UBRing::UbrMapRemoteShm(SHM *local_trx_shm, const char *local_name)
{
    RETURN_CODE rc = UbrMapRemoteShmAddTimer(local_trx_shm, local_name);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Connect Trx failed, local shm name=" << local_trx_shm->name;
        return -1;
    }
    PrewriteUbrRx(&_trx->ubr_rx);
    PrewriteUbrTx(&_trx->ubr_tx);
    ((UbrEventQMsg *)_trx->ubr_rx.remote_tx_event_q.addr)->flag = UBR_STATE_CONNECTED;
    ((UbrEventQMsg *)_trx->ubr_rx.local_rx_event_q.addr)->flag = UBR_STATE_CONNECTED;
    _trx->ubr_tx.trx_state = UBR_STATE_CONNECTED;
    _trx->ubr_rx.trx_state = UBR_STATE_CONNECTED;
    return 0;
}

RETURN_CODE UBRing::UbrMapRemoteShmAddTimer(SHM *local_trx_shm, const char *local_name)
{
    uint64_t start_time = GetCurNanoSeconds();

    size_t remote_server_len = UBR_MSG_LEN * (((UbrDataStatusQMsg *)(_trx->ubr_tx.local_data_status_q.addr))->tail + 1) +
                             UBR_MSG_LEN * ((DATAQ_ADDR_OFFSET / UBR_MSG_LEN) + 1);
    SHM remote_trx_shm = {nullptr, remote_server_len, 0, {0}, local_trx_shm->fd};
    int result = snprintf(remote_trx_shm.name,
        SHM_MAX_NAME_BUFF_LEN,
        "%s_%s_%s",
        SHM_NAME_PREFIX,
        local_name,
        SERVER_SHM_NAME_SUFFIX);
    if (BAIDU_UNLIKELY(result < 0)) {
        LOG(ERROR) << "Copy server shared memory name failed, local_name=" << local_name
                   << ", ret=" << result;
        return UBRING_ERR;
    }
    UbrSetSleepTask(UBR_TASK_CONNECT_MAP_FRONT);
    RETURN_CODE rc = ApplyAndMapRemoteShm(&remote_trx_shm);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Connect Trx map shared memory failed, remote shm=" << remote_trx_shm.name;
        return rc;
    }

    if (BAIDU_UNLIKELY(UbrAddTimer() != UBRING_OK)) {
        LOG(ERROR) << "Ubr add timer failed, local_name=" << local_name;
        // The trx slot stays acquired on purpose: the endpoint keeps this
        // UBRing (it falls back to TCP) and returns the slot through its own
        // Reset()/UbrTrxClose path. Releasing it here would race that owner.
        ShmRemoteFree(&_trx->remote_shm);
        return UBRING_ERR;
    }

    UbrSetSleepTask(UBR_TASK_CONNECT_MAP_AFTER);

    uint32_t timeout = ((UbrDataStatusQMsg *)(_trx->ubr_tx.local_data_status_q.addr))->timeout;
    if (HasTimedOut(start_time, timeout) != UBRING_OK) {
        LOG(ERROR) << "Local shm " << local_trx_shm->name << " wait for connect remote map timeout.";
        UbrTimerDelAndWait(&_trx->hb_timer);
        UbrTimerDelAndWait(&_trx->close_timer);
        // Same ownership as above: the slot is returned by the endpoint's
        // close path, not here.
        ShmRemoteFree(&_trx->remote_shm);
        return UBRING_ERR_TIMEOUT;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::ApplyAndMapLocalShm(SHM *local_trx_shm, const char *local_name)
{
    if (BAIDU_UNLIKELY(_trx == nullptr || local_trx_shm == nullptr)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null, local_name=" << local_name;
        return UBRING_ERR;
    }
    int result = snprintf(local_trx_shm->name,
        SHM_MAX_NAME_BUFF_LEN,
        "%s_%s_%s",
        SHM_NAME_PREFIX,
        local_name,
        CLIENT_SHM_NAME_SUFFIX);
    if (BAIDU_UNLIKELY(result < 0)) {
        LOG(ERROR) << "Copy client localTrx shared memory name failed, local_name=" << local_name << ", ret=" << result;
        return UBRING_ERR;
    }

    RETURN_CODE rc = ShmLocalCalloc(local_trx_shm);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx apply local shared memory failed, local shm name=" << local_trx_shm->name << ", rc=" << rc;
        if (rc == SHM_ERR_EXIST || rc == SHM_ERR_NOT_FOUND) {
            rc = UBR_ERR_ADDR_IN_USE;
        }
        UBRingManager::ReleaseUbrTrxFromMgr(_trx, ATOMIC_LOAD(_trx->ubr_id));
        return rc;
    }
    rc = UbrTrxMapLocalShm(local_trx_shm);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx map local shared memory failed, local shm name=" << local_trx_shm->name;
        ShmLocalFree(local_trx_shm);
        UBRingManager::ReleaseUbrTrxFromMgr(_trx, ATOMIC_LOAD(_trx->ubr_id));
        return rc;
    }
    ((UbrDataStatusQMsg *)_trx->ubr_tx.local_data_status_q.addr)->timeout =
        FLAGS_ub_connect_timeout_s;
    _trx->ubr_rx.capacity = (uint32_t)(_trx->ubr_rx.local_data_q.len / UBR_MSG_LEN);
    rc = UBRingManager::GetUbrDealMsgMaxCnt(_trx->ubr_rx.capacity, &_trx->ubr_rx.deal_msg_max_cnt);
    if (rc != UBRING_OK) {
        LOG(ERROR) << "Get ubring deal msg max cnt, local shm name=" << local_trx_shm->name;
        ShmLocalFree(local_trx_shm);
        UBRingManager::ReleaseUbrTrxFromMgr(_trx, ATOMIC_LOAD(_trx->ubr_id));
        return rc;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::ApplyAndMapRemoteShm(SHM *remote_trx_shm)
{
    RETURN_CODE rc = ShmRemoteMalloc(remote_trx_shm);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx apply remote shared memory failed.";
        return rc;
    }
    rc = UbrTrxMapRemoteShm(remote_trx_shm);
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx map shared memory failed.";
        ShmRemoteFree(remote_trx_shm);
        return rc;
    }
    _trx->ubr_tx.capacity = (uint32_t)(_trx->ubr_tx.remote_data_q.len / UBR_MSG_LEN);
    return UBRING_OK;
}

RETURN_CODE UBRing::WritevHasEnoughSpace(size_t buf_len)
{
    UbrDataStatusQMsg *data_status_msg = (UbrDataStatusQMsg *)_trx->ubr_tx.local_data_status_q.addr;
    uint32_t cap = _trx->ubr_tx.capacity;
    uint32_t tail = data_status_msg->tail;
    uint32_t remain_chunk_num =
        (_trx->ubr_tx.write_pos > tail) ? (tail + cap - _trx->ubr_tx.write_pos) : (tail - _trx->ubr_tx.write_pos);
    uint32_t need_msg_chunk_num = CalcUbrMsgChunkCnt((uint32_t)buf_len);
    if (need_msg_chunk_num >= cap) {
        LOG(ERROR) << "Ubr write failed, payload length=" << buf_len
                   << " needs " << need_msg_chunk_num << " chunks, capacity=" << cap << ".";
        errno = EMSGSIZE;
        return UBRING_ERR;
    }
    if (remain_chunk_num < need_msg_chunk_num) {
        return UBRING_RETRY;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrClearResourceCheck(UbrTrx *trx)
{
    if (BAIDU_UNLIKELY(trx == nullptr)) {
        LOG(ERROR) << "Trx close failed, trx is null.";
        return UBRING_ERR;
    }

    UbrEventQMsg* local_tx_event_q = (UbrEventQMsg *)trx->ubr_tx.local_tx_event_q.addr;
    if (BAIDU_UNLIKELY(local_tx_event_q == nullptr)) {
        LOG(ERROR) << "Trx close failed, local_tx_event_q addr is NULL, trx local name=" << trx->local_shm.name;
        return UBRING_ERR;
    }
    if (local_tx_event_q->flag == UBR_STATE_CONNECTED) {
        local_tx_event_q->flag = UBR_STATE_CLOSING;
    }

    // Wait out a dispatched callback before the slot's trx is cleared. When
    // this runs inside a per-trx callback, UbrTimerDelAndWait recognizes its own
    // timer and degrades to the non-blocking delete.
    UbrStopTrxTimer(&trx->close_timer);
    UbrStopTrxTimer(&trx->hb_timer);

    if (local_tx_event_q->flag == UBR_STATE_CLOSING) {
        local_tx_event_q->flag = UBR_STATE_CLOSED;
        trx->ubr_tx.trx_state = UBR_STATE_CLOSED;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::ClearTrxResource(UbrTrx *trx, uint64_t expect_ubr_id)
{
    RETURN_CODE rc = UbrClearResourceCheck(trx);
    if (rc != UBRING_OK) {
        return rc;
    }

    rc = UbrAddAsynClearTimer(trx, expect_ubr_id);
    if (rc != UBRING_OK) {
        LOG(ERROR) << "Trx close, add " << trx->local_shm.name << " close clear timer failed.";
        return UBRING_ERR;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::UbrTrxCloseCheck(UbrTrx *trx, uint64_t expect_ubr_id)
{
    if (BAIDU_UNLIKELY(trx == nullptr)) {
        LOG(ERROR) << "Trx close failed, client trx is null.";
        return UBRING_ERR;
    }
    // Validate the generation and claim the close in one critical section:
    // checking here and claiming afterwards would let a slot released and
    // reused in between be closed as if it were still this generation's.
    RETURN_CODE rc = UBRingManager::TryClaimTrxClose(trx->trx_mgr_index,
                                                     expect_ubr_id);
    if (rc == UBRING_REENTRY) {
        LOG(INFO) << "Trx close skipped, already closing, trx local name=" << trx->local_shm.name;
    }
    return rc;
}

ssize_t UBRing::StartReadv(UbrTrx *trx, const struct iovec *iov, int iovcnt, size_t remain_buf_len)
{
    ssize_t total_recv_len = 0;
    int iov_index = 0;
    size_t iov_pos = 0;
    UbrMsgFormat *data_msg = (UbrMsgFormat *)trx->ubr_rx.local_data_q.addr;
    bool not_eof_encountered = true;
    while (not_eof_encountered && remain_buf_len > 0) {
        if (BAIDU_UNLIKELY(CheckTrxRecvPreCheck(trx) != UBRING_OK)) {
            return UBRING_ERR;
        }
        UbrMsgFormat *current_chunk = &data_msg[trx->ubr_rx.read_pos];
        uint8_t flag = current_chunk->header[UBR_MSG_FLAG_INDEX];
        if (flag == UBR_MSG_CHUNK_NONE) {
            if (total_recv_len > 0) {
                break;
            }
            errno = EAGAIN;
            return -1;
        }
        if (flag == UBR_MSG_CHUNK_EOF) {
            not_eof_encountered = false;
        }
        uint8_t chunk_msg_len = current_chunk->header[UBR_MSG_LEN_INDEX];
        uint8_t cur_index = current_chunk->header[UBR_MSG_CUR_INDEX];
        if (BAIDU_UNLIKELY(!IsRecvChunkHeaderValid(chunk_msg_len, cur_index))) {
            LOG(ERROR) << "Trx readv failed, invalid chunk header msg_len="
                       << (uint32_t)chunk_msg_len << " cur_index=" << (uint32_t)cur_index;
            errno = EBADMSG;
            return UBRING_ERR;
        }
        uint8_t recv_len =
            remain_buf_len > (size_t)(chunk_msg_len - cur_index) ? (chunk_msg_len - cur_index) : (uint8_t)remain_buf_len;
        while (iov_index < iovcnt && recv_len > 0) {
            size_t copy_len =
                recv_len > (iov[iov_index].iov_len - iov_pos) ? iov[iov_index].iov_len - iov_pos : (size_t)recv_len;
            memcpy((uint8_t *)iov[iov_index].iov_base + iov_pos, current_chunk->payload.inner + cur_index, copy_len);
            recv_len -= (uint8_t)copy_len;
            iov_pos += copy_len;
            cur_index += (uint8_t)copy_len;
            if (iov_pos == iov[iov_index].iov_len) {
                iov_index++;
                iov_pos = 0;
            }
            remain_buf_len -= copy_len;
            total_recv_len += (ssize_t)copy_len;
        }
        current_chunk->header[UBR_MSG_CUR_INDEX] = cur_index;
        if (current_chunk->header[UBR_MSG_CUR_INDEX] == chunk_msg_len) {
            current_chunk->header[UBR_MSG_FLAG_INDEX] = UBR_MSG_CHUNK_NONE;
            UpdateDataQTail(trx);
            trx->ubr_rx.read_pos = (trx->ubr_rx.read_pos + 1) % trx->ubr_rx.capacity;
        }
    }
    return total_recv_len;
}
}  // namespace ubring
}  // namespace brpc
