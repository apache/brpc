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

#include "brpc/ubshm/ubr_trx.h"
#include "brpc/ubshm/shm/shm_def.h"
#include "brpc/ubshm/common/common.h"

namespace brpc {
namespace ubring {
typedef enum {
    UBR_MGR_UNIT_FREE = 0,
    UBR_MGR_UNIT_USED = 1
} UbrMgrUnitStatus;

typedef struct TagUbrMgr {
    uint32_t trx_num;
    uint32_t trx_cap;
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

    static void UbrMgrFini();

    static RETURN_CODE AcquireUbrTrxFromMgr(UbrTrx **trx);

    // Release the pool slot of `trx'. `expect_ubr_id' must be snapshotted
    // from trx->ubr_id before the caller started releasing the trx: a
    // release racing a reuse of the same slot is refused.
    static RETURN_CODE ReleaseUbrTrxFromMgr(UbrTrx *trx,
                                            uint64_t expect_ubr_id);

    // Snapshot the cleanup control object of a pool slot. The caller
    // receives a reference and must ReleaseRef it on every exit path.
    static UbrCleanupCtl* SnapshotUnitCleanupCtl(uint32_t idx);

    // Under the manager lock, confirm the pool slot is still used by the
    // given generation (not released or reused meanwhile).
    static bool IsUbrTrxSlotUsed(uint32_t idx, uint64_t expect_ubr_id);

    // Atomically (under the manager lock) anchor `ctl' to the pool slot,
    // but only while the slot is still used by the given generation and
    // carries no other cleanup control object. On success the ctl gains
    // the manager anchor reference (released when the anchor is detached
    // or the slot is retired); returns false -- leaving the slot and the
    // reference counts untouched -- when the trx was released, reused, or
    // a cleanup is already anchored.
    static bool TryPublishUnitCleanupCtl(uint32_t idx, uint64_t expect_ubr_id,
                                         UbrCleanupCtl *ctl);

    // Detach `ctl' from the pool slot if it is still anchored there.
    // Returns true when the detach happened (the manager reference was
    // released).
    static bool DetachUnitCleanupCtl(uint32_t idx, UbrCleanupCtl *ctl);

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