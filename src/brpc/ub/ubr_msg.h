//
// Created by z00926396 on 2026/4/11.
//

#ifndef BRPC_UBR_MSG_H
#define BRPC_UBR_MSG_H
#define UBR_MSG_HEADER_LEN 4
#define UBR_MSG_PAYLOAD_LEN 60
#define UBR_MSG_LEN (UBR_MSG_HEADER_LEN + UBR_MSG_PAYLOAD_LEN)

#define UBR_MSG_FLAG_INDEX 0
#define UBR_MSG_LEN_INDEX 1
#define UBR_MSG_CUR_INDEX 2

namespace brpc {
namespace ub {
typedef enum {
    UBR_MSG_CHUNK_NONE = 0,
    UBR_MSG_CHUNK_EXIST = 1,
    UBR_MSG_CHUNK_EOF = 2
} UbrMsgHdrFlag;

typedef struct TagUbrMsgPayload {
    uint8_t inner[UBR_MSG_PAYLOAD_LEN];
} UbrMsgPayload;

typedef struct __attribute__((aligned(64))) TagUbrMsgFormat {
    UbrMsgPayload payload;

    uint8_t header[UBR_MSG_HEADER_LEN];
} UbrMsgFormat;

static inline uint32_t CalcUbrMsgChunkCnt(uint32_t bufLen)
{
    uint32_t msgChunkNum = (bufLen + UBR_MSG_PAYLOAD_LEN - 1) / UBR_MSG_PAYLOAD_LEN;
    return msgChunkNum;
}
}
}
#endif //BRPC_UBR_MSG_H