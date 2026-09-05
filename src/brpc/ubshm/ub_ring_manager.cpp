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

#include <gflags/gflags.h>
#include "brpc/ubshm/ub_ring.h"
#include "brpc/ubshm/ub_ring_manager.h"
#include "butil/logging.h"

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
    if (UNLIKELY(deal_msg_max_cnt == nullptr)) {
        LOG(ERROR) << "Get update factor failed, deal_msg_max_cnt is null.";
        return UBRING_ERR;
    }
    if (UNLIKELY(FLAGS_tail_update_after_read == 0)) {
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
    g_ubr_mgr.trx_mgr_unit_status = nullptr;
    g_ubr_mgr.trx_mgr = nullptr;
    g_ubr_mgr.trx_mgr_unit_id = nullptr;
    g_ubr_mgr.trx_mgr_unit_ctl = nullptr;
    return UBRING_OK;
}

RETURN_CODE UBRingManager::UbrMgrInit() {
    RETURN_CODE rc = UbrMgrDefault();
    if (UNLIKELY(rc != UBRING_OK)) {
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
    if (UNLIKELY(g_ubr_mgr.trx_mgr == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_status == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_id == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_ctl == nullptr)) {
        LOG(ERROR) << "Ubr manager memory allocation failed.";
        UbrMgrFini();
        return UBRING_ERR;
    }

    memset(g_ubr_mgr.trx_mgr, 0, trx_mgr_size);
    memset(g_ubr_mgr.trx_mgr_unit_status, UBR_MGR_UNIT_FREE, trx_mgr_status_size);
    memset(g_ubr_mgr.trx_mgr_unit_id, 0, trx_mgr_id_size);
    memset(g_ubr_mgr.trx_mgr_unit_ctl, 0, trx_mgr_ctl_size);
    LinkInfoInit();
    return UBRING_OK;
}

void UBRingManager::UbrMgrFini() {
    // Cancel the pending delayed cleanups and wait for the in-flight ones
    // (each holds one extra reference) to finish, before the pool memory
    // they touch is freed. A ctl whose timer is still starting can only be
    // cancelled in a later round, hence the retry-to-stability loop.
    bool busy = true;
    while (busy) {
        busy = false;
        {
            LOCK_GUARD(g_ubr_trx_mgr_mtx);
            if (g_ubr_mgr.trx_mgr_unit_ctl != nullptr) {
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
        LOCK_GUARD(g_ubr_trx_mgr_mtx);
        if (g_ubr_mgr.trx_mgr_unit_ctl != nullptr) {
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
        LOCK_GUARD(g_ubr_listener_mgr_mtx);
    }
    g_ubr_mgr.trx_num = 0;
    g_ubr_mgr.trx_cap = 0;
    LinkInfoFini();
}

RETURN_CODE UBRingManager::AcquireUbrTrxFromMgr(UbrTrx **trx) {
    if (UNLIKELY(trx == nullptr)) {
        LOG(ERROR) << "Acquire trx failed, trx is null.";
        return UBRING_ERR;
    }

    if (UNLIKELY(g_ubr_mgr.trx_mgr == nullptr)) {
        LOG(ERROR) << "Acquire trx failed, trx_mgr is null.";
        return UBRING_ERR;
    }

    LOCK_GUARD(g_ubr_trx_mgr_mtx);
    if (g_ubr_mgr.trx_num >= g_ubr_mgr.trx_cap) {
        LOG(ERROR) << "Acquire trx failed, trx number is full.";
        return UBRING_ERR;
    }

    for (uint32_t i = 0; i < g_ubr_mgr.trx_cap; ++i) {
        if (g_ubr_mgr.trx_mgr_unit_status[i] == UBR_MGR_UNIT_FREE) {
            memset(&g_ubr_mgr.trx_mgr[i], 0, sizeof(UbrTrx));
            // The explicit re-initialization after memset is deliberate: it
            // documents the per-acquisition invariants of these fields.
            g_ubr_mgr.trx_mgr[i].close_timer = nullptr;
            g_ubr_mgr.trx_mgr[i].hb_timer = nullptr;
            g_ubr_mgr.trx_mgr[i].cleanup_ctl = nullptr;
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
    if (UNLIKELY(trx == nullptr)) {
        LOG(ERROR) << "Release trx failed, trx is null.";
        return UBRING_ERR;
    }
    if (UNLIKELY(g_ubr_mgr.trx_mgr == nullptr)) {
        LOG(ERROR) << "Release trx failed, trx_mgr is null.";
        return UBRING_ERR;
    }

    LOCK_GUARD(g_ubr_trx_mgr_mtx);
    uint32_t idx = trx->trx_mgr_index;
    if (g_ubr_mgr.trx_mgr_unit_status[idx] == UBR_MGR_UNIT_FREE) {
        LOG(INFO) << "Release trx already freed, name=" << trx->local_shm.name;
        return UBRING_OK;
    }

    if (UNLIKELY(g_ubr_mgr.trx_mgr_unit_id[idx] != expect_ubr_id)) {
        // The slot was released and acquired again meanwhile; the stale
        // caller must not touch the new occupant.
        LOG(WARNING) << "Release stale trx refused, name=" << trx->local_shm.name;
        return UBRING_OK;
    }

    if (g_ubr_mgr.trx_num == 0) {
        LOG(ERROR) << "Release trx failed, trx number is 0.";
        return UBRING_ERR;
    }

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

UbrCleanupCtl* UBRingManager::SnapshotUnitCleanupCtl(uint32_t idx) {
    LOCK_GUARD(g_ubr_trx_mgr_mtx);
    if (UNLIKELY(g_ubr_mgr.trx_mgr_unit_ctl == nullptr || idx >= g_ubr_mgr.trx_cap)) {
        return nullptr;
    }
    UbrCleanupCtl* ctl = g_ubr_mgr.trx_mgr_unit_ctl[idx];
    if (ctl != nullptr) {
        ctl->ref.fetch_add(1);               // snapshot reference
    }
    return ctl;
}

bool UBRingManager::IsUbrTrxSlotUsed(uint32_t idx, uint64_t expect_ubr_id) {
    LOCK_GUARD(g_ubr_trx_mgr_mtx);
    if (UNLIKELY(g_ubr_mgr.trx_mgr_unit_id == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_status == nullptr ||
                 idx >= g_ubr_mgr.trx_cap)) {
        return false;
    }
    return g_ubr_mgr.trx_mgr_unit_status[idx] == UBR_MGR_UNIT_USED &&
           g_ubr_mgr.trx_mgr_unit_id[idx] == expect_ubr_id;
}

bool UBRingManager::TryPublishUnitCleanupCtl(uint32_t idx,
                                             uint64_t expect_ubr_id,
                                             UbrCleanupCtl *ctl) {
    LOCK_GUARD(g_ubr_trx_mgr_mtx);
    if (UNLIKELY(g_ubr_mgr.trx_mgr_unit_ctl == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_status == nullptr ||
                 g_ubr_mgr.trx_mgr_unit_id == nullptr ||
                 idx >= g_ubr_mgr.trx_cap ||
                 g_ubr_mgr.trx_mgr_unit_status[idx] != UBR_MGR_UNIT_USED ||
                 g_ubr_mgr.trx_mgr_unit_id[idx] != expect_ubr_id ||
                 g_ubr_mgr.trx_mgr_unit_ctl[idx] != nullptr)) {
        return false;                        // released / reused / already anchored
    }
    ctl->ref.fetch_add(1);                   // manager anchor reference
    g_ubr_mgr.trx_mgr_unit_ctl[idx] = ctl;
    return true;
}

bool UBRingManager::DetachUnitCleanupCtl(uint32_t idx, UbrCleanupCtl *ctl) {
    LOCK_GUARD(g_ubr_trx_mgr_mtx);
    if (UNLIKELY(g_ubr_mgr.trx_mgr_unit_ctl == nullptr ||
                 idx >= g_ubr_mgr.trx_cap ||
                 g_ubr_mgr.trx_mgr_unit_ctl[idx] != ctl)) {
        return false;
    }
    g_ubr_mgr.trx_mgr_unit_ctl[idx] = nullptr;
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
        LOCK_GUARD(g_link_info_mgr_mtx);
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
    if (UNLIKELY(shm_name == nullptr)) {
        LOG(ERROR) << "Ub event callback failed, shm name is null.";
        return UBRING_ERR;
    }
    if (UNLIKELY(g_ubr_mgr.trx_mgr == nullptr)) {
        LOG(ERROR) << "Ub event callback failed, trx mgr is null.";
        return UBRING_ERR;
    }
    LOG(INFO) << "Ub event callback is processing. shm_name=" << shm_name;

    for (uint32_t i = 0; i < g_ubr_mgr.trx_cap; ++i) {
        if (g_ubr_mgr.trx_mgr_unit_status[i] == UBR_MGR_UNIT_FREE) {
            continue;
        }

        if (strcmp(g_ubr_mgr.trx_mgr[i].local_shm.name, shm_name) == 0 ||   // the failed link is this trx's local shm
            strcmp(g_ubr_mgr.trx_mgr[i].remote_shm.name, shm_name) == 0) {  // the failed link is this trx's remote shm
            ++g_ub_event_cnt;
            int fd = (int)g_ubr_mgr.trx_mgr[i].local_shm.fd;
            LOG(WARNING) << "Ub event callback, the fd of the faulty link is " << fd;
            return UBRing::UbrPassiveClearTrx(&g_ubr_mgr.trx_mgr[i]);
        }
    }
    return UBRING_ERR;
}
}
}
