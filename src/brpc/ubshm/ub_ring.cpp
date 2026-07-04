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

#include <errno.h>
#include <iostream>
#include <gflags/gflags.h>
#include <unistd.h>
#include <ctime>
#include "bthread/bthread.h"
#include "butil/logging.h"
#include "brpc/ubshm/ub_ring.h"
#include "brpc/ubshm/ub_ring_manager.h"
#include "brpc/ubshm/shm/shm_ipc.h"

namespace brpc {
namespace ubring {
uint32_t g_sleepTime[UBR_TASK_STEP_NUM] = {0};
#define TIME_COVERSION 1000
DEFINE_int32(ub_disconnect_timeout, 5, "Ubshm disconnection timeout.");
DEFINE_int32(ub_connect_timeout, 1, "Ubshm connection timeout.");
DEFINE_int32(ub_hb_timer_interval, 5, "Heartbeat timer interval.");
DEFINE_int32(ub_hb_retry_cnt, 10, "Heartbeat retry times.");
DEFINE_int32(ub_event_queue_timer_interval, 100, "Interval of the disconnection timer.");

UBRing::UBRing()
{}
UBRing::~UBRing()
{}

RETURN_CODE UBRing::UbrTrxMapShm(SHM *localShm, SHM *remoteShm)
{
    RETURN_CODE rc = UbrTrxMapLocalShm(localShm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx map local shared memory failed.";
        return rc;
    }
    rc = UbrTrxMapRemoteShm(remoteShm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx map remote shared memory failed.";
        return rc;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrTrxClose() {
    RETURN_CODE closeCheckRc = UbrTrxCloseCheck(_trx);
    if (UNLIKELY(closeCheckRc != UBRING_OK)) {
        if (closeCheckRc == UBRING_REENTRY) {
            LOG(INFO) << "Trx close skipped, already closing, local name=" << _trx->localShm.name;
            return UBRING_OK;
        }
        return UBRING_ERR;
    }
    if (_trx->ubrRx.remoteTxEventQ.addr != nullptr) {
        ((UbrEventQMsg *)_trx->ubrRx.remoteTxEventQ.addr)->flag = UBR_STATE_CLOSING;
    }

    uint32_t disconnectTimeout = FLAGS_ub_disconnect_timeout;
    uint64_t startTime = GetCurNanoSeconds();

    if (_trx->ubrTx.localTxEventQ.addr != nullptr && ((UbrEventQMsg *)_trx->ubrTx.localTxEventQ.addr)->flag == UBR_STATE_CONNECTED) {
        ((UbrEventQMsg *)_trx->ubrTx.localTxEventQ.addr)->flag = UBR_STATE_CLOSED;
        _trx->ubrTx.trxState = UBR_STATE_CLOSED;
    }

    if (_trx->ubrTx.remoteRxEventQ.addr != nullptr) {
        ((UbrEventQMsg *)_trx->ubrTx.remoteRxEventQ.addr)->flag = UBR_STATE_CLOSED;
    }
    while (_trx->ubrRx.localRxEventQ.addr != nullptr && ((UbrEventQMsg *)_trx->ubrRx.localRxEventQ.addr)->flag != UBR_STATE_CLOSED) {
        UbrSetSleepTask(UBR_TASK_CLOSE);
        if (HasTimedOut(startTime, disconnectTimeout) != UBRING_OK) {
            LOG(WARNING) << "Local shm " << _trx->localShm.name
            << " wait for the peer to close timed out, force cleanup.";
            _trx->ubrRx.trxState = UBR_STATE_CLOSED;
            // Force synchronous cleanup instead of relying on async timer
            DeleteTimerSafe((uint32_t)_trx->timerFd);
            DeleteTimerSafe((uint32_t)_trx->hbTimerFd);
            if (_trx->ubrTx.remoteRxEventQ.addr != nullptr) {
                ((UbrEventQMsg *)_trx->ubrTx.remoteRxEventQ.addr)->flag = UBR_STATE_CLOSED;
            }
            if (UNLIKELY(UbrTrxFreeShm(_trx) != UBRING_OK)) {
                LOG(WARNING) << "Force close, local shm " << _trx->localShm.name << " free failed.";
            }
            if (UNLIKELY(UBRingManager::ReleaseUbrTrxFromMgr(_trx) != UBRING_OK)) {
                LOG(WARNING) << "Force close, release trx " << _trx->localShm.name << " failed.";
            }
            return UBRING_ERR_TIMEOUT;
        }
        bthread_usleep(1000);  // 1ms, yield to other bthreads
    }
    _trx->ubrRx.trxState = UBR_STATE_CLOSED;
    RETURN_CODE rc;
    if (UNLIKELY((rc = ClearTrxResource(_trx, startTime, UBR_SEND_CLOSE)) != UBRING_OK)) {
        if (rc == UBRING_REENTRY) {
            LOG(INFO) << "Trx close, peer is closing, trx local name=" << _trx->localShm.name;
            return UBRING_OK;
        }
        LOG(ERROR) << "Trx close, clear trx resource failed, trx local name=" << _trx->localShm.name;
        return UBRING_ERR;
    }
    // Unlink local shm name immediately so process exit does not leave visible leftovers.
    RETURN_CODE unlinkRc = ShmFree(&_trx->localShm);
    if (unlinkRc != UBRING_OK && unlinkRc != SHM_ERR_NOT_FOUND && unlinkRc != SHM_ERR_RESOURCE_ATTACHED) {
        LOG(WARNING) << "Trx close, unlink local shm failed, trx local name=" << _trx->localShm.name
                     << ", rc=" << unlinkRc;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrAddCloseTimer() {
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "Trx add close timer failed, trx is null.";
        return UBRING_ERR;
    }

    uint32_t eventQTimerInterval = FLAGS_ub_event_queue_timer_interval * TIME_COVERSION;
    itimerspec timeSpec = {
            .it_interval = {.tv_sec = 0, .tv_nsec = eventQTimerInterval},
            .it_value = {.tv_sec = 0, .tv_nsec = 1}
    };
    int timerFd = TimerStart(&timeSpec, UbrTrxCloseCallback, (void*)_trx);
    if (UNLIKELY(timerFd == -1)) {
        LOG(ERROR) << "Start ubr close timer failed, trx local name=" << _trx->localShm.name;
        return UBRING_ERR;
    }
    _trx->timerFd = timerFd;
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrAddTimer() {
    if (UNLIKELY(UbrAddCloseTimer() != UBRING_OK)) {
        LOG(ERROR) << "Ubr " << _trx->localShm.name << " add closed timer failed.";
        return UBRING_ERR;
    }

    if (UNLIKELY(UbrAddHBTimer() != UBRING_OK)) {
        DeleteTimerSafe((uint32_t)_trx->timerFd);
        LOG(ERROR) << "Ubr " << _trx->localShm.name << " add heartbeat timer failed.";
        return UBRING_ERR;
    }
    return UBRING_OK;
}

void* UBRing::UbrTrxCloseCallback(void* args) {
    auto* trx = (UbrTrx*) args;
    if (UNLIKELY(UBRing::UbrTrxCallbackCheck(trx) != UBRING_OK)) {
        return nullptr;
    }

    auto* localRxEventQ = (UbrEventQMsg *)trx->ubrRx.localRxEventQ.addr;
    auto* localTxEventQ = (UbrEventQMsg *)trx->ubrTx.localTxEventQ.addr;
    if (localRxEventQ->flag != UBR_STATE_CLOSED || localTxEventQ->flag == UBR_STATE_CLOSED) {
        return nullptr;
    }
    trx->ubrRx.trxState = UBR_STATE_CLOSED;
    int fd = (int)trx->localShm.fd;
    do {
        if (ATOMIC_LOAD(trx->closeCnt) == 0) {
            break;
        }
        ATOMIC_SUB(trx->closeCnt, 1);

        uint64_t startTime = GetCurNanoSeconds();

        if (localTxEventQ->flag == UBR_STATE_CONNECTED || ATOMIC_LOAD(trx->closeCnt) == 1) {
            localTxEventQ->flag = UBR_STATE_CLOSED;
            trx->ubrTx.trxState = UBR_STATE_CLOSED;
        }
        UbrEventQMsg* remoteRxEventQ = (UbrEventQMsg *)trx->ubrTx.remoteRxEventQ.addr;
        if (remoteRxEventQ == nullptr) {
            LOG(ERROR) << "Trx close callback failed, " << trx->localShm.name << " remoteRxEventQ is NULL.";
            break;
        }
        remoteRxEventQ->flag = UBR_STATE_CLOSED;
        RETURN_CODE clearRc = ClearTrxResource(trx, startTime, UBR_CALL_BACK_CLOSE, 1);
        if (UNLIKELY(clearRc != UBRING_OK && clearRc != UBRING_REENTRY)) {
            LOG(ERROR) << "Trx close callback failed, " << trx->localShm.name << " clear trx resource failed.";
            break;
        }
    } while (0);
    return nullptr;
}

RETURN_CODE UBRing::UbrAddHBTimer() {
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "Trx add heartbeat timer failed, trx is null.";
        return UBRING_ERR;
    }

    itimerspec timeSpec = {
            .it_interval = {.tv_sec = FLAGS_ub_hb_timer_interval, .tv_nsec = 0},
            .it_value = {.tv_sec = 0, .tv_nsec = 1}
    };
    int timerFd = TimerStart(&timeSpec, UbrTrxHBCallback, (void*)_trx);
    if (UNLIKELY(timerFd == -1)) {
        LOG(ERROR) << "Start ubr heartbeat timer failed.";
        return UBRING_ERR;
    }
    _trx->hbTimerFd = timerFd;
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrPassiveClearTrx(UbrTrx *trx, int fd, PASSIVE_DISC_TYPE type) {
    RETURN_CODE passiveCloseCheckRc = UbrTrxCloseCheck(trx);
    if (UNLIKELY(passiveCloseCheckRc != UBRING_OK)) {
        if (passiveCloseCheckRc == UBRING_REENTRY) {
            LOG(INFO) << "Passive close skipped, active close in progress, name=" << trx->localShm.name;
            uint64_t startTime = GetCurNanoSeconds();
            return ClearTrxResource(trx, startTime, UBR_CALL_BACK_CLOSE);
        }
        return UBRING_ERR;
    }
    trx->ubrTx.trxState = UBR_STATE_CLOSED;
    trx->ubrRx.trxState = UBR_STATE_CLOSED;
    DeleteTimerSafe((uint32_t)trx->timerFd);
    const char *typeName = NULL;
    if (type == UBR_HEARTBEAT) {
        DeleteTimer((uint32_t)trx->hbTimerFd);
        typeName = "Trx heartbeat";
    } else if (type == UBR_UB_EVENT) {
        DeleteTimerSafe((uint32_t)trx->hbTimerFd);
        typeName = "Ub event callback";
    }
    bthread_usleep(FLAGS_ub_flying_io_timeout * 1000000LL);  // yield-friendly sleep

    int rc = ShmLocalFree(&trx->remoteShm);
    if (rc != UBRING_OK) {
        LOG(ERROR) << typeName << ", delete remote shm failed. ret=" << rc;
    }
    rc = ShmLocalFree(&trx->localShm);
    if (rc != UBRING_OK) {
        LOG(ERROR) << typeName << ", delete local shm failed. ret=" << rc;
    }

    UBRingManager::ReleaseUbrTrxFromMgr(trx);
    return UBRING_OK;
}

void* UBRing::UbrTrxHBCallback(void* args) {
    auto* trx = (UbrTrx*) args;
    if (UNLIKELY(UbrTrxCallbackCheck(trx) != UBRING_OK)) {
        return NULL;
    }

    auto* localDataStatus = (UbrDataStatusQMsg *)trx->ubrTx.localDataStatusQ.addr;
    auto* remoteDataStatus = (UbrDataStatusQMsg *)trx->ubrRx.remoteDataStatusQ.addr;
    if (UNLIKELY(localDataStatus == NULL || remoteDataStatus == NULL)) {
        LOG(ERROR) << "Heartbeat error, datastatus is NULL.";
        return NULL;
    }

    if (trx->ubrTx.trxState != UBR_STATE_CONNECTED || trx->ubrRx.trxState != UBR_STATE_CONNECTED) {
        LOG_EVERY_SECOND(INFO) << "Heartbeat cannot be started, wait connected state.";
        return NULL;
    }

    remoteDataStatus->heartBeat = 1;
    if (localDataStatus->heartBeat == 1) {
        localDataStatus->heartBeat = 0;
        trx->ubrTx.hbRetryCnt = 0;
        return NULL;
    }

    ++trx->ubrTx.hbRetryCnt;
    if (trx->ubrTx.hbRetryCnt <= FLAGS_ub_hb_retry_cnt) {
        return NULL;
    }

    int fd = (int)trx->localShm.fd;
    LOG(INFO) << "Hlc heartbeat, start to clear trx resource. hbTimerFd=" << fd << ", shmName=" << trx->localShm.name;
    UbrPassiveClearTrx(trx, fd, UBR_HEARTBEAT);
    LOG(INFO) << "Hlc heartbeat clear trx resource finish.";
    return NULL;
}

RETURN_CODE UBRing::UbrAddAsynClearTimer(UbrTrx *trx) {
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Trx add close timer failed, trx is null.";
        return UBRING_ERR;
    }

    if (trx->clearTimerFd > 0) {
        return UBRING_OK;
    }

    itimerspec timeSpec = {
            .it_interval = {.tv_sec = 0, .tv_nsec = 0},
            .it_value = {.tv_sec = FLAGS_ub_flying_io_timeout, .tv_nsec = 0}
    };

    int timerFd = TimerStart(&timeSpec, UbrAsynClearCallback, (void*)trx);
    if (UNLIKELY(timerFd == -1)) {
        LOG(ERROR) << "Start ubr close timer failed, trx name=%s.", trx->localShm.name;
        return UBRING_ERR;
    }
    trx->clearTimerFd = timerFd;
    return UBRING_OK;
}

void *UBRing::UbrAsynClearCallback(void *args)
{
    auto* trx = (UbrTrx*) args;
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Trx close, trx is null.";
        return NULL;
    }

    if (UNLIKELY(UbrTrxFreeShm(trx) != UBRING_OK)) {
        LOG(ERROR) << "Trx close, wait for local shm " << trx->localShm.name << " free fail.";
    }

    if (UNLIKELY(UBRingManager::ReleaseUbrTrxFromMgr(trx) != UBRING_OK)) {
        LOG(ERROR) << "Trx close, release shm " << trx->localShm.name << " trx failed.";
    }
    return NULL;
}

int UBRing::UbrTrxSend(const void *buf, uint32_t bufLen)
{
    if (UNLIKELY(CheckTrxSendPreCheck(_trx) != UBRING_OK)) {
        return UBRING_ERR;
    }
    // 1.2 Calculate space
    auto *dataStatusMsg = (UbrDataStatusQMsg *)_trx->ubrTx.localDataStatusQ.addr;
    auto *dataMsg = (UbrMsgFormat *)_trx->ubrTx.remoteDataQ.addr;
    uint32_t cap = _trx->ubrTx.capacity;
    uint32_t tail = dataStatusMsg->tail;
    uint32_t remainChunkNum =
        (_trx->ubrTx.writePos > tail) ? (tail + cap - _trx->ubrTx.writePos) : (tail - _trx->ubrTx.writePos);
    uint32_t needMsgChunkNum = CalcUbrMsgChunkCnt(bufLen);
    if (needMsgChunkNum >= cap) {
        LOG(ERROR) << "Ubr send failed, payload length=" << bufLen
                   << " needs " << needMsgChunkNum << " chunks, capacity=" << cap << ".";
        errno = EMSGSIZE;
        return UBRING_ERR;
    }
    if (remainChunkNum < needMsgChunkNum) {
        return UBRING_RETRY;
    }
    UbrMsgFormat *msg = &(_trx->ubrTx.localMsgSpace);
    uint32_t totalSendLen = 0;
    uint32_t remainBufLen = bufLen;
    uint8_t isLastPkt = 0;
    _trx->ubrTx.outIoId++;
    ((UbrEventQMsg *)_trx->ubrTx.remoteRxEventQ.addr)->ioId = _trx->ubrTx.outIoId;
    while (remainBufLen > 0) {
        isLastPkt = (uint8_t)(remainBufLen <= UBR_MSG_PAYLOAD_LEN);
        msg->header[UBR_MSG_FLAG_INDEX] = isLastPkt ? UBR_MSG_CHUNK_EOF : UBR_MSG_CHUNK_EXIST;
        msg->header[UBR_MSG_LEN_INDEX] = isLastPkt ? (uint8_t)remainBufLen : UBR_MSG_PAYLOAD_LEN;
        msg->header[UBR_MSG_CUR_INDEX] = 0;
        memcpy(msg->payload.inner, (const uint8_t *)buf + totalSendLen, msg->header[UBR_MSG_LEN_INDEX]);
        Copy64Byte((int8_t *)&dataMsg[_trx->ubrTx.writePos], (int8_t *)msg);
        _trx->ubrTx.writePos = (_trx->ubrTx.writePos + 1) % cap;
        totalSendLen += msg->header[UBR_MSG_LEN_INDEX];
        remainBufLen -= msg->header[UBR_MSG_LEN_INDEX];
    }
    return (int)totalSendLen;
}

int UBRing::UbrTrxRecv(void *buf, uint32_t bufLen)
{
    RETURN_CODE rc = UBRING_OK;
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, buf, bufLen)) != UBRING_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }
    UbrMsgFormat *dataMsg = (UbrMsgFormat *)_trx->ubrRx.localDataQ.addr;
    uint32_t readPosEnd = _trx->ubrRx.readPos;
    uint8_t flag = dataMsg[readPosEnd].header[UBR_MSG_FLAG_INDEX];
    if (flag == UBR_MSG_CHUNK_NONE) {
        return UBRING_RETRY;
    }
    return UbrTrxRecvBlockMode(static_cast<uint8_t *>(buf), bufLen);
}

int UBRing::UbrTrxRecvBlockMode(uint8_t *dest, uint32_t bufLen)
{
    RETURN_CODE rc = UBRING_OK;
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, dest, bufLen)) != UBRING_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }

    int32_t totalCopied = 0;
    int32_t remainingLen = (int32_t)bufLen;
    bool notEofEncountered = true;

    UbrRx *ubrRx = &_trx->ubrRx;
    UbrMsgFormat *dataMsg = (UbrMsgFormat *)ubrRx->localDataQ.addr;
    bool needUpdateEpollEofPos = ubrRx->readPos == ubrRx->epEofPos;

    while (notEofEncountered && remainingLen > 0) {
        if (UNLIKELY(CheckTrxRecvPreCheck(_trx) != UBRING_OK)) {
            return UBRING_ERR;
        }
        UbrMsgFormat *currentChunk = &dataMsg[ubrRx->readPos];
        uint8_t flag = currentChunk->header[UBR_MSG_FLAG_INDEX];
        if (flag == UBR_MSG_CHUNK_NONE) {
            if (totalCopied > 0) {
                break;
            }
            errno = EAGAIN;
            return -1;
        }
        if (flag == UBR_MSG_CHUNK_EOF) {
            notEofEncountered = false;
        }
        uint8_t chunkMsgLen = currentChunk->header[UBR_MSG_LEN_INDEX];
        uint8_t curIndex = currentChunk->header[UBR_MSG_CUR_INDEX];
        uint8_t availableData = chunkMsgLen - curIndex;

        int32_t copyLen = (remainingLen < availableData) ? remainingLen : availableData;
        memcpy(dest + totalCopied, dataMsg[ubrRx->readPos].payload.inner + curIndex, (size_t)copyLen);
        totalCopied += copyLen;
        remainingLen -= copyLen;
        currentChunk->header[UBR_MSG_CUR_INDEX] += (uint8_t)copyLen;
        if (LIKELY(currentChunk->header[UBR_MSG_CUR_INDEX] == chunkMsgLen)) {
            currentChunk->header[UBR_MSG_FLAG_INDEX] = UBR_MSG_CHUNK_NONE;
            UpdateDataQTail(_trx);
            ubrRx->readPos = (ubrRx->readPos + 1) % ubrRx->capacity;
        }
    }
    if (needUpdateEpollEofPos) {
        ubrRx->epEofPos = ubrRx->readPos;
    }
    return (int)totalCopied;
}

