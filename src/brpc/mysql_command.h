// Copyright (c) 2019 Baidu, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Yang,Liming (yangliming01@baidu.com)

#ifndef BRPC_MYSQL_COMMAND_H
#define BRPC_MYSQL_COMMAND_H

#include <vector>
#include "butil/iobuf.h"
#include "butil/status.h"
#include "brpc/mysql_common.h"

namespace brpc {
// mysql command types
enum MysqlCommandType : unsigned char {
    MYSQL_COM_SLEEP,
    MYSQL_COM_QUIT,
    MYSQL_COM_INIT_DB,
    MYSQL_COM_QUERY,
    MYSQL_COM_FIELD_LIST,
    MYSQL_COM_CREATE_DB,
    MYSQL_COM_DROP_DB,
    MYSQL_COM_REFRESH,
    MYSQL_COM_SHUTDOWN,
    MYSQL_COM_STATISTICS,
    MYSQL_COM_PROCESS_INFO,
    MYSQL_COM_CONNECT,
    MYSQL_COM_PROCESS_KILL,
    MYSQL_COM_DEBUG,
    MYSQL_COM_PING,
    MYSQL_COM_TIME,
    MYSQL_COM_DELAYED_INSERT,
    MYSQL_COM_CHANGE_USER,
    MYSQL_COM_BINLOG_DUMP,
    MYSQL_COM_TABLE_DUMP,
    MYSQL_COM_CONNECT_OUT,
    MYSQL_COM_REGISTER_SLAVE,
    MYSQL_COM_STMT_PREPARE,
    MYSQL_COM_STMT_EXECUTE,
    MYSQL_COM_STMT_SEND_LONG_DATA,
    MYSQL_COM_STMT_CLOSE,
    MYSQL_COM_STMT_RESET,
    MYSQL_COM_SET_OPTION,
    MYSQL_COM_STMT_FETCH,
    MYSQL_COM_DAEMON,
    MYSQL_COM_BINLOG_DUMP_GTID,
    MYSQL_COM_RESET_CONNECTION,
};

butil::Status MysqlMakeCommand(butil::IOBuf* outbuf,
                               const MysqlCommandType type,
                               const butil::StringPiece& stmt);

// Prepared Statement Protocol
// an prepared statement has a unique statement id in one connection (in brpc SocketId), an prepared
// statement can be executed in many connections, so ever connection has a different statement id.
// In bprc, we can only get a connection in the stage of PackXXXRequest which is behind our building
// mysql protocol stage, but building prepared statement need the statement id of a connection, so
// we will need to building this fragment at PackXXXRequest stage.

// maybe we can Add a wrapper function, call CallMethod many times use bind_sock
class MysqlStatementStub;
// prepared statement execute command header, will be called at PackXXXRequest stage.
butil::Status MysqlMakeExecutePacket(butil::IOBuf* outbuf,
                                     uint32_t stmt_id,
                                     const butil::IOBuf& body);
// prepared statement execute command body, will be called at building mysql protocol stage.
butil::Status MysqlMakeExecuteData(MysqlStatementStub* stmt,
                                   uint16_t index,
                                   const void* value,
                                   MysqlFieldType type,
                                   bool is_unsigned = false);
// prepared statement long data header
butil::Status MysqlMakeLongDataPacket(butil::IOBuf* outbuf,
                                      uint32_t stmt_id,
                                      uint16_t param_id,
                                      const butil::IOBuf& body);

}  // namespace brpc
#endif
