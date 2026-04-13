//
// Created by z00926396 on 2026/4/11.
//

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