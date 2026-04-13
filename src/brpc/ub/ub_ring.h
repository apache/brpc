#ifndef BRPC_UB_RING_H
#define BRPC_UB_RING_H

#include <sys/stat.h>
#include <sys/file.h>
#include "butil/macros.h"
#include "brpc/ub/ubr_trx.h"
#include "brpc/ub/ub_ring_manager.h"
#include "brpc/ub/shm/shm_mgr.h"
#include "brpc/ub/timer/timer_mgr.h"

namespace brpc {
namespace ub {
DECLARE_int32(ub_flying_io_timeout);
extern uint32_t g_sleepTime[UBR_TASK_STEP_NUM];

class UBRing {
public:
    UBRing();
    ~UBRing();
    DISALLOW_COPY_AND_ASSIGN(UBRing);

    RETURN_CODE UbrTrxMapShm(SHM *localShm, SHM *remoteShm);

    RETURN_CODE UbrTrxClose();

    RETURN_CODE UbrAddCloseTimer();

    RETURN_CODE UbrAddTimer();

    static void *UbrTrxCloseCallback(void *args);

    RETURN_CODE UbrAddHBTimer();

    static void *UbrTrxHBCallback(void *args);

    static RETURN_CODE UbrPassiveClearTrx(UbrTrx *trx, int fd, PASSIVE_DISC_TYPE type);

    static RETURN_CODE UbrAddAsynClearTimer(UbrTrx *trx);

    static void *UbrAsynClearCallback(void *args);

    int UbrTrxSend(const void *buf, uint32_t bufLen);

    int UbrTrxRecv(void *buf, uint32_t bufLen);

    int UbrTrxRecvBlockMode(uint8_t *dest, uint32_t bufLen);

    ssize_t UbrTrxWritev(const struct iovec *iov, int iovcnt);
    ssize_t UbrTrxReadv(const struct iovec *iov, int iovcnt);
    ssize_t UbrTrxReadvBlockMode(const struct iovec *iov, int iovcnt);

    RETURN_CODE IsUbrTrxReadable(uint32_t epEvent);

    RETURN_CODE IsUbrTrxWriteable(uint32_t epEvent);

    RETURN_CODE UbrSetTimeout(UbrTaskStep taskType, int timeout);

    static RETURN_CODE UbrTrxFreeShm(UbrTrx *trx);

    void PrewriteUbrTx(UbrTx *tx);
    void PrewriteUbrRx(UbrRx *rx);

    static inline void UbrSetSleepTask(UbrTaskStep taskType)
    {
        if (taskType >= UBR_TASK_STEP_NUM || taskType < 0) {
            return;
        }
        uint32_t type = (uint32_t)taskType;
        sleep(g_sleepTime[type]);
        return;
    }

    static inline RETURN_CODE CheckTrxConnectParam(const char *listenerName, const char *localName)
    {
        if (UNLIKELY(listenerName == NULL)) {
            LOG(ERROR) << "The request listener name is null.";
            return HLC_ERR;
        }
        if (UNLIKELY(localName == NULL)) {
            LOG(ERROR) << "The request trx shared memory name is null.";
            return HLC_ERR;
        }
        return HLC_OK;
    }

    int UbrAllocateServerShm(SHM* remote_trx_shm, SHM* local_trx_shm);

    int UbrMapRemoteShm(SHM *local_trx_shm, const char *local_name);

    int UbrAllocateLocalShm(SHM *local_trx_shm, const char *shm_name);

    RETURN_CODE UbrMapRemoteShmAddTimer(SHM *localTrxShm, const char *localName);

