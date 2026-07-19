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

#ifndef BRPC_UB_RING_H
#define BRPC_UB_RING_H

#include <sys/stat.h>
#include <sys/file.h>
#include "butil/macros.h"
#include "butil/reader_writer.h"
#include "brpc/ubshm/ubr_trx.h"
#include "brpc/ubshm/shm/shm_mgr.h"
#include "brpc/ubshm/timer/timer_mgr.h"

namespace brpc {
namespace ubring {
DECLARE_int32(ub_flying_io_timeout);
extern uint32_t g_sleep_time[UBR_TASK_STEP_NUM];

class UBRing : public butil::IReader {
public:
    UBRing();
    ~UBRing();
    DISALLOW_COPY_AND_ASSIGN(UBRing);
    
    ssize_t ReadV(const iovec* iov, int iovcnt) override {
        return UbrTrxReadv(iov, iovcnt);
    }

    RETURN_CODE UbrTrxMapShm(SHM *local_shm, SHM *remote_shm);

    RETURN_CODE UbrTrxClose();

    RETURN_CODE UbrAddCloseTimer();

    RETURN_CODE UbrAddTimer();

    static void *UbrTrxCloseCallback(void *args);

    RETURN_CODE UbrAddHBTimer();

    static void *UbrTrxHBCallback(void *args);

    static RETURN_CODE UbrPassiveClearTrx(UbrTrx *trx, int fd, PASSIVE_DISC_TYPE type);

    static RETURN_CODE UbrAddAsynClearTimer(UbrTrx *trx);

    static void *UbrAsynClearCallback(void *args);

    int UbrTrxSend(const void *buf, uint32_t buf_len);

    int UbrTrxRecv(void *buf, uint32_t buf_len);

    int UbrTrxRecvBlockMode(uint8_t *dest, uint32_t buf_len);

    ssize_t UbrTrxWritev(const struct iovec *iov, int iovcnt);
    ssize_t UbrTrxReadv(const struct iovec *iov, int iovcnt);
    ssize_t UbrTrxReadvBlockMode(const struct iovec *iov, int iovcnt);

    RETURN_CODE IsUbrTrxReadable(uint32_t ep_event);

    RETURN_CODE IsUbrTrxWriteable(uint32_t ep_event);

    RETURN_CODE UbrSetTimeout(UbrTaskStep task_type, int timeout);

    static RETURN_CODE UbrTrxFreeShm(UbrTrx *trx);

    RETURN_CODE UbrUnlinkLocalShm();

    void PrewriteUbrTx(UbrTx *tx);
    void PrewriteUbrRx(UbrRx *rx);

    static inline void UbrSetSleepTask(UbrTaskStep task_type)
    {
        if (task_type >= UBR_TASK_STEP_NUM || task_type < 0) {
            return;
        }
        uint32_t type = (uint32_t)task_type;
        sleep(g_sleep_time[type]);
        return;
    }

    static inline RETURN_CODE CheckTrxConnectParam(const char *listener_name, const char *local_name)
    {
        if (UNLIKELY(listener_name == NULL)) {
            LOG(ERROR) << "The request listener name is null.";
            return UBRING_ERR;
        }
        if (UNLIKELY(local_name == NULL)) {
            LOG(ERROR) << "The request trx shared memory name is null.";
            return UBRING_ERR;
        }
        return UBRING_OK;
    }

    int UbrAllocateServerShm(SHM* remote_trx_shm, SHM* local_trx_shm);

    int UbrMapRemoteShm(SHM *local_trx_shm, const char *local_name);

    int UbrAllocateLocalShm(SHM *local_trx_shm, const char *shm_name);

    RETURN_CODE UbrMapRemoteShmAddTimer(SHM *local_trx_shm, const char *local_name);

