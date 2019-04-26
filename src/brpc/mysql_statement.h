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

#ifndef BRPC_MYSQL_STATEMENT_H
#define BRPC_MYSQL_STATEMENT_H
#include <string>
#include <bthread/bthread.h>
#include "brpc/channel.h"
#include "brpc/mysql_statement_inl.h"

namespace brpc {

class MysqlStatement {
public:
    MysqlStatement(const Channel& channel, const butil::StringPiece& str);
    const butil::StringPiece str() const;
    uint32_t stmt_id(SocketId sock_id) const;
    void set_stmt_id(SocketId sock_id, uint32_t stmt_id);
    uint16_t param_number() const;

private:
    DISALLOW_COPY_AND_ASSIGN(MysqlStatement);
    void ClearStaleSockIdFromIdMap();
    void Init(const Channel& channel);

    const std::string _str;    // prepare statement string
    mutable DBDKVMap _id_map;  // SocketId and statement id
    uint16_t _param_number;
    ConnectionType _connection_type;
    std::atomic_int _set_counter;  // set stmt id call counter
};

inline MysqlStatement::MysqlStatement(const Channel& channel, const butil::StringPiece& str)
    : _str(str.data(), str.size()), _param_number(0), _set_counter(0) {
    Init(channel);
}

inline const butil::StringPiece MysqlStatement::str() const {
    return butil::StringPiece(_str);
}

inline uint32_t MysqlStatement::stmt_id(SocketId sock_id) const {
    if (_connection_type != CONNECTION_TYPE_SHORT) {
        DBDKVMap::ScopedPtr ptr;
        if (_id_map.Read(&ptr) != 0) {
            return 0;
        }
        const uint32_t* p = ptr->seek(sock_id);
        return (p != NULL ? *p : 0);
    } else {
        return 0;
    }
}

inline void MysqlStatement::set_stmt_id(SocketId sock_id, uint32_t stmt_id) {
    if (_connection_type != CONNECTION_TYPE_SHORT) {
        ClearStaleSockIdFromIdMap();
        _id_map.Modify(my_update_kv, sock_id, stmt_id);
    }
}

inline uint16_t MysqlStatement::param_number() const {
    return _param_number;
}

typedef std::unique_ptr<MysqlStatement> MysqlStatementUniquePtr;

MysqlStatementUniquePtr NewMysqlStatement(const Channel& channel, const butil::StringPiece& str);

}  // namespace brpc
#endif
