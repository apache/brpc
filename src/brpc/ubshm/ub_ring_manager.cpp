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
#include <vector>
#include <gflags/gflags.h>
#include "brpc/ubshm/ub_ring.h"
#include "brpc/ubshm/ub_ring_manager.h"
#include "butil/logging.h"
#include "butil/scoped_lock.h"

namespace brpc {
namespace ubring {

// A UbrCleanupCtl reference count of 1 means only the manager anchor is
// left, i.e. no cleanup callback is in flight for it.
static constexpr int kAnchoredRefOnly = 1;

DEFINE_int32(ubr_max_managed_num, 1024, "maximum number of managed ubring");
DEFINE_int32(tail_update_after_read, 8, "Position of the tail update after the read");

UbrMgr UBRingManager::g_ubr_mgr;
UbrLinkInfoMgr UBRingManager::g_link_info_mgr;
pthread_mutex_t UBRingManager::g_ubr_trx_mgr_mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t UBRingManager::g_ubr_listener_mgr_mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t UBRingManager::g_link_info_mgr_mtx = PTHREAD_MUTEX_INITIALIZER;

uint64_t g_ubr_trx_num = 0;
uint64_t g_ub_event_cnt = 0;
uint64_t g_ubr_listener_num = 0;

RETURN_CODE UBRingManager::GetUbrDealMsgMaxCnt(const uint32_t capacity, uint32_t *deal_msg_max_cnt) {
    if (BAIDU_UNLIKELY(deal_msg_max_cnt == nullptr)) {
        LOG(ERROR) << "Get update factor failed, deal_msg_max_cnt is null.";
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY(FLAGS_tail_update_after_read == 0)) {
        LOG(ERROR) << "Get update factor failed, factor is 0.";
        return UBRING_ERR;
    }
    *deal_msg_max_cnt = capacity / FLAGS_tail_update_after_read;
    return UBRING_OK;
}

RETURN_CODE UBRingManager::UbrMgrDefault()
{
    g_ubr_mgr.trx_num = 0;
    g_ubr_mgr.trx_cap = FLAGS_ubr_max_managed_num;
    g_ubr_mgr.shutting_down = false;
    g_ubr_mgr.active_pool_ops = 0;
    g_ubr_mgr.trx_mgr_unit_status = nullptr;
    g_ubr_mgr.trx_mgr = nullptr;
    g_ubr_mgr.trx_mgr_unit_id = nullptr;
    g_ubr_mgr.trx_mgr_unit_ctl = nullptr;
    return UBRING_OK;
}

RETURN_CODE UBRingManager::UbrMgrInit() {
    RETURN_CODE rc = UbrMgrDefault();
    if (BAIDU_UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Ubr manager set default values failed.";
        return rc;
    }

    size_t trx_mgr_size = g_ubr_mgr.trx_cap * sizeof(UbrTrx);
    g_ubr_mgr.trx_mgr = (UbrTrx *)malloc(trx_mgr_size);
    size_t trx_mgr_status_size = g_ubr_mgr.trx_cap * sizeof(UbrMgrUnitStatus);
    g_ubr_mgr.trx_mgr_unit_status = (UbrMgrUnitStatus *)malloc(trx_mgr_status_size);
    size_t trx_mgr_id_size = g_ubr_mgr.trx_cap * sizeof(uint64_t);
    g_ubr_mgr.trx_mgr_unit_id = (uint64_t *)malloc(trx_mgr_id_size);
    size_t trx_mgr_ctl_size = g_ubr_mgr.trx_cap * sizeof(UbrCleanupCtl *);
    g_ubr_mgr.trx_mgr_unit_ctl = (UbrCleanupCtl **)malloc(trx_mgr_ctl_size);
    if (BAIDU_UNLIKELY(g_ubr_mgr.trx_mgr == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_status == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_id == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_ctl == nullptr)) {
        LOG(ERROR) << "Ubr manager memory allocation failed.";
        UbrMgrFini();
        return UBRING_ERR;
    }

    // UbrTrx holds butil::atomic members, so it is not trivially copyable and
    // must not be memset: value-initialize every slot instead. A slot is
    // re-initialized by AcquireUbrTrxFromMgr before it is handed out, and no
    // code reads a slot that was never acquired.
    for (uint32_t i = 0; i < g_ubr_mgr.trx_cap; ++i) {
        new (&g_ubr_mgr.trx_mgr[i]) UbrTrx();
    }
    memset(g_ubr_mgr.trx_mgr_unit_status, UBR_MGR_UNIT_FREE, trx_mgr_status_size);
    memset(g_ubr_mgr.trx_mgr_unit_id, 0, trx_mgr_id_size);
    memset(g_ubr_mgr.trx_mgr_unit_ctl, 0, trx_mgr_ctl_size);
    LinkInfoInit();
    return UBRING_OK;
}

void UBRingManager::UbrMgrFini() {
    // Refuse new pool users and new acquisitions first, unconditionally: the
    // partial-init path below (a failed allocation) must not let a callback
    // walk arrays that the failure left null either.
    {
        BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
        g_ubr_mgr.shutting_down = true;
    }

    // The pool arrays are only safe to walk once UbrMgrInit has allocated and
    // zeroed all four of them; its allocation-failure path calls this function
    // with some of them still null and the others uninitialized.
    const bool pool_ready = g_ubr_mgr.trx_mgr != nullptr &&
                            g_ubr_mgr.trx_mgr_unit_status != nullptr &&
                            g_ubr_mgr.trx_mgr_unit_id != nullptr &&
                            g_ubr_mgr.trx_mgr_unit_ctl != nullptr;

    // Wait out close/heartbeat callbacks that are already dispatched, then
    // stop the timers, before the pool is freed: UbrTimerDel alone is
    // non-blocking, so a callback that already passed its generation check
    // could still be reading UbrTrx when FREE_PTR(trx_mgr) runs.
    // UbrTimerDelAndWait blocks until a dispatched callback has returned, but
    // it must run without g_ubr_trx_mgr_mtx because the callbacks take that
    // lock themselves; snapshot the slots under the lock first.
    if (pool_ready) {
        std::vector<uint32_t> used_slots;
        {
            BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
            for (uint32_t i = 0; i < g_ubr_mgr.trx_cap; ++i) {
                if (g_ubr_mgr.trx_mgr_unit_status[i] == UBR_MGR_UNIT_USED) {
                    used_slots.push_back(i);
                }
            }
        }
        // Wait for the callback-driven pool accesses that are already running
        // (they read UbrTrx without owning a timer), then for the timers. Both
        // waits must happen without g_ubr_trx_mgr_mtx: the close/heartbeat
        // callbacks and the accesses themselves take that lock.
        for (;;) {
            {
                BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
                if (g_ubr_mgr.active_pool_ops == 0) {
                    break;
                }
            }
            usleep(1000);
        }
        // No timer can be armed after the flag: arming and taking the flag are
        // serialized by the manager lock (ArmTimersExclusive), and a cleanup
        // control object can no longer be published either
        // (TryPublishUnitCleanupCtl refuses once shutting down). So the timers
        // armed before the flag are the complete set, and stopping them all
        // leaves the pool quiescent.
        for (uint32_t i : used_slots) {
            UbrTimerDelAndWait(&g_ubr_mgr.trx_mgr[i].close_timer);
            UbrTimerDelAndWait(&g_ubr_mgr.trx_mgr[i].hb_timer);
        }
    }

    // Cancel the pending delayed cleanups and wait for the in-flight ones
    // (each holds one extra reference) to finish, before the pool memory
    // they touch is freed. A ctl whose timer is still starting can only be
    // cancelled in a later round, hence the retry-to-stability loop.
    bool busy = true;
    while (busy) {
        busy = false;
        {
            BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
            if (pool_ready) {
                for (uint32_t i = 0; i < g_ubr_mgr.trx_cap; ++i) {
                    UbrCleanupCtl* ctl = g_ubr_mgr.trx_mgr_unit_ctl[i];
                    if (ctl == nullptr) {
                        continue;
                    }
                    if (UbrTimerDel(&ctl->timer) == 0) {
                        ctl->ReleaseRef();   // timer/callback reference
                    }
                    if (ctl->ref.load() > kAnchoredRefOnly) {
                        busy = true;
                    }
                }
            }
        }
        if (busy) {
            LOG_EVERY_SECOND(INFO) << "UbrMgrFini waits for in-flight cleanups.";
            usleep(1000);
        }
    }
    {
        BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
        if (pool_ready) {
            for (uint32_t i = 0; i < g_ubr_mgr.trx_cap; ++i) {
                UbrCleanupCtl* ctl = g_ubr_mgr.trx_mgr_unit_ctl[i];
                if (ctl != nullptr) {
                    g_ubr_mgr.trx_mgr_unit_ctl[i] = nullptr;
                    ctl->ReleaseRef();           // manager reference
                }
            }
        }
        FREE_PTR(g_ubr_mgr.trx_mgr);
        FREE_PTR(g_ubr_mgr.trx_mgr_unit_status);
        FREE_PTR(g_ubr_mgr.trx_mgr_unit_id);
        FREE_PTR(g_ubr_mgr.trx_mgr_unit_ctl);
    }
    {
        BAIDU_SCOPED_LOCK(g_ubr_listener_mgr_mtx);
    }
    g_ubr_mgr.trx_num = 0;
    g_ubr_mgr.trx_cap = 0;
    LinkInfoFini();
}

RETURN_CODE UBRingManager::AcquireUbrTrxFromMgr(UbrTrx **trx) {
    if (BAIDU_UNLIKELY(trx == nullptr)) {
        LOG(ERROR) << "Acquire trx failed, trx is null.";
        return UBRING_ERR;
    }

    if (BAIDU_UNLIKELY(g_ubr_mgr.trx_mgr == nullptr)) {
        LOG(ERROR) << "Acquire trx failed, trx_mgr is null.";
        return UBRING_ERR;
    }

    BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
    if (BAIDU_UNLIKELY(g_ubr_mgr.shutting_down)) {
        LOG(ERROR) << "Acquire trx failed, ubr manager is shutting down.";
        return UBRING_ERR;
    }
    if (g_ubr_mgr.trx_num >= g_ubr_mgr.trx_cap) {
        LOG(ERROR) << "Acquire trx failed, trx number is full.";
        return UBRING_ERR;
    }

    for (uint32_t i = 0; i < g_ubr_mgr.trx_cap; ++i) {
        if (g_ubr_mgr.trx_mgr_unit_status[i] == UBR_MGR_UNIT_FREE) {
            // Value-initialize the slot so its butil::atomic members are
            // properly constructed while every other field is zeroed, which
            // is what the previous memset did.
            new (&g_ubr_mgr.trx_mgr[i]) UbrTrx();
            // The explicit re-initialization is deliberate: it documents the
            // per-acquisition invariants of these fields.
            g_ubr_mgr.trx_mgr[i].close_timer = nullptr;
            g_ubr_mgr.trx_mgr[i].hb_timer = nullptr;
            g_ubr_mgr.trx_mgr[i].cleanup_ctl = nullptr;
            g_ubr_mgr.trx_mgr[i].cleanup_forced.store(false);
            // Retire the previous acquisition's cleanup control object.
            UbrCleanupCtl* old_ctl = g_ubr_mgr.trx_mgr_unit_ctl[i];
            g_ubr_mgr.trx_mgr_unit_ctl[i] = nullptr;
            if (old_ctl != nullptr) {
                if (UbrTimerDel(&old_ctl->timer) == 0) {
                    old_ctl->ReleaseRef();       // timer/callback reference
                }
                old_ctl->ReleaseRef();           // manager anchor
            }
            g_ubr_mgr.trx_mgr_unit_status[i] = UBR_MGR_UNIT_USED;
            *trx = &g_ubr_mgr.trx_mgr[i];
            (*trx)->trx_mgr_index = i;
            ATOMIC_STORE((*trx)->ubr_id, g_ubr_trx_num);
            g_ubr_mgr.trx_mgr_unit_id[i] = g_ubr_trx_num;
            (*trx)->close_state = UBR_CLOSE_FIRST;
            (*trx)->close_cnt = MAX_CLOSE_COUNT;
            ++g_ubr_mgr.trx_num;
            ++g_ubr_trx_num;
            return UBRING_OK;
        }
    }
    LOG(ERROR) << "Acquire trx failed, no available space.";
    return UBRING_ERR;
}

RETURN_CODE UBRingManager::ReleaseUbrTrxFromMgr(UbrTrx *trx,
                                                uint64_t expect_ubr_id) {
    if (BAIDU_UNLIKELY(trx == nullptr)) {
        LOG(ERROR) << "Release trx failed, trx is null.";
        return UBRING_ERR;
    }
    if (BAIDU_UNLIKELY(g_ubr_mgr.trx_mgr == nullptr)) {
        LOG(ERROR) << "Release trx failed, trx_mgr is null.";
        return UBRING_ERR;
    }

    BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
    uint32_t idx = trx->trx_mgr_index;
    if (g_ubr_mgr.trx_mgr_unit_status[idx] == UBR_MGR_UNIT_FREE) {
        LOG(INFO) << "Release trx already freed, name=" << trx->local_shm.name;
        return UBRING_OK;
    }

    if (BAIDU_UNLIKELY(g_ubr_mgr.trx_mgr_unit_id[idx] != expect_ubr_id)) {
        // The slot was released and acquired again meanwhile; the stale
        // caller must not touch the new occupant.
        LOG(WARNING) << "Release stale trx refused, name=" << trx->local_shm.name;
        return UBRING_OK;
    }

    if (g_ubr_mgr.trx_num == 0) {
        LOG(ERROR) << "Release trx failed, trx number is 0.";
        return UBRING_ERR;
    }

    // Disarm the per-acquisition periodic timers before the slot becomes
    // reusable: a surviving task would keep re-arming and firing against the
    // slot's next occupant. Non-blocking delete only -- this path holds
    // g_ubr_trx_mgr_mtx and the timer callbacks take the same lock, so
    // UbrTimerDelAndWait would deadlock; it is not needed either, because
    // every caller has already quiesced the per-trx callbacks before reaching
    // here: the clear paths wait through UbrStopTrxTimer (or run on the single
    // bthread timer thread) before they call ReleaseUbrTrxFromMgr, the force
    // close waits in UbrTrxClose, and UbrMgrFini waits for all of them.
    UbrTimerDel(&trx->close_timer);
    UbrTimerDel(&trx->hb_timer);

    // Mutate the trx only after the generation check passed.
    trx->local_shm.addr = nullptr;
    trx->ubr_tx.local_tx_event_q.addr = nullptr;
    trx->ubr_tx.local_data_status_q.addr = nullptr;
    trx->ubr_rx.local_rx_event_q.addr = nullptr;
    trx->ubr_rx.remote_data_status_q.addr = nullptr;
    g_ubr_mgr.trx_mgr_unit_status[idx] = UBR_MGR_UNIT_FREE;
    --g_ubr_mgr.trx_num;
    return UBRING_OK;
}

UbrCleanupClaim UBRingManager::ClaimTrxCleanupForced(
        uint32_t idx, uint64_t expect_ubr_id, UbrCleanupCtl** out_ctl) {
    if (out_ctl != nullptr) {
        *out_ctl = nullptr;
    }
    BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
    if (BAIDU_UNLIKELY(g_ubr_mgr.trx_mgr == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_status == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_id == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_ctl == nullptr ||
                 idx >= g_ubr_mgr.trx_cap)) {
        return UBR_CLEANUP_CLAIM_NOT_OURS;
    }
    // Same generation check as TryClaimTrxClose: a caller that snapshotted the
    // slot before a release/reuse must not claim the new occupant's cleanup.
    if (g_ubr_mgr.trx_mgr_unit_status[idx] != UBR_MGR_UNIT_USED ||
        g_ubr_mgr.trx_mgr_unit_id[idx] != expect_ubr_id) {
        return UBR_CLEANUP_CLAIM_NOT_OURS;
    }
    UbrCleanupCtl* ctl = g_ubr_mgr.trx_mgr_unit_ctl[idx];
    if (ctl != nullptr) {
        // An anchored ctl always belongs to the current generation: the
        // publication checked it and the acquire retires the previous one.
        // Keep the explicit check as a belt-and-braces guard.
        if (BAIDU_UNLIKELY(ctl->ubr_id != expect_ubr_id)) {
            return UBR_CLEANUP_CLAIM_NOT_OURS;
        }
        ctl->ref.fetch_add(1);               // snapshot reference
        if (out_ctl != nullptr) {
            *out_ctl = ctl;
        }
        return UBR_CLEANUP_CLAIM_HAS_CTL;
    }
    // No cleanup was published: this call owns the forced cleanup, and
    // refusing later publications keeps it the only owner. TryPublishUnitCleanupCtl
    // checks the flag in this same critical section, so a publication either
    // happened before (we would have seen its ctl above) or is refused.
    g_ubr_mgr.trx_mgr[idx].cleanup_forced.store(true);
    return UBR_CLEANUP_CLAIM_OWNED_NULL;
}

bool UBRingManager::IsUbrTrxSlotUsed(uint32_t idx, uint64_t expect_ubr_id) {
    BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
    if (BAIDU_UNLIKELY(g_ubr_mgr.trx_mgr_unit_id == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_status == nullptr ||
                 idx >= g_ubr_mgr.trx_cap)) {
        return false;
    }
    return g_ubr_mgr.trx_mgr_unit_status[idx] == UBR_MGR_UNIT_USED &&
           g_ubr_mgr.trx_mgr_unit_id[idx] == expect_ubr_id;
}

bool UBRingManager::BeginPoolAccess() {
    BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
    // Check every array the caller will walk, not just trx_mgr: UbrMgrInit can
    // fail after allocating some of them, and that path calls UbrMgrFini with
    // pool_ready == false.
    if (BAIDU_UNLIKELY(g_ubr_mgr.shutting_down ||
                 g_ubr_mgr.trx_mgr == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_status == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_id == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_ctl == nullptr)) {
        return false;
    }
    ++g_ubr_mgr.active_pool_ops;
    return true;
}

void UBRingManager::FinishPoolAccess() {
    BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
    if (g_ubr_mgr.active_pool_ops > 0) {
        --g_ubr_mgr.active_pool_ops;
    }
}

bool UBRingManager::ArmTimersExclusive(const std::function<bool()>& arm) {
    BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
    if (BAIDU_UNLIKELY(g_ubr_mgr.shutting_down)) {
        return false;
    }
    return arm();
}

RETURN_CODE UBRingManager::TryClaimTrxClose(uint32_t idx, uint64_t expect_ubr_id) {
    BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
    if (BAIDU_UNLIKELY(g_ubr_mgr.trx_mgr == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_status == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_id == nullptr ||
                 idx >= g_ubr_mgr.trx_cap)) {
        return UBRING_ERR;
    }
    // A caller that snapshotted the slot before a release/reuse must not claim
    // the close of the slot's new occupant: its generation check and the claim
    // happen together here, under the lock the acquire/release paths also take.
    if (g_ubr_mgr.trx_mgr_unit_status[idx] != UBR_MGR_UNIT_USED ||
        g_ubr_mgr.trx_mgr_unit_id[idx] != expect_ubr_id) {
        return UBRING_ERR;
    }
    UbrTrx* trx = &g_ubr_mgr.trx_mgr[idx];
    // Validate before claiming, so a rejected close leaves the counter intact
    // and the slot is not stuck in "closing" forever.
    if (BAIDU_UNLIKELY(trx->ubr_tx.local_tx_event_q.addr == nullptr)) {
        LOG(ERROR) << "Trx close failed, local_tx_event_q addr is NULL, trx local name="
                   << trx->local_shm.name;
        return UBRING_ERR;
    }
    int expected = MAX_CLOSE_COUNT;
    if (!ATOMIC_COMPARE_EXCHANGE_STRONG(trx->close_cnt, expected, MAX_CLOSE_COUNT - 1)) {
        return UBRING_REENTRY;
    }
    return UBRING_OK;
}

bool UBRingManager::TryPublishUnitCleanupCtl(uint32_t idx,
                                             uint64_t expect_ubr_id,
                                             UbrCleanupCtl *ctl) {
    BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
    if (BAIDU_UNLIKELY(g_ubr_mgr.shutting_down ||
                 g_ubr_mgr.trx_mgr == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_ctl == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_status == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_id == nullptr ||
                 idx >= g_ubr_mgr.trx_cap ||
                 g_ubr_mgr.trx_mgr_unit_status[idx] != UBR_MGR_UNIT_USED ||
                 g_ubr_mgr.trx_mgr_unit_id[idx] != expect_ubr_id ||
                 g_ubr_mgr.trx_mgr[idx].cleanup_forced.load() ||
                 g_ubr_mgr.trx_mgr_unit_ctl[idx] != nullptr)) {
        return false;                        // shutting down / released / reused / forced / already anchored
    }
    // Publish on the trx and anchor it in the pool slot inside one critical
    // section: a force close that snapshots the slot (also under this lock)
    // can then never observe the trx-side publication without its anchor, nor
    // claim the no-ctl cleanup path for a cleanup that is being scheduled.
    UbrCleanupCtl* expected = nullptr;
    if (!g_ubr_mgr.trx_mgr[idx].cleanup_ctl.compare_exchange_strong(expected, ctl)) {
        return false;                        // another schedule won
    }
    ctl->ref.fetch_add(1);                   // manager anchor reference
    g_ubr_mgr.trx_mgr_unit_ctl[idx] = ctl;
    return true;
}

bool UBRingManager::DetachUnitCleanupCtl(uint32_t idx, UbrCleanupCtl *ctl) {
    BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
    if (BAIDU_UNLIKELY(g_ubr_mgr.trx_mgr == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_ctl == nullptr ||
                 idx >= g_ubr_mgr.trx_cap ||
                 g_ubr_mgr.trx_mgr_unit_ctl[idx] != ctl)) {
        return false;
    }
    g_ubr_mgr.trx_mgr_unit_ctl[idx] = nullptr;
    // Clear the trx-side publication in the same critical section, and only
    // while it still belongs to `ctl': a slot reused meanwhile may already
    // carry the new occupant's control object.
    UbrCleanupCtl* published = ctl;
    g_ubr_mgr.trx_mgr[idx].cleanup_ctl.compare_exchange_strong(published, nullptr);
    ctl->ReleaseRef();                           // manager reference
    return true;
}

void UBRingManager::LinkInfoInit(void) {

    size_t link_info_mgr_size = FLAGS_ubr_max_managed_num * sizeof(UbrLinkInfo);
    g_link_info_mgr.all_link_info = (UbrLinkInfo*) malloc(link_info_mgr_size);
    if (g_link_info_mgr.all_link_info == nullptr) {
        LOG(ERROR) << "all_link_info is NULL";
        LinkInfoFini();
        return;
    }

    g_link_info_mgr.link_mgr_unit_status = (UbrMgrUnitStatus*) malloc(link_info_mgr_size);
    if (g_link_info_mgr.link_mgr_unit_status == nullptr) {
        LinkInfoFini();
        return;
    }

    memset(g_link_info_mgr.all_link_info, 0, link_info_mgr_size);
    memset(g_link_info_mgr.link_mgr_unit_status, 0, link_info_mgr_size);
}

void UBRingManager::LinkInfoFini(void) {
    if (g_link_info_mgr.link_mgr_unit_status == nullptr || g_link_info_mgr.all_link_info == nullptr) {
        LOG(ERROR) << "LinkInfo is NULL";
        return;
    }
    {
        BAIDU_SCOPED_LOCK(g_link_info_mgr_mtx);
        FREE_PTR(g_link_info_mgr.all_link_info);
        FREE_PTR(g_link_info_mgr.link_mgr_unit_status);
    }

    g_link_info_mgr.link_num = 0;
}

void UBRingManager::AcquireLinkInfoToMgr(const char *listener_name, UbrTrx *trx) {
    if (listener_name == nullptr || trx == nullptr) {
        LOG(ERROR) << "LinkInfo acquire fail.";
        return;
    }

    if (g_link_info_mgr.link_mgr_unit_status == nullptr || g_link_info_mgr.all_link_info == nullptr) {
        LOG(ERROR) << "LinkInfo is NULL.";
        return;
    }
    uint32_t ubr_index = trx->trx_mgr_index;
    char* connect_name = trx->local_shm.name;
    if (g_link_info_mgr.link_mgr_unit_status[ubr_index] == UBR_MGR_UNIT_FREE) {
        strncpy(g_link_info_mgr.all_link_info[ubr_index].connect_name,
                      connect_name, SHM_MAX_NAME_BUFF_LEN);
        strncpy(g_link_info_mgr.all_link_info[ubr_index].listener_name,
                      listener_name, SHM_MAX_NAME_BUFF_LEN);
        g_link_info_mgr.link_mgr_unit_status[ubr_index] = UBR_MGR_UNIT_USED;
        g_link_info_mgr.link_num++;
    }
}

void UBRingManager::ReleaseLinkInfoFromMgr(UbrTrx *trx) {
    if (trx == nullptr || g_link_info_mgr.link_mgr_unit_status == nullptr) {
        LOG(ERROR) << "LinkInfo release fail.";
        return;
    }

    if (g_link_info_mgr.link_mgr_unit_status[trx->trx_mgr_index] == UBR_MGR_UNIT_FREE) {
        LOG(ERROR) << "Release linkInfo failed, trx is not in manager.";
        return;
    }
    g_link_info_mgr.link_mgr_unit_status[trx->trx_mgr_index] = UBR_MGR_UNIT_FREE;
    g_link_info_mgr.link_num--;
}

int32_t UBRingManager::UbEventCallback(const char *shm_name)
{
    if (BAIDU_UNLIKELY(shm_name == nullptr)) {
        LOG(ERROR) << "Ub event callback failed, shm name is null.";
        return UBRING_ERR;
    }
    // This callback runs on an SDK thread with no timer or cleanup reference
    // keeping the pool alive, so register the access first: it is refused once
    // UbrMgrFini started, and UbrMgrFini waits for the accepted accesses before
    // it frees the pool. The UbrTrx* below is therefore valid until
    // FinishPoolAccess, even though the lookup lock is released in between.
    if (!BeginPoolAccess()) {
        LOG(WARNING) << "Ub event callback skipped, ubr manager is shutting down. shm_name="
                     << shm_name;
        return UBRING_ERR;
    }

    // Look the faulty link up under the manager lock: the pool walk reads the
    // slot status and the shm names, both of which a concurrent acquire or
    // release rewrites. The generation is snapshotted in the same critical
    // section, and the close claim below re-validates it, so a slot released
    // and reused after this lookup cannot be closed by mistake.
    uint32_t idx = 0;
    uint64_t expect_ubr_id = 0;
    int fd = -1;
    bool found = false;
    {
        BAIDU_SCOPED_LOCK(g_ubr_trx_mgr_mtx);
        for (uint32_t i = 0; i < g_ubr_mgr.trx_cap; ++i) {
            if (g_ubr_mgr.trx_mgr_unit_status[i] == UBR_MGR_UNIT_FREE) {
                continue;
            }
            if (strcmp(g_ubr_mgr.trx_mgr[i].local_shm.name, shm_name) == 0 ||   // the failed link is this trx's local shm
                strcmp(g_ubr_mgr.trx_mgr[i].remote_shm.name, shm_name) == 0) {  // the failed link is this trx's remote shm
                idx = i;
                expect_ubr_id = ATOMIC_LOAD(g_ubr_mgr.trx_mgr[i].ubr_id);
                fd = (int)g_ubr_mgr.trx_mgr[i].local_shm.fd;
                found = true;
                break;
            }
        }
    }
    int32_t rc = UBRING_ERR;
    if (found) {
        ++g_ub_event_cnt;
        LOG(WARNING) << "Ub event callback, the fd of the faulty link is " << fd;
        rc = UBRing::UbrPassiveClearTrx(&g_ubr_mgr.trx_mgr[idx], expect_ubr_id);
    }
    FinishPoolAccess();
    return rc;
}
}
}