    static inline RETURN_CODE CheckTrxSendPreCheck(UbrTrx *trx)
    {
        if (UNLIKELY(trx->ubr_tx.trx_state != UBR_STATE_CONNECTED)) {
            LOG(ERROR) << "Trx send failed, trx is not connected state.";
            return UBRING_ERR;
        }

        return UBRING_OK;
    }
    static RETURN_CODE CheckTrxRecvParam(UbrTrx *trx, const void *buf, uint32_t buf_len)
    {
        if (UNLIKELY(trx == NULL)) {
            LOG(ERROR) << "Trx recv failed, trx is null.";
            return UBRING_ERR;
        }

        if (UNLIKELY((UbrEventQMsg *)trx->ubr_rx.local_rx_event_q.addr == NULL)) {
            LOG(ERROR) << "Trx send failed, local_tx_event_q addr is NULL.";
            return UBRING_ERR;
        }

        if (UNLIKELY(trx->ubr_rx.trx_state != UBR_STATE_CONNECTED)) {
            LOG(ERROR) << "Trx recv failed, trx is not connected statep=" << trx->ubr_rx.trx_state;
            return UBR_NOT_CONNECTED;
        }
        if (UNLIKELY(buf == NULL)) {
            LOG(ERROR) << "Trx recv failed, buf is null.";
            return UBRING_ERR;
        }
        if (UNLIKELY(buf_len == 0)) {
            LOG(ERROR) << "Trx recv failed, buf_len is 0.";
            return UBRING_ERR;
        }
        return UBRING_OK;
    }

    static inline RETURN_CODE CheckTrxRecvPreCheck(UbrTrx *trx)
    {
        if (UNLIKELY(trx->ubr_rx.trx_state != UBR_STATE_CONNECTED)) {
            LOG(ERROR) << "Trx recv failed, trx is not connected state.";
            return UBRING_ERR;
        }
        return UBRING_OK;
    }

    static inline void UpdateDataQTail(UbrTrx *trx)
    {
        ((UbrDataStatusQMsg *)trx->ubr_rx.remote_data_status_q.addr)->tail = trx->ubr_rx.read_pos;
    }

    static RETURN_CODE UbrTrxCallbackCheck(UbrTrx *trx)
    {
        if (trx == NULL) {
            LOG(ERROR) << "Trx close callback failed, trx is null.";
            return UBRING_ERR;
        }
        if (UNLIKELY(trx->local_shm.addr == NULL)) {
            LOG(ERROR) << "Trx close failed, local_shm addr is NULL.";
            return UBRING_ERR;
        }
        if (UNLIKELY(trx->ubr_rx.local_rx_event_q.addr == NULL)) {
            LOG(ERROR) << "Trx close failed, local_rx_event_q addr is NULL.";
            return UBRING_ERR;
        }
        if (UNLIKELY(trx->ubr_tx.local_tx_event_q.addr == NULL)) {
            LOG(ERROR) << "Trx close failed, local_tx_event_q addr is NULL.";
            return UBRING_ERR;
        }
        return UBRING_OK;
    }

private:
    RETURN_CODE UbrTrxMapLocalShm(SHM *local_shm);
    RETURN_CODE UbrTrxMapRemoteShm(SHM *remote_shm);
    RETURN_CODE ApplyAndMapLocalShm(SHM *local_trx_shm, const char *local_name);
    RETURN_CODE ApplyAndMapRemoteShm(SHM *remote_trx_shm);
    static RETURN_CODE UbrTrxCloseCheck(UbrTrx *trx);
    void ReleaseFileLock(int lock_fd);
    ssize_t StartReadv(UbrTrx *trx, const struct iovec *iov, int iovcnt, size_t remain_buf_len);
    void PreWriteAddr(uint8_t *addr, size_t len);
    RETURN_CODE WritevHasEnoughSpace(size_t buf_len);
    RETURN_CODE UbrServerTrxInit(SHM *local_shm, SHM *remote_shm);
    static RETURN_CODE UbrClearResourceCheck(UbrTrx *trx, uint64_t start_time, UbrCloseType close_type);
    static RETURN_CODE ClearTrxResource(UbrTrx *trx, uint64_t start_time, UbrCloseType close_type, int op=0);

    UbrTrx* _trx{nullptr};
};
}
}

#endif //BRPC_UB_RING_H