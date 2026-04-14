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

UbrMgr UBRingManager::g_ubrMgr;
UbrLinkInfoMgr UBRingManager::g_linkInfoMgr;
pthread_mutex_t UBRingManager::g_ubrTrxMgrMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t UBRingManager::g_ubrListenerMgrMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t UBRingManager::g_linkInfoMgrMtx = PTHREAD_MUTEX_INITIALIZER;

uint64_t g_ubrTrxNum = 0;
uint64_t g_ubEventCnt = 0;
uint64_t g_ubrListenerNum = 0;

RETURN_CODE UBRingManager::GetHlcDealMsgMaxCnt(const uint32_t capacity, uint32_t *dealMsgMaxCnt) {
    if (UNLIKELY(dealMsgMaxCnt == NULL)) {
        LOG(ERROR) << "Get update factor failed, dealMsgMaxCnt is null.";
        return HLC_ERR;
    }
    if (UNLIKELY(FLAGS_tail_update_after_read == 0)) {
        LOG(ERROR) << "Get update factor failed, factor is 0.";
        return HLC_ERR;
    }
    *dealMsgMaxCnt = capacity / FLAGS_tail_update_after_read;
    return HLC_OK;
}

RETURN_CODE UBRingManager::UbrMgrDefault()
{
    g_ubrMgr.trxNum = 0;
    g_ubrMgr.trxCap = FLAGS_ubr_max_managed_num;
    g_ubrMgr.trxMgrUnitStatus = NULL;
    g_ubrMgr.trxMgr = NULL;
    return HLC_OK;
}

RETURN_CODE UBRingManager::UbrMgrInit() {
    RETURN_CODE rc = UbrMgrDefault();
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Ubr manager set default values failed.";
        return rc;
    }

    size_t trxMgrSize = g_ubrMgr.trxCap * sizeof(UbrTrx);
    g_ubrMgr.trxMgr = (UbrTrx *)malloc(trxMgrSize);
    size_t trxMgrStatusSize = g_ubrMgr.trxCap * sizeof(UbrMgrUnitStatus);
    g_ubrMgr.trxMgrUnitStatus = (UbrMgrUnitStatus *)malloc(trxMgrStatusSize);
    if (UNLIKELY(g_ubrMgr.trxMgr == NULL ||
                 g_ubrMgr.trxMgrUnitStatus == NULL)) {
        LOG(ERROR) << "Ubr manager memory allocation failed.";
        UbrMgrFini();
        return HLC_ERR;
    }

    memset(g_ubrMgr.trxMgr, 0, trxMgrSize);
    memset(g_ubrMgr.trxMgrUnitStatus, UBR_MGR_UNIT_FREE, trxMgrStatusSize);
    LinkInfoInit();
    return HLC_OK;
    return UBR_NOT_CONNECTED;
}

void UBRingManager::UbrMgrFini() {
    {
        LOCK_GUARD(g_ubrTrxMgrMtx);
        FREE_PTR(g_ubrMgr.trxMgr);
        FREE_PTR(g_ubrMgr.trxMgrUnitStatus);
    }
    {
        LOCK_GUARD(g_ubrListenerMgrMtx);
    }
    g_ubrMgr.trxNum = 0;
    g_ubrMgr.trxCap = 0;
    LinkInfoFini();
}