ssize_t UBRing::UbrTrxWritev(const struct iovec *iov, int iovcnt)
{
    if (UNLIKELY(CheckTrxSendPreCheck(_trx) != UBRING_OK)) {
        return UBRING_ERR;
    }

    size_t bufLen = 0;
    for (int i = 0; i < iovcnt; i++) {
        bufLen += iov[i].iov_len;
    }
    RETURN_CODE rc = WritevHasEnoughSpace(bufLen);
    if (rc != UBRING_OK) {
        return rc;
    }

    UbrMsgFormat *dataMsg = (UbrMsgFormat *)_trx->ubrTx.remoteDataQ.addr;
    UbrMsgFormat *msg = &(_trx->ubrTx.localMsgSpace);
    int curIov = 0;
    size_t curIovPos = 0;
    ssize_t totalSendLen = 0;
    size_t pktRemainN = 0;
    size_t iovRemain = 0;
    size_t fulled = 0;
    uint8_t isLastPkt = 0;
    uint8_t curPktLen = 0;
    _trx->ubrTx.outIoId++;
    ((UbrEventQMsg *)_trx->ubrTx.remoteRxEventQ.addr)->ioId = _trx->ubrTx.outIoId;
    while (bufLen > 0) {
        isLastPkt = (uint8_t)(bufLen <= UBR_MSG_PAYLOAD_LEN);
        curPktLen = isLastPkt ? (uint8_t)bufLen : UBR_MSG_PAYLOAD_LEN;
        msg->header[UBR_MSG_FLAG_INDEX] = isLastPkt ? UBR_MSG_CHUNK_EOF : UBR_MSG_CHUNK_EXIST;
        msg->header[UBR_MSG_LEN_INDEX] = curPktLen;
        msg->header[UBR_MSG_CUR_INDEX] = 0;
        pktRemainN = curPktLen;
        while (curIov < iovcnt && pktRemainN > 0) {
            iovRemain = (iov[curIov].iov_len - curIovPos);
            fulled = iovRemain > pktRemainN ? pktRemainN : iovRemain;
            memcpy((msg->payload.inner + (curPktLen - (uint8_t)pktRemainN)),
                (uint8_t *)(iov[curIov].iov_base) + curIovPos,
                fulled);
            pktRemainN -= fulled;
            curIovPos += fulled;
            if (curIovPos == iov[curIov].iov_len) {
                curIov++;
                curIovPos = 0;
            }
        }

        Copy64Byte((int8_t *)&dataMsg[_trx->ubrTx.writePos], (int8_t *)msg);
        _trx->ubrTx.writePos = (_trx->ubrTx.writePos + 1) % _trx->ubrTx.capacity;
        totalSendLen += (ssize_t)curPktLen;
        bufLen -= (int)curPktLen;
    }
    return totalSendLen;
}

