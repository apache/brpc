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

#ifndef BRPC_MYSQL_STATEMENT_INL_H
#define BRPC_MYSQL_STATEMENT_INL_H
#include <gflags/gflags.h>
#include "butil/containers/flat_map.h"  // FlatMap
#include "butil/containers/doubly_buffered_data.h"
#include "brpc/socket_id.h"

namespace brpc {
DECLARE_int32(mysql_statment_map_size);

struct MysqlStatementId {
    uint32_t stmt_id;  // statement id
    uint64_t version;  // socket's fd version
};

typedef butil::FlatMap<SocketId, MysqlStatementId> MysqlStatementKVMap;
typedef butil::DoublyBufferedData<MysqlStatementKVMap> MysqlStatementDBD;

inline size_t my_init_kv(MysqlStatementKVMap& m) {
    if (FLAGS_mysql_statment_map_size < 100) {
        FLAGS_mysql_statment_map_size = 100;
    }
    m.init(FLAGS_mysql_statment_map_size);
    return 1;
}

inline size_t my_update_kv(MysqlStatementKVMap& m, SocketId key, MysqlStatementId value) {
    MysqlStatementId* p = m.seek(key);
    if (p == NULL) {
        m.insert(key, value);
    } else {
        *p = value;
    }
    return 1;
}

inline size_t my_delete_k(MysqlStatementKVMap& m, SocketId key) {
    return m.erase(key);
}

}  // namespace brpc
#endif
