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
// mysql prepared statement Unique Ptr
class MysqlStatement;
typedef std::unique_ptr<MysqlStatement> MysqlStatementUniquePtr;
// mysql prepared statement
class MysqlStatement {
public:
    const butil::StringPiece str() const;
    uint16_t param_count() const;
    uint32_t StatementId(SocketId sock_id) const;
    void SetStatementId(SocketId sock_id, uint32_t stmt_id);

private:
    MysqlStatement(const Channel& channel, const butil::StringPiece& str);
    void Init(const Channel& channel);
    DISALLOW_COPY_AND_ASSIGN(MysqlStatement);

    friend MysqlStatementUniquePtr NewMysqlStatement(const Channel& channel,
                                                     const butil::StringPiece& str);

    const std::string _str;  // prepare statement string
    uint16_t _param_count;
    mutable MysqlStatementDBD _id_map;  // SocketId and statement id
    ConnectionType _connection_type;
};

inline MysqlStatement::MysqlStatement(const Channel& channel, const butil::StringPiece& str)
    : _str(str.data(), str.size()), _param_count(0) {
    Init(channel);
}

inline const butil::StringPiece MysqlStatement::str() const {
    return butil::StringPiece(_str);
}

inline uint16_t MysqlStatement::param_count() const {
    return _param_count;
}

MysqlStatementUniquePtr NewMysqlStatement(const Channel& channel, const butil::StringPiece& str);

}  // namespace brpc
#endif