ssize_t UBRing::UbrTrxReadv(const struct iovec *iov, int iovcnt)
{
    RETURN_CODE rc = UBRING_OK;
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, iov, (uint32_t)iovcnt)) != UBRING_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }
    UbrMsgFormat *dataMsg = (UbrMsgFormat *)_trx->ubrRx.localDataQ.addr;
    uint32_t readPosEnd = _trx->ubrRx.readPos;
    uint8_t flag = dataMsg[readPosEnd].header[UBR_MSG_FLAG_INDEX];
    if (flag == UBR_MSG_CHUNK_NONE) {
        errno = EAGAIN;
        return -1;
    }
    ssize_t nr = UbrTrxReadvBlockMode(iov, iovcnt);
    if (UNLIKELY(nr == -1)) {
        LOG(ERROR) << "Non-blocking readv msg in failed, connection has been closed.";
        errno = EPIPE;
        return -1;
    }
    return nr;
}

ssize_t UBRing::UbrTrxReadvBlockMode(const struct iovec *iov, int iovcnt)
{
    RETURN_CODE rc = UBRING_OK;
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, iov, (uint32_t)iovcnt)) != UBRING_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }

    size_t remainBufLen = 0;
    for (int i = 0; i < iovcnt; i++) {
        remainBufLen += iov[i].iov_len;
    }

    bool needUpdateEpollEofPos = _trx->ubrRx.readPos == _trx->ubrRx.epEofPos;
    ssize_t totalRecvLen = StartReadv(_trx, iov, iovcnt, remainBufLen);

    if (needUpdateEpollEofPos) {
        _trx->ubrRx.epEofPos = _trx->ubrRx.readPos;
    }
    return totalRecvLen;
}

