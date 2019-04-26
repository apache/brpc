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

void MysqlStatement::ClearStaleSockIdFromIdMap() {
    if (_connection_type == CONNECTION_TYPE_SHORT) {
        return;
    }
    // add call counter
    int cnt = _set_counter.fetch_add(1, std::memory_order_relaxed);
    // get stale socket id
    std::vector<SocketId> keys;
    {
        DBDKVMap::ScopedPtr ptr;
        if (((cnt + 1) % FLAGS_mysql_statment_map_size) == 0 && _id_map.Read(&ptr) == 0) {
            for (KVMap::const_iterator it = ptr->begin(); it != ptr->end(); ++it) {
                SocketUniquePtr sock;
                if (Socket::Address(it->first, &sock) != 0 || !sock->IsAvailable()) {
                    keys.push_back(it->first);
                }
            }
        }
    }
    // remove stale socket id
    for (const auto& k : keys) {
        _id_map.Modify(my_delete_k, k);
    }
}

void MysqlStatement::Init(const Channel& channel) {
    ChannelOptions opts = channel.options();
    _connection_type = ConnectionType(opts.connection_type);
    if (_connection_type != CONNECTION_TYPE_SHORT) {
        _id_map.Modify(my_init_kv);
    }
    _param_number = std::count(_str.begin(), _str.end(), '?');
}

}  // namespace brpc
