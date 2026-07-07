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

// Authors: Yang,Liming (yangliming01@baidu.com)

#ifndef BRPC_MYSQL_COMMON_H
#define BRPC_MYSQL_COMMON_H

#include <sstream>
#include <map>
#include "butil/logging.h"  // LOG()

namespace brpc {
// Mysql collations
extern const char* MysqlDefaultCollation;
extern const std::map<std::string, uint8_t> MysqlCollations;

enum MysqlFieldType : uint8_t {
    MYSQL_FIELD_TYPE_DECIMAL = 0x00,
    MYSQL_FIELD_TYPE_TINY = 0x01,
    MYSQL_FIELD_TYPE_SHORT = 0x02,
    MYSQL_FIELD_TYPE_LONG = 0x03,
    MYSQL_FIELD_TYPE_FLOAT = 0x04,
    MYSQL_FIELD_TYPE_DOUBLE = 0x05,
    MYSQL_FIELD_TYPE_NULL = 0x06,
    MYSQL_FIELD_TYPE_TIMESTAMP = 0x07,
    MYSQL_FIELD_TYPE_LONGLONG = 0x08,
    MYSQL_FIELD_TYPE_INT24 = 0x09,
    MYSQL_FIELD_TYPE_DATE = 0x0A,
    MYSQL_FIELD_TYPE_TIME = 0x0B,
    MYSQL_FIELD_TYPE_DATETIME = 0x0C,
    MYSQL_FIELD_TYPE_YEAR = 0x0D,
    MYSQL_FIELD_TYPE_NEWDATE = 0x0E,
    MYSQL_FIELD_TYPE_VARCHAR = 0x0F,
    MYSQL_FIELD_TYPE_BIT = 0x10,
    MYSQL_FIELD_TYPE_JSON = 0xF5,
    MYSQL_FIELD_TYPE_NEWDECIMAL = 0xF6,
    MYSQL_FIELD_TYPE_ENUM = 0xF7,
    MYSQL_FIELD_TYPE_SET = 0xF8,
    MYSQL_FIELD_TYPE_TINY_BLOB = 0xF9,
    MYSQL_FIELD_TYPE_MEDIUM_BLOB = 0xFA,
    MYSQL_FIELD_TYPE_LONG_BLOB = 0xFB,
    MYSQL_FIELD_TYPE_BLOB = 0xFC,
    MYSQL_FIELD_TYPE_VAR_STRING = 0xFD,
    MYSQL_FIELD_TYPE_STRING = 0xFE,
    MYSQL_FIELD_TYPE_GEOMETRY = 0xFF,
};

enum MysqlFieldFlag : uint16_t {
    MYSQL_NOT_NULL_FLAG = 0x0001,
    MYSQL_PRI_KEY_FLAG = 0x0002,
    MYSQL_UNIQUE_KEY_FLAG = 0x0004,
    MYSQL_MULTIPLE_KEY_FLAG = 0x0008,
    MYSQL_BLOB_FLAG = 0x0010,
    MYSQL_UNSIGNED_FLAG = 0x0020,
    MYSQL_ZEROFILL_FLAG = 0x0040,
    MYSQL_BINARY_FLAG = 0x0080,
    MYSQL_ENUM_FLAG = 0x0100,
    MYSQL_AUTO_INCREMENT_FLAG = 0x0200,
    MYSQL_TIMESTAMP_FLAG = 0x0400,
    MYSQL_SET_FLAG = 0x0800,
};

enum MysqlServerStatus : uint16_t {
    MYSQL_SERVER_STATUS_IN_TRANS = 1,
    MYSQL_SERVER_STATUS_AUTOCOMMIT = 2,   /* Server in auto_commit mode */
    MYSQL_SERVER_MORE_RESULTS_EXISTS = 8, /* Multi query - next query exists */
    MYSQL_SERVER_QUERY_NO_GOOD_INDEX_USED = 16,
    MYSQL_SERVER_QUERY_NO_INDEX_USED = 32,
    /**
      The server was able to fulfill the clients request and opened a
      read-only non-scrollable cursor for a query. This flag comes
      in reply to COM_STMT_EXECUTE and COM_STMT_FETCH commands.
    */
    MYSQL_SERVER_STATUS_CURSOR_EXISTS = 64,
    /**
      This flag is sent when a read-only cursor is exhausted, in reply to
      COM_STMT_FETCH command.
    */
    MYSQL_SERVER_STATUS_LAST_ROW_SENT = 128,
    MYSQL_SERVER_STATUS_DB_DROPPED = 256, /* A database was dropped */
    MYSQL_SERVER_STATUS_NO_BACKSLASH_ESCAPES = 512,
    /**
      Sent to the client if after a prepared statement reprepare
      we discovered that the new statement returns a different
      number of result set columns.
    */
    MYSQL_SERVER_STATUS_METADATA_CHANGED = 1024,
    MYSQL_SERVER_QUERY_WAS_SLOW = 2048,

