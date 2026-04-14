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

#include <iostream>
#include <gflags/gflags.h>
#include <unistd.h>
#include <ctime>
#include "butil/logging.h"
#include "brpc/ub/ub_ring.h"

namespace brpc {
namespace ub {
uint32_t g_sleep_time[UBR_TASK_STEP_NUM] = {0};
#define TIME_COVERSION 1000
DEFINE_int32(ub_disconnect_timeout, 1, "Ubshm disconnection timeout.");
DEFINE_int32(ub_connect_timeout, 1, "Ubshm connection timeout.");
DEFINE_int32(ub_hb_timer_interval, 1, "Heartbeat timer interval.");
DEFINE_int32(ub_hb_retry_cnt, 3, "Heartbeat retry times.");
DEFINE_int32(ub_event_queue_timer_interval, 100, "Interval of the disconnection timer.");

UBRing::UBRing()
{}
UBRing::~UBRing()
{}

RETURN_CODE UBRing::UbrTrxMapShm(SHM *local_shm, SHM *remote_shm)
{
    RETURN_CODE rc = UbrTrxMapLocalShm(local_shm);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Trx map local shared memory failed.";
        return rc;
    }
    rc = UbrTrxMapRemoteShm(remote_shm);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Trx map remote shared memory failed.";
        return rc;
    }
    return HLC_OK;
}

RETURN_CODE UBRing::UbrTrxClose() {
    if (UNLIKELY(UbrTrxCloseCheck(_trx) != HLC_OK)) {
        return HLC_ERR;
    }
    ((UbrEventQMsg *)_trx->ubr_rx.remote_tx_event_q.addr)->flag = UBR_STATE_CLOSING;

    uint32_t disconnect_timeout = FLAGS_ub_disconnect_timeout;
    uint64_t start_time = GetCurNanoSeconds();

    if (((UbrEventQMsg *)_trx->ubr_tx.local_tx_event_q.addr)->flag == UBR_STATE_CONNECTED) {
        ((UbrEventQMsg *)_trx->ubr_tx.local_tx_event_q.addr)->flag = UBR_STATE_CLOSED;
        _trx->ubr_tx.trx_state = UBR_STATE_CLOSED;
    }

    ((UbrEventQMsg *)_trx->ubr_tx.remote_rx_event_q.addr)->flag = UBR_STATE_CLOSED;
    while (((UbrEventQMsg *)_trx->ubr_rx.local_rx_event_q.addr)->flag != UBR_STATE_CLOSED) {
        UbrSetSleepTask(UBR_TASK_CLOSE);
        if (HasTimedOut(start_time, disconnect_timeout) != HLC_OK) {
            LOG(ERROR) << "Local shm " << _trx->local_shm.name
            << " wait for the peer to close the connection failed.";
            _trx->ubr_rx.trx_state = UBR_STATE_CLOSED;
            ClearTrxResource(_trx, start_time, UBR_SEND_CLOSE);
            return HLC_ERR_TIMEOUT;
        }
        usleep(1);
    }
    _trx->ubr_rx.trx_state = UBR_STATE_CLOSED;
    RETURN_CODE rc;
    if (UNLIKELY((rc = ClearTrxResource(_trx, start_time, UBR_SEND_CLOSE)) != HLC_OK)) {
        LOG(ERROR) << "Trx close, clear trx resource failed, trx local name=" << _trx->local_shm.name;
        return HLC_ERR;
    }
    LOG(INFO) << "The peer is closed, local name=" << _trx->local_shm.name;
    return HLC_OK;
}

RETURN_CODE UBRing::UbrAddCloseTimer() {
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "Trx add close timer failed, trx is null.";
        return HLC_ERR;
    }

    uint32_t event_q_timer_interval = FLAGS_ub_event_queue_timer_interval * TIME_COVERSION;
    struct itimerspec time_spec = {
            .it_interval = {.tv_sec = 0, .tv_nsec = event_q_timer_interval},
            .it_value = {.tv_sec = 0, .tv_nsec = 1}
    };
    int timer_fd = TimerStart(&time_spec, UbrTrxCloseCallback, (void*)_trx);
    if (UNLIKELY(timer_fd == -1)) {
        LOG(ERROR) << "Start ubr close timer failed, trx local name=" << _trx->local_shm.name;
        return HLC_ERR;
    }
    _trx->timer_fd = timer_fd;
    return HLC_OK;
}

RETURN_CODE UBRing::UbrAddTimer() {
    if (UNLIKELY(UbrAddCloseTimer() != HLC_OK)) {
        LOG(ERROR) << "Ubr " << _trx->local_shm.name << " add closed timer failed.";
        return HLC_ERR;
    }

    if (UNLIKELY(UbrAddHBTimer() != HLC_OK)) {
        DeleteTimerSafe((uint32_t)_trx->timer_fd);
        LOG(ERROR) << "Ubr " << _trx->local_shm.name << " add heartbeat timer failed.";
        return HLC_ERR;
    }
    return HLC_OK;
}

void* UBRing::UbrTrxCloseCallback(void* args) {
    auto* trx = (UbrTrx*) args;
    if (UNLIKELY(UBRing::UbrTrxCallbackCheck(trx) != HLC_OK)) {
        return nullptr;
    }

    auto* local_rx_event_q = (UbrEventQMsg *)trx->ubr_rx.local_rx_event_q.addr;
    auto* local_tx_event_q = (UbrEventQMsg *)trx->ubr_tx.local_tx_event_q.addr;
    if (local_rx_event_q->flag != UBR_STATE_CLOSED || local_tx_event_q->flag == UBR_STATE_CLOSED) {
        return nullptr;
    }
    trx->ubr_rx.trx_state = UBR_STATE_CLOSED;
    int fd = (int)trx->local_shm.fd;
    do {
        if (ATOMIC_LOAD(trx->close_cnt) == 0) {
            LOG(ERROR) << "Trx close callback failed, exist other closing call, name=" << trx->local_shm.name;
            break;
        }
        ATOMIC_SUB(trx->close_cnt, 1);

        uint64_t start_time = GetCurNanoSeconds();

        if (local_tx_event_q->flag == UBR_STATE_CONNECTED || ATOMIC_LOAD(trx->close_cnt) == 1) {
            local_tx_event_q->flag = UBR_STATE_CLOSED;
            trx->ubr_tx.trx_state = UBR_STATE_CLOSED;
        }
        UbrEventQMsg* remote_rx_event_q = (UbrEventQMsg *)trx->ubr_tx.remote_rx_event_q.addr;
        if (remote_rx_event_q == nullptr) {
            LOG(ERROR) << "Trx close callback failed, " << trx->local_shm.name << " remote_rx_event_q is NULL.";
            break;
        }
        remote_rx_event_q->flag = UBR_STATE_CLOSED;
        if (UNLIKELY(ClearTrxResource(trx, start_time, UBR_CALL_BACK_CLOSE, 1) != HLC_OK)) {
            LOG(ERROR) << "Trx close callback failed, " << trx->local_shm.name << " clear trx resource failed.";
            break;
        }
    } while (0);
    return nullptr;
}