RETURN_CODE UBRingManager::AcquireUbrTrxFromMgr(UbrTrx **trx) {
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Acquire trx failed, trx is null.";
        return HLC_ERR;
    }

    if (UNLIKELY(g_ubrMgr.trxMgr == NULL)) {
        LOG(ERROR) << "Acquire trx failed, trxMgr is null.";
        return HLC_ERR;
    }

    LOCK_GUARD(g_ubrTrxMgrMtx);
    if (g_ubrMgr.trxNum >= g_ubrMgr.trxCap) {
        LOG(ERROR) << "Acquire trx failed, trx number is full.";
        return HLC_ERR;
    }

    for (uint32_t i = 0; i < g_ubrMgr.trxCap; ++i) {
        if (g_ubrMgr.trxMgrUnitStatus[i] == UBR_MGR_UNIT_FREE) {
            memset(&g_ubrMgr.trxMgr[i], 0, sizeof(UbrTrx));
            g_ubrMgr.trxMgrUnitStatus[i] = UBR_MGR_UNIT_USED;
            *trx = &g_ubrMgr.trxMgr[i];
            (*trx)->trxMgrIndex = i;
            (*trx)->ubrId = g_ubrTrxNum;
            (*trx)->closeState = UBR_CLOSE_FIRST;
            (*trx)->closeCnt = MAX_CLOSE_COUNT;
            ++g_ubrMgr.trxNum;
            ++g_ubrTrxNum;
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

    trx->localShm.addr = NULL;
    trx->ubrTx.localTxEventQ.addr = NULL;
    trx->ubrTx.localDataStatusQ.addr = NULL;
    trx->ubrRx.localRxEventQ.addr = NULL;
    trx->ubrRx.remoteDataStatusQ.addr = NULL;
    if (UNLIKELY(g_ubrMgr.trxMgr == NULL)) {
        LOG(ERROR) << "Release trx failed, trxMgr is null.";
        return HLC_ERR;
    }

    LOCK_GUARD(g_ubrTrxMgrMtx);
    if (g_ubrMgr.trxNum == 0) {
        LOG(ERROR) << "Release trx failed, trx number is 0.";
        return HLC_ERR;
    }

    uint32_t idx = trx->trxMgrIndex;
    if (g_ubrMgr.trxMgrUnitStatus[idx] == UBR_MGR_UNIT_FREE) {
        LOG(ERROR) << "Release trx failed, trx is not in manager.";
        return HLC_ERR;
    }
    g_ubrMgr.trxMgrUnitStatus[idx] = UBR_MGR_UNIT_FREE;
    --g_ubrMgr.trxNum;
    return HLC_OK;
}

void UBRingManager::LinkInfoInit(void) {

    size_t linkInfoMgrSize = FLAGS_ubr_max_managed_num * sizeof(UbrLinkInfo);
    g_linkInfoMgr.allLinkInfo = (UbrLinkInfo*) malloc(linkInfoMgrSize);
    if (g_linkInfoMgr.allLinkInfo == NULL) {
        LOG(ERROR) << "allLinkInfo is NULL";
        LinkInfoFini();
        return;
    }

    g_linkInfoMgr.linkMgrUnitStatus = (UbrMgrUnitStatus*) malloc(linkInfoMgrSize);
    if (g_linkInfoMgr.linkMgrUnitStatus == NULL) {
        LinkInfoFini();
        return;
    }

    memset(g_linkInfoMgr.allLinkInfo, 0, linkInfoMgrSize);
    memset(g_linkInfoMgr.linkMgrUnitStatus, 0, linkInfoMgrSize);
}

void UBRingManager::LinkInfoFini(void) {
    if (g_linkInfoMgr.linkMgrUnitStatus == NULL || g_linkInfoMgr.allLinkInfo == NULL) {
        LOG(ERROR) << "LinkInfo is NULL";
        return;
    }
    {
        LOCK_GUARD(g_linkInfoMgrMtx);
        FREE_PTR(g_linkInfoMgr.allLinkInfo);
        FREE_PTR(g_linkInfoMgr.linkMgrUnitStatus);
    }

    g_linkInfoMgr.linkNum = 0;
}

void UBRingManager::AcquireLinkInfoToMgr(const char *listenerName, UbrTrx *trx) {
    if (listenerName == NULL || trx == NULL) {
        LOG(ERROR) << "LinkInfo acquire fail.";
        return;
    }

    if (g_linkInfoMgr.linkMgrUnitStatus == NULL || g_linkInfoMgr.allLinkInfo == NULL) {
        LOG(ERROR) << "LinkInfo is NULL.";
        return;
    }
    uint32_t ubrIndex = trx->trxMgrIndex;
    char* connectName = trx->localShm.name;
    if (g_linkInfoMgr.linkMgrUnitStatus[ubrIndex] == UBR_MGR_UNIT_FREE) {
        strncpy(g_linkInfoMgr.allLinkInfo[ubrIndex].connectName, 
                      connectName, SHM_MAX_NAME_BUFF_LEN);
        strncpy(g_linkInfoMgr.allLinkInfo[ubrIndex].listenerName, 
                      listenerName, SHM_MAX_NAME_BUFF_LEN);
        g_linkInfoMgr.linkMgrUnitStatus[ubrIndex] = UBR_MGR_UNIT_USED;
        g_linkInfoMgr.linkNum++;
    }
}

void UBRingManager::ReleaseLinkInfoFromMgr(UbrTrx *trx) {
    if (trx == NULL || g_linkInfoMgr.linkMgrUnitStatus == NULL) {
        LOG(ERROR) << "LinkInfo release fail.";
        return;
    }

    if (g_linkInfoMgr.linkMgrUnitStatus[trx->trxMgrIndex] == UBR_MGR_UNIT_FREE) {
        LOG(ERROR) << "Release linkInfo failed, trx is not in manager.";
        return;
    }
    g_linkInfoMgr.linkMgrUnitStatus[trx->trxMgrIndex] = UBR_MGR_UNIT_FREE;
    g_linkInfoMgr.linkNum--;
}

int32_t UBRingManager::UbEventCallback(const char *shmName)
{
    if (UNLIKELY(shmName == NULL)) {
        LOG(ERROR) << "Ub event callback failed, shm name is null.";
        return HLC_ERR;
    }
    if (UNLIKELY(g_ubrMgr.trxMgr == NULL)) {
        LOG(ERROR) << "Ub event callback failed, trx mgr is null.";
        return HLC_ERR;
    }
    LOG(DEBUG) << "Ub event callback is processing. shm_name=" << shmName;

    for (uint32_t i = 0; i < g_ubrMgr.trxCap; ++i) {
        if (g_ubrMgr.trxMgrUnitStatus[i] == UBR_MGR_UNIT_FREE) {
            continue;
        }

        if (strcmp(g_ubrMgr.trxMgr[i].localShm.name, shmName) == 0 ||   // 故障链路为该trx的本端shm
            strcmp(g_ubrMgr.trxMgr[i].remoteShm.name, shmName) == 0) {  // 故障链路为该trx的对端shm
            ++g_ubEventCnt;
            int fd = (int)g_ubrMgr.trxMgr[i].localShm.fd;
            LOG(INFO) << "Ub event callback, the fd of the faulty link is " << fd;
            return UBRing::UbrPassiveClearTrx(&g_ubrMgr.trxMgr[i], fd, UBR_UB_EVENT);
        }
    }
    return HLC_ERR;
}
}
}