RETURN_CODE UBRing::IsUbrTrxReadable(uint32_t epEvent)
{
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "The trx to be checked is NULL.";
        return UBRING_ERR;
    }
    if (UNLIKELY(_trx->localShm.addr == NULL)) {
        LOG(ERROR) << "The trx localShm to be checked is NULL.";
        return UBRING_ERR;
    }
    if (UNLIKELY(_trx->ubrTx.trxState != UBR_STATE_CONNECTED)) {
        return UBRING_ERR;
    }

    uint64_t ioId = ((UbrEventQMsg *)_trx->ubrRx.localRxEventQ.addr)->ioId;
    if ((epEvent & EPOLLET) && ioId == _trx->ubrRx.inIoId) {
        return MPA_MUXER_NOT_READY;
    }

    uint32_t readPosEnd = _trx->ubrRx.readPos;
    if (epEvent & EPOLLET) {
        readPosEnd = _trx->ubrRx.epEofPos;
    }

    UbrMsgFormat *dataMsg = (UbrMsgFormat *)_trx->ubrRx.localDataQ.addr;
    uint8_t flag = dataMsg[readPosEnd].header[UBR_MSG_FLAG_INDEX];
    if (flag == UBR_MSG_CHUNK_NONE) {
        return MPA_MUXER_NOT_READY;
    }
    if (epEvent & EPOLLET) {
        _trx->ubrRx.inIoId = ioId;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::IsUbrTrxWriteable(uint32_t epEvent)
{
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "The trx to be checked is NULL.";
        return UBRING_ERR;
    }
    if (UNLIKELY(_trx->localShm.addr == NULL)) {
        LOG(ERROR) << "The trx localShm to be checked is NULL.";
        return UBRING_ERR;
    }
    if (UNLIKELY((UbrEventQMsg *)_trx->ubrTx.localTxEventQ.addr == NULL)) {
        LOG(ERROR) << "The trx localTxEventQ addr is NULL.";
        return UBRING_ERR;
    }
    if (UNLIKELY((UbrEventQMsg *)_trx->ubrTx.localDataStatusQ.addr == NULL)) {
        LOG(ERROR) << "The trx localDataStatusQ addr is NULL.";
        return UBRING_ERR;
    }

    if (UNLIKELY(_trx->ubrTx.trxState != UBR_STATE_CONNECTED)) {
        LOG(ERROR) << "The trx is not connected state.";
        return UBRING_ERR;
    }

    UbrDataStatusQMsg *dataStatusMsg = (UbrDataStatusQMsg *)_trx->ubrTx.localDataStatusQ.addr;
    uint32_t cap = _trx->ubrTx.capacity;
    uint32_t tail = dataStatusMsg->tail;
    uint32_t remainChunkNum =
        (_trx->ubrTx.writePos > tail) ? (tail + cap - _trx->ubrTx.writePos) : (tail - _trx->ubrTx.writePos);
    if (remainChunkNum == 0) {
        _trx->ubrTx.epLastCap = remainChunkNum;
        return MPA_MUXER_NOT_READY;
    }

    if ((epEvent & EPOLLET) && (_trx->ubrTx.epLastCap >= remainChunkNum)) {
        _trx->ubrTx.epLastCap = remainChunkNum;
        return MPA_MUXER_NOT_READY;
    }
    _trx->ubrTx.epLastCap = remainChunkNum;
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrSetTimeout(UbrTaskStep taskType, int timeout)
{
    if (taskType >= UBR_TASK_STEP_NUM || timeout < 0) {
        LOG(ERROR) << "Set timeout failed, invalid task type.";
        return UBRING_ERR;
    }

    g_sleepTime[taskType] = (uint32_t)timeout;
    LOG(INFO) << "Set timeout success, taskType=" << taskType << ", timeout=" << timeout;
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrTrxFreeShm(UbrTrx *trx)
{
    if (trx == NULL) {
        LOG(ERROR) << "Trx is NULL.";
        return UBRING_ERR;
    }

    RETURN_CODE rc = UBRING_OK;
    rc = ShmMunmap(&trx->localShm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx close, local unmap " << trx->localShm.name << " shm fail.";
        return UBRING_ERR;
    }

    rc = ShmFree(&trx->localShm);
    if (UNLIKELY(rc != UBRING_OK)) {
        if (rc != SHM_ERR_RESOURCE_ATTACHED && rc != SHM_ERR_NOT_FOUND) {
            LOG(ERROR) << "Wait for " << trx->localShm.name << " local shm free fail.";
            return UBRING_ERR;
        }
        LOG(INFO) << "Local shm " << trx->localShm.name << " already freed, continue to free remote shm.";
    }

    RETURN_CODE remoteRc = UBRING_OK;
    if (trx->remoteShm.addr != NULL) {
        remoteRc = ShmRemoteFree(&trx->remoteShm);
    }
    if (remoteRc != UBRING_OK) {
        LOG(WARNING) << "Free remote shm " << trx->remoteShm.name << " failed, rc=" << remoteRc;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::UbrUnlinkLocalShm()
{
    if (UNLIKELY(_trx == NULL)) {
        return UBRING_ERR;
    }
    RETURN_CODE rc = ShmFree(&_trx->localShm);
    if (rc != UBRING_OK && rc != SHM_ERR_NOT_FOUND && rc != SHM_ERR_RESOURCE_ATTACHED) {
        LOG(WARNING) << "Unlink local shm " << _trx->localShm.name << " failed, rc=" << rc;
        return rc;
    }
    return UBRING_OK;
}

void UBRing::PreWriteAddr(uint8_t *addr, size_t len)
{
    if (addr == NULL) {
        return;
    }

    size_t i = 0;
    while (i < len) {
        if (i + sizeof(uint64_t) <= len) {
            *(uint64_t *)(addr + i) = (uint64_t)0;
            i += sizeof(uint64_t);
        } else if (i + sizeof(uint32_t) < len) {
            *(uint32_t *)(addr + i) = (uint32_t)0;
            i += sizeof(uint32_t);
        } else if (i + sizeof(uint16_t) < len) {
            *(uint16_t *)(addr + i) = (uint16_t)0;
            i += sizeof(uint16_t);
        } else {
            *(addr + i) = (uint8_t)0;
            i += sizeof(uint8_t);
        }
    }
}

void UBRing::PrewriteUbrTx(UbrTx *tx)
{
    if (tx == NULL) {
        return;
    }
    PreWriteAddr(tx->remoteDataQ.addr, tx->capacity * sizeof(UbrMsgFormat));
}

void UBRing::PrewriteUbrRx(UbrRx *rx)
{
    if (rx == NULL) {
        return;
    }
    PreWriteAddr(rx->localDataQ.addr, rx->capacity * sizeof(UbrMsgFormat));
}

RETURN_CODE UBRing::UbrTrxMapLocalShm(SHM *localShm)
{
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null.";
        return UBRING_ERR;
    }
    if (UNLIKELY(localShm == NULL || localShm->addr == NULL)) {
        LOG(ERROR) << "Trx map Shared memory failed, localShm is null or addr is NULL.";
        return UBRING_ERR;
    }
    _trx->localShm = *localShm;
    _trx->ubrTx.localTxEventQ.addr = localShm->addr + TX_EVENTQ_ADDR_OFFSET;
    _trx->ubrTx.localTxEventQ.len = UBR_EVENTQ_LEN;
    _trx->ubrRx.localRxEventQ.addr = localShm->addr + RX_EVENTQ_ADDR_OFFSET;
    _trx->ubrRx.localRxEventQ.len = UBR_EVENTQ_LEN;
    _trx->ubrTx.localDataStatusQ.addr = localShm->addr + DATASTATUSQ_ADDR_OFFSET;
    _trx->ubrTx.localDataStatusQ.len = UBR_DATASTATUSQ_LEN;
    size_t addrAlignedOffset = Aligned64Offset(localShm->addr + DATAQ_ADDR_OFFSET);
    _trx->ubrRx.localDataQ.addr = localShm->addr + DATAQ_ADDR_OFFSET + addrAlignedOffset;
    _trx->ubrRx.localDataQ.len = localShm->len - DATAQ_ADDR_OFFSET - addrAlignedOffset;
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrTrxMapRemoteShm(SHM *remoteShm)
{
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null.";
        return UBRING_ERR;
    }
    if (UNLIKELY(remoteShm == NULL || remoteShm->addr == NULL)) {
        LOG(ERROR) << "Trx map Shared memory failed, remoteShm is null or addr is NULL.";
        return UBRING_ERR;
    }
    _trx->remoteShm = *remoteShm;
    _trx->ubrRx.remoteTxEventQ.addr = remoteShm->addr + TX_EVENTQ_ADDR_OFFSET;
    _trx->ubrRx.remoteTxEventQ.len = UBR_EVENTQ_LEN;
    _trx->ubrTx.remoteRxEventQ.addr = remoteShm->addr + RX_EVENTQ_ADDR_OFFSET;
    _trx->ubrTx.remoteRxEventQ.len = UBR_EVENTQ_LEN;
    _trx->ubrRx.remoteDataStatusQ.addr = remoteShm->addr + DATASTATUSQ_ADDR_OFFSET;
    _trx->ubrRx.remoteDataStatusQ.len = UBR_DATASTATUSQ_LEN;
    size_t addrAlignedOffset = Aligned64Offset(remoteShm->addr + DATAQ_ADDR_OFFSET);
    _trx->ubrTx.remoteDataQ.addr = remoteShm->addr + DATAQ_ADDR_OFFSET + addrAlignedOffset;
    _trx->ubrTx.remoteDataQ.len = remoteShm->len - DATAQ_ADDR_OFFSET - addrAlignedOffset;
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrServerTrxInit(SHM *localShm, SHM *remoteShm)
{
    RETURN_CODE rc = UbrTrxMapShm(localShm, remoteShm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) <<"Trx map shared memory failed.";
        return rc;
    }

    uint32_t localDataMsgCap = (uint32_t)(_trx->ubrRx.localDataQ.len / UBR_MSG_LEN);
    uint32_t remoteDataMsgCap = (uint32_t)(_trx->ubrTx.remoteDataQ.len / UBR_MSG_LEN);
    _trx->ubrRx.capacity = localDataMsgCap;
    _trx->ubrTx.capacity = remoteDataMsgCap;
    rc = UBRingManager::GetUbrDealMsgMaxCnt(_trx->ubrRx.capacity, &_trx->ubrRx.dealMsgMaxCnt);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Get ubring deal msg max cnt.";
        return rc;
    }
    PrewriteUbrRx(&_trx->ubrRx);
    PrewriteUbrTx(&_trx->ubrTx);

    ((UbrDataStatusQMsg *)(_trx->ubrTx.localDataStatusQ.addr))->tail = remoteDataMsgCap - 1;
    ((UbrDataStatusQMsg *)(_trx->ubrRx.remoteDataStatusQ.addr))->tail = localDataMsgCap - 1;

    if (UNLIKELY(UbrAddTimer() != UBRING_OK)) {
        LOG(ERROR) << "Ubr add timer failed, localName=" << localShm->name;
        return UBRING_ERR;
    }

    ((UbrDataStatusQMsg *)(_trx->ubrTx.localDataStatusQ.addr))->timeout = FLAGS_ub_connect_timeout;
    ((UbrDataStatusQMsg *)(_trx->ubrRx.remoteDataStatusQ.addr))->timeout = FLAGS_ub_connect_timeout;

    ((UbrEventQMsg *)_trx->ubrTx.remoteRxEventQ.addr)->flag = UBR_STATE_CONNECTED;
    ((UbrEventQMsg *)_trx->ubrRx.localRxEventQ.addr)->flag = UBR_STATE_CONNECTED;
    _trx->ubrTx.trxState = UBR_STATE_CONNECTED;
    _trx->ubrRx.trxState = UBR_STATE_CONNECTED;
    return UBRING_OK;
}

int UBRing::UbrAllocateServerShm(SHM* remote_trx_shm, SHM* local_trx_shm) {
    UbrSetSleepTask(UBR_TASK_ACCEPT_MAP_FRONT);
    if (UNLIKELY((ShmRemoteMalloc(remote_trx_shm)) != UBRING_OK)) {
        LOG(ERROR) << "Trx apply remote shared memory failed.";
        return -1;
    }

    if (UNLIKELY((ShmLocalCalloc(local_trx_shm)) != UBRING_OK)) {
        LOG(ERROR) << "Trx apply local shared memory failed.";
        ShmRemoteFree(remote_trx_shm);
        return -1;
    }

    UbrTrx **ubrTrxPtr = &_trx;
    if (UNLIKELY((UBRingManager::AcquireUbrTrxFromMgr(ubrTrxPtr)) != UBRING_OK)) {
        LOG(ERROR) << "Acquire ubrtrx failed.";
        ShmRemoteFree(remote_trx_shm);
        ShmLocalFree(local_trx_shm);
        return -1;
    }
    _trx->type = TCP_TRX;
    if (UNLIKELY((UbrServerTrxInit(local_trx_shm, remote_trx_shm)) != UBRING_OK)) {
        LOG(ERROR) << "Server trx init failed.";
        UbrTrxFreeShm(_trx);
        UBRingManager::ReleaseUbrTrxFromMgr(_trx);
        _trx = nullptr;
        return -1;
    }
    return 0;
}

int UBRing::UbrAllocateLocalShm(SHM *local_trx_shm, const char *shm_name)
{
    if (UNLIKELY((UBRingManager::AcquireUbrTrxFromMgr(&(_trx))) != UBRING_OK)) {
        LOG(ERROR) << "Acquire ubrtrx failed, localName=" << shm_name;
        return -1;
    }

    _trx->type = TCP_TRX;
    if (UNLIKELY((ApplyAndMapLocalShm(local_trx_shm, shm_name)) != UBRING_OK)) {
        LOG(ERROR) << "Trx apply or map local shared memory failed, localName=" << shm_name;
        _trx = nullptr;
        return -1;
    }
    return 0;
}

int UBRing::UbrMapRemoteShm(SHM *local_trx_shm, const char *local_name)
{
    RETURN_CODE rc = UbrMapRemoteShmAddTimer(local_trx_shm, local_name);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Connect Trx failed, local shm name=" << local_trx_shm->name;
        return -1;
    }
    PrewriteUbrRx(&_trx->ubrRx);
    PrewriteUbrTx(&_trx->ubrTx);
    ((UbrEventQMsg *)_trx->ubrRx.remoteTxEventQ.addr)->flag = UBR_STATE_CONNECTED;
    ((UbrEventQMsg *)_trx->ubrRx.localRxEventQ.addr)->flag = UBR_STATE_CONNECTED;
    _trx->ubrTx.trxState = UBR_STATE_CONNECTED;
    _trx->ubrRx.trxState = UBR_STATE_CONNECTED;
    return 0;
}

RETURN_CODE UBRing::UbrMapRemoteShmAddTimer(SHM *localTrxShm, const char *localName)
{
    uint64_t startTime = GetCurNanoSeconds();

    size_t remoteServerLen = UBR_MSG_LEN * (((UbrDataStatusQMsg *)(_trx->ubrTx.localDataStatusQ.addr))->tail + 1) +
                             UBR_MSG_LEN * ((DATAQ_ADDR_OFFSET / UBR_MSG_LEN) + 1);
    SHM remoteTrxShm = {NULL, remoteServerLen, 0, {0}, localTrxShm->fd};
    int result = snprintf(remoteTrxShm.name,
        SHM_MAX_NAME_BUFF_LEN,
        "%s_%s_%s",
        SHM_NAME_PREFIX,
        localName,
        SERVER_SHM_NAME_SUFFIX);
    if (UNLIKELY(result < 0)) {
        LOG(ERROR) << "Copy server shared memory name failed, localName=%s, ret=%d.", localName, result;
        return UBRING_ERR;
    }
    UbrSetSleepTask(UBR_TASK_CONNECT_MAP_FRONT);
    RETURN_CODE rc = ApplyAndMapRemoteShm(&remoteTrxShm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Connect Trx map shared memory failed, remote shm=" << remoteTrxShm.name;
        return rc;
    }

    if (UNLIKELY(UbrAddTimer() != UBRING_OK)) {
        LOG(ERROR) << "Ubr add timer failed, localName=" << localName;
        ShmRemoteFree(&_trx->remoteShm);
        return UBRING_ERR;
    }

    UbrSetSleepTask(UBR_TASK_CONNECT_MAP_AFTER);

    uint32_t timeout = ((UbrDataStatusQMsg *)(_trx->ubrTx.localDataStatusQ.addr))->timeout;
    if (HasTimedOut(startTime, timeout) != UBRING_OK) {
        LOG(ERROR) << "Local shm " << localTrxShm->name << " wait for connect remote map timeout.";
        DeleteTimerSafe((uint32_t)_trx->hbTimerFd);
        DeleteTimerSafe((uint32_t)_trx->timerFd);
        ShmRemoteFree(&_trx->remoteShm);
        return UBRING_ERR_TIMEOUT;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::ApplyAndMapLocalShm(SHM *localTrxShm, const char *localName)
{
    if (UNLIKELY(_trx == NULL || localTrxShm == NULL)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null, localName=" << localName;
        return UBRING_ERR;
    }
    int result = snprintf(localTrxShm->name,
        SHM_MAX_NAME_BUFF_LEN,
        "%s_%s_%s",
        SHM_NAME_PREFIX,
        localName,
        CLIENT_SHM_NAME_SUFFIX);
    if (UNLIKELY(result < 0)) {
        LOG(ERROR) << "Copy client localTrx shared memory name failed, localName=" << localName << ", ret=" << result;
        return UBRING_ERR;
    }

    RETURN_CODE rc = ShmLocalCalloc(localTrxShm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx apply local shared memory failed, local shm name=" << localTrxShm->name << ", rc=" << rc;
        if (rc == SHM_ERR_EXIST || rc == SHM_ERR_NOT_FOUND) {
            rc = UBR_ERR_ADDR_IN_USE;
        }
        UBRingManager::ReleaseUbrTrxFromMgr(_trx);
        return rc;
    }
    rc = UbrTrxMapLocalShm(localTrxShm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx map local shared memory failed, local shm name=" << localTrxShm->name;
        ShmLocalFree(localTrxShm);
        UBRingManager::ReleaseUbrTrxFromMgr(_trx);
        return rc;
    }
    ((UbrDataStatusQMsg *)_trx->ubrTx.localDataStatusQ.addr)->timeout = FLAGS_ub_connect_timeout;
    _trx->ubrRx.capacity = (uint32_t)(_trx->ubrRx.localDataQ.len / UBR_MSG_LEN);
    rc = UBRingManager::GetUbrDealMsgMaxCnt(_trx->ubrRx.capacity, &_trx->ubrRx.dealMsgMaxCnt);
    if (rc != UBRING_OK) {
        LOG(ERROR) << "Get ubring deal msg max cnt, local shm name=" << localTrxShm->name;
        ShmLocalFree(localTrxShm);
        UBRingManager::ReleaseUbrTrxFromMgr(_trx);
        return rc;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::ApplyAndMapRemoteShm(SHM *remoteTrxShm)
{
    RETURN_CODE rc = ShmRemoteMalloc(remoteTrxShm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx apply remote shared memory failed.";
        return rc;
    }
    rc = UbrTrxMapRemoteShm(remoteTrxShm);
    if (UNLIKELY(rc != UBRING_OK)) {
        LOG(ERROR) << "Trx map shared memory failed.";
        ShmRemoteFree(remoteTrxShm);
        return rc;
    }
    _trx->ubrTx.capacity = (uint32_t)(_trx->ubrTx.remoteDataQ.len / UBR_MSG_LEN);
    return UBRING_OK;
}

RETURN_CODE UBRing::WritevHasEnoughSpace(size_t bufLen)
{
    UbrDataStatusQMsg *dataStatusMsg = (UbrDataStatusQMsg *)_trx->ubrTx.localDataStatusQ.addr;
    uint32_t cap = _trx->ubrTx.capacity;
    uint32_t tail = dataStatusMsg->tail;
    uint32_t remainChunkNum =
        (_trx->ubrTx.writePos > tail) ? (tail + cap - _trx->ubrTx.writePos) : (tail - _trx->ubrTx.writePos);
    uint32_t needMsgChunkNum = CalcUbrMsgChunkCnt((uint32_t)bufLen);
    if (needMsgChunkNum >= cap) {
        LOG(ERROR) << "Ubr write failed, payload length=" << bufLen
                   << " needs " << needMsgChunkNum << " chunks, capacity=" << cap << ".";
        errno = EMSGSIZE;
        return UBRING_ERR;
    }
    if (remainChunkNum < needMsgChunkNum) {
        return UBRING_RETRY;
    }
    return UBRING_OK;
}

RETURN_CODE UBRing::UbrClearResourceCheck(UbrTrx *trx, uint64_t startTime, UbrCloseType closeType)
{
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Trx close failed, trx is null.";
        return UBRING_ERR;
    }

    UbrEventQMsg* localTxEventQ = (UbrEventQMsg *)trx->ubrTx.localTxEventQ.addr;
    if (localTxEventQ->flag == UBR_STATE_CONNECTED) {
        localTxEventQ->flag = UBR_STATE_CLOSING;
    }

    if (closeType == UBR_SEND_CLOSE) {
        DeleteTimerSafe((uint32_t)trx->timerFd);
    } else {
        DeleteTimer((uint32_t)trx->timerFd);
    }
    DeleteTimerSafe((uint32_t)trx->hbTimerFd);

    if (localTxEventQ->flag == UBR_STATE_CLOSING) {
        localTxEventQ->flag = UBR_STATE_CLOSED;
        trx->ubrTx.trxState = UBR_STATE_CLOSED;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::ClearTrxResource(UbrTrx *trx, uint64_t startTime, UbrCloseType closeType, int op)
{
    RETURN_CODE rc = UbrClearResourceCheck(trx, startTime, closeType);
    if (rc != UBRING_OK) {
        return rc;
    }

    rc = UbrAddAsynClearTimer(trx);
    if (rc != UBRING_OK) {
        LOG(ERROR) << "Trx close, add " << trx->localShm.name << " close clear timer failed.";
        return UBRING_ERR;
    }

    return UBRING_OK;
}

RETURN_CODE UBRing::UbrTrxCloseCheck(UbrTrx *trx)
{
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Trx close failed, client trx is null.";
        return UBRING_ERR;
    }
    int expected = MAX_CLOSE_COUNT;
    if (!ATOMIC_COMPARE_EXCHANGE_STRONG(trx->closeCnt, expected, MAX_CLOSE_COUNT - 1)) {
        LOG(INFO) << "Trx close skipped, already closing, trx local name=" << trx->localShm.name;
        return UBRING_REENTRY;
    }

    if (UNLIKELY(trx->ubrTx.localTxEventQ.addr == nullptr)) {
        LOG(ERROR) << "Trx close failed, localTxEventQ addr is NULL, trx local name=" << trx->localShm.name;
        return UBRING_ERR;
    }
    return UBRING_OK;
}

ssize_t UBRing::StartReadv(UbrTrx *trx, const struct iovec *iov, int iovcnt, size_t remainBufLen)
{
    ssize_t totalRecvLen = 0;
    int iovIndex = 0;
    size_t iovPos = 0;
    UbrMsgFormat *dataMsg = (UbrMsgFormat *)trx->ubrRx.localDataQ.addr;
    bool notEofEncountered = true;
    while (notEofEncountered && remainBufLen > 0) {
        if (UNLIKELY(CheckTrxRecvPreCheck(trx) != UBRING_OK)) {
            return UBRING_ERR;
        }
        UbrMsgFormat *currentChunk = &dataMsg[trx->ubrRx.readPos];
        uint8_t flag = currentChunk->header[UBR_MSG_FLAG_INDEX];
        if (flag == UBR_MSG_CHUNK_NONE) {
            if (totalRecvLen > 0) {
                break;
            }
            errno = EAGAIN;
            return -1;
        }
        if (flag == UBR_MSG_CHUNK_EOF) {
            notEofEncountered = false;
        }
        uint8_t chunkMsgLen = currentChunk->header[UBR_MSG_LEN_INDEX];
        uint8_t curIndex = currentChunk->header[UBR_MSG_CUR_INDEX];
        uint8_t recvLen =
            remainBufLen > (size_t)(chunkMsgLen - curIndex) ? (chunkMsgLen - curIndex) : (uint8_t)remainBufLen;
        while (iovIndex < iovcnt && recvLen > 0) {
            size_t copyLen =
                recvLen > (iov[iovIndex].iov_len - iovPos) ? iov[iovIndex].iov_len - iovPos : (size_t)recvLen;
            memcpy((uint8_t *)iov[iovIndex].iov_base + iovPos, currentChunk->payload.inner + curIndex, copyLen);
            recvLen -= (uint8_t)copyLen;
            iovPos += copyLen;
            curIndex += (uint8_t)copyLen;
            if (iovPos == iov[iovIndex].iov_len) {
                iovIndex++;
                iovPos = 0;
            }
            remainBufLen -= copyLen;
            totalRecvLen += (ssize_t)copyLen;
        }
        currentChunk->header[UBR_MSG_CUR_INDEX] = curIndex;
        if (currentChunk->header[UBR_MSG_CUR_INDEX] == chunkMsgLen) {
            currentChunk->header[UBR_MSG_FLAG_INDEX] = UBR_MSG_CHUNK_NONE;
            UpdateDataQTail(trx);
            trx->ubrRx.readPos = (trx->ubrRx.readPos + 1) % trx->ubrRx.capacity;
        }
    }
    return totalRecvLen;
}
}  // namespace ubring
}  // namespace brpc