RETURN_CODE UBRing::UbrAddHBTimer() {
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "Trx add heartbeat timer failed, trx is null.";
        return HLC_ERR;
    }

    struct itimerspec time_spec = {
            .it_interval = {.tv_sec = FLAGS_ub_hb_timer_interval, .tv_nsec = 0},
            .it_value = {.tv_sec = 0, .tv_nsec = 1}
    };
    int timer_fd = TimerStart(&time_spec, UbrTrxHBCallback, (void*)_trx);
    if (UNLIKELY(timer_fd == -1)) {
        LOG(ERROR) << "Start ubr heartbeat timer failed.";
        return HLC_ERR;
    }
    _trx->hb_timer_fd = timer_fd;
    return HLC_OK;
}

RETURN_CODE UBRing::UbrPassiveClearTrx(UbrTrx *trx, int fd, PASSIVE_DISC_TYPE type) {
    if (UNLIKELY(UbrTrxCloseCheck(trx) != HLC_OK)) {
        return HLC_ERR;
    }
    trx->ubr_tx.trx_state = UBR_STATE_CLOSED;
    trx->ubr_rx.trx_state = UBR_STATE_CLOSED;
    DeleteTimerSafe((uint32_t)trx->timer_fd);
    const char *type_name = NULL;
    if (type == UBR_HEARTBEAT) {
        DeleteTimer((uint32_t)trx->hb_timer_fd);
        type_name = "Trx heartbeat";
    } else if (type == UBR_UB_EVENT) {
        DeleteTimerSafe((uint32_t)trx->hb_timer_fd);
        type_name = "Ub event callback";
    }
    sleep(FLAGS_ub_flying_io_timeout);

    int rc = ShmLocalFree(&trx->remote_shm);
    if (rc != HLC_OK) {
        LOG(ERROR) << type_name << ", delete remote shm failed. ret=" << rc;
    }
    rc = ShmLocalFree(&trx->local_shm);
    if (rc != HLC_OK) {
        LOG(ERROR) << type_name << ", delete local shm failed. ret=" << rc;
    }

    UBRingManager::ReleaseUbrTrxFromMgr(trx);
    return HLC_OK;
}

void* UBRing::UbrTrxHBCallback(void* args) {
    auto* trx = (UbrTrx*) args;
    if (UNLIKELY(UbrTrxCallbackCheck(trx) != HLC_OK)) {
        return NULL;
    }

    auto* local_data_status = (UbrDataStatusQMsg *)trx->ubr_tx.local_data_status_q.addr;
    auto* remote_data_status = (UbrDataStatusQMsg *)trx->ubr_rx.remote_data_status_q.addr;
    if (UNLIKELY(local_data_status == NULL || remote_data_status == NULL)) {
        LOG(ERROR) << "Heartbeat error, datastatus is NULL.";
        return NULL;
    }

    if (trx->ubr_tx.trx_state != UBR_STATE_CONNECTED || trx->ubr_rx.trx_state != UBR_STATE_CONNECTED) {
        LOG_EVERY_SECOND(INFO) << "Heartbeat cannot be started, wait connected state.";
        return NULL;
    }

    remote_data_status->heart_beat = 1;
    if (local_data_status->heart_beat == 1) {
        local_data_status->heart_beat = 0;
        trx->ubr_tx.hb_retry_cnt = 0;
        return NULL;
    }

    ++trx->ubr_tx.hb_retry_cnt;
    if (trx->ubr_tx.hb_retry_cnt <= FLAGS_ub_hb_retry_cnt) {
        return NULL;
    }

    int fd = (int)trx->local_shm.fd;
    LOG(INFO) << "Hlc heartbeat, start to clear trx resource. hbTimerFd=" << fd << ", shmName=" << trx->local_shm.name;
    UbrPassiveClearTrx(trx, fd, UBR_HEARTBEAT);
    LOG(INFO) << "Hlc heartbeat clear trx resource finish.";
    return NULL;
}

RETURN_CODE UBRing::UbrAddAsynClearTimer(UbrTrx *trx) {
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Trx add close timer failed, trx is null.";
        return HLC_ERR;
    }

    struct itimerspec time_spec = {
            .it_interval = {.tv_sec = 0, .tv_nsec = 0},
            .it_value = {.tv_sec = FLAGS_ub_flying_io_timeout, .tv_nsec = 0}
    };

    int timer_fd = TimerStart(&time_spec, UbrAsynClearCallback, (void*)trx);
    if (UNLIKELY(timer_fd == -1)) {
        LOG(ERROR) << "Start ubr close timer failed, trx name=%s.", trx->local_shm.name;
        return HLC_ERR;
    }
    trx->clear_timer_fd = timer_fd;
    return HLC_OK;
}

void *UBRing::UbrAsynClearCallback(void *args)
{
    auto* trx = (UbrTrx*) args;
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Trx close, trx is null.";
        return NULL;
    }

    if (UNLIKELY(ShmRemoteFree(&trx->remote_shm) != HLC_OK)) {
        LOG(ERROR) << "Trx close, remote shm " << trx->remote_shm.name << " free failed.";
    }

    if (UNLIKELY(UbrTrxFreeShm(trx) != HLC_OK)) {
        LOG(ERROR) << "Trx close, wait for local shm " << trx->local_shm.name << " free fail.";
    }

    if (UNLIKELY(UBRingManager::ReleaseUbrTrxFromMgr(trx) != HLC_OK)) {
        LOG(ERROR) << "Trx close, release shm " << trx->local_shm.name << " trx failed.";
    }
    return NULL;
}