    static inline RETURN_CODE CheckTrxSendPreCheck(UbrTrx *trx)
    {
        if (UNLIKELY(trx->ubrTx.trxState != UBR_STATE_CONNECTED)) {
            LOG(ERROR) << "Trx send failed, trx is not connected state.";
            return HLC_ERR;
        }

        return HLC_OK;
    }
    static RETURN_CODE CheckTrxRecvParam(UbrTrx *trx, const void *buf, uint32_t bufLen)
    {
        if (UNLIKELY(trx == NULL)) {
            LOG(ERROR) << "Trx recv failed, trx is null.";
            return HLC_ERR;
        }

        if (UNLIKELY((UbrEventQMsg *)trx->ubrRx.localRxEventQ.addr == NULL)) {
            LOG(ERROR) << "Trx send failed, localTxEventQ addr is NULL.";
            return HLC_ERR;
        }

        if (UNLIKELY(trx->ubrRx.trxState != UBR_STATE_CONNECTED)) {
            LOG(ERROR) << "Trx recv failed, trx is not connected statep=" << trx->ubrRx.trxState;
            return UBR_NOT_CONNECTED;
        }
        if (UNLIKELY(buf == NULL)) {
            LOG(ERROR) << "Trx recv failed, buf is null.";
            return HLC_ERR;
        }
        if (UNLIKELY(bufLen == 0)) {
            LOG(ERROR) << "Trx recv failed, bufLen is 0.";
            return HLC_ERR;
        }
        return HLC_OK;
    }

    static inline RETURN_CODE CheckTrxRecvPreCheck(UbrTrx *trx)
    {
        if (UNLIKELY(trx->ubrRx.trxState != UBR_STATE_CONNECTED)) {
            LOG(ERROR) << "Trx recv failed, trx is not connected state.";
            return HLC_ERR;
        }
        return HLC_OK;
    }

    static inline void UpdateDataQTail(UbrTrx *trx)
    {
        ((UbrDataStatusQMsg *)trx->ubrRx.remoteDataStatusQ.addr)->tail = trx->ubrRx.readPos;
    }

    static RETURN_CODE UbrTrxCallbackCheck(UbrTrx *trx)
    {
        if (trx == NULL) {
            LOG(ERROR) << "Trx close callback failed, trx is null.";
            return HLC_ERR;
        }
        if (UNLIKELY(trx->localShm.addr == NULL)) {
            LOG(ERROR) << "Trx close failed, localShm addr is NULL.";
            return HLC_ERR;
        }
        if (UNLIKELY(trx->ubrRx.localRxEventQ.addr == NULL)) {
            LOG(ERROR) << "Trx close failed, localRxEventQ addr is NULL.";
            return HLC_ERR;
        }
        if (UNLIKELY(trx->ubrTx.localTxEventQ.addr == NULL)) {
            LOG(ERROR) << "Trx close failed, localTxEventQ addr is NULL.";
            return HLC_ERR;
        }
        return HLC_OK;
    }

private:
    RETURN_CODE UbrTrxMapLocalShm(SHM *localShm);
    RETURN_CODE UbrTrxMapRemoteShm(SHM *remoteShm);
    RETURN_CODE ApplyAndMapLocalShm(SHM *localTrxShm, const char *localName);
    RETURN_CODE ApplyAndMapRemoteShm(SHM *remoteTrxShm);
    static RETURN_CODE UbrTrxCloseCheck(UbrTrx *trx);
    void ReleaseFileLock(int lockFd);
    ssize_t StartReadv(UbrTrx *trx, const struct iovec *iov, int iovcnt, size_t remainBufLen);
    void PreWriteAddr(uint8_t *addr, size_t len);
    RETURN_CODE WritevHasEnoughSpace(size_t bufLen);
    RETURN_CODE UbrServerTrxInit(SHM *localShm, SHM *remoteShm);
    static RETURN_CODE UbrClearResourceCheck(UbrTrx *trx, uint64_t startTime, UbrCloseType closeType);
    static RETURN_CODE ClearTrxResource(UbrTrx *trx, uint64_t startTime, UbrCloseType closeType, int op=0);

    UbrTrx* _trx{nullptr};
};
}
}

#endif //BRPC_UB_RING_H