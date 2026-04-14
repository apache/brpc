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
#include "brpc/ub/ub_ring_manager.h"
#include "butil/logging.h"

namespace brpc {
namespace ub {
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

RETURN_CODE UBRingManager::GetHlcDealMsgMaxCnt(const uint32_t capacity, uint32_t *deal_msg_max_cnt) {
    if (UNLIKELY(deal_msg_max_cnt == NULL)) {
        LOG(ERROR) << "Get update factor failed, dealMsgMaxCnt is null.";
        return HLC_ERR;
    }
    if (UNLIKELY(FLAGS_tail_update_after_read == 0)) {
        LOG(ERROR) << "Get update factor failed, factor is 0.";
        return HLC_ERR;
    }
    *deal_msg_max_cnt = capacity / FLAGS_tail_update_after_read;
    return HLC_OK;
}

RETURN_CODE UBRingManager::UbrMgrDefault()
{
    g_ubr_mgr.trx_num = 0;
    g_ubr_mgr.trx_cap = FLAGS_ubr_max_managed_num;
    g_ubr_mgr.trx_mgr_unit_status = NULL;
    g_ubr_mgr.trx_mgr = NULL;
    return HLC_OK;
}

RETURN_CODE UBRingManager::UbrMgrInit() {
    RETURN_CODE rc = UbrMgrDefault();
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Ubr manager set default values failed.";
        return rc;
    }

    size_t trx_mgr_size = g_ubr_mgr.trx_cap * sizeof(UbrTrx);
    g_ubr_mgr.trx_mgr = (UbrTrx *)malloc(trx_mgr_size);
    size_t trx_mgr_status_size = g_ubr_mgr.trx_cap * sizeof(UbrMgrUnitStatus);
    g_ubr_mgr.trx_mgr_unit_status = (UbrMgrUnitStatus *)malloc(trx_mgr_status_size);
    if (UNLIKELY(g_ubr_mgr.trx_mgr == NULL ||
                 g_ubr_mgr.trx_mgr_unit_status == NULL)) {
        LOG(ERROR) << "Ubr manager memory allocation failed.";
        UbrMgrFini();
        return HLC_ERR;
    }

    memset(g_ubr_mgr.trx_mgr, 0, trx_mgr_size);
    memset(g_ubr_mgr.trx_mgr_unit_status, UBR_MGR_UNIT_FREE, trx_mgr_status_size);
    LinkInfoInit();
    return HLC_OK;
    return UBR_NOT_CONNECTED;
}

void UBRingManager::UbrMgrFini() {
    {
        LOCK_GUARD(g_ubr_trx_mgr_mtx);
        FREE_PTR(g_ubr_mgr.trx_mgr);
        FREE_PTR(g_ubr_mgr.trx_mgr_unit_status);
    }
    {
        LOCK_GUARD(g_ubr_listener_mgr_mtx);
    }
    g_ubr_mgr.trx_num = 0;
    g_ubr_mgr.trx_cap = 0;
    LinkInfoFini();
}

RETURN_CODE UBRingManager::AcquireUbrTrxFromMgr(UbrTrx **trx) {
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Acquire trx failed, trx is null.";
        return HLC_ERR;
    }

    if (UNLIKELY(g_ubr_mgr.trx_mgr == NULL)) {
        LOG(ERROR) << "Acquire trx failed, trxMgr is null.";
        return HLC_ERR;
    }

    LOCK_GUARD(g_ubr_trx_mgr_mtx);
    if (g_ubr_mgr.trx_num >= g_ubr_mgr.trx_cap) {
        LOG(ERROR) << "Acquire trx failed, trx number is full.";
        return HLC_ERR;
    }

    for (uint32_t i = 0; i < g_ubr_mgr.trx_cap; ++i) {
        if (g_ubr_mgr.trx_mgr_unit_status[i] == UBR_MGR_UNIT_FREE) {
            memset(&g_ubr_mgr.trx_mgr[i], 0, sizeof(UbrTrx));
            g_ubr_mgr.trx_mgr_unit_status[i] = UBR_MGR_UNIT_USED;
            *trx = &g_ubr_mgr.trx_mgr[i];
            (*trx)->trx_mgr_index = i;
            (*trx)->ubr_id = g_ubr_trx_num;
            (*trx)->close_state = UBR_CLOSE_FIRST;
            (*trx)->close_cnt = MAX_CLOSE_COUNT;
            ++g_ubr_mgr.trx_num;
            ++g_ubr_trx_num;
            return HLC_OK;
        }
    }
    LOG(ERROR) << "Acquire trx failed, no available space.";
    return HLC_ERR;
}