int UBRing::UbrTrxSend(const void *buf, uint32_t buf_len)
{
    if (UNLIKELY(CheckTrxSendPreCheck(_trx) != HLC_OK)) {
        return HLC_ERR;
    }
    // 1.2 计算空间
    auto *data_status_msg = (UbrDataStatusQMsg *)_trx->ubr_tx.local_data_status_q.addr;
    auto *data_msg = (UbrMsgFormat *)_trx->ubr_tx.remote_data_q.addr;
    uint32_t cap = _trx->ubr_tx.capacity;
    uint32_t tail = data_status_msg->tail;
    uint32_t remain_chunk_num =
        (_trx->ubr_tx.write_pos > tail) ? (tail + cap - _trx->ubr_tx.write_pos) : (tail - _trx->ubr_tx.write_pos);
    uint32_t need_msg_chunk_num = CalcUbrMsgChunkCnt(buf_len);
    if (remain_chunk_num < need_msg_chunk_num) {
        return HLC_RETRY;
    }
    UbrMsgFormat *msg = &(_trx->ubr_tx.local_msg_space);
    uint32_t total_send_len = 0;
    uint32_t remain_buf_len = buf_len;
    uint8_t is_last_pkt = 0;
    _trx->ubr_tx.out_io_id++;
    ((UbrEventQMsg *)_trx->ubr_tx.remote_rx_event_q.addr)->io_id = _trx->ubr_tx.out_io_id;
    while (remain_buf_len > 0) {
        is_last_pkt = (uint8_t)(remain_buf_len <= UBR_MSG_PAYLOAD_LEN);
        msg->header[UBR_MSG_FLAG_INDEX] = is_last_pkt ? UBR_MSG_CHUNK_EOF : UBR_MSG_CHUNK_EXIST;
        msg->header[UBR_MSG_LEN_INDEX] = is_last_pkt ? (uint8_t)remain_buf_len : UBR_MSG_PAYLOAD_LEN;
        msg->header[UBR_MSG_CUR_INDEX] = 0;
        memcpy(msg->payload.inner, (const uint8_t *)buf + total_send_len, msg->header[UBR_MSG_LEN_INDEX]);
        Copy64Byte((int8_t *)&data_msg[_trx->ubr_tx.write_pos], (int8_t *)msg);
        _trx->ubr_tx.write_pos = (_trx->ubr_tx.write_pos + 1) % cap;
        total_send_len += msg->header[UBR_MSG_LEN_INDEX];
        remain_buf_len -= msg->header[UBR_MSG_LEN_INDEX];
    }
    return (int)total_send_len;
}

int UBRing::UbrTrxRecv(void *buf, uint32_t buf_len)
{
    RETURN_CODE rc = HLC_OK;
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, buf, buf_len)) != HLC_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }
    UbrMsgFormat *data_msg = (UbrMsgFormat *)_trx->ubr_rx.local_data_q.addr;
    uint32_t read_pos_end = _trx->ubr_rx.read_pos;
    uint8_t flag = data_msg[read_pos_end].header[UBR_MSG_FLAG_INDEX];
    if (flag == UBR_MSG_CHUNK_NONE) {
        return HLC_RETRY;
    }
    return UbrTrxRecvBlockMode(static_cast<uint8_t *>(buf), buf_len);
}

int UBRing::UbrTrxRecvBlockMode(uint8_t *dest, uint32_t buf_len)
{
    RETURN_CODE rc = HLC_OK;
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, dest, buf_len)) != HLC_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }

    int32_t total_copied = 0;
    int32_t remaining_len = (int32_t)buf_len;
    bool not_eof_encountered = true;

    UbrRx *ubr_rx = &_trx->ubr_rx;
    UbrMsgFormat *data_msg = (UbrMsgFormat *)ubr_rx->local_data_q.addr;
    bool need_update_epoll_eof_pos = ubr_rx->read_pos == ubr_rx->ep_eof_pos;

    while (not_eof_encountered && remaining_len > 0) {
        if (UNLIKELY(CheckTrxRecvPreCheck(_trx) != HLC_OK)) {
            return HLC_ERR;
        }
        UbrMsgFormat *current_chunk = &data_msg[ubr_rx->read_pos];
        uint8_t flag = current_chunk->header[UBR_MSG_FLAG_INDEX];
        if (flag == UBR_MSG_CHUNK_NONE) {
            continue;
        }
        if (flag == UBR_MSG_CHUNK_EOF) {
            not_eof_encountered = false;
        }
        uint8_t chunk_msg_len = current_chunk->header[UBR_MSG_LEN_INDEX];
        uint8_t cur_index = current_chunk->header[UBR_MSG_CUR_INDEX];
        uint8_t available_data = chunk_msg_len - cur_index;

        int32_t copy_len = (remaining_len < available_data) ? remaining_len : available_data;
        memcpy(dest + total_copied, data_msg[ubr_rx->read_pos].payload.inner + cur_index, (size_t)copy_len);
        total_copied += copy_len;
        remaining_len -= copy_len;
        current_chunk->header[UBR_MSG_CUR_INDEX] += (uint8_t)copy_len;
        if (LIKELY(current_chunk->header[UBR_MSG_CUR_INDEX] == chunk_msg_len)) {
            current_chunk->header[UBR_MSG_FLAG_INDEX] = UBR_MSG_CHUNK_NONE;
            UpdateDataQTail(_trx);
            ubr_rx->read_pos = (ubr_rx->read_pos + 1) % ubr_rx->capacity;
        }
    }
    if (need_update_epoll_eof_pos) {
        ubr_rx->ep_eof_pos = ubr_rx->read_pos;
    }
    return (int)total_copied;
}

