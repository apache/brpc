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
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx map local shared memory failed.";
        return rc;
    }
    rc = UbrTrxMapRemoteShm(remote_shm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx map remote shared memory failed.";
        return rc;
    }
    return UBRING_OK;
}

static void UbrDoAsynClearWork(UbrTrx *trx, uint64_t expect_ubr_id) {
    if (UNLIKELY(UBRing::UbrTrxFreeShm(trx) != UBRING_OK)) {
        LOG(ERROR) << "Trx close, wait for local shm " << trx->local_shm.name << " free fail.";
    }
    if (UNLIKELY(UBRingManager::ReleaseUbrTrxFromMgr(trx, expect_ubr_id) != UBRING_OK)) {
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
    if (UNLIKELY(UBRingManager::ReleaseUbrTrxFromMgr(trx, expect_ubr_id) != UBRING_OK)) {
        LOG(ERROR) << "Trx passive clear, release shm " << trx->local_shm.name << " trx failed.";
    }
}

// Schedule the delayed cleanup of `trx'. The cleanup ownership lives in the
// per-acquisition control object, so exactly one of the delayed-clear
// callback and a force close ever runs the cleanup. `work' is the cleanup
// body, used directly when the timer cannot be started.
static RETURN_CODE UbrScheduleClearTimer(UbrTrx *trx, void* (*cb)(void*),
                                         void (*work)(UbrTrx*, uint64_t)) {
    if (UNLIKELY(trx == nullptr || trx->local_shm.addr == nullptr)) {
        return UBRING_OK;                    // released trx, stale event
    }
    if (__atomic_load_n(&trx->cleanup_ctl, __ATOMIC_SEQ_CST) != nullptr) {
        return UBRING_OK;                    // cleanup already scheduled
    }
    auto* ctl = new (std::nothrow) UbrCleanupCtl();
    if (UNLIKELY(ctl == nullptr)) {
        LOG(ERROR) << "Fail to malloc ubr cleanup ctl.";
        return UBRING_ERR;
    }
    ctl->trx = trx;
    ctl->ubr_id = ATOMIC_LOAD(trx->ubr_id);
    ctl->state.store(UBR_CLEANUP_PENDING);
    ctl->timer = nullptr;
    ctl->ref.store(2);                       // timer/callback + starter; the
                                             // manager anchor is taken by
                                             // TryPublishUnitCleanupCtl

    UbrCleanupCtl* expected = nullptr;
    if (!__atomic_compare_exchange_n(&trx->cleanup_ctl, &expected, ctl, false,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        delete ctl;                          // another schedule won
        return UBRING_OK;
    }
    if (!UBRingManager::TryPublishUnitCleanupCtl(trx->trx_mgr_index,
                                                 ctl->ubr_id, ctl)) {
        // The slot was released (and possibly reused) before we could
        // anchor: force close or the new occupant owns it now. Nothing
        // was armed yet -- just undo the trx-side publication.
        expected = ctl;
        __atomic_compare_exchange_n(&trx->cleanup_ctl, &expected,
                                    (UbrCleanupCtl*) nullptr, false,
                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        ctl->ReleaseRef();                   // timer/callback reference, never armed
        ctl->ReleaseRef();                   // starter reference
        return UBRING_OK;
    }
    RETURN_CODE rc = UbrTimerStart(&ctl->timer,
            (uint64_t)FLAGS_ub_flying_io_timeout_s * SEC_TO_USEC, 0, cb, ctl);
    if (UNLIKELY(rc != UBRING_OK)) {
        // The timer was never scheduled: this path owns the manager,
        // timer/callback and starter references. Roll the schedule back
        // and run the cleanup inline so the trx does not end up with
        // neither timers nor a queued cleanup. If force close claimed the
        // ownership meanwhile, leave the manager anchor to it.
        int state_expected = UBR_CLEANUP_PENDING;
        if (ATOMIC_COMPARE_EXCHANGE_STRONG(ctl->state, state_expected, UBR_CLEANUP_RUNNING)) {
            if (ATOMIC_LOAD(trx->ubr_id) == ctl->ubr_id) {
                work(trx, ctl->ubr_id);
            }
            ATOMIC_STORE(ctl->state, UBR_CLEANUP_DONE);
            // Detach only after DONE: while the inline cleanup runs, the
            // anchor and cleanup_ctl must keep telling a concurrent force
            // close that this cleanup is owned (RUNNING/DONE), otherwise it
            // would fall into its no-ctl branch and clean the trx again.
            UbrCleanupCtl* published = ctl;
            __atomic_compare_exchange_n(&trx->cleanup_ctl, &published,
                                        (UbrCleanupCtl*) nullptr, false,
                                        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
            UBRingManager::DetachUnitCleanupCtl(trx->trx_mgr_index, ctl);
        }
        ctl->ReleaseRef();                   // timer/callback reference
        ctl->ReleaseRef();                   // starter reference
        return UBRING_ERR;
    }
    if (UNLIKELY(ATOMIC_LOAD(trx->ubr_id) != ctl->ubr_id)) {
        // Published onto a slot that was released and reused meanwhile.
        if (UbrTimerDel(&ctl->timer) == 0) {
            ctl->ReleaseRef();               // timer/callback reference
        }
        UbrCleanupCtl* published = ctl;
        if (!__atomic_compare_exchange_n(&trx->cleanup_ctl, &published,
                                         (UbrCleanupCtl*) nullptr, false,
                                         __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            published = nullptr;
        }
        UBRingManager::DetachUnitCleanupCtl(trx->trx_mgr_index, ctl);
        ctl->ReleaseRef();                   // starter reference
        return UBRING_OK;
    }
    ctl->ReleaseRef();                       // starter reference
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrTrxClose() {
    RETURN_CODE close_check_rc = UbrTrxCloseCheck(_trx);
    if (UNLIKELY(close_check_rc != UBRING_OK)) {
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
            const uint64_t expect_ubr_id = ATOMIC_LOAD(_trx->ubr_id);
            UbrTimerDelAndWait(&_trx->close_timer);
            UbrTimerDelAndWait(&_trx->hb_timer);
            UbrCleanupCtl* ctl = UBRingManager::SnapshotUnitCleanupCtl(_trx->trx_mgr_index);
            if (ctl != nullptr && ctl->ubr_id != expect_ubr_id) {
                ctl->ReleaseRef();               // snapshot reference
                ctl = nullptr;                   // slot reused, not ours
            }
            bool cleanup_owned = false;
            if (ctl != nullptr) {
                int expected = UBR_CLEANUP_PENDING;
                if (ATOMIC_COMPARE_EXCHANGE_STRONG(ctl->state, expected, UBR_CLEANUP_RUNNING)) {
                    cleanup_owned = true;
                    if (UbrTimerDel(&ctl->timer) == 0) {
                        ctl->ReleaseRef();   // timer/callback reference
                    }
                }
            } else if (ATOMIC_LOAD(_trx->ubr_id) == expect_ubr_id) {
                cleanup_owned = true;
            }
            if (cleanup_owned) {
                if (_trx->ubr_tx.remote_rx_event_q.addr != nullptr) {
                    ((UbrEventQMsg *)_trx->ubr_tx.remote_rx_event_q.addr)->flag = UBR_STATE_CLOSED;
                }
                if (UNLIKELY(UbrTrxFreeShm(_trx) != UBRING_OK)) {
                    LOG(WARNING) << "Force close, local shm " << _trx->local_shm.name << " free failed.";
                }
                if (UNLIKELY(UBRingManager::ReleaseUbrTrxFromMgr(_trx, expect_ubr_id) != UBRING_OK)) {
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
    if (UNLIKELY((rc = ClearTrxResource(_trx)) != UBRING_OK)) {
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
    if (UNLIKELY(local_rx_event_q == nullptr)) {
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
    if (UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "Trx add close timer failed, trx is null.";
        return UBRING_ERR;
    }

    const uint32_t interval_us = FLAGS_ub_event_queue_timer_interval_us;
    _trx->close_chk_in_io_id = ATOMIC_LOAD(_trx->ubr_rx.in_io_id);
    _trx->close_chk_out_io_id = ATOMIC_LOAD(_trx->ubr_tx.out_io_id);
    RETURN_CODE rc = UbrTimerStart(&_trx->close_timer, 0, interval_us,
                                   UbrTrxCloseCallback, (void*)_trx,
                                   UbrCloseTimerBackoff);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Start ubr close timer failed, trx local name=" << _trx->local_shm.name;
        return UBRING_ERR;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrAddTimer() {
    if (UNLIKELY(UbrAddCloseTimer() != UBRING_OK)) {
        LOG(ERROR) << "Ubr " << _trx->local_shm.name << " add closed timer failed.";
        return UBRING_ERR;
    }

    if (UNLIKELY(UbrAddHBTimer() != UBRING_OK)) {
        UbrTimerDelAndWait(&_trx->close_timer);
        LOG(ERROR) << "Ubr " << _trx->local_shm.name << " add heartbeat timer failed.";
        return UBRING_ERR;
    }
    return UBRING_OK;
}

void* UBRing::UbrTrxCloseCallback(void* args) {
    auto* trx = (UbrTrx*) args;
    if (UNLIKELY(UBRing::UbrTrxCallbackCheck(trx) != UBRING_OK)) {
        return nullptr;
    }

    auto* local_rx_event_q = (UbrEventQMsg *)trx->ubr_rx.local_rx_event_q.addr;
    auto* local_tx_event_q = (UbrEventQMsg *)trx->ubr_tx.local_tx_event_q.addr;
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
        RETURN_CODE clear_rc = ClearTrxResource(trx);
        if (UNLIKELY(clear_rc != UBRING_OK && clear_rc != UBRING_REENTRY)) {
            LOG(ERROR) << "Trx close callback failed, " << trx->local_shm.name << " clear trx resource failed.";
            break;
        }
    } while (0);
    return nullptr;
}

RETURN_CODE UBRing::UbrAddHBTimer() {
    if (UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "Trx add heartbeat timer failed, trx is null.";
        return UBRING_ERR;
    }

    const uint64_t interval_us = (uint64_t)FLAGS_ub_hb_timer_interval_s * SEC_TO_USEC;
    RETURN_CODE rc = UbrTimerStart(&_trx->hb_timer, 0, interval_us,
                                   UbrTrxHBCallback, (void*)_trx);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Start ubr heartbeat timer failed.";
        return UBRING_ERR;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrPassiveClearTrx(UbrTrx *trx) {
    RETURN_CODE passive_close_check_rc = UbrTrxCloseCheck(trx);
    if (UNLIKELY(passive_close_check_rc != UBRING_OK)) {
        if (passive_close_check_rc == UBRING_REENTRY) {
            LOG(INFO) << "Passive close skipped, active close in progress, name=" << trx->local_shm.name;
            return ClearTrxResource(trx);
        }
        return UBRING_ERR;
    }
    trx->ubr_tx.trx_state = UBR_STATE_CLOSED;
    trx->ubr_rx.trx_state = UBR_STATE_CLOSED;
    // Non-blocking: this may run inside the heartbeat callback itself.
    UbrTimerDel(&trx->close_timer);
    UbrTimerDel(&trx->hb_timer);
    // Wait for in-flight IO on a one-shot timer instead of sleeping on the
    // timer thread.
    return UbrScheduleClearTimer(trx, UbrPassiveClearCallback, UbrDoPassiveClearWork);
}

void* UBRing::UbrPassiveClearCallback(void* args) {
    auto* ctl = (UbrCleanupCtl*)args;
    if (UNLIKELY(ctl == nullptr)) {
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
    UbrTrx* trx = ctl->trx;
    if (UNLIKELY(UBRingManager::IsUbrTrxSlotUsed(trx->trx_mgr_index, ctl->ubr_id))) {
        UbrDoPassiveClearWork(trx, ctl->ubr_id);
    }
    ATOMIC_STORE(ctl->state, UBR_CLEANUP_DONE);
    ctl->ReleaseRef();                       // timer/callback reference
    return nullptr;
}

void* UBRing::UbrTrxHBCallback(void* args) {
    auto* trx = (UbrTrx*) args;
    if (UNLIKELY(UbrTrxCallbackCheck(trx) != UBRING_OK)) {
        return nullptr;
    }

    auto* local_data_status = (UbrDataStatusQMsg *)trx->ubr_tx.local_data_status_q.addr;
    auto* remote_data_status = (UbrDataStatusQMsg *)trx->ubr_rx.remote_data_status_q.addr;
    if (UNLIKELY(local_data_status == nullptr || remote_data_status == nullptr)) {
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
    UbrPassiveClearTrx(trx);
    LOG(INFO) << "Ubr heartbeat clear trx resource finish.";
    return nullptr;
}

RETURN_CODE UBRing::UbrAddAsynClearTimer(UbrTrx *trx) {
    if (UNLIKELY(trx == nullptr)) {
        LOG(ERROR) << "Trx add close timer failed, trx is null.";
        return UBRING_ERR;
    }
    return UbrScheduleClearTimer(trx, UbrAsynClearCallback, UbrDoAsynClearWork);
}

void *UBRing::UbrAsynClearCallback(void *args)
{
    auto* ctl = (UbrCleanupCtl*) args;
    if (UNLIKELY(ctl == nullptr)) {
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
    UbrTrx* trx = ctl->trx;
    if (UNLIKELY(UBRingManager::IsUbrTrxSlotUsed(trx->trx_mgr_index, ctl->ubr_id))) {
        UbrDoAsynClearWork(trx, ctl->ubr_id);
    }
    ATOMIC_STORE(ctl->state, UBR_CLEANUP_DONE);
    ctl->ReleaseRef();                       // timer/callback reference
    return nullptr;
}

int UBRing::UbrTrxSend(const void *buf, uint32_t buf_len)
{
    if (UNLIKELY(CheckTrxSendPreCheck(_trx) != UBRING_OK)) {
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
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, buf, buf_len)) != UBRING_OK)) {
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
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, dest, buf_len)) != UBRING_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }

    int32_t total_copied = 0;
    int32_t remaining_len = (int32_t)buf_len;
    bool not_eof_encountered = true;

    UbrRx *ubr_rx = &_trx->ubr_rx;
    UbrMsgFormat *data_msg = (UbrMsgFormat *)ubr_rx->local_data_q.addr;
    bool need_update_epoll_eof_pos = ubr_rx->read_pos == ubr_rx->ep_eof_pos;

    while (not_eof_encountered && remaining_len > 0) {
        if (UNLIKELY(CheckTrxRecvPreCheck(_trx) != UBRING_OK)) {
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
        if (UNLIKELY(!IsRecvChunkHeaderValid(chunk_msg_len, cur_index))) {
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
        if (LIKELY(current_chunk->header[UBR_MSG_CUR_INDEX] == chunk_msg_len)) {
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
    if (UNLIKELY(CheckTrxSendPreCheck(_trx) != UBRING_OK)) {
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
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, iov, (uint32_t)iovcnt)) != UBRING_OK)) {
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
    if (UNLIKELY(nr == -1)) {
        LOG(ERROR) << "Non-blocking readv msg in failed, connection has been closed.";
        errno = EPIPE;
        return -1;
    }
    return nr;
}

ssize_t UBRing::UbrTrxReadvBlockMode(const struct iovec *iov, int iovcnt)
{
    RETURN_CODE rc = UBRING_OK;
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, iov, (uint32_t)iovcnt)) != UBRING_OK)) {
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
    if (UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "The trx to be checked is NULL.";
        return UBRING_ERR;
    }
    if (UNLIKELY(_trx->local_shm.addr == nullptr)) {
        LOG(ERROR) << "The trx local_shm to be checked is NULL.";
        return UBRING_ERR;
    }
    if (UNLIKELY(_trx->ubr_tx.trx_state != UBR_STATE_CONNECTED)) {
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
    if (UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "The trx to be checked is NULL.";
        return UBRING_ERR;
    }
    if (UNLIKELY(_trx->local_shm.addr == nullptr)) {
        LOG(ERROR) << "The trx local_shm to be checked is NULL.";
        return UBRING_ERR;
    }
    if (UNLIKELY((UbrEventQMsg *)_trx->ubr_tx.local_tx_event_q.addr == nullptr)) {
        LOG(ERROR) << "The trx local_tx_event_q addr is NULL.";
        return UBRING_ERR;
    }
    if (UNLIKELY((UbrEventQMsg *)_trx->ubr_tx.local_data_status_q.addr == nullptr)) {
        LOG(ERROR) << "The trx local_data_status_q addr is NULL.";
        return UBRING_ERR;
    }

    if (UNLIKELY(_trx->ubr_tx.trx_state != UBR_STATE_CONNECTED)) {
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
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx close, local unmap " << trx->local_shm.name << " shm fail.";
        return UBRING_ERR;
    }

    rc = ShmFree(&trx->local_shm);
    if (UNLIKELY(rc != UBRING_OK)) {
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
    if (UNLIKELY(_trx == nullptr)) {
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
    if (UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null.";
        return UBRING_ERR;
    }
    if (UNLIKELY(local_shm == nullptr || local_shm->addr == nullptr)) {
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
    if (UNLIKELY(_trx == nullptr)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null.";
        return UBRING_ERR;
    }
    if (UNLIKELY(remote_shm == nullptr || remote_shm->addr == nullptr)) {
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
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) <<"Trx map shared memory failed.";
        return rc;
    }

    uint32_t local_data_msg_cap = (uint32_t)(_trx->ubr_rx.local_data_q.len / UBR_MSG_LEN);
    uint32_t remote_data_msg_cap = (uint32_t)(_trx->ubr_tx.remote_data_q.len / UBR_MSG_LEN);
    _trx->ubr_rx.capacity = local_data_msg_cap;
    _trx->ubr_tx.capacity = remote_data_msg_cap;
    rc = UBRingManager::GetUbrDealMsgMaxCnt(_trx->ubr_rx.capacity, &_trx->ubr_rx.deal_msg_max_cnt);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Get ubring deal msg max cnt.";
        return rc;
    }
    PrewriteUbrRx(&_trx->ubr_rx);
    PrewriteUbrTx(&_trx->ubr_tx);

    ((UbrDataStatusQMsg *)(_trx->ubr_tx.local_data_status_q.addr))->tail = remote_data_msg_cap - 1;
    ((UbrDataStatusQMsg *)(_trx->ubr_rx.remote_data_status_q.addr))->tail = local_data_msg_cap - 1;

    if (UNLIKELY(UbrAddTimer() != UBRING_OK)) {
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
    if (UNLIKELY((ShmRemoteMalloc(remote_trx_shm)) != UBRING_OK)) {
        LOG(ERROR) << "Trx apply remote shared memory failed.";
        return -1;
    }

    if (UNLIKELY((ShmLocalCalloc(local_trx_shm)) != UBRING_OK)) {
        LOG(ERROR) << "Trx apply local shared memory failed.";
        ShmRemoteFree(remote_trx_shm);
        return -1;
    }

    UbrTrx **ubr_trx_ptr = &_trx;
    if (UNLIKELY((UBRingManager::AcquireUbrTrxFromMgr(ubr_trx_ptr)) != UBRING_OK)) {
        LOG(ERROR) << "Acquire ubrtrx failed.";
        ShmRemoteFree(remote_trx_shm);
        ShmLocalFree(local_trx_shm);
        return -1;
    }
    _trx->type = TCP_TRX;
    if (UNLIKELY((UbrServerTrxInit(local_trx_shm, remote_trx_shm)) != UBRING_OK)) {
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
    if (UNLIKELY((UBRingManager::AcquireUbrTrxFromMgr(&(_trx))) != UBRING_OK)) {
        LOG(ERROR) << "Acquire ubrtrx failed, local_name=" << shm_name;
        return -1;
    }

    _trx->type = TCP_TRX;
    if (UNLIKELY((ApplyAndMapLocalShm(local_trx_shm, shm_name)) != UBRING_OK)) {
        LOG(ERROR) << "Trx apply or map local shared memory failed, local_name=" << shm_name;
        _trx = nullptr;
        return -1;
    }
    return 0;
}

int UBRing::UbrMapRemoteShm(SHM *local_trx_shm, const char *local_name)
{
    RETURN_CODE rc = UbrMapRemoteShmAddTimer(local_trx_shm, local_name);
    if (UNLIKELY(rc != UBRING_OK)) {
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
    if (UNLIKELY(result < 0)) {
        LOG(ERROR) << "Copy server shared memory name failed, local_name=" << local_name
                   << ", ret=" << result;
        return UBRING_ERR;
    }
    UbrSetSleepTask(UBR_TASK_CONNECT_MAP_FRONT);
    RETURN_CODE rc = ApplyAndMapRemoteShm(&remote_trx_shm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Connect Trx map shared memory failed, remote shm=" << remote_trx_shm.name;
        return rc;
    }

    if (UNLIKELY(UbrAddTimer() != UBRING_OK)) {
        LOG(ERROR) << "Ubr add timer failed, local_name=" << local_name;
        ShmRemoteFree(&_trx->remote_shm);
        return UBRING_ERR;
    }

    UbrSetSleepTask(UBR_TASK_CONNECT_MAP_AFTER);

    uint32_t timeout = ((UbrDataStatusQMsg *)(_trx->ubr_tx.local_data_status_q.addr))->timeout;
    if (HasTimedOut(start_time, timeout) != UBRING_OK) {
        LOG(ERROR) << "Local shm " << local_trx_shm->name << " wait for connect remote map timeout.";
        UbrTimerDelAndWait(&_trx->hb_timer);
        UbrTimerDelAndWait(&_trx->close_timer);
        ShmRemoteFree(&_trx->remote_shm);
        return UBRING_ERR_TIMEOUT;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::ApplyAndMapLocalShm(SHM *local_trx_shm, const char *local_name)
{
    if (UNLIKELY(_trx == nullptr || local_trx_shm == nullptr)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null, local_name=" << local_name;
        return UBRING_ERR;
    }
    int result = snprintf(local_trx_shm->name,
        SHM_MAX_NAME_BUFF_LEN,
        "%s_%s_%s",
        SHM_NAME_PREFIX,
        local_name,
        CLIENT_SHM_NAME_SUFFIX);
    if (UNLIKELY(result < 0)) {
        LOG(ERROR) << "Copy client localTrx shared memory name failed, local_name=" << local_name << ", ret=" << result;
        return UBRING_ERR;
    }

    RETURN_CODE rc = ShmLocalCalloc(local_trx_shm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx apply local shared memory failed, local shm name=" << local_trx_shm->name << ", rc=" << rc;
        if (rc == SHM_ERR_EXIST || rc == SHM_ERR_NOT_FOUND) {
            rc = UBR_ERR_ADDR_IN_USE;
        }
        UBRingManager::ReleaseUbrTrxFromMgr(_trx, ATOMIC_LOAD(_trx->ubr_id));
        return rc;
    }
    rc = UbrTrxMapLocalShm(local_trx_shm);
    if (UNLIKELY(rc != UBRING_OK)) {
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
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx apply remote shared memory failed.";
        return rc;
    }
    rc = UbrTrxMapRemoteShm(remote_trx_shm);
    if (UNLIKELY(rc != UBRING_OK)) {
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
    if (UNLIKELY(trx == nullptr)) {
        LOG(ERROR) << "Trx close failed, trx is null.";
        return UBRING_ERR;
    }

    UbrEventQMsg* local_tx_event_q = (UbrEventQMsg *)trx->ubr_tx.local_tx_event_q.addr;
    if (UNLIKELY(local_tx_event_q == nullptr)) {
        LOG(ERROR) << "Trx close failed, local_tx_event_q addr is NULL, trx local name=" << trx->local_shm.name;
        return UBRING_ERR;
    }
    if (local_tx_event_q->flag == UBR_STATE_CONNECTED) {
        local_tx_event_q->flag = UBR_STATE_CLOSING;
    }

    // Non-blocking: may run inside the close callback itself.
    UbrTimerDel(&trx->close_timer);
    UbrTimerDel(&trx->hb_timer);

    if (local_tx_event_q->flag == UBR_STATE_CLOSING) {
        local_tx_event_q->flag = UBR_STATE_CLOSED;
        trx->ubr_tx.trx_state = UBR_STATE_CLOSED;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::ClearTrxResource(UbrTrx *trx)
{
    RETURN_CODE rc = UbrClearResourceCheck(trx);
    if (rc != UBRING_OK) {
        return rc;
    }

    rc = UbrAddAsynClearTimer(trx);
    if (rc != UBRING_OK) {
        LOG(ERROR) << "Trx close, add " << trx->local_shm.name << " close clear timer failed.";
        return UBRING_ERR;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::UbrTrxCloseCheck(UbrTrx *trx)
{
    if (UNLIKELY(trx == nullptr)) {
        LOG(ERROR) << "Trx close failed, client trx is null.";
        return UBRING_ERR;
    }
    int expected = MAX_CLOSE_COUNT;
    if (!ATOMIC_COMPARE_EXCHANGE_STRONG(trx->close_cnt, expected, MAX_CLOSE_COUNT - 1)) {
        LOG(INFO) << "Trx close skipped, already closing, trx local name=" << trx->local_shm.name;
        return UBRING_REENTRY;
    }

    if (UNLIKELY(trx->ubr_tx.local_tx_event_q.addr == nullptr)) {
        LOG(ERROR) << "Trx close failed, local_tx_event_q addr is NULL, trx local name=" << trx->local_shm.name;
        return UBRING_ERR;
    }
    return UBRING_OK;
}

ssize_t UBRing::StartReadv(UbrTrx *trx, const struct iovec *iov, int iovcnt, size_t remain_buf_len)
{
    ssize_t total_recv_len = 0;
    int iov_index = 0;
    size_t iov_pos = 0;
    UbrMsgFormat *data_msg = (UbrMsgFormat *)trx->ubr_rx.local_data_q.addr;
    bool not_eof_encountered = true;
    while (not_eof_encountered && remain_buf_len > 0) {
        if (UNLIKELY(CheckTrxRecvPreCheck(trx) != UBRING_OK)) {
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
        if (UNLIKELY(!IsRecvChunkHeaderValid(chunk_msg_len, cur_index))) {
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
