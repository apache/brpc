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

// Authors: Ge,Jun (gejun@baidu.com)

#include "brpc/mysql_command.h"
#include "butil/sys_byteorder.h"
#include "butil/logging.h"  // LOG()

namespace {};
namespace brpc {
butil::Status MysqlMakeCommand(butil::IOBuf* outbuf,
                               const MysqlCommandType type,
                               const butil::StringPiece& command,
                               const uint8_t seq) {
    // TODO: maybe need to do some command syntex verify
    const int header_size = 4;
    if (outbuf == NULL || command.size() == 0) {
        return butil::Status(EINVAL, "[MysqlMakeCommand] Param[outbuf] or [stmt] is NULL");
    }
    if (command.size() > mysql_max_package_size) {
        return butil::Status(EINVAL, "[MysqlMakeCommand] statement size is too big");
    }
    uint32_t header = butil::ByteSwapToLE32(command.size() + 1) | seq;  // stmt + type
    outbuf->append(&header, header_size);
    outbuf->push_back(type);
    outbuf->append(command.data(), command.size());
    return butil::Status::OK();
}

butil::Status MysqlMakeCommand(butil::IOBuf* outbuf,
                               const MysqlCommandType type,
                               const butil::StringPiece* commands,
                               const size_t n,
                               const uint8_t seq) {
    // TODO: maybe need to do some command syntex verify
    const int header_size = 4;
    if (outbuf == NULL || commands == NULL || n == 0) {
        return butil::Status(EINVAL,
                             "[MysqlMakeCommand] Param[outbuf] or [commands] is NULL or [n] is 0");
    }
    butil::IOBuf::Area area = outbuf->reserve(header_size);  // reserve for header
    if (area == butil::IOBuf::INVALID_AREA) {
        return butil::Status(EINVAL, "[MysqlMakeCommand] reserve for header failed");
    }
    outbuf->push_back(type);
    for (size_t i = 0; i < n; ++i) {
        outbuf->append(commands[i].data(), commands[i].size());
        outbuf->push_back(';');
    }
    if (outbuf->size() > mysql_max_package_size) {
        return butil::Status(EINVAL, "[MysqlMakeCommand] statement size is too big");
    }
    uint32_t header = butil::ByteSwapToLE32(outbuf->size() - header_size) | seq;
    if (outbuf->unsafe_assign(area, &header) != 0) {
        return butil::Status(EINVAL, "[MysqlMakeCommand] unsafe assign for header failed");
    }
    return butil::Status::OK();
}

}  // namespace brpc