ssize_t UBRing::UbrTrxWritev(const struct iovec *iov, int iovcnt)
{
    if (UNLIKELY(CheckTrxSendPreCheck(_trx) != HLC_OK)) {
        return HLC_ERR;
    }

    size_t buf_len = 0;
    for (int i = 0; i < iovcnt; i++) {
        buf_len += iov[i].iov_len;
    }
    RETURN_CODE rc = WritevHasEnoughSpace(buf_len);
    if (rc != HLC_OK) {
        return rc;
    }

    UbrMsgFormat *data_msg = (UbrMsgFormat *)_trx->ubr_tx.remote_data_q.addr;
    UbrMsgFormat *msg = &(_trx->ubr_tx.local_msg_space);
    int cur_iov = 0;
    size_t cur_iov_pos = 0;
    ssize_t total_send_len = 0;
    size_t pkt_remain_n = 0;
    size_t iov_remain = 0;
    size_t fulled = 0;
    uint8_t is_last_pkt = 0;
    uint8_t cur_pkt_len = 0;
    _trx->ubr_tx.out_io_id++;
    ((UbrEventQMsg *)_trx->ubr_tx.remote_rx_event_q.addr)->io_id = _trx->ubr_tx.out_io_id;
    while (buf_len > 0) {
        is_last_pkt = (uint8_t)(buf_len <= UBR_MSG_PAYLOAD_LEN);
        cur_pkt_len = is_last_pkt ? (uint8_t)buf_len : UBR_MSG_PAYLOAD_LEN;
        msg->header[UBR_MSG_FLAG_INDEX] = is_last_pkt ? UBR_MSG_CHUNK_EOF : UBR_MSG_CHUNK_EXIST;
        msg->header[UBR_MSG_LEN_INDEX] = cur_pkt_len;
        msg->header[UBR_MSG_CUR_INDEX] = 0;
        pkt_remain_n = cur_pkt_len;
        while (cur_iov < iovcnt && pkt_remain_n > 0) {
            iov_remain = (iov[cur_iov].iov_len - cur_iov_pos);
            fulled = iov_remain > pkt_remain_n ? pkt_remain_n : iov_remain;
            memcpy((msg->payload.inner + (cur_pkt_len - (uint8_t)pkt_remain_n)),
                (uint8_t *)(iov[cur_iov].iov_base) + cur_iov_pos,
                fulled);
            pkt_remain_n -= fulled;
            cur_iov_pos += fulled;
            if (cur_iov_pos == iov[cur_iov].iov_len) {
                cur_iov++;
                cur_iov_pos = 0;
            }
        }

        Copy64Byte((int8_t *)&data_msg[_trx->ubr_tx.write_pos], (int8_t *)msg);
        _trx->ubr_tx.write_pos = (_trx->ubr_tx.write_pos + 1) % _trx->ubr_tx.capacity;
        total_send_len += (ssize_t)cur_pkt_len;
        buf_len -= (int)cur_pkt_len;
    }
    return total_send_len;
}

ssize_t UBRing::UbrTrxReadv(const struct iovec *iov, int iovcnt)
{
    RETURN_CODE rc = HLC_OK;
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, iov, (uint32_t)iovcnt)) != HLC_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }
    UbrMsgFormat *data_msg = (UbrMsgFormat *)_trx->ubr_rx.local_data_q.addr;
    uint32_t read_pos_end = _trx->ubr_rx.read_pos;
    uint8_t flag = data_msg[read_pos_end].header[UBR_MSG_FLAG_INDEX];
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
    RETURN_CODE rc = HLC_OK;
    if (UNLIKELY((rc = CheckTrxRecvParam(_trx, iov, (uint32_t)iovcnt)) != HLC_OK)) {
        return (rc == UBR_NOT_CONNECTED) ? 0 : rc;
    }

    size_t remain_buf_len = 0;
    for (int i = 0; i < iovcnt; i++) {
        remain_buf_len += iov[i].iov_len;
    }

    bool need_update_epoll_eof_pos = _trx->ubr_rx.read_pos == _trx->ubr_rx.ep_eof_pos;
    ssize_t total_recv_len = StartReadv(_trx, iov, iovcnt, remain_buf_len);

    if (need_update_epoll_eof_pos) {
        _trx->ubr_rx.ep_eof_pos = _trx->ubr_rx.read_pos;
    }
    return total_recv_len;
}

RETURN_CODE UBRing::IsUbrTrxReadable(uint32_t ep_event)
{
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "The trx to be checked is NULL.";
        return HLC_ERR;
    }
    if (UNLIKELY(_trx->local_shm.addr == NULL)) {
        LOG(ERROR) << "The trx local_shm to be checked is NULL.";
        return HLC_ERR;
    }
    if (UNLIKELY(_trx->ubr_tx.trx_state != UBR_STATE_CONNECTED)) {
        // TODO mwj 这几块的日志是否需要删除
        // LOG(ERROR) << "The trx is not connected state.";
        return HLC_ERR;
    }

    uint64_t io_id = ((UbrEventQMsg *)_trx->ubr_rx.local_rx_event_q.addr)->io_id;
    if ((ep_event & EPOLLET) && io_id == _trx->ubr_rx.in_io_id) {
        return MPA_MUXER_NOT_READY;
    }

    uint32_t read_pos_end = _trx->ubr_rx.read_pos;
    if (ep_event & EPOLLET) {
        read_pos_end = _trx->ubr_rx.ep_eof_pos;
    }

    UbrMsgFormat *data_msg = (UbrMsgFormat *)_trx->ubr_rx.local_data_q.addr;
    uint8_t flag = data_msg[read_pos_end].header[UBR_MSG_FLAG_INDEX];
    if (flag == UBR_MSG_CHUNK_NONE) {
        return MPA_MUXER_NOT_READY;
    }
    if (ep_event & EPOLLET) {
        _trx->ubr_rx.in_io_id = io_id;
    }
    return HLC_OK;
}

