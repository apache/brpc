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

namespace {
const uint32_t max_allowed_packet = 67108864;
const uint32_t max_packet_size = 16777215;

template <class H, class F, class D>
butil::Status MakePacket(butil::IOBuf* outbuf, const H& head, const F& func, const D& data) {
    long pkg_len = head.size() + data.size();
    if (pkg_len > max_allowed_packet) {
        return butil::Status(
            EINVAL,
            "[MakePacket] statement size is too big, maxAllowedPacket = %d, pkg_len = %ld",
            max_allowed_packet,
            pkg_len);
    }
    uint32_t size, header;
    uint8_t seq = 0;
    size_t offset = 0;
    for (; pkg_len > 0; pkg_len -= max_packet_size, ++seq) {
        if (pkg_len > max_packet_size) {
            size = max_packet_size;
        } else {
            size = pkg_len;
        }
        header = butil::ByteSwapToLE32(size);
        ((uint8_t*)&header)[3] = seq;
        outbuf->append(&header, 4);
        if (seq == 0) {
            const uint32_t old_size = outbuf->size();
            outbuf->append(head);
            size -= outbuf->size() - old_size;
        }
        func(outbuf, data, size, offset);
        offset += size;
    }

    return butil::Status::OK();
}

}  // namespace

butil::Status MysqlMakeCommand(butil::IOBuf* outbuf,
                               const MysqlCommandType type,
                               const butil::StringPiece& command) {
    if (outbuf == NULL || command.size() == 0) {
        return butil::Status(EINVAL, "[MysqlMakeCommand] Param[outbuf] or [stmt] is NULL");
    }
    auto func =
        [](butil::IOBuf* outbuf, const butil::StringPiece& command, size_t size, size_t offset) {
            outbuf->append(command.data() + offset, size);
        };
    butil::IOBuf head;
    head.push_back(type);
    return MakePacket(outbuf, head, func, command);
}

butil::Status MysqlMakeExecutePacket(butil::IOBuf* outbuf,
                                     uint32_t stmt_id,
                                     const butil::IOBuf& edata) {
    butil::IOBuf head;  // cmd_type + stmt_id + flag + reserved + body_size
    head.push_back(MYSQL_COM_STMT_EXECUTE);
    const uint32_t si = butil::ByteSwapToLE32(stmt_id);
    head.append(&si, 4);
    head.push_back('\0');
    head.push_back((char)0x01);
    head.push_back('\0');
    head.push_back('\0');
    head.push_back('\0');
    auto func = [](butil::IOBuf* outbuf, const butil::IOBuf& data, size_t size, size_t offset) {
        data.append_to(outbuf, size, offset);
    };
    return MakePacket(outbuf, head, func, edata);
}

butil::Status MysqlMakeExecuteData(MysqlStatementStub* stmt,
                                   uint16_t index,
                                   const void* value,
                                   MysqlFieldType type,
                                   bool is_unsigned) {
    const uint16_t n = stmt->stmt()->param_count();
    uint32_t long_data_size = max_allowed_packet / (n + 1);
    if (long_data_size < 64) {
        long_data_size = 64;
    }
    // if param count is zero finished.
    if (n == 0) {
        return butil::Status::OK();
    }
    butil::IOBuf& buf = stmt->execute_data();
    MysqlStatementStub::NullMask& null_mask = stmt->null_mask();
    MysqlStatementStub::ParamTypes& param_types = stmt->param_types();
    // else param number larger than zero.
    if (index >= n) {
        LOG(ERROR) << "too many params";
        return butil::Status(EINVAL, "[MysqlMakeExecuteData] too many params");
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
            buf.append(value, 4);
            break;
        case MYSQL_FIELD_TYPE_DOUBLE:
            param_types.types[index + index] = MYSQL_FIELD_TYPE_DOUBLE;
            param_types.types[index + index + 1] = 0x00;
            buf.append(value, 8);
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
                if (p->size() < long_data_size) {
                    std::string len = pack_encode_length(p->size());
                    buf.append(len);
                    buf.append(p->data(), p->size());
                } else {
                    stmt->save_long_data(index, *p);
                }
            }
        } break;
        case MYSQL_FIELD_TYPE_NULL: {
            param_types.types[index + index] = MYSQL_FIELD_TYPE_NULL;
            param_types.types[index + index + 1] = 0x00;
            null_mask.mask[index / 8] |= 1 << (index & 7);
        } break;
        default:
            LOG(ERROR) << "wrong param type";
            return butil::Status(EINVAL, "[MysqlMakeExecuteData] wrong param type");
    }

    // all args have been building
    if (index + 1 == n) {
        buf.unsafe_assign(null_mask.area, null_mask.mask.data());
        buf.unsafe_assign(param_types.area, param_types.types.data());
    }

    return butil::Status::OK();
}

butil::Status MysqlMakeLongDataPacket(butil::IOBuf* outbuf,
                                      uint32_t stmt_id,
                                      uint16_t param_id,
                                      const butil::IOBuf& ldata) {
    butil::IOBuf head;
    head.push_back(MYSQL_COM_STMT_SEND_LONG_DATA);
    const uint32_t si = butil::ByteSwapToLE32(stmt_id);
    outbuf->append(&si, 4);
    const uint16_t pi = butil::ByteSwapToLE16(param_id);
    outbuf->append(&pi, 2);
    size_t len, pos = 0;
    for (size_t pkg_len = ldata.size(); pkg_len > 0; pkg_len -= max_allowed_packet) {
        if (pkg_len < max_allowed_packet) {
            len = pkg_len;
        } else {
            len = max_allowed_packet;
        }
        butil::IOBuf data;
        ldata.append_to(&data, len, pos);
        pos += pkg_len;
        auto func = [](butil::IOBuf* outbuf, const butil::IOBuf& data, size_t size, size_t offset) {
            data.append_to(outbuf, size, offset);
        };
        auto rc = MakePacket(outbuf, head, func, data);
        if (!rc.ok()) {
            return rc;
        }
    }
    return butil::Status::OK();
}

}  // namespace brpc
