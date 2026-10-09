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

#ifndef BRPC_UB_RING_MANAGER_H
#define BRPC_UB_RING_MANAGER_H

#include <functional>

#include "brpc/ubshm/ubr_trx.h"
#include "brpc/ubshm/shm/shm_def.h"
#include "brpc/ubshm/common/common.h"

namespace brpc {
namespace ubring {
typedef enum {
    UBR_MGR_UNIT_FREE = 0,
    UBR_MGR_UNIT_USED = 1
} UbrMgrUnitStatus;

// Result of UBRingManager::ClaimTrxCleanupForced.
typedef enum {
    // The slot is not (or no longer) used by the requested generation: the
    // caller must not touch it and must not run the cleanup.
    UBR_CLEANUP_CLAIM_NOT_OURS = 0,
    // A delayed cleanup is published for that generation and was handed back
    // in *out_ctl with a snapshot reference the caller must release.
    UBR_CLEANUP_CLAIM_HAS_CTL = 1,
    // No delayed cleanup existed; the forced claim took the cleanup ownership
    // and later publications for that generation are refused.
    UBR_CLEANUP_CLAIM_OWNED_NULL = 2
} UbrCleanupClaim;

typedef struct TagUbrMgr {
    uint32_t trx_num;
    uint32_t trx_cap;
    // Set under g_ubr_trx_mgr_mtx once UbrMgrFini started: no pool slot may be
    // acquired afterwards, so the shutdown path only has to wait for the
    // connections that were already in the pool when it took the flag.
    bool shutting_down;
    // Callback-driven operations that read the pool without owning a timer or
    // cleanup reference (the faulty-shm event lookup). Guarded by
    // g_ubr_trx_mgr_mtx; UbrMgrFini waits for it to drop to zero before it
    // frees the pool.
    uint32_t active_pool_ops;
    UbrTrx *trx_mgr;
    UbrMgrUnitStatus *trx_mgr_unit_status;
    uint64_t *trx_mgr_unit_id;
    UbrCleanupCtl **trx_mgr_unit_ctl;
} UbrMgr;

typedef struct TagUbrLinkInfo {
    char connect_name[SHM_MAX_NAME_BUFF_LEN];
    char listener_name[SHM_MAX_NAME_BUFF_LEN];
} UbrLinkInfo;

typedef struct TagUbrLinkInfoMgr {
    uint32_t link_num;
    UbrLinkInfo* all_link_info;
    UbrMgrUnitStatus *link_mgr_unit_status;
} UbrLinkInfoMgr;

class UBRingManager {
public:
    ~UBRingManager(){
        UbrMgrFini();
    }

    static RETURN_CODE GetUbrDealMsgMaxCnt(const uint32_t capacity, uint32_t *deal_msg_max_cnt);

    static RETURN_CODE UbrMgrDefault();

    static RETURN_CODE UbrMgrInit();

    // Tear the pool down. Contract: no connection may be in setup or teardown
    // while this runs. The shutdown barrier below only covers the paths that
    // register themselves (pool acquisitions, timer arming, cleanup-control
    // publication, and the faulty-shm callback through BeginPoolAccess);
    // UbrTrxClose / ReleaseUbrTrxFromMgr and the rest of a connection setup
    // keep touching UbrTrx and would race the FREE_PTR at the end. Today this
    // is only reachable from the init-failure path and from tests, but a real
    // graceful-shutdown caller must drain the connections first (or extend the
    // barrier to those paths).
    //
    // This is terminal: the shutdown flag stays set and the pool stays freed,
    // so the manager only becomes usable again through a new UbrMgrInit (which
    // clears the flag and reallocates the pool) -- not by calling Fini twice.
    static void UbrMgrFini();

    static RETURN_CODE AcquireUbrTrxFromMgr(UbrTrx **trx);

    // Release the pool slot of `trx'. `expect_ubr_id' must be snapshotted
    // from trx->ubr_id before the caller started releasing the trx: a
    // release racing a reuse of the same slot is refused.
    static RETURN_CODE ReleaseUbrTrxFromMgr(UbrTrx *trx,
                                            uint64_t expect_ubr_id);