RETURN_CODE UBRing::IsUbrTrxWriteable(uint32_t ep_event)
{
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "The trx to be checked is NULL.";
        return HLC_ERR;
    }
    if (UNLIKELY(_trx->local_shm.addr == NULL)) {
        LOG(ERROR) << "The trx local_shm to be checked is NULL.";
        return HLC_ERR;
    }
    if (UNLIKELY((UbrEventQMsg *)_trx->ubr_tx.local_tx_event_q.addr == NULL)) {
        LOG(ERROR) << "The trx local_tx_event_q addr is NULL.";
        return HLC_ERR;
    }
    if (UNLIKELY((UbrEventQMsg *)_trx->ubr_tx.local_data_status_q.addr == NULL)) {
        LOG(ERROR) << "The trx local_data_status_q addr is NULL.";
        return HLC_ERR;
    }

    if (UNLIKELY(_trx->ubr_tx.trx_state != UBR_STATE_CONNECTED)) {
        LOG(ERROR) << "The trx is not connected state.";
        return HLC_ERR;
    }

    UbrDataStatusQMsg *data_status_msg = (UbrDataStatusQMsg *)_trx->ubr_tx.local_data_status_q.addr;
    uint32_t cap = _trx->ubr_tx.capacity;
    uint32_t tail = data_status_msg->tail;
    uint32_t remain_chunk_num =
        (_trx->ubr_tx.write_pos > tail) ? (tail + cap - _trx->ubr_tx.write_pos) : (tail - _trx->ubr_tx.write_pos);
    if (remain_chunk_num == 0) {
        _trx->ubr_tx.ep_last_cap = remain_chunk_num;
        return MPA_MUXER_NOT_READY;
    }

    if ((ep_event & EPOLLET) && (_trx->ubr_tx.ep_last_cap >= remain_chunk_num)) {
        _trx->ubr_tx.ep_last_cap = remain_chunk_num;
        return MPA_MUXER_NOT_READY;
    }
    _trx->ubr_tx.ep_last_cap = remain_chunk_num;
    return HLC_OK;
}

RETURN_CODE UBRing::UbrSetTimeout(UbrTaskStep task_type, int timeout)
{
    if (task_type >= UBR_TASK_STEP_NUM || timeout < 0) {
        LOG(ERROR) << "Set timeout failed, invalid task type.";
        return HLC_ERR;
    }

    g_sleep_time[task_type] = (uint32_t)timeout;
    LOG(INFO) << "Set timeout success, task_type=" << task_type << ", timeout=" << timeout;
    return HLC_OK;
}

RETURN_CODE UBRing::UbrTrxFreeShm(UbrTrx *trx)
{
    if (trx == NULL) {
        LOG(ERROR) << "Trx is NULL.";
        return HLC_ERR;
    }

    RETURN_CODE rc = HLC_OK;
    rc = ShmMunmap(&trx->local_shm);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Trx close, local unmap " << trx->local_shm.name << " shm fail.";
        return HLC_ERR;
    }

    rc = ShmFree(&trx->local_shm);
    if (UNLIKELY(rc != HLC_OK)) {
        if (UNLIKELY(rc == SHM_ERR_RESOURCE_ATTACHED || rc == SHM_ERR_NOT_FOUND)) {
            LOG(INFO) << "Wait for " << trx->remote_shm.name << " remote free shm.";
            return HLC_OK;
        }
        LOG(ERROR) << "Wait for " << trx->local_shm.name << " local shm free fail.";
        return HLC_ERR;
    }

    size_t name_len = strlen(trx->remote_shm.name);
    if (!(name_len <= 0 || name_len > SHM_MAX_NAME_LEN || trx->remote_shm.len <= 0)) {
        rc = ShmFree(&trx->remote_shm);
    }
    if (rc != HLC_OK) {
        if (rc == SHM_ERR_RESOURCE_ATTACHED || rc == SHM_ERR_NOT_FOUND) {
            LOG(INFO) << "Wait for " << trx->remote_shm.name << " remote free shm.";
            return HLC_OK;
        }
        LOG(ERROR) << "Wait for " << trx->remote_shm.name << " remote shm free fail.";
        return HLC_ERR;
    }

    return HLC_OK;
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
    PreWriteAddr(tx->remote_data_q.addr, tx->capacity * sizeof(UbrMsgFormat));
}

void UBRing::PrewriteUbrRx(UbrRx *rx)
{
    if (rx == NULL) {
        return;
    }
    PreWriteAddr(rx->local_data_q.addr, rx->capacity * sizeof(UbrMsgFormat));
}

RETURN_CODE UBRing::UbrTrxMapLocalShm(SHM *local_shm)
{
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null.";
        return HLC_ERR;
    }
    if (UNLIKELY(local_shm == NULL)) {
        LOG(ERROR) << "Trx map Shared memory failed, local_shm is null.";
        return HLC_ERR;
    }
    _trx->local_shm = *local_shm;
    _trx->ubr_tx.local_tx_event_q.addr = local_shm->addr + TX_EVENTQ_ADDR_OFFSET;
    _trx->ubr_tx.local_tx_event_q.len = UBR_EVENTQ_LEN;
    _trx->ubr_rx.local_rx_event_q.addr = local_shm->addr + RX_EVENTQ_ADDR_OFFSET;
    _trx->ubr_rx.local_rx_event_q.len = UBR_EVENTQ_LEN;
    _trx->ubr_tx.local_data_status_q.addr = local_shm->addr + DATASTATUSQ_ADDR_OFFSET;
    _trx->ubr_tx.local_data_status_q.len = UBR_DATASTATUSQ_LEN;
    size_t addr_aligned_offset = Aligned64Offset(local_shm->addr + DATAQ_ADDR_OFFSET);
    LOG(DEBUG) << "UbrRx's local_data_q address will aligned with offset=" << addr_aligned_offset;
    _trx->ubr_rx.local_data_q.addr = local_shm->addr + DATAQ_ADDR_OFFSET + addr_aligned_offset;
    _trx->ubr_rx.local_data_q.len = local_shm->len - DATAQ_ADDR_OFFSET - addr_aligned_offset;
    return HLC_OK;
}

