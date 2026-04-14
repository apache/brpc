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

#ifndef BRPC_UBR_TRX_H
#define BRPC_UBR_TRX_H
#include <stdint.h>
#include <stdlib.h>
#include <sys/uio.h>
#include "brpc/ub/shm/shm_def.h"
#include "brpc/ub/common/common.h"
#include "brpc/ub/common/thread_lock.h"
#include "brpc/ub/ubr_msg.h"

/* +----------------------------------------------------------------------------+
   │                                 UbrTrx shm                                 │
   +-------------+-------------+-------------+---------------+------------------+
   │  TxEventQ   │  RxEventQ   │ DataStatusQ │ zero(44Bytes) |     DataQ        │
   +-------------+-------------+-------------+---------------+------------------+ */

#define UBR_EVENTQ_LEN sizeof(UbrEventQMsg)
#define UBR_DATASTATUSQ_LEN sizeof(UbrDataStatusQMsg)

#define TX_EVENTQ_ADDR_OFFSET 0
#define RX_EVENTQ_ADDR_OFFSET UBR_EVENTQ_LEN
#define DATASTATUSQ_ADDR_OFFSET ((UBR_EVENTQ_LEN) << 1)
#define DATAQ_ADDR_OFFSET (DATASTATUSQ_ADDR_OFFSET + UBR_DATASTATUSQ_LEN)
#define MB_TO_BYTE (1024 * 1024)
#define MAX_CLOSE_COUNT 2

#define SHM_NAME_PREFIX "HLC"
#define SERVER_SHM_NAME_SUFFIX "S"
#define CLIENT_SHM_NAME_SUFFIX "C"

namespace brpc {
namespace ub {
extern RETURN_CODE(*g_before_tcp_close)(int);
extern RETURN_CODE(*g_after_tcp_close)(int);

typedef enum {
    UBR_STATE_NONE,
    UBR_STATE_CONNECTED,
    UBR_STATE_CLOSING,
    UBR_STATE_CLOSED
} EventQState;

typedef enum {
    UBR_SEND_CLOSE,
    UBR_CALL_BACK_CLOSE
} UbrCloseType;

typedef enum {
    UBR_CLOSE_FIRST,
    UBR_CLOSE_SECOND,
    UBR_CLOSE_END
} UbrCloseCount;

typedef enum {
    UDP_TRX,
    TCP_TRX,
    UBR_TRX
} UbrTrxType;

typedef enum {
    UBR_TASK_CONNECT_MAP_FRONT,
    UBR_TASK_CONNECT_MAP_AFTER,
    UBR_TASK_ACCEPT_MAP_FRONT,
    UBR_TASK_ACCEPT_MAP_AFTER,
    UBR_TASK_CLOSE,
    UBR_TASK_STEP_NUM
} UbrTaskStep;

typedef struct TagUbrDataStatusQMsg {
    uint32_t tail;
    uint32_t timeout;
    uint8_t heart_beat;
} UbrDataStatusQMsg;

typedef struct TagUbrEventQMsg {
    uint64_t io_id;
    EventQState flag;
} UbrEventQMsg;

typedef struct TagUbrAddrInfo {
    uint8_t *addr;
    size_t len;
} UbrAddrInfo;

typedef struct TagUbrTx {
    UbrAddrInfo remote_data_q;
    UbrAddrInfo remote_rx_event_q;
    UbrAddrInfo local_data_status_q;
    UbrAddrInfo local_tx_event_q;
    uint64_t out_io_id;
    uint32_t write_pos;
    uint32_t capacity;
    UbrMsgFormat local_msg_space;
    uint32_t hb_retry_cnt;
    uint32_t ep_last_cap;
    volatile EventQState trx_state;
} UbrTx;

typedef struct TagUbrRx {
    UbrAddrInfo local_data_q;
    UbrAddrInfo local_rx_event_q;
    UbrAddrInfo remote_data_status_q;
    UbrAddrInfo remote_tx_event_q;
    uint64_t in_io_id;
    uint32_t read_pos;
    uint32_t capacity;
    uint32_t deal_msg_num;
    uint32_t deal_msg_max_cnt;
    uint32_t ep_eof_pos;
    volatile EventQState trx_state;
} UbrRx;

typedef struct TagUbrTrx {
    UbrTx ubr_tx;
    UbrRx ubr_rx;
    uint64_t ubr_id;
    uint32_t trx_mgr_index;
    UbrTrxType type;
    SHM local_shm;
    SHM remote_shm;
    int timer_fd;
    int hb_timer_fd;
    int clear_timer_fd;
    AtomicInt close_cnt;
    AtomicInt close_state;
} UbrTrx;

typedef struct TagFileLock {
    int lock_fd;
    char* lock_path;
} FileLock;

typedef struct TagUbrLinkLock {
    int file_lock_num;
    FileLock* file_lock;
} UbrLinkLock;

typedef enum {
    UBR_UB_EVENT,
    UBR_HEARTBEAT,
} PASSIVE_DISC_TYPE;

}
}
#endif //BRPC_UBR_TRX_H