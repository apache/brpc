//
// Created by z00926396 on 2026/4/11.
//

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
extern RETURN_CODE(*g_BeforeTcpClose)(int);
extern RETURN_CODE(*g_AfterTcpClose)(int);

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
    uint8_t heartBeat;
} UbrDataStatusQMsg;

typedef struct TagUbrEventQMsg {
    uint64_t ioId;
    EventQState flag;
} UbrEventQMsg;

typedef struct TagUbrAddrInfo {
    uint8_t *addr;
    size_t len;
} UbrAddrInfo;

typedef struct TagUbrTx {
    UbrAddrInfo remoteDataQ;
    UbrAddrInfo remoteRxEventQ;
    UbrAddrInfo localDataStatusQ;
    UbrAddrInfo localTxEventQ;
    uint64_t outIoId;
    uint32_t writePos;
    uint32_t capacity;
    UbrMsgFormat localMsgSpace;
    uint32_t hbRetryCnt;
    uint32_t epLastCap;
    volatile EventQState trxState;
} UbrTx;

typedef struct TagUbrRx {
    UbrAddrInfo localDataQ;
    UbrAddrInfo localRxEventQ;
    UbrAddrInfo remoteDataStatusQ;
    UbrAddrInfo remoteTxEventQ;
    uint64_t inIoId;
    uint32_t readPos;
    uint32_t capacity;
    uint32_t dealMsgNum;
    uint32_t dealMsgMaxCnt;
    uint32_t epEofPos;
    volatile EventQState trxState;
} UbrRx;

typedef struct TagUbrTrx {
    UbrTx ubrTx;
    UbrRx ubrRx;
    uint64_t ubrId;
    uint32_t trxMgrIndex;
    UbrTrxType type;
    SHM localShm;
    SHM remoteShm;
    int timerFd;
    int hbTimerFd;
    int clearTimerFd;
    AtomicInt closeCnt;
    AtomicInt closeState;
} UbrTrx;

typedef struct TagFileLock {
    int lockFd;
    char* lockPath;
} FileLock;

typedef struct TagUbrLinkLock {
    int fileLockNum;
    FileLock* fileLock;
} UbrLinkLock;

typedef enum {
    UBR_UB_EVENT,
    UBR_HEARTBEAT,
}PASSIVE_DISC_TYPE;

}
}
#endif //BRPC_UBR_TRX_H