// Copyright (c) 2015 Baidu, Inc.
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

#include "brpc/mysql_command.h"
#include "brpc/mysql_common.h"
#include "butil/sys_byteorder.h"
#include "butil/logging.h"  // LOG()

namespace brpc {
butil::Status MysqlMakeCommand(butil::IOBuf* outbuf,
                               const MysqlCommandType type,
                               const butil::StringPiece& command,
                               const uint8_t seq) {
    // TODO: maybe need to do some command syntex verify
    if (outbuf == NULL || command.size() == 0) {
        return butil::Status(EINVAL, "[MysqlMakeCommand] Param[outbuf] or [stmt] is NULL");
    }
    if (command.size() > mysql_max_package_size) {
        return butil::Status(EINVAL, "[MysqlMakeCommand] statement size is too big");
    }
    uint32_t header = butil::ByteSwapToLE32(command.size() + 1) | seq;  // stmt + type
    outbuf->append(&header, mysql_header_size);
    outbuf->push_back(type);
    outbuf->append(command.data(), command.size());
    return butil::Status::OK();
}

}  // namespace brpc
