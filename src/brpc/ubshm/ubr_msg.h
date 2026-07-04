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

#ifndef BRPC_UBR_MSG_H
#define BRPC_UBR_MSG_H
#include "butil/compiler_specific.h"
#define UBR_MSG_HEADER_LEN 4
#define UBR_MSG_PAYLOAD_LEN 60
#define UBR_MSG_LEN (UBR_MSG_HEADER_LEN + UBR_MSG_PAYLOAD_LEN)

#define UBR_MSG_FLAG_INDEX 0
#define UBR_MSG_LEN_INDEX 1
#define UBR_MSG_CUR_INDEX 2

namespace brpc {
namespace ubring {
typedef enum {
    UBR_MSG_CHUNK_NONE = 0,
    UBR_MSG_CHUNK_EXIST = 1,
    UBR_MSG_CHUNK_EOF = 2
} UbrMsgHdrFlag;

typedef struct TagUbrMsgPayload {
    uint8_t inner[UBR_MSG_PAYLOAD_LEN];
} UbrMsgPayload;

typedef struct BAIDU_CACHELINE_ALIGNMENT TagUbrMsgFormat {
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