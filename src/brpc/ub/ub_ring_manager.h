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

#include <mutex>
#include <cstring>
#include "brpc/ub/ub_ring.h"
#include "brpc/ub/common/common.h"

namespace brpc {
namespace ub {
typedef enum {
    UBR_MGR_UNIT_FREE = 0,
    UBR_MGR_UNIT_USED = 1
} UbrMgrUnitStatus;

typedef struct TagUbrMgr {
    uint32_t trxNum;
    uint32_t trxCap;
    UbrTrx *trxMgr;
    UbrMgrUnitStatus *trxMgrUnitStatus;
} UbrMgr;

typedef struct TagUbrLinkInfo {
    char connectName[SHM_MAX_NAME_BUFF_LEN];
    char listenerName[SHM_MAX_NAME_BUFF_LEN];
} UbrLinkInfo;

typedef struct TagUbrLinkInfoMgr {
    uint32_t linkNum;
    UbrLinkInfo* allLinkInfo;
    UbrMgrUnitStatus *linkMgrUnitStatus;
} UbrLinkInfoMgr;

class UBRingManager {
public:
    ~UBRingManager(){
        UbrMgrFini();
    }

    static RETURN_CODE GetHlcDealMsgMaxCnt(const uint32_t capacity, uint32_t *dealMsgMaxCnt);

    static RETURN_CODE UbrMgrDefault();

    static RETURN_CODE UbrMgrInit();

    static void UbrMgrFini();

    static RETURN_CODE AcquireUbrTrxFromMgr(UbrTrx **trx);

    static RETURN_CODE ReleaseUbrTrxFromMgr(UbrTrx *trx);

    static void LinkInfoInit(void);
    static void LinkInfoFini(void);
    static void AcquireLinkInfoToMgr(const char* listenerName, UbrTrx *trx);
    static void ReleaseLinkInfoFromMgr(UbrTrx* trx);
    static int32_t UbEventCallback(const char *shmName);

private:
    UBRingManager() {
    }

    static UbrMgr g_ubrMgr;
    static UbrLinkInfoMgr g_linkInfoMgr;
    static pthread_mutex_t g_ubrTrxMgrMtx;
    static pthread_mutex_t g_ubrListenerMgrMtx;
    static pthread_mutex_t g_linkInfoMgrMtx;
};
}
}

#endif //BRPC_UB_RING_MANAGER_H