RETURN_CODE UBRing::UbrTrxMapRemoteShm(SHM *remote_shm)
{
    if (UNLIKELY(_trx == NULL)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null.";
        return HLC_ERR;
    }
    if (UNLIKELY(remote_shm == NULL)) {
        LOG(ERROR) << "Trx map Shared memory failed, remote_shm is null.";
        return HLC_ERR;
    }
    _trx->remote_shm = *remote_shm;
    _trx->ubr_rx.remote_tx_event_q.addr = remote_shm->addr + TX_EVENTQ_ADDR_OFFSET;
    _trx->ubr_rx.remote_tx_event_q.len = UBR_EVENTQ_LEN;
    _trx->ubr_tx.remote_rx_event_q.addr = remote_shm->addr + RX_EVENTQ_ADDR_OFFSET;
    _trx->ubr_tx.remote_rx_event_q.len = UBR_EVENTQ_LEN;
    _trx->ubr_rx.remote_data_status_q.addr = remote_shm->addr + DATASTATUSQ_ADDR_OFFSET;
    _trx->ubr_rx.remote_data_status_q.len = UBR_DATASTATUSQ_LEN;
    size_t addr_aligned_offset = Aligned64Offset(remote_shm->addr + DATAQ_ADDR_OFFSET);
    LOG(DEBUG) << "UbrTx's remote_data_q will aligned with offset=" << addr_aligned_offset;
    _trx->ubr_tx.remote_data_q.addr = remote_shm->addr + DATAQ_ADDR_OFFSET + addr_aligned_offset;
    _trx->ubr_tx.remote_data_q.len = remote_shm->len - DATAQ_ADDR_OFFSET - addr_aligned_offset;
    return HLC_OK;
}

RETURN_CODE UBRing::UbrServerTrxInit(SHM *local_shm, SHM *remote_shm)
{
    RETURN_CODE rc = UbrTrxMapShm(local_shm, remote_shm);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) <<"Trx map shared memory failed.";
        return rc;
    }

    uint32_t local_data_msg_cap = (uint32_t)(_trx->ubr_rx.local_data_q.len / UBR_MSG_LEN);
    uint32_t remote_data_msg_cap = (uint32_t)(_trx->ubr_tx.remote_data_q.len / UBR_MSG_LEN);
    _trx->ubr_rx.capacity = local_data_msg_cap;
    _trx->ubr_tx.capacity = remote_data_msg_cap;
    rc = UBRingManager::GetHlcDealMsgMaxCnt(_trx->ubr_rx.capacity, &_trx->ubr_rx.deal_msg_max_cnt);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Get hlc deal msg max cnt.";
        return rc;
    }
    PrewriteUbrRx(&_trx->ubr_rx);
    PrewriteUbrTx(&_trx->ubr_tx);

    ((UbrDataStatusQMsg *)(_trx->ubr_tx.local_data_status_q.addr))->tail = remote_data_msg_cap - 1;
    ((UbrDataStatusQMsg *)(_trx->ubr_rx.remote_data_status_q.addr))->tail = local_data_msg_cap - 1;

    if (UNLIKELY(UbrAddTimer() != HLC_OK)) {
        LOG(ERROR) << "Ubr add timer failed, localName=" << local_shm->name;
        return HLC_ERR;
    }

    ((UbrDataStatusQMsg *)(_trx->ubr_tx.local_data_status_q.addr))->timeout = FLAGS_ub_connect_timeout;
    ((UbrDataStatusQMsg *)(_trx->ubr_rx.remote_data_status_q.addr))->timeout = FLAGS_ub_connect_timeout;

    ((UbrEventQMsg *)_trx->ubr_tx.remote_rx_event_q.addr)->flag = UBR_STATE_CONNECTED;
    ((UbrEventQMsg *)_trx->ubr_rx.local_rx_event_q.addr)->flag = UBR_STATE_CONNECTED;
    _trx->ubr_tx.trx_state = UBR_STATE_CONNECTED;
    _trx->ubr_rx.trx_state = UBR_STATE_CONNECTED;
    return HLC_OK;
}

int UBRing::UbrAllocateServerShm(SHM* remote_trx_shm, SHM* local_trx_shm) {
    UbrSetSleepTask(UBR_TASK_ACCEPT_MAP_FRONT);
    if (UNLIKELY((ShmRemoteMalloc(remote_trx_shm)) != HLC_OK)) {
        LOG(ERROR) << "Trx apply remote shared memory failed.";
        return -1;
    }

    if (UNLIKELY((ShmLocalCalloc(local_trx_shm)) != HLC_OK)) {
        LOG(ERROR) << "Trx apply local shared memory failed.";
        return -1;
    }

    UbrTrx **ubr_trx_ptr = &_trx;
    if (UNLIKELY((UBRingManager::AcquireUbrTrxFromMgr(ubr_trx_ptr)) != HLC_OK)) {
        LOG(ERROR) << "Acquire ubrtrx failed.";
        ShmRemoteFree(remote_trx_shm);
        ShmLocalFree(local_trx_shm);
        return -1;
    }
    _trx->type = TCP_TRX;
    if (UNLIKELY((UbrServerTrxInit(local_trx_shm, remote_trx_shm)) != HLC_OK)) {
        LOG(ERROR) << "Server trx init failed.";
        ShmRemoteFree(remote_trx_shm);
        UbrTrxFreeShm(_trx);
        UBRingManager::ReleaseUbrTrxFromMgr(_trx);
        return -1;
    }
    return 0;
}

int UBRing::UbrAllocateLocalShm(SHM *local_trx_shm, const char *shm_name)
{
    if (UNLIKELY((UBRingManager::AcquireUbrTrxFromMgr(&(_trx))) != HLC_OK)) {
        LOG(ERROR) << "Acquire ubrtrx failed, localName=" << shm_name;
        return -1;
    }

    _trx->type = TCP_TRX;
    if (UNLIKELY((ApplyAndMapLocalShm(local_trx_shm, shm_name)) != HLC_OK)) {
        LOG(ERROR) << "Trx apply or map local shared memory failed, localName=" << shm_name;
        return -1;
    }
    return 0;
}

int UBRing::UbrMapRemoteShm(SHM *local_trx_shm, const char *local_name)
{
    RETURN_CODE rc = UbrMapRemoteShmAddTimer(local_trx_shm, local_name);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Connect Trx failed, local shm name=" << local_trx_shm->name;
        return -1;
    }
    PrewriteUbrRx(&_trx->ubr_rx);
    PrewriteUbrTx(&_trx->ubr_tx);
    ((UbrEventQMsg *)_trx->ubr_rx.remote_tx_event_q.addr)->flag = UBR_STATE_CONNECTED;
    ((UbrEventQMsg *)_trx->ubr_rx.local_rx_event_q.addr)->flag = UBR_STATE_CONNECTED;
    _trx->ubr_tx.trx_state = UBR_STATE_CONNECTED;
    _trx->ubr_rx.trx_state = UBR_STATE_CONNECTED;
    return 0;
}

RETURN_CODE UBRing::UbrMapRemoteShmAddTimer(SHM *local_trx_shm, const char *local_name)
{
    uint64_t start_time = GetCurNanoSeconds();

    size_t remote_server_len = UBR_MSG_LEN * (((UbrDataStatusQMsg *)(_trx->ubr_tx.local_data_status_q.addr))->tail + 1) +
                             UBR_MSG_LEN * ((DATAQ_ADDR_OFFSET / UBR_MSG_LEN) + 1);
    SHM remote_trx_shm = {NULL, remote_server_len, 0, {0}, local_trx_shm->fd};
    int result = snprintf(remote_trx_shm.name,
        SHM_MAX_NAME_BUFF_LEN,
        "%s_%s_%s",
        SHM_NAME_PREFIX,
        local_name,
        SERVER_SHM_NAME_SUFFIX);
    if (UNLIKELY(result < 0)) {
        LOG(ERROR) << "Copy server shared memory name failed, localName=%s, ret=%d.", local_name, result;
        return HLC_ERR;
    }
    UbrSetSleepTask(UBR_TASK_CONNECT_MAP_FRONT);
    RETURN_CODE rc = ApplyAndMapRemoteShm(&remote_trx_shm);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Connect Trx map shared memory failed, remote shm=" << remote_trx_shm.name;
        return rc;
    }

    if (UNLIKELY(UbrAddTimer() != HLC_OK)) {
        LOG(ERROR) << "Ubr add timer failed, localName=" << local_name;
        ShmRemoteFree(&remote_trx_shm);
        return HLC_ERR;
    }

    UbrSetSleepTask(UBR_TASK_CONNECT_MAP_AFTER);

    uint32_t timeout = ((UbrDataStatusQMsg *)(_trx->ubr_tx.local_data_status_q.addr))->timeout;
    if (HasTimedOut(start_time, timeout) != HLC_OK) {
        LOG(ERROR) << "Local shm " << local_trx_shm->name << " wait for connect remote map timeout.";
        DeleteTimerSafe((uint32_t)_trx->hb_timer_fd);
        DeleteTimerSafe((uint32_t)_trx->timer_fd);
        ShmRemoteFree(&remote_trx_shm);
        return HLC_ERR_TIMEOUT;
    }

    return HLC_OK;
}

RETURN_CODE UBRing::ApplyAndMapLocalShm(SHM *local_trx_shm, const char *local_name)
{
    if (UNLIKELY(_trx == NULL || local_trx_shm == NULL)) {
        LOG(ERROR) << "Trx map Shared memory failed, trx is null, localName=" << local_name;
        return HLC_ERR;
    }
    int result = snprintf(local_trx_shm->name,
        SHM_MAX_NAME_BUFF_LEN,
        "%s_%s_%s",
        SHM_NAME_PREFIX,
        local_name,
        CLIENT_SHM_NAME_SUFFIX);
    if (UNLIKELY(result < 0)) {
        LOG(ERROR) << "Copy client localTrx shared memory name failed, localName=" << local_name << ", ret=" << result;
        return HLC_ERR;
    }

    RETURN_CODE rc = ShmLocalCalloc(local_trx_shm);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Trx apply local shared memory failed, local shm name=" << local_trx_shm->name;
        if (rc == SHM_ERR_EXIST || rc == SHM_ERR_NOT_FOUND) {
            rc = UBR_ERR_ADDR_IN_USE;
        }
        UBRingManager::ReleaseUbrTrxFromMgr(_trx);
        return rc;
    }
    rc = UbrTrxMapLocalShm(local_trx_shm);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Trx map local shared memory failed, local shm name=" << local_trx_shm->name;
        ShmLocalFree(local_trx_shm);
        UBRingManager::ReleaseUbrTrxFromMgr(_trx);
        return rc;
    }
    ((UbrDataStatusQMsg *)_trx->ubr_tx.local_data_status_q.addr)->timeout = FLAGS_ub_connect_timeout;
    _trx->ubr_rx.capacity = (uint32_t)(_trx->ubr_rx.local_data_q.len / UBR_MSG_LEN);
    rc = UBRingManager::GetHlcDealMsgMaxCnt(_trx->ubr_rx.capacity, &_trx->ubr_rx.deal_msg_max_cnt);
    if (rc != HLC_OK) {
        LOG(ERROR) << "Get hlc deal msg max cnt, local shm name=" << local_trx_shm->name;
        ShmLocalFree(local_trx_shm);
        UBRingManager::ReleaseUbrTrxFromMgr(_trx);
        return rc;
    }
    return HLC_OK;
}

RETURN_CODE UBRing::ApplyAndMapRemoteShm(SHM *remote_trx_shm)
{
    RETURN_CODE rc = ShmRemoteMalloc(remote_trx_shm);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Trx apply remote shared memory failed.";
        return rc;
    }
    rc = UbrTrxMapRemoteShm(remote_trx_shm);
    if (UNLIKELY(rc != HLC_OK)) {
        LOG(ERROR) << "Trx map shared memory failed.";
        ShmRemoteFree(remote_trx_shm);
        return rc;
    }
    _trx->ubr_tx.capacity = (uint32_t)(_trx->ubr_tx.remote_data_q.len / UBR_MSG_LEN);
    return HLC_OK;
}

RETURN_CODE UBRing::WritevHasEnoughSpace(size_t buf_len)
{
    UbrDataStatusQMsg *data_status_msg = (UbrDataStatusQMsg *)_trx->ubr_tx.local_data_status_q.addr;
    uint32_t cap = _trx->ubr_tx.capacity;
    uint32_t tail = data_status_msg->tail;
    uint32_t remain_chunk_num =
        (_trx->ubr_tx.write_pos > tail) ? (tail + cap - _trx->ubr_tx.write_pos) : (tail - _trx->ubr_tx.write_pos);
    uint32_t need_msg_chunk_num = CalcUbrMsgChunkCnt((uint32_t)buf_len);
    if (remain_chunk_num < need_msg_chunk_num) {
        return HLC_RETRY;
    }
    return HLC_OK;
}

RETURN_CODE UBRing::UbrClearResourceCheck(UbrTrx *trx, uint64_t start_time, UbrCloseType close_type)
{
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Trx close failed, trx is null.";
        return HLC_ERR;
    }

    UbrEventQMsg *local_tx_event_q = (UbrEventQMsg *)trx->ubr_tx.local_tx_event_q.addr;
    while (ATOMIC_LOAD(trx->close_cnt) == 1 && local_tx_event_q->flag == UBR_STATE_CLOSING) {
        if (HasTimedOut(start_time, FLAGS_ub_disconnect_timeout) != HLC_OK) {
            LOG(ERROR) << "Trx close failed, wait close time out.";
            break;
        }
        usleep(1);
    }
    int first_clear_expected = UBR_CLOSE_FIRST;
    int second_clear_expected = UBR_CLOSE_SECOND;
    if (local_tx_event_q->flag == UBR_STATE_CLOSING) {
        if (ATOMIC_COMPARE_EXCHANGE_STRONG(trx->close_state, first_clear_expected, UBR_CLOSE_SECOND)) {
            LOG(ERROR) << "Trx close, exist process is closing, name=" << trx->local_shm.name;
            return HLC_REENTRY;
        } else if (ATOMIC_COMPARE_EXCHANGE_STRONG(trx->close_state, second_clear_expected, UBR_CLOSE_END)) {
            local_tx_event_q->flag = UBR_STATE_CLOSED;
            trx->ubr_tx.trx_state = UBR_STATE_CLOSED;
        }
    }

    if (close_type == UBR_SEND_CLOSE) {
        DeleteTimerSafe((uint32_t)trx->timer_fd);
    } else {
        DeleteTimer((uint32_t)trx->timer_fd);
    }
    DeleteTimerSafe((uint32_t)trx->hb_timer_fd);
    return HLC_OK;
}

RETURN_CODE UBRing::ClearTrxResource(UbrTrx *trx, uint64_t start_time, UbrCloseType close_type, int op)
{
    UbrEventQMsg *local_tx_event_q = (UbrEventQMsg *)trx->ubr_tx.local_tx_event_q.addr;
    RETURN_CODE rc = UbrClearResourceCheck(trx, start_time, close_type);
    if (rc != HLC_OK) {
        return rc;
    }

    rc = UbrAddAsynClearTimer(trx);
    if (rc != HLC_OK) {
        LOG(ERROR) << "Trx close, add " << trx->local_shm.name << " close clear timer failed.";
        return HLC_ERR;
    }

    return HLC_OK;
}

RETURN_CODE UBRing::UbrTrxCloseCheck(UbrTrx *trx)
{
    if (UNLIKELY(trx == NULL)) {
        LOG(ERROR) << "Trx close failed, client trx is null.";
        return HLC_ERR;
    }
    int expected = MAX_CLOSE_COUNT;
    if (!ATOMIC_COMPARE_EXCHANGE_STRONG(trx->close_cnt, expected, MAX_CLOSE_COUNT - 1)) {
        LOG(ERROR) << "Trx close failed, exist other close acquire, trx local name=" << trx->local_shm.name;
        return HLC_ERR;
    }

    if (UNLIKELY(trx->ubr_tx.local_tx_event_q.addr == NULL)) {
        LOG(ERROR) << "Trx close failed, local_tx_event_q addr is NULL, trx local name=" << trx->local_shm.name;
        return HLC_ERR;
    }
    return HLC_OK;
}

ssize_t UBRing::StartReadv(UbrTrx *trx, const struct iovec *iov, int iovcnt, size_t remain_buf_len)
{
    ssize_t total_recv_len = 0;
    int iov_index = 0;
    size_t iov_pos = 0;
    UbrMsgFormat *data_msg = (UbrMsgFormat *)trx->ubr_rx.local_data_q.addr;
    bool not_eof_encountered = true;
    while (not_eof_encountered && remain_buf_len > 0) {
        if (UNLIKELY(CheckTrxRecvPreCheck(trx) != HLC_OK)) {
            return HLC_ERR;
        }
        UbrMsgFormat *current_chunk = &data_msg[trx->ubr_rx.read_pos];
        uint8_t flag = current_chunk->header[UBR_MSG_FLAG_INDEX];
        if (flag == UBR_MSG_CHUNK_NONE) {
            continue;
        }
        if (flag == UBR_MSG_CHUNK_EOF) {
            not_eof_encountered = false;
        }
        uint8_t chunk_msg_len = current_chunk->header[UBR_MSG_LEN_INDEX];
        uint8_t cur_index = current_chunk->header[UBR_MSG_CUR_INDEX];
        uint8_t recv_len =
            remain_buf_len > (size_t)(chunk_msg_len - cur_index) ? (chunk_msg_len - cur_index) : (uint8_t)remain_buf_len;
        while (iov_index < iovcnt && recv_len > 0) {
            size_t copy_len =
                recv_len > (iov[iov_index].iov_len - iov_pos) ? iov[iov_index].iov_len - iov_pos : (size_t)recv_len;
            memcpy((uint8_t *)iov[iov_index].iov_base + iov_pos, current_chunk->payload.inner + cur_index, copy_len);
            recv_len -= (uint8_t)copy_len;
            iov_pos += copy_len;
            cur_index += (uint8_t)copy_len;
            if (iov_pos == iov[iov_index].iov_len) {
                iov_index++;
                iov_pos = 0;
            }
            remain_buf_len -= copy_len;
            total_recv_len += (ssize_t)copy_len;
        }
        current_chunk->header[UBR_MSG_CUR_INDEX] = cur_index;
        if (current_chunk->header[UBR_MSG_CUR_INDEX] == chunk_msg_len) {
            current_chunk->header[UBR_MSG_FLAG_INDEX] = UBR_MSG_CHUNK_NONE;
            UpdateDataQTail(trx);
            trx->ubr_rx.read_pos = (trx->ubr_rx.read_pos + 1) % trx->ubr_rx.capacity;
        }
    }
    return total_recv_len;
}
}  // namespace ub
}  // namespace brpc