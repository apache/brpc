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

#include <vector>
#include <gflags/gflags.h>
#include "brpc/socket.h"
#include "brpc/mysql_statement.h"

namespace brpc {
DEFINE_int32(mysql_statment_map_size,
             100,
             "Mysql statement map size, usually equal to max bthread number");

MysqlStatementUniquePtr NewMysqlStatement(const Channel& channel, const butil::StringPiece& str) {
    MysqlStatementUniquePtr ptr(new MysqlStatement(channel, str));
    return ptr;
}

uint32_t MysqlStatement::StatementId(SocketId socket_id) const {
    if (_connection_type == CONNECTION_TYPE_SHORT) {
        return 0;
    }
    MysqlStatementDBD::ScopedPtr ptr;
    if (_id_map.Read(&ptr) != 0) {
        return 0;
    }
    const MysqlStatementId* p = ptr->seek(socket_id);
    if (p == NULL) {
        return 0;
    }
    SocketUniquePtr socket;
    if (Socket::Address(socket_id, &socket) == 0) {
        uint64_t fd_version = socket->fd_version();
        if (fd_version == p->version) {
            return p->stmt_id;
        }
    }
    return 0;
}

void MysqlStatement::SetStatementId(SocketId socket_id, uint32_t stmt_id) {
    if (_connection_type == CONNECTION_TYPE_SHORT) {
        return;
    }
    SocketUniquePtr socket;
    if (Socket::Address(socket_id, &socket) == 0) {
        uint64_t fd_version = socket->fd_version();
        MysqlStatementId value{stmt_id, fd_version};
        _id_map.Modify(my_update_kv, socket_id, value);
    }
}

void MysqlStatement::Init(const Channel& channel) {
    _param_count = std::count(_str.begin(), _str.end(), '?');
    ChannelOptions opts = channel.options();
    _connection_type = ConnectionType(opts.connection_type);
    if (_connection_type != CONNECTION_TYPE_SHORT) {
        _id_map.Modify(my_init_kv);
    }
}

}  // namespace brpc