RETURN_CODE UBRingManager::ReleaseUbrTrxFromMgr(UbrTrx *trx) {
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Release trx failed, trx is null.";
        return HLC_ERR;
    }

    trx->local_shm.addr = NULL;
    trx->ubr_tx.local_tx_event_q.addr = NULL;
    trx->ubr_tx.local_data_status_q.addr = NULL;
    trx->ubr_rx.local_rx_event_q.addr = NULL;
    trx->ubr_rx.remote_data_status_q.addr = NULL;
    if (UNLIKELY(g_ubr_mgr.trx_mgr == NULL)) {
        LOG(ERROR) << "Release trx failed, trxMgr is null.";
        return HLC_ERR;
    }

    LOCK_GUARD(g_ubr_trx_mgr_mtx);
    if (g_ubr_mgr.trx_num == 0) {
        LOG(ERROR) << "Release trx failed, trx number is 0.";
        return HLC_ERR;
    }

    uint32_t idx = trx->trx_mgr_index;
    if (g_ubr_mgr.trx_mgr_unit_status[idx] == UBR_MGR_UNIT_FREE) {
        LOG(ERROR) << "Release trx failed, trx is not in manager.";
        return HLC_ERR;
    }
    g_ubr_mgr.trx_mgr_unit_status[idx] = UBR_MGR_UNIT_FREE;
    --g_ubr_mgr.trx_num;
    return HLC_OK;
}

void UBRingManager::LinkInfoInit(void) {

    size_t link_info_mgr_size = FLAGS_ubr_max_managed_num * sizeof(UbrLinkInfo);
    g_link_info_mgr.all_link_info = (UbrLinkInfo*) malloc(link_info_mgr_size);
    if (g_link_info_mgr.all_link_info == NULL) {
        LOG(ERROR) << "allLinkInfo is NULL";
        LinkInfoFini();
        return;
    }

    g_link_info_mgr.link_mgr_unit_status = (UbrMgrUnitStatus*) malloc(link_info_mgr_size);
    if (g_link_info_mgr.link_mgr_unit_status == NULL) {
        LinkInfoFini();
        return;
    }

    memset(g_link_info_mgr.all_link_info, 0, link_info_mgr_size);
    memset(g_link_info_mgr.link_mgr_unit_status, 0, link_info_mgr_size);
}

void UBRingManager::LinkInfoFini(void) {
    if (g_link_info_mgr.link_mgr_unit_status == NULL || g_link_info_mgr.all_link_info == NULL) {
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
    if (listener_name == NULL || trx == NULL) {
        LOG(ERROR) << "LinkInfo acquire fail.";
        return;
    }

    if (g_link_info_mgr.link_mgr_unit_status == NULL || g_link_info_mgr.all_link_info == NULL) {
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
    if (trx == NULL || g_link_info_mgr.link_mgr_unit_status == NULL) {
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
    if (UNLIKELY(shm_name == NULL)) {
        LOG(ERROR) << "Ub event callback failed, shm name is null.";
        return HLC_ERR;
    }
    if (UNLIKELY(g_ubr_mgr.trx_mgr == NULL)) {
        LOG(ERROR) << "Ub event callback failed, trx mgr is null.";
        return HLC_ERR;
    }
    LOG(DEBUG) << "Ub event callback is processing. shm_name=" << shm_name;

    for (uint32_t i = 0; i < g_ubr_mgr.trx_cap; ++i) {
        if (g_ubr_mgr.trx_mgr_unit_status[i] == UBR_MGR_UNIT_FREE) {
            continue;
        }

        if (strcmp(g_ubr_mgr.trx_mgr[i].local_shm.name, shm_name) == 0 ||
            strcmp(g_ubr_mgr.trx_mgr[i].remote_shm.name, shm_name) == 0) {
            ++g_ub_event_cnt;
            int fd = (int)g_ubr_mgr.trx_mgr[i].local_shm.fd;
            LOG(INFO) << "Ub event callback, the fd of the faulty link is " << fd;
            return UBRing::UbrPassiveClearTrx(&g_ubr_mgr.trx_mgr[i], fd, UBR_UB_EVENT);
        }
    }
    return HLC_ERR;
}
}
}