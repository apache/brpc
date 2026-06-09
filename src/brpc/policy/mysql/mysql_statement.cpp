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

#include <vector>
#include <gflags/gflags.h>
#include "butil/logging.h"
#include "brpc/socket.h"
#include "brpc/policy/mysql/mysql_statement.h"

namespace brpc {
DEFINE_int32(mysql_statement_map_size,
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
    {
        MysqlStatementDBD::ScopedPtr ptr;
        if (_id_map.Read(&ptr) != 0) {
            LOG(WARNING) << "MysqlStatement::StatementId: failed to read the "
                          "statement-id map (DoublyBufferedData::Read failed) "
                          "for socket_id=" << socket_id << ", returning stmt_id=0";
            return 0;
        }
        const MysqlStatementId* p = ptr->seek(socket_id);
        if (p == NULL) {
            LOG(WARNING) << "MysqlStatement::StatementId: no prepared statement id "
                          "cached for socket_id=" << socket_id
                       << " (statement not found / not prepared on this "
                          "connection), returning stmt_id=0";
            return 0;
        }
        SocketUniquePtr socket;
        if (Socket::Address(socket_id, &socket) == 0) {
            uint64_t fd_version = socket->fd_version();
            if (fd_version == p->version) {
                return p->stmt_id;
            }
        }
    }
    // The socket was closed/recycled (version mismatch or address failed):
    // the cached stmt_id is stale and the server has dropped the prepared
    // statement. Erase the entry so it doesn't accumulate for the process
    // lifetime; a fresh prepare will re-insert via SetStatementId.
    //
    // NOTE: the read ScopedPtr above is released (closing scope) BEFORE this
    // Modify(), since DoublyBufferedData::Modify() blocks until all live
    // Read() references are gone -- holding `ptr` here would deadlock.
    _id_map.Modify(my_delete_k, socket_id);
    LOG(WARNING) << "MysqlStatement::StatementId: cached statement id for "
                  "socket_id=" << socket_id << " is stale (socket closed/recycled "
                  "-- fd_version mismatch or Socket::Address failed); erased the "
                  "entry and returning stmt_id=0 to force a re-prepare";
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

namespace {
// Count only top-level placeholder '?' in a SQL statement, skipping any '?'
// that appears inside a single-quoted / double-quoted / backtick-quoted
// literal, or inside a -- , # , or /* */ comment. This mirrors how a SQL
// lexer treats quoting so a valid statement containing a literal '?'
// (e.g. WHERE name = '?') is not miscounted and wrongly rejected on prepare.
uint16_t CountPlaceholders(const std::string& s) {
    uint16_t count = 0;
    const size_t n = s.size();
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (c == '\'' || c == '"' || c == '`') {
            // Skip the quoted span. Handles backslash escapes and the SQL
            // doubled-quote escape ('' inside '...').
            const char quote = c;
            ++i;
            while (i < n) {
                const char d = s[i];
                if (d == '\\' && quote != '`') {
                    ++i;  // skip escaped char
                } else if (d == quote) {
                    if (i + 1 < n && s[i + 1] == quote) {
                        ++i;  // doubled quote -> literal quote, stay in string
                    } else {
                        break;  // closing quote
                    }
                }
                ++i;
            }
        } else if (c == '-' && i + 1 < n && s[i + 1] == '-') {
            // line comment until end of line
            i += 2;
            while (i < n && s[i] != '\n') {
                ++i;
            }
        } else if (c == '#') {
            // line comment until end of line
            ++i;
            while (i < n && s[i] != '\n') {
                ++i;
            }
        } else if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            // block comment until */
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) {
                ++i;
            }
            ++i;  // land on '/' (loop ++i moves past it)
        } else if (c == '?') {
            ++count;
        }
    }
    return count;
}
}  // namespace

void MysqlStatement::Init(const Channel& channel) {
    _param_count = CountPlaceholders(_str);
    ChannelOptions opts = channel.options();
    _connection_type = ConnectionType(opts.connection_type);
    if (_connection_type != CONNECTION_TYPE_SHORT) {
        _id_map.Modify(my_init_kv);
    } else {
        LOG_EVERY_SECOND(WARNING)
            << "Prepared statement on a 'short' connection re-prepares on every "
               "execute (a new TCP connection per request cannot cache the "
               "server stmt_id); use connection_type='pooled' for prepared "
               "statements.";
    }
}

}  // namespace brpc
