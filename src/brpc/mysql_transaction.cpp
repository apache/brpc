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

#include <sstream>
#include "butil/logging.h"  // LOG()
#include "brpc/mysql_transaction.h"
#include "brpc/mysql.h"
#include "brpc/socket.h"
#include "brpc/details/controller_private_accessor.h"

namespace brpc {
// mysql transaction isolation level string
const char* mysql_isolation_level[] = {
    "REPEATABLE READ", "READ COMMITTED", "READ UNCOMMITTED", "SERIALIZABLE"};

SocketId MysqlTransaction::GetSocketId() const {
    return _socket->id();
}

bool MysqlTransaction::DoneTransaction(const char* command) {
    bool rc = false;
    MysqlRequest request(this);
    if (_socket == NULL) {  // must already commit or rollback, return true.
        return true;
    } else if (!request.Query(command)) {
        LOG(ERROR) << "Fail to query command" << command;
    } else {
        MysqlResponse response;
        Controller cntl;
        _channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        if (!cntl.Failed()) {
            if (response.reply(0).is_ok()) {
                rc = true;
            } else {
                LOG(ERROR) << "Fail " << command << " transaction, " << response;
            }
        } else {
            LOG(ERROR) << "Fail " << command << " transaction, " << cntl.ErrorText();
        }
    }
    if (rc && _connection_type == CONNECTION_TYPE_POOLED) {
        _socket->ReturnToPool();
    }
    _socket.reset();
    return rc;
}

MysqlTransactionUniquePtr NewMysqlTransaction(Channel& channel,
                                              const MysqlTransactionOptions& opts) {
    const char* command[2] = {"START TRANSACTION READ ONLY", "START TRANSACTION"};

    if (channel.options().connection_type == CONNECTION_TYPE_SINGLE) {
        LOG(ERROR) << "mysql transaction can't use connection type 'single'";
        return NULL;
    }
    std::stringstream ss;
    // repeatable read is mysql default isolation level, so ignore it.
    if (opts.isolation_level != MysqlIsoRepeatableRead) {
        ss << "SET TRANSACTION ISOLATION LEVEL " << mysql_isolation_level[opts.isolation_level]
           << ";";
    }

    if (opts.readonly) {
        ss << command[0];
    } else {
        ss << command[1];
    }

    MysqlRequest request;
    if (!request.Query(ss.str())) {
        LOG(ERROR) << "Fail to query command" << ss.str();
        return NULL;
    }

    MysqlTransactionUniquePtr tx;
    MysqlResponse response;
    Controller cntl;
    ControllerPrivateAccessor(&cntl).set_bind_sock_action(BIND_SOCK_ACTIVE);
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    if (!cntl.Failed()) {
        // repeatable read isolation send one reply, other isolation has two reply
        if ((opts.isolation_level == MysqlIsoRepeatableRead && response.reply(0).is_ok()) ||
            (response.reply(0).is_ok() && response.reply(1).is_ok())) {
            SocketUniquePtr socket;
            ControllerPrivateAccessor(&cntl).get_bind_sock(&socket);
            if (socket == NULL) {
                LOG(ERROR) << "Fail create mysql transaction, get bind socket failed";
            } else {
                tx.reset(new MysqlTransaction(channel, socket, cntl.connection_type()));
            }
        } else {
            LOG(ERROR) << "Fail create mysql transaction, " << response;
        }
    } else {
        LOG(ERROR) << "Fail create mysql transaction, " << cntl.ErrorText();
    }
    return tx;
}

}  // namespace brpc
