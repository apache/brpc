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

#ifndef BRPC_MONGO_HEAD_H
#define BRPC_MONGO_HEAD_H

#include "butil/sys_byteorder.h"


namespace brpc {

// Sync with
//   https://github.com/mongodb/mongo-c-driver/blob/master/src/mongoc/mongoc-opcode.h
//   https://docs.mongodb.org/manual/reference/mongodb-wire-protocol/#request-opcodes
enum MongoOpCode {
    MONGO_OPCODE_REPLY         = 1,
    MONGO_OPCODE_MSG           = 1000,
    MONGO_OPCODE_UPDATE        = 2001,
    MONGO_OPCODE_INSERT        = 2002,
    MONGO_OPCODE_QUERY         = 2004,
    MONGO_OPCODE_GET_MORE      = 2005,
    MONGO_OPCODE_DELETE        = 2006,
    MONGO_OPCODE_KILL_CURSORS  = 2007,
};

inline bool is_mongo_opcode(int32_t op_code) {
    switch (op_code) {
    case MONGO_OPCODE_REPLY:         return true;
    case MONGO_OPCODE_MSG:           return true;
    case MONGO_OPCODE_UPDATE:        return true; 
    case MONGO_OPCODE_INSERT:        return true; 
    case MONGO_OPCODE_QUERY:         return true; 
    case MONGO_OPCODE_GET_MORE:      return true; 
    case MONGO_OPCODE_DELETE:        return true; 
    case MONGO_OPCODE_KILL_CURSORS : return true;
    }
    return false;
}

// All data of mongo protocol is little-endian.
// https://docs.mongodb.org/manual/reference/mongodb-wire-protocol/#byte-ordering
#pragma pack(1)
struct mongo_head_t {
    int32_t message_length;  // total message size, including this
    int32_t request_id;      // identifier for this message
    int32_t response_to;     // requestID from the original request
                             // (used in responses from db)
    int32_t op_code;         // request type, see MongoOpCode.

    void make_host_endian() {
        if (!ARCH_CPU_LITTLE_ENDIAN) {
            message_length = butil::ByteSwap((uint32_t)message_length);
            request_id = butil::ByteSwap((uint32_t)request_id);
            response_to = butil::ByteSwap((uint32_t)response_to);
            op_code = butil::ByteSwap((uint32_t)op_code);
        }
    }
};
#pragma pack()

} // namespace brpc


#endif // BRPC_MONGO_HEAD_H
