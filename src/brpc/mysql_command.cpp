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

#include "butil/sys_byteorder.h"
#include "butil/logging.h"  // LOG()
#include "brpc/mysql_command.h"
#include "brpc/mysql_common.h"
#include "brpc/mysql.h"

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
    uint32_t header = butil::ByteSwapToLE32(command.size() + 1) | seq;  // command + type
    outbuf->append(&header, mysql_header_size);
    outbuf->push_back(type);
    outbuf->append(command.data(), command.size());
    return butil::Status::OK();
}

butil::Status MysqlMakeExecuteHeader(butil::IOBuf* outbuf, uint32_t stmt_id, uint32_t body_size) {
    uint32_t header = butil::ByteSwapToLE32(
        1 + 4 + 1 + 4 + body_size);  // cmd_type + stmt_id + flag + reserved + body_size
    outbuf->append(&header, mysql_header_size);
    outbuf->push_back(MYSQL_COM_STMT_EXECUTE);  // cmd
    const uint32_t si = butil::ByteSwapToLE32(stmt_id);
    outbuf->append(&si, 4);         // stmt_id
    outbuf->push_back('\0');        // flag
    outbuf->push_back((char)0x01);  // reserved
    outbuf->push_back('\0');
    outbuf->push_back('\0');
    outbuf->push_back('\0');
    return butil::Status::OK();
}

butil::Status MysqlMakeExecuteBody(MysqlStatementStub* stmt,
                                   uint16_t index,
                                   const void* value,
                                   MysqlFieldType type,
                                   bool is_unsigned) {
    const uint16_t n = stmt->stmt()->param_number();
    // if param number is zero finished.
    if (n == 0) {
        return butil::Status::OK();
    }
    butil::IOBuf& buf = stmt->execute_data();
    MysqlStatementStub::NullMask& null_mask = stmt->null_mask();
    MysqlStatementStub::ParamTypes& param_types = stmt->param_types();
    // else param number larger than zero.
    if (index >= n) {
        LOG(ERROR) << "too many params";
        return butil::Status(EINVAL, "[MysqlMakeExecute] too many params");
    }
    // reserve null mask and param types packing at first param
    if (index == 0) {
        const size_t mask_len = (n + 7) / 8;
        const size_t types_len = 2 * n;
        null_mask.mask.resize(mask_len, 0);
        null_mask.area = buf.reserve(mask_len);
        buf.push_back((char)0x01);
        param_types.types.reserve(types_len);
        param_types.area = buf.reserve(types_len);
    }
    // pack param value
    switch (type) {
        case MYSQL_FIELD_TYPE_TINY:
            if (is_unsigned) {
                param_types.types[index + index] = MYSQL_FIELD_TYPE_TINY;
                param_types.types[index + index + 1] = 0x80;
            } else {
                param_types.types[index + index] = MYSQL_FIELD_TYPE_TINY;
                param_types.types[index + index + 1] = 0x00;
            }
            buf.append(value, 1);
            break;
        case MYSQL_FIELD_TYPE_SHORT:
            if (is_unsigned) {
                param_types.types[index + index] = MYSQL_FIELD_TYPE_SHORT;
                param_types.types[index + index + 1] = 0x80;
            } else {
                param_types.types[index + index] = MYSQL_FIELD_TYPE_SHORT;
                param_types.types[index + index + 1] = 0x00;
            }
            {
                uint16_t v = butil::ByteSwapToLE16(*(uint16_t*)value);
                buf.append(&v, 2);
            }
            break;
        case MYSQL_FIELD_TYPE_LONG:
            if (is_unsigned) {
                param_types.types[index + index] = MYSQL_FIELD_TYPE_LONG;
                param_types.types[index + index + 1] = 0x80;

            } else {
                param_types.types[index + index] = MYSQL_FIELD_TYPE_LONG;
                param_types.types[index + index + 1] = 0x00;
            }
            {
                uint32_t v = butil::ByteSwapToLE32(*(uint32_t*)value);
                buf.append(&v, 4);
            }
            break;
        case MYSQL_FIELD_TYPE_LONGLONG:
            if (is_unsigned) {
                param_types.types[index + index] = MYSQL_FIELD_TYPE_LONGLONG;
                param_types.types[index + index + 1] = 0x80;
            } else {
                param_types.types[index + index] = MYSQL_FIELD_TYPE_LONGLONG;
                param_types.types[index + index + 1] = 0x00;
            }
            {
                uint64_t v = butil::ByteSwapToLE64(*(uint64_t*)value);
                buf.append(&v, 8);
            }
            break;
        case MYSQL_FIELD_TYPE_FLOAT:
            param_types.types[index + index] = MYSQL_FIELD_TYPE_FLOAT;
            param_types.types[index + index + 1] = 0x00;
            {
                uint32_t v = butil::ByteSwapToLE32(*(uint32_t*)value);
                buf.append(&v, 4);
            }
            break;
        case MYSQL_FIELD_TYPE_DOUBLE:
            param_types.types[index + index] = MYSQL_FIELD_TYPE_DOUBLE;
            param_types.types[index + index + 1] = 0x00;
            {
                uint64_t v = butil::ByteSwapToLE32(*(uint64_t*)value);
                buf.append(&v, 8);
            }
            break;
        case MYSQL_FIELD_TYPE_STRING: {
            const butil::StringPiece* p = (butil::StringPiece*)value;
            if (p == NULL || p->data() == NULL) {
                param_types.types[index + index] = MYSQL_FIELD_TYPE_NULL;
                param_types.types[index + index + 1] = 0x00;
                null_mask.mask[index / 8] |= 1 << (index & 7);
            } else {
                param_types.types[index + index] = MYSQL_FIELD_TYPE_STRING;
                param_types.types[index + index + 1] = 0x00;
                std::string len = pack_encode_length(p->size());
                buf.append(len);
                buf.append(p->data(), p->size());
            }
        } break;
        case MYSQL_FIELD_TYPE_NULL: {
            param_types.types[index + index] = MYSQL_FIELD_TYPE_NULL;
            param_types.types[index + index + 1] = 0x00;
            null_mask.mask[index / 8] |= 1 << (index & 7);
        } break;
        default:
            LOG(ERROR) << "wrong param type";
            return butil::Status(EINVAL, "[MysqlMakeExecute] wrong param type");
    }

    // all args have been building
    if (index + 1 == n) {
        buf.unsafe_assign(null_mask.area, null_mask.mask.data());
        buf.unsafe_assign(param_types.area, param_types.types.data());
    }

    return butil::Status::OK();
}

butil::Status MysqlMakeLongDataHeader(butil::IOBuf* outbuf,
                                      uint32_t stmt_id,
                                      uint16_t param_id,
                                      uint32_t body_size) {
    uint32_t header =
        butil::ByteSwapToLE32(1 + 4 + 2 + body_size);  // cmd_type + stmt_id + param_id + body_size
    outbuf->append(&header, mysql_header_size);
    outbuf->push_back(MYSQL_COM_STMT_SEND_LONG_DATA);
    const uint32_t si = butil::ByteSwapToLE32(stmt_id);
    outbuf->append(&si, 4);  // stmt_id
    const uint16_t pi = butil::ByteSwapToLE16(param_id);
    outbuf->append(&pi, 2);  // param_id
    return butil::Status::OK();
}

butil::Status MysqlMakeLongDataBody(MysqlStatementStub* stmt,
                                    uint16_t param_id,
                                    const butil::StringPiece& data) {
    stmt->save_long_data(param_id, data);
}

}  // namespace brpc