    // Snapshot the delayed cleanup control object of a pool slot and, when
    // there is none, claim the cleanup of `expect_ubr_id' for the force close
    // that will run it. Both the snapshot and the claim happen under the
    // manager lock, i.e. in the same critical section as
    // TryPublishUnitCleanupCtl, so the two are totally ordered: either the
    // publication ran first (UBR_CLEANUP_CLAIM_HAS_CTL, and the existing
    // ctl->state arbitration decides who runs the cleanup) or the claim ran
    // first (UBR_CLEANUP_CLAIM_OWNED_NULL, and every later publication for
    // this generation is refused through trx->cleanup_forced). Splitting the
    // two steps -- a snapshot, then an out-of-lock re-check that no ctl
    // appeared -- leaves a window in which a concurrent SDK-fault callback
    // publishes a new cleanup after an empty snapshot and both paths run the
    // cleanup. `*out_ctl' is only written on UBR_CLEANUP_CLAIM_HAS_CTL, and
    // carries a snapshot reference the caller must ReleaseRef.
    static UbrCleanupClaim ClaimTrxCleanupForced(uint32_t idx,
                                                 uint64_t expect_ubr_id,
                                                 UbrCleanupCtl** out_ctl);

    // Under the manager lock, confirm the pool slot is still used by the
    // given generation (not released or reused meanwhile).
    static bool IsUbrTrxSlotUsed(uint32_t idx, uint64_t expect_ubr_id);

    // Atomically claim the close of the pool slot `idx' for the generation
    // `expect_ubr_id': under the manager lock it validates that the slot is
    // still used by that generation -- so a snapshot taken before a
    // release/reuse cannot claim the next occupant's close -- and then moves
    // the close counter from MAX_CLOSE_COUNT to MAX_CLOSE_COUNT - 1 so no
    // other path can claim the same close. Returns UBRING_REENTRY when the
    // same generation is already closing, UBRING_ERR when the caller must not
    // touch the slot (released, reused, or not fully initialized).
    static RETURN_CODE TryClaimTrxClose(uint32_t idx, uint64_t expect_ubr_id);

    // Atomically (under the manager lock) publish `ctl' on the pool slot and
    // anchor it there, but only while the slot is still used by the given
    // generation and carries no other cleanup control object. Both the
    // trx-side field and the manager anchor are set in the same critical
    // section, so a concurrent force close never observes one without the
    // other. On success the ctl gains the manager anchor reference (released
    // when the anchor is detached or the slot is retired); returns false --
    // leaving the slot and the reference counts untouched -- when the trx was
    // released, reused, a force close already claimed the cleanup, or a
    // cleanup is already anchored.
    static bool TryPublishUnitCleanupCtl(uint32_t idx, uint64_t expect_ubr_id,
                                         UbrCleanupCtl *ctl);

    // Detach `ctl' from the pool slot if it is still anchored there, clearing
    // the trx-side field in the same critical section (and only while it still
    // belongs to `ctl'). Returns true when the detach happened (the manager
    // reference was released).
    static bool DetachUnitCleanupCtl(uint32_t idx, UbrCleanupCtl *ctl);

    // Register / release a callback-driven pool access that does not own a
    // timer or cleanup reference (currently the faulty-shm event lookup).
    // BeginPoolAccess fails once UbrMgrFini started, and UbrMgrFini waits for
    // the accepted accesses to finish before it frees the pool, so the caller
    // may keep using a UbrTrx* it obtained while the access is open. Every
    // successful BeginPoolAccess must be paired with FinishPoolAccess, which
    // is safe to call after UbrMgrFini returned.
    static bool BeginPoolAccess();
    static void FinishPoolAccess();

    // Run `arm' with the manager lock held, and only while the manager is not
    // shutting down: arming a per-trx timer and taking the shutdown flag are
    // then serialized, so once UbrMgrFini set the flag no new timer can appear
    // behind its wait loop. `arm' must not block on a timer callback (it may
    // only start timers); it returns whether everything was armed. Returns
    // false when the manager is shutting down, in which case `arm' never ran.
    static bool ArmTimersExclusive(const std::function<bool()>& arm);

    static void LinkInfoInit(void);
    static void LinkInfoFini(void);
    static void AcquireLinkInfoToMgr(const char* listener_name, UbrTrx *trx);
    static void ReleaseLinkInfoFromMgr(UbrTrx* trx);
    static int32_t UbEventCallback(const char *shm_name);

private:
    UBRingManager() {
    }

    static UbrMgr g_ubr_mgr;
    static UbrLinkInfoMgr g_link_info_mgr;
    static pthread_mutex_t g_ubr_trx_mgr_mtx;
    static pthread_mutex_t g_ubr_listener_mgr_mtx;
    static pthread_mutex_t g_link_info_mgr_mtx;
};
}
}

#endif //BRPC_UB_RING_MANAGER_H