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
#include <ios>

namespace brpc {

butil::Status MysqlMakeCommand(butil::IOBuf* outbuf,
                               const MysqlCommandType type,
                               const std::string& stmt,
                               const uint8_t seq) {
    if (outbuf == NULL || stmt.length() == 0) {
        return butil::Status(EINVAL, "Param[outbuf] or [stmt] is NULL");
    }
    outbuf->clear();
    butil::IOBuf payload;
    payload.push_back(type);
    payload.append(stmt);
    uint32_t header = butil::ByteSwapToLE32(payload.size());
    outbuf->append(&header, 3);
    outbuf->push_back(seq);
    outbuf->append(payload);
    return butil::Status::OK();
}


}  // namespace brpc