    /**
      To mark ResultSet containing output parameter values.
    */
    MYSQL_SERVER_PS_OUT_PARAMS = 4096,

    /**
      Set at the same time as MYSQL_SERVER_STATUS_IN_TRANS if the started
      multi-statement transaction is a read-only transaction. Cleared
      when the transaction commits or aborts. Since this flag is sent
      to clients in OK and EOF packets, the flag indicates the
      transaction status at the end of command execution.
    */
    MYSQL_SERVER_STATUS_IN_TRANS_READONLY = 8192,
    MYSQL_SERVER_SESSION_STATE_CHANGED = 1UL << 14,
};

// 1. normal statement 2. prepared statement 3. need prepare statement
enum MysqlStmtType : uint32_t {
    MYSQL_NORMAL_STATEMENT = 1,
    MYSQL_PREPARED_STATEMENT = 2,
    MYSQL_NEED_PREPARE = 3,
};

const char* MysqlFieldTypeToString(MysqlFieldType);

inline std::string pack_encode_length(const uint64_t value) {
    std::stringstream ss;
    if (value <= 250) {
        ss.put((char)value);
    } else if (value <= 0xffff) {
        ss.put((char)0xfc).put((char)value).put((char)(value >> 8));
    } else if (value <= 0xffffff) {
        ss.put((char)0xfd).put((char)value).put((char)(value >> 8)).put((char)(value >> 16));
    } else {
        ss.put((char)0xfe)
            .put((char)value)
            .put((char)(value >> 8))
            .put((char)(value >> 16))
            .put((char)(value >> 24))
            .put((char)(value >> 32))
            .put((char)(value >> 40))
            .put((char)(value >> 48))
            .put((char)(value >> 56));
    }
    return ss.str();
}

// little endian order to host order
#if !defined(ARCH_CPU_LITTLE_ENDIAN)

inline uint16_t mysql_uint2korr(const uint8_t* A) {
    return (uint16_t)(((uint16_t)(A[0])) + ((uint16_t)(A[1]) << 8));
}

inline uint32_t mysql_uint3korr(const uint8_t* A) {
    return (uint32_t)(((uint32_t)(A[0])) + (((uint32_t)(A[1])) << 8) + (((uint32_t)(A[2])) << 16));
}

inline uint32_t mysql_uint4korr(const uint8_t* A) {
    return (uint32_t)(((uint32_t)(A[0])) + (((uint32_t)(A[1])) << 8) + (((uint32_t)(A[2])) << 16) +
                      (((uint32_t)(A[3])) << 24));
}

inline uint64_t mysql_uint8korr(const uint8_t* A) {
    return (uint64_t)(((uint64_t)(A[0])) + (((uint64_t)(A[1])) << 8) + (((uint64_t)(A[2])) << 16) +
                      (((uint64_t)(A[3])) << 24) + (((uint64_t)(A[4])) << 32) +
                      (((uint64_t)(A[5])) << 40) + (((uint64_t)(A[6])) << 48) +
                      (((uint64_t)(A[7])) << 56));
}

#else

inline uint16_t mysql_uint2korr(const uint8_t* A) {
    return *(uint16_t*)A;
}

inline uint32_t mysql_uint3korr(const uint8_t* A) {
    return (uint32_t)(((uint32_t)(A[0])) + (((uint32_t)(A[1])) << 8) + (((uint32_t)(A[2])) << 16));
}

inline uint32_t mysql_uint4korr(const uint8_t* A) {
    return *(uint32_t*)A;
}

inline uint64_t mysql_uint8korr(const uint8_t* A) {
    return *(uint64_t*)A;
}

#endif

}  // namespace brpc
#endif
