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

#include "brpc/policy/mysql/mysql_common.h"
#include "brpc/policy/mysql/mysql_reply.h"
#include "butil/logging.h"  // LOG()

namespace brpc {

#define MY_ALLOC_CHECK(expr)                     \
    do {                                         \
        if ((expr) == false) {                   \
            return PARSE_ERROR_ABSOLUTELY_WRONG; \
        }                                        \
    } while (0)

#define MY_PARSE_CHECK(expr)    \
    do {                        \
        ParseError rc = (expr); \
        if (rc != PARSE_OK) {   \
            return rc;          \
        }                       \
    } while (0)

template <class Type>
inline bool my_alloc_check(butil::Arena* arena, const size_t n, Type*& pointer) {
    if (pointer == nullptr) {
        pointer = (Type*)arena->allocate(sizeof(Type) * n);
        if (pointer == nullptr) {
            LOG(ERROR) << "my_alloc_check: arena failed to allocate " << (sizeof(Type) * n)
                       << " bytes (n=" << n << ")";
            return false;
        }
        for (size_t i = 0; i < n; ++i) {
            new (pointer + i) Type;
        }
    }
    return true;
}

template <>
inline bool my_alloc_check(butil::Arena* arena, const size_t n, char*& pointer) {
    if (pointer == nullptr) {
        pointer = (char*)arena->allocate(sizeof(char) * n);
        if (pointer == nullptr) {
            LOG(ERROR) << "my_alloc_check: arena failed to allocate " << n << " char bytes";
            return false;
        }
    }
    return true;
}

namespace {
struct MysqlHeader {
    uint32_t payload_size;
    uint32_t seq;
};
const char* digits01 =
    "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123"
    "456789";
const char* digits10 =
    "0000000000111111111122222222223333333333444444444455555555556666666666777777777788888888889999"
    "999999";

// Emit a zero fractional-second part ".000..." for a column that declares
// `decimal` digits but whose binary value carries no microsecond bytes on the
// wire (e.g. DATETIME(3) with a zero fraction is sent with len==7, TIME(3)
// with len==8). Keeps the formatted string length consistent with dstlen.
inline void write_zero_microsecs(uint8_t decimal, char* d) {
    if (decimal == 0 || decimal == 0x1f) {
        return;
    }
    uint8_t n = decimal > 6 ? 6 : decimal;
    size_t i = 0;
    d[i++] = '.';
    for (uint8_t k = 0; k < n; ++k) {
        d[i++] = '0';
    }
}
}  // namespace

const char* MysqlRspTypeToString(MysqlRspType type) {
    switch (type) {
        case MYSQL_RSP_OK:
            return "ok";
        case MYSQL_RSP_ERROR:
            return "error";
        case MYSQL_RSP_RESULTSET:
            return "resultset";
        case MYSQL_RSP_EOF:
            return "eof";
        case MYSQL_RSP_AUTH:
            return "auth";
        case MYSQL_RSP_AUTH_MORE_DATA:
            return "auth_more_data";
        case MYSQL_RSP_PREPARE_OK:
            return "prepare_ok";
        default:
            return "Unknown Response Type";
    }
}

// check if the buf is contain a full package
inline bool is_full_package(const butil::IOBuf& buf) {
    uint8_t header[4];
    const uint8_t* p = (const uint8_t*)buf.fetch(header, sizeof(header));
    if (p == nullptr) {
        return false;
    }
    uint32_t payload_size = mysql_uint3korr(p);
    if (buf.size() < payload_size + 4) {
        return false;
    }
    return true;
}
// if is eof package. Per the MySQL protocol, a 0xFE-leading packet is an
// EOF only when it is short (payload < 9 bytes); a longer 0xFE-leading
// packet is a row (or header) whose first length-encoded value needs the
// 8-byte form (e.g. a >=16MB LONGBLOB field).
inline bool is_an_eof(const butil::IOBuf& buf) {
    uint8_t tmp[5];
    const uint8_t* p = (const uint8_t*)buf.fetch(tmp, sizeof(tmp));
    if (p == nullptr) {
        return false;
    }
    return p[4] == MYSQL_RSP_EOF && mysql_uint3korr(p) < 9;
}
// parse header. When |payload| is not null, the packet's payload is cut
// from |buf| into it so that subsequent field decoding cannot run past the
// packet boundary into a coalesced next packet.
inline bool parse_header(butil::IOBuf& buf, MysqlHeader* value, butil::IOBuf* payload) {
    if (!is_full_package(buf)) {
        return false;
    }
    {
        uint8_t tmp[3];
        buf.cutn(tmp, sizeof(tmp));
        value->payload_size = mysql_uint3korr(tmp);
    }
    {
        uint8_t tmp;
        buf.cut1((char*)&tmp);
        value->seq = tmp;
    }
    if (payload != nullptr) {
        buf.cutn(payload, value->payload_size);
    }
    return true;
}
// use this carefully, we depending on parse_header for checking IOBuf contain full package
// Parse a MySQL length-encoded integer into |value|. Returns false when the
// prefix byte or its 2/3/8 value bytes are not fully present in |buf| (a
// truncated packet), or when the prefix is the invalid 0xFF marker; |value|
// is set to 0 in that case. Never reads uninitialized memory: on success
// every byte of the returned value was cut from |buf|.
inline bool parse_encode_length(butil::IOBuf& buf, uint64_t* value) {
    *value = 0;
    uint8_t f = 0;
    if (!buf.cut1((char*)&f)) {
        return false;
    }
    if (f <= 250) {
        *value = f;
        return true;
    }
    if (f == 251) {  // NULL
        return true;
    }
    size_t n = 0;
    if (f == 252) {
        n = 2;
    } else if (f == 253) {
        n = 3;
    } else if (f == 254) {
        n = 8;
    } else {
        return false;  // 0xFF is not a valid length-encoded prefix
    }
    uint8_t tmp[8];
    if (buf.cutn(tmp, n) != n) {
        return false;
    }
    switch (n) {
        case 2:
            *value = mysql_uint2korr(tmp);
            return true;
        case 3:
            *value = mysql_uint3korr(tmp);
            return true;
        default:
            *value = mysql_uint8korr(tmp);
            return true;
    }
}

// Parse one length-encoded string of a column definition into |out|. Both
// the length prefix and the payload must be fully contained in the remaining
// buffer; otherwise the packet is malformed and parsing fails instead of
// publishing uninitialized arena memory or desyncing the stream.
inline ParseError parse_column_string(butil::IOBuf& buf,
                                      butil::Arena* arena,
                                      butil::StringPiece* out,
                                      const char* field) {
    uint64_t len = 0;
    if (!parse_encode_length(buf, &len) || len > buf.size()) {
        LOG(WARNING) << "MysqlReply::Column::Parse: " << field << " length " << len
                     << " exceeds remaining buffer size " << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    char* d = nullptr;
    MY_ALLOC_CHECK(my_alloc_check(arena, (size_t)len, d));
    buf.cutn(d, len);
    out->set(d, len);
    return PARSE_OK;
}

// Cut exactly |n| fixed-width bytes from |buf| into |tmp|. parse_header
// guarantees the whole packet payload is already buffered, so a short read
// means the packet is malformed and must be rejected instead of leaving
// |tmp| (or the destination it feeds) partially uninitialized.
inline ParseError parse_fixed(butil::IOBuf& buf, void* tmp, size_t n) {
    if (buf.cutn(tmp, n) != n) {
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    return PARSE_OK;
}

ParseError MysqlReply::ConsumePartialIOBuf(butil::IOBuf& buf,
                                           butil::Arena* arena,
                                           bool is_auth,
                                           MysqlStmtType stmt_type,
                                           bool* more_results) {
    *more_results = false;
    if (!is_full_package(buf)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    uint8_t header[4 + 1];  // use the extra byte to judge message type
    const uint8_t* p = (const uint8_t*)buf.fetch(header, sizeof(header));
    if (_type == MYSQL_RSP_UNKNOWN &&
        (p == nullptr || mysql_uint3korr(p) == 0)) {
        LOG(ERROR) << "Invalid mysql packet with empty payload";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    uint8_t type = (_type == MYSQL_RSP_UNKNOWN) ? p[4] : (uint8_t)_type;
    // During the connection (auth) phase the server may send an AuthMoreData
    // packet (first byte 0x01) as part of the caching_sha2_password exchange
    // -- a fast-auth/full-auth status byte or the RSA public key.  It must be
    // recognized here BEFORE the greeting branch, because the greeting
    // (HandshakeV10, first byte 0x0a) and AuthMoreData (0x01) are otherwise
    // both non-OK/non-error auth packets.  Outside the auth phase, a first
    // byte of 0x01 is a normal resultset column-count, handled below.
    if (is_auth && type == 0x01) {
        // Peek the status byte after the 4-byte header + 0x01 tag.  A
        // fast-auth-success marker (0x03) is immediately followed by a
        // terminal OK packet, and the server typically ships both in one TCP
        // segment.  The response wrapper parses exactly one reply per pass
        // and rejects trailing bytes, so when the OK is already buffered we
        // skip the 0x03 packet here and let the OK become this reply (the
        // auth state machine then proceeds to send the first real query).
        // When the OK has not arrived yet, we expose the AuthMoreData so the
        // state machine can wait for it.  A full-auth marker (0x04) and the
        // RSA-pubkey payload always require a client response, so they are
        // never coalesced.
        uint8_t status[4 + 2];
        const uint8_t* sp = (const uint8_t*)buf.fetch(status, sizeof(status));
        const bool fast_auth_success = (sp != nullptr && sp[5] == 0x03);
        if (fast_auth_success) {
            // Determine, WITHOUT consuming anything, whether the OK packet
            // that follows the fast-auth marker is also fully buffered.
            const uint32_t amd_total = 4 + mysql_uint3korr(sp);
            butil::IOBuf rest;
            // Non-destructively copy the bytes that follow the 0x01 packet.
            buf.append_to(&rest, buf.size(), amd_total);
            if (!is_full_package(rest)) {
                // OK not arrived yet: expose the fast-auth marker untouched
                // and let the state machine wait for the next packet.
                _type = MYSQL_RSP_AUTH_MORE_DATA;
                MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.auth_more_data));
                MY_PARSE_CHECK(_data.auth_more_data->Parse(buf, arena));
                return PARSE_OK;
            }
            // Both packets buffered: drop the 0x01 packet from |buf| and
            // parse the following OK/ERR as this reply.
            butil::IOBuf discard;
            buf.cutn(&discard, amd_total);
            const uint8_t* p2 = (const uint8_t*)buf.fetch(header, sizeof(header));
            if (p2 == nullptr || mysql_uint3korr(p2) == 0) {
                LOG(ERROR) << "Invalid mysql packet with empty payload after "
                              "fast-auth marker";
                return PARSE_ERROR_ABSOLUTELY_WRONG;
            }
            type = p2[4];
        } else {
            _type = MYSQL_RSP_AUTH_MORE_DATA;
            MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.auth_more_data));
            MY_PARSE_CHECK(_data.auth_more_data->Parse(buf, arena));
            return PARSE_OK;
        }
    }
    if (is_auth && type != 0x00 && type != 0xFF) {
        _type = MYSQL_RSP_AUTH;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.auth));
        MY_PARSE_CHECK(_data.auth->Parse(buf, arena));
        return PARSE_OK;
    }
    // A 0xFE-leading packet is an EOF only when it is short (payload < 9
    // bytes, per the MySQL protocol); a longer 0xFE-leading packet starts a
    // length-encoded column count and belongs to the result-set branch below.
    const bool is_eof_packet =
        (type == 0xFE) && (_type == MYSQL_RSP_EOF || mysql_uint3korr(p) < 9);
    if (type == 0x00 && (is_auth || stmt_type != MYSQL_NEED_PREPARE)) {
        _type = MYSQL_RSP_OK;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.ok));
        MY_PARSE_CHECK(_data.ok->Parse(buf, arena));
        *more_results = _data.ok->status() & MYSQL_SERVER_MORE_RESULTS_EXISTS;
    } else if ((type == 0x00 && stmt_type == MYSQL_NEED_PREPARE) ||
               _type == MYSQL_RSP_PREPARE_OK) {
        _type = MYSQL_RSP_PREPARE_OK;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.prepare_ok));
        MY_PARSE_CHECK(_data.prepare_ok->Parse(buf, arena));
    } else if (type == 0xFF) {
        _type = MYSQL_RSP_ERROR;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.error));
        MY_PARSE_CHECK(_data.error->Parse(buf, arena));
    } else if (is_eof_packet) {
        _type = MYSQL_RSP_EOF;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.eof));
        MY_PARSE_CHECK(_data.eof->Parse(buf));
        *more_results = _data.eof->status() & MYSQL_SERVER_MORE_RESULTS_EXISTS;
    } else if (type >= 0x01 && type <= 0xFE) {
        // Any other leading byte is the length-encoded column count of a
        // result set, including the multi-byte prefixes 0xFB (251) and
        // 0xFC (252..65535) and a long 0xFE-leading count. These bytes must
        // not be matched against the synthetic MysqlRspType values: a fresh
        // 0xFC is a result-set header, not a prepare-ok (resume of an
        // already-classified reply is keyed on |_type| above instead).
        _type = MYSQL_RSP_RESULTSET;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.result_set));
        MY_PARSE_CHECK(_data.result_set->Parse(buf, arena, !(stmt_type == MYSQL_NORMAL_STATEMENT)));
        *more_results = _data.result_set->_eof2.status() & MYSQL_SERVER_MORE_RESULTS_EXISTS;
    } else {
        LOG(ERROR) << "Unknown Response Type "
                   << "type=" << unsigned(type) << " buf_size=" << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    return PARSE_OK;
}

void MysqlReply::Print(std::ostream& os) const {
    if (_type == MYSQL_RSP_AUTH) {
        const Auth& auth = *_data.auth;
        os << "\nprotocol:" << (unsigned)auth._protocol << "\nversion:" << auth._version
           << "\nthread_id:" << auth._thread_id << "\nsalt:" << auth._salt
           << "\ncapacity:" << auth._capability << "\nlanguage:" << (unsigned)auth._collation
           << "\nstatus:" << auth._status << "\nextended_capacity:" << auth._extended_capability
           << "\nauth_plugin_length:" << auth._auth_plugin_length << "\nsalt2:" << auth._salt2
           << "\nauth_plugin:" << auth._auth_plugin;
    } else if (_type == MYSQL_RSP_AUTH_MORE_DATA) {
        const AuthMoreData& amd = *_data.auth_more_data;
        os << "\nauth_more_data.size:" << amd._data.size();
    } else if (_type == MYSQL_RSP_OK) {
        const Ok& ok = *_data.ok;
        os << "\naffect_row:" << ok._affect_row << "\nindex:" << ok._index
           << "\nstatus:" << ok._status << "\nwarning:" << ok._warning << "\nmessage:" << ok._msg;
    } else if (_type == MYSQL_RSP_ERROR) {
        const Error& err = *_data.error;
        os << "\nerrcode:" << err._errcode << "\nstatus:" << err._status
           << "\nmessage:" << err._msg;
    } else if (_type == MYSQL_RSP_RESULTSET) {
        const ResultSet& r = *_data.result_set;
        os << "\nheader.column_count:" << r._header._column_count;
        for (uint64_t i = 0; i < r._header._column_count; ++i) {
            os << "\ncolumn[" << i << "].catalog:" << r._columns[i]._catalog << "\ncolumn[" << i
               << "].database:" << r._columns[i]._database << "\ncolumn[" << i
               << "].table:" << r._columns[i]._table << "\ncolumn[" << i
               << "].origin_table:" << r._columns[i]._origin_table << "\ncolumn[" << i
               << "].name:" << r._columns[i]._name << "\ncolumn[" << i
               << "].origin_name:" << r._columns[i]._origin_name << "\ncolumn[" << i
               << "].charset:" << (uint16_t)r._columns[i]._charset << "\ncolumn[" << i
               << "].length:" << r._columns[i]._length << "\ncolumn[" << i
               << "].type:" << (unsigned)r._columns[i]._type << "\ncolumn[" << i
               << "].flag:" << (unsigned)r._columns[i]._flag << "\ncolumn[" << i
               << "].decimal:" << (unsigned)r._columns[i]._decimal;
        }
        os << "\neof1.warning:" << r._eof1._warning;
        os << "\neof1.status:" << r._eof1._status;
        int n = 0;
        for (const Row* row = r._first->_next; row != r._last->_next; row = row->_next) {
            os << "\nrow(" << n++ << "):";
            for (uint64_t j = 0; j < r._header._column_count; ++j) {
                if (row->field(j).is_nil()) {
                    os << "NULL\t";
                    continue;
                }
                switch (row->field(j)._type) {
                    case MYSQL_FIELD_TYPE_NULL:
                        os << "NULL";
                        break;
                    case MYSQL_FIELD_TYPE_TINY:
                        if (r._columns[j]._flag & MYSQL_UNSIGNED_FLAG) {
                            os << unsigned(row->field(j).tiny());
                        } else {
                            os << signed(row->field(j).stiny());
                        }
                        break;
                    case MYSQL_FIELD_TYPE_SHORT:
                    case MYSQL_FIELD_TYPE_YEAR:
                        if (r._columns[j]._flag & MYSQL_UNSIGNED_FLAG) {
                            os << unsigned(row->field(j).small());
                        } else {
                            os << signed(row->field(j).ssmall());
                        }
                        break;
                    case MYSQL_FIELD_TYPE_INT24:
                    case MYSQL_FIELD_TYPE_LONG:
                        if (r._columns[j]._flag & MYSQL_UNSIGNED_FLAG) {
                            os << row->field(j).integer();
                        } else {
                            os << row->field(j).sinteger();
                        }
                        break;
                    case MYSQL_FIELD_TYPE_LONGLONG:
                        if (r._columns[j]._flag & MYSQL_UNSIGNED_FLAG) {
                            os << row->field(j).bigint();
                        } else {
                            os << row->field(j).sbigint();
                        }
                        break;
                    case MYSQL_FIELD_TYPE_FLOAT:
                        os << row->field(j).float32();
                        break;
                    case MYSQL_FIELD_TYPE_DOUBLE:
                        os << row->field(j).float64();
                        break;
                    case MYSQL_FIELD_TYPE_DECIMAL:
                    case MYSQL_FIELD_TYPE_NEWDECIMAL:
                    case MYSQL_FIELD_TYPE_VARCHAR:
                    case MYSQL_FIELD_TYPE_BIT:
                    case MYSQL_FIELD_TYPE_ENUM:
                    case MYSQL_FIELD_TYPE_SET:
                    case MYSQL_FIELD_TYPE_TINY_BLOB:
                    case MYSQL_FIELD_TYPE_MEDIUM_BLOB:
                    case MYSQL_FIELD_TYPE_LONG_BLOB:
                    case MYSQL_FIELD_TYPE_BLOB:
                    case MYSQL_FIELD_TYPE_VAR_STRING:
                    case MYSQL_FIELD_TYPE_STRING:
                    case MYSQL_FIELD_TYPE_GEOMETRY:
                    case MYSQL_FIELD_TYPE_JSON:
                    case MYSQL_FIELD_TYPE_TIME:
                    case MYSQL_FIELD_TYPE_DATE:
                    case MYSQL_FIELD_TYPE_NEWDATE:
                    case MYSQL_FIELD_TYPE_TIMESTAMP:
                    case MYSQL_FIELD_TYPE_DATETIME:
                        os << row->field(j).string();
                        break;
                    default:
                        os << "Unknown field type";
                }
                os << "\t";
            }
        }
        os << "\neof2.warning:" << r._eof2._warning;
        os << "\neof2.status:" << r._eof2._status;
    } else if (_type == MYSQL_RSP_EOF) {
        const Eof& e = *_data.eof;
        os << "\nwarning:" << e._warning << "\nstatus:" << e._status;
    } else if (_type == MYSQL_RSP_PREPARE_OK) {
        const PrepareOk& prep = *_data.prepare_ok;
        os << "\nstmt_id:" << prep._header._stmt_id
           << "\ncolumn_count:" << prep._header._column_count
           << "\nparam_count:" << prep._header._param_count;
        for (uint16_t i = 0; i < prep._header._param_count; ++i) {
            os << "\nparam[" << i << "].catalog:" << prep._params[i]._catalog << "\nparam[" << i
               << "].database:" << prep._params[i]._database << "\nparam[" << i
               << "].table:" << prep._params[i]._table << "\nparam[" << i
               << "].origin_table:" << prep._params[i]._origin_table << "\nparam[" << i
               << "].name:" << prep._params[i]._name << "\nparam[" << i
               << "].origin_name:" << prep._params[i]._origin_name << "\nparam[" << i
               << "].charset:" << (uint16_t)prep._params[i]._charset << "\nparam[" << i
               << "].length:" << prep._params[i]._length << "\nparam[" << i
               << "].type:" << (unsigned)prep._params[i]._type << "\nparam[" << i
               << "].flag:" << (unsigned)prep._params[i]._flag << "\nparam[" << i
               << "].decimal:" << (unsigned)prep._params[i]._decimal;
        }
        for (uint16_t i = 0; i < prep._header._column_count; ++i) {
            os << "\ncolumn[" << i << "].catalog:" << prep._columns[i]._catalog << "\ncolumn[" << i
               << "].database:" << prep._columns[i]._database << "\ncolumn[" << i
               << "].table:" << prep._columns[i]._table << "\ncolumn[" << i
               << "].origin_table:" << prep._columns[i]._origin_table << "\ncolumn[" << i
               << "].name:" << prep._columns[i]._name << "\ncolumn[" << i
               << "].origin_name:" << prep._columns[i]._origin_name << "\ncolumn[" << i
               << "].charset:" << (uint16_t)prep._columns[i]._charset << "\ncolumn[" << i
               << "].length:" << prep._columns[i]._length << "\ncolumn[" << i
               << "].type:" << (unsigned)prep._columns[i]._type << "\ncolumn[" << i
               << "].flag:" << (unsigned)prep._columns[i]._flag << "\ncolumn[" << i
               << "].decimal:" << (unsigned)prep._columns[i]._decimal;
        }
    } else {
        os << "Unknown response type";
    }
}

ParseError MysqlReply::Auth::Parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    const std::string delim(1, 0x00);
    MysqlHeader header;
    butil::IOBuf payload;
    if (!parse_header(buf, &header, &payload)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    MY_PARSE_CHECK(parse_fixed(payload, &_protocol, 1));
    {
        butil::IOBuf version;
        if (payload.cut_until(&version, delim) != 0) {
            LOG(WARNING) << "MysqlReply::Auth::Parse: server version is not NUL-terminated";
            return PARSE_ERROR_ABSOLUTELY_WRONG;
        }
        char* d = nullptr;
        MY_ALLOC_CHECK(my_alloc_check(arena, version.size(), d));
        version.copy_to(d);
        _version.set(d, version.size());
    }
    {
        uint8_t tmp[4];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _thread_id = mysql_uint4korr(tmp);
    }
    {
        butil::IOBuf salt;
        if (payload.cut_until(&salt, delim) != 0) {
            LOG(WARNING) << "MysqlReply::Auth::Parse: salt is not NUL-terminated";
            return PARSE_ERROR_ABSOLUTELY_WRONG;
        }
        char* d = nullptr;
        MY_ALLOC_CHECK(my_alloc_check(arena, salt.size(), d));
        salt.copy_to(d);
        _salt.set(d, salt.size());
    }
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, &tmp, sizeof(tmp)));
        _capability = mysql_uint2korr(tmp);
    }
    MY_PARSE_CHECK(parse_fixed(payload, &_collation, 1));
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _status = mysql_uint2korr(tmp);
    }
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _extended_capability = mysql_uint2korr(tmp);
    }
    MY_PARSE_CHECK(parse_fixed(payload, &_auth_plugin_length, 1));
    payload.pop_front(10);
    {
        butil::IOBuf salt2;
        if (payload.cut_until(&salt2, delim) != 0) {
            LOG(WARNING) << "MysqlReply::Auth::Parse: salt2 is not NUL-terminated";
            return PARSE_ERROR_ABSOLUTELY_WRONG;
        }
        char* d = nullptr;
        MY_ALLOC_CHECK(my_alloc_check(arena, salt2.size(), d));
        salt2.copy_to(d);
        _salt2.set(d, salt2.size());
    }
    {
        if (_auth_plugin_length > payload.size()) {
            LOG(ERROR) << "MysqlReply::Auth::Parse: auth_plugin length " << _auth_plugin_length
                       << " exceeds remaining buffer size " << payload.size();
            return PARSE_ERROR_ABSOLUTELY_WRONG;
        }
        char* d = nullptr;
        MY_ALLOC_CHECK(my_alloc_check(arena, _auth_plugin_length, d));
        payload.cutn(d, _auth_plugin_length);
        _auth_plugin.set(d, _auth_plugin_length);
    }
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::AuthMoreData::Parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    MysqlHeader header;
    butil::IOBuf payload;
    if (!parse_header(buf, &header, &payload)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    _seq = (uint8_t)header.seq;
    // Drop the 0x01 AuthMoreData tag; expose only the bytes after it (a
    // single status byte 0x03/0x04, or the PEM-encoded RSA public key).
    payload.pop_front(1);
    const int64_t len = (int64_t)payload.size();
    if (len > 0) {
        char* d = nullptr;
        MY_ALLOC_CHECK(my_alloc_check(arena, len, d));
        payload.cutn(d, len);
        _data.set(d, len);
    } else {
        _data.set(nullptr, 0);
    }
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::ResultSetHeader::Parse(butil::IOBuf& buf) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    MysqlHeader header;
    butil::IOBuf payload;
    if (!parse_header(buf, &header, &payload)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    if (!parse_encode_length(payload, &_column_count)) {
        LOG(ERROR) << "MysqlReply::ResultSetHeader::Parse: truncated column count";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    // Guard against an absurd/malicious column count driving unbounded
    // allocations downstream (per-column arrays and the row NULL-bitmap).
    // MySQL's hard limit is 4096 columns per table; 65535 is a generous cap
    // that no legitimate result set exceeds.
    if (_column_count > 65535) {
        LOG(ERROR) << "illegal column count " << _column_count;
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    if (!payload.empty()) {
        if (!parse_encode_length(payload, &_extra_msg)) {
            LOG(ERROR) << "MysqlReply::ResultSetHeader::Parse: truncated extra message";
            return PARSE_ERROR_ABSOLUTELY_WRONG;
        }
    } else {
        _extra_msg = 0;
    }
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::Column::Parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    MysqlHeader header;
    butil::IOBuf payload;
    if (!parse_header(buf, &header, &payload)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }

    // Each length-encoded string must fit within the remaining payload; an
    // oversized length would otherwise drive my_alloc_check/cutn/.set past the
    // packet (mirrors the hardened auth_plugin path above).
    MY_PARSE_CHECK(parse_column_string(payload, arena, &_catalog, "catalog"));
    MY_PARSE_CHECK(parse_column_string(payload, arena, &_database, "database"));
    MY_PARSE_CHECK(parse_column_string(payload, arena, &_table, "table"));
    MY_PARSE_CHECK(parse_column_string(payload, arena, &_origin_table, "origin_table"));
    MY_PARSE_CHECK(parse_column_string(payload, arena, &_name, "name"));
    MY_PARSE_CHECK(parse_column_string(payload, arena, &_origin_name, "origin_name"));
    payload.pop_front(1);
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _charset = mysql_uint2korr(tmp);
    }
    {
        uint8_t tmp[4];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _length = mysql_uint4korr(tmp);
    }
    MY_PARSE_CHECK(parse_fixed(payload, &_type, 1));
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _flag = (MysqlFieldFlag)mysql_uint2korr(tmp);
    }
    MY_PARSE_CHECK(parse_fixed(payload, &_decimal, 1));
    payload.pop_front(2);
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::Ok::Parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    MysqlHeader header;
    butil::IOBuf payload;
    if (!parse_header(buf, &header, &payload)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    payload.pop_front(1);

    if (!parse_encode_length(payload, &_affect_row)) {
        LOG(WARNING) << "MysqlReply::Ok::Parse: truncated affected-rows value";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    if (!parse_encode_length(payload, &_index)) {
        LOG(WARNING) << "MysqlReply::Ok::Parse: truncated last-insert-id value";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _status = mysql_uint2korr(tmp);
    }
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _warning = mysql_uint2korr(tmp);
    }

    const int64_t len = (int64_t)payload.size();
    if (len > 0) {
        char* msg = nullptr;
        MY_ALLOC_CHECK(my_alloc_check(arena, len, msg));
        payload.cutn(msg, len);
        _msg.set(msg, len);
    }
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::Eof::Parse(butil::IOBuf& buf) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    MysqlHeader header;
    butil::IOBuf payload;
    if (!parse_header(buf, &header, &payload)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    payload.pop_front(1);
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _warning = mysql_uint2korr(tmp);
    }
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _status = mysql_uint2korr(tmp);
    }
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::Error::Parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    MysqlHeader header;
    butil::IOBuf payload;
    if (!parse_header(buf, &header, &payload)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    // error message, Null-Terminated string.
    // payload layout: 0xFF(1) + errcode(2) + '#'(1) + sql_state(5) = 9 bytes;
    // guard against a malformed short packet to avoid reading past the
    // packet boundary.
    if (header.payload_size < 9) {
        LOG(WARNING) << "MysqlReply::Error::Parse: truncated ERR packet, payload_size "
                   << header.payload_size << " < 9 (0xFF+errcode+'#'+sql_state)";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    payload.pop_front(1);  // 0xFF
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _errcode = mysql_uint2korr(tmp);
    }
    payload.pop_front(1);  // '#'
    // 5 byte server status
    char* status = nullptr;
    MY_ALLOC_CHECK(my_alloc_check(arena, 5, status));
    MY_PARSE_CHECK(parse_fixed(payload, status, 5));
    _status.set(status, 5);
    const uint64_t len = payload.size();
    char* msg = nullptr;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, msg));
    payload.cutn(msg, len);
    _msg.set(msg, len);
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::Row::Parse(butil::IOBuf& buf,
                                  const MysqlReply::Column* columns,
                                  uint64_t column_count,
                                  MysqlReply::Field* fields,
                                  bool binary,
                                  butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    MysqlHeader header;
    butil::IOBuf payload;
    if (!parse_header(buf, &header, &payload)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    if (!binary) {  // mysql text protocol
        for (uint64_t i = 0; i < column_count; ++i) {
            MY_PARSE_CHECK(fields[i].Parse(payload, columns + i, arena));
        }
    } else {  // mysql binary protocol
        uint8_t hdr = 0;
        MY_PARSE_CHECK(parse_fixed(payload, &hdr, 1));
        if (hdr != 0x00) {
            LOG(WARNING) << "MysqlReply::Row::Parse: binary row packet header byte is "
                       << unsigned(hdr) << ", expected 0x00";
            return PARSE_ERROR_ABSOLUTELY_WRONG;
        }
        // NULL-bitmap, [(column-count + 7 + 2) / 8 bytes]. Allocate from the
        // arena instead of a stack VLA: column_count is attacker-controlled
        // (length-encoded in the result-set header), so a large value would
        // otherwise be an unbounded stack allocation / stack overflow.
        const uint64_t size = ((column_count + 7 + 2) >> 3);
        uint8_t* null_mask = nullptr;
        MY_ALLOC_CHECK(my_alloc_check(arena, (size_t)size, null_mask));
        for (uint64_t i = 0; i < size; ++i) {
            null_mask[i] = 0;
        }
        MY_PARSE_CHECK(parse_fixed(payload, null_mask, (size_t)size));
        for (uint64_t i = 0; i < column_count; ++i) {
            MY_PARSE_CHECK(fields[i].Parse(payload, columns + i, i, column_count, null_mask, arena));
        }
    }
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::Field::Parse(butil::IOBuf& buf,
                                    const MysqlReply::Column* column,
                                    butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    // field type
    _type = column->_type;
    // is unsigned flag set
    _unsigned = column->_flag & MYSQL_UNSIGNED_FLAG;
    // parse encode length
    uint64_t len = 0;
    if (!parse_encode_length(buf, &len)) {
        LOG(WARNING) << "MysqlReply::Field::Parse: truncated length-encoded field length";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    // is it null?
    if (len == 0 && !(column->_flag & MYSQL_NOT_NULL_FLAG)) {
        _is_nil = true;
        set_parsed();
        return PARSE_OK;
    }
    // The length-encoded value must fit in the remaining buffer. cutn clamps to
    // what is available, so an oversized len leaves the tail of the len-byte
    // allocation uninitialized while _data.str is published as len bytes,
    // exposing uninitialized arena memory and desyncing the packet stream.
    // The binary Field::Parse and Column::Parse paths already guard this.
    if (len > buf.size()) {
        LOG(WARNING) << "MysqlReply::Field::Parse: field length " << len
                   << " exceeds remaining buffer size " << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    // field is not null
    butil::IOBuf str;
    buf.cutn(&str, len);
    switch (_type) {
        case MYSQL_FIELD_TYPE_NULL:
            _is_nil = true;
            break;
        case MYSQL_FIELD_TYPE_TINY:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                _data.tiny = strtoul(str.to_string().c_str(), nullptr, 10);
            } else {
                _data.stiny = strtol(str.to_string().c_str(), nullptr, 10);
            }
            break;
        case MYSQL_FIELD_TYPE_SHORT:
        case MYSQL_FIELD_TYPE_YEAR:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                _data.small = strtoul(str.to_string().c_str(), nullptr, 10);
            } else {
                _data.ssmall = strtol(str.to_string().c_str(), nullptr, 10);
            }
            break;
        case MYSQL_FIELD_TYPE_INT24:
        case MYSQL_FIELD_TYPE_LONG:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                _data.integer = strtoul(str.to_string().c_str(), nullptr, 10);
            } else {
                _data.sinteger = strtol(str.to_string().c_str(), nullptr, 10);
            }
            break;
        case MYSQL_FIELD_TYPE_LONGLONG:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                _data.bigint = strtoul(str.to_string().c_str(), nullptr, 10);
            } else {
                _data.sbigint = strtol(str.to_string().c_str(), nullptr, 10);
            }
            break;
        case MYSQL_FIELD_TYPE_FLOAT:
            _data.float32 = strtof(str.to_string().c_str(), nullptr);
            break;
        case MYSQL_FIELD_TYPE_DOUBLE:
            _data.float64 = strtod(str.to_string().c_str(), nullptr);
            break;
        case MYSQL_FIELD_TYPE_DECIMAL:
        case MYSQL_FIELD_TYPE_NEWDECIMAL:
        case MYSQL_FIELD_TYPE_VARCHAR:
        case MYSQL_FIELD_TYPE_BIT:
        case MYSQL_FIELD_TYPE_ENUM:
        case MYSQL_FIELD_TYPE_SET:
        case MYSQL_FIELD_TYPE_TINY_BLOB:
        case MYSQL_FIELD_TYPE_MEDIUM_BLOB:
        case MYSQL_FIELD_TYPE_LONG_BLOB:
        case MYSQL_FIELD_TYPE_BLOB:
        case MYSQL_FIELD_TYPE_VAR_STRING:
        case MYSQL_FIELD_TYPE_STRING:
        case MYSQL_FIELD_TYPE_GEOMETRY:
        case MYSQL_FIELD_TYPE_JSON:
        case MYSQL_FIELD_TYPE_TIME:
        case MYSQL_FIELD_TYPE_DATE:
        case MYSQL_FIELD_TYPE_NEWDATE:
        case MYSQL_FIELD_TYPE_TIMESTAMP:
        case MYSQL_FIELD_TYPE_DATETIME: {
            char* d = nullptr;
            MY_ALLOC_CHECK(my_alloc_check(arena, len, d));
            str.copy_to(d);
            _data.str.set(d, len);
        } break;
        default:
            LOG(ERROR) << "Unknown field type";
            set_parsed();
            return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::Field::Parse(butil::IOBuf& buf,
                                    const MysqlReply::Column* column,
                                    uint64_t column_index,
                                    uint64_t column_count,
                                    const uint8_t* null_mask,
                                    butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    // field type
    _type = column->_type;
    // is unsigned flag set
    _unsigned = column->_flag & MYSQL_UNSIGNED_FLAG;
    // (byte >> bit-pos) % 2 == 1
    if (((null_mask[(column_index + 2) >> 3] >> ((column_index + 2) & 7)) & 1) == 1) {
        _is_nil = true;
        set_parsed();
        return PARSE_OK;
    }

    switch (_type) {
        case MYSQL_FIELD_TYPE_NULL:
            _is_nil = true;
            break;
        case MYSQL_FIELD_TYPE_TINY:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                MY_PARSE_CHECK(parse_fixed(buf, &_data.tiny, 1));
            } else {
                MY_PARSE_CHECK(parse_fixed(buf, &_data.stiny, 1));
            }
            break;
        case MYSQL_FIELD_TYPE_SHORT:
        case MYSQL_FIELD_TYPE_YEAR:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                uint8_t* p = (uint8_t*)&_data.small;
                MY_PARSE_CHECK(parse_fixed(buf, p, 2));
                _data.small = mysql_uint2korr(p);
            } else {
                uint8_t* p = (uint8_t*)&_data.ssmall;
                MY_PARSE_CHECK(parse_fixed(buf, p, 2));
                _data.ssmall = (int16_t)mysql_uint2korr(p);
            }
            break;
        case MYSQL_FIELD_TYPE_INT24:
        case MYSQL_FIELD_TYPE_LONG:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                uint8_t* p = (uint8_t*)&_data.integer;
                MY_PARSE_CHECK(parse_fixed(buf, p, 4));
                _data.integer = mysql_uint4korr(p);
            } else {
                uint8_t* p = (uint8_t*)&_data.sinteger;
                MY_PARSE_CHECK(parse_fixed(buf, p, 4));
                _data.sinteger = (int32_t)mysql_uint4korr(p);
            }
            break;
        case MYSQL_FIELD_TYPE_LONGLONG:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                uint8_t* p = (uint8_t*)&_data.bigint;
                MY_PARSE_CHECK(parse_fixed(buf, p, 8));
                _data.bigint = mysql_uint8korr(p);
            } else {
                uint8_t* p = (uint8_t*)&_data.sbigint;
                MY_PARSE_CHECK(parse_fixed(buf, p, 8));
                _data.sbigint = (int64_t)mysql_uint8korr(p);
            }
            break;
        case MYSQL_FIELD_TYPE_FLOAT: {
            uint8_t* p = (uint8_t*)&_data.float32;
            MY_PARSE_CHECK(parse_fixed(buf, p, 4));
        } break;
        case MYSQL_FIELD_TYPE_DOUBLE: {
            uint8_t* p = (uint8_t*)&_data.float64;
            MY_PARSE_CHECK(parse_fixed(buf, p, 8));
        } break;
        case MYSQL_FIELD_TYPE_DECIMAL:
        case MYSQL_FIELD_TYPE_NEWDECIMAL:
        case MYSQL_FIELD_TYPE_VARCHAR:
        case MYSQL_FIELD_TYPE_BIT:
        case MYSQL_FIELD_TYPE_ENUM:
        case MYSQL_FIELD_TYPE_SET:
        case MYSQL_FIELD_TYPE_TINY_BLOB:
        case MYSQL_FIELD_TYPE_MEDIUM_BLOB:
        case MYSQL_FIELD_TYPE_LONG_BLOB:
        case MYSQL_FIELD_TYPE_BLOB:
        case MYSQL_FIELD_TYPE_VAR_STRING:
        case MYSQL_FIELD_TYPE_STRING:
        case MYSQL_FIELD_TYPE_GEOMETRY:
        case MYSQL_FIELD_TYPE_JSON: {
            uint64_t len = 0;
            if (!parse_encode_length(buf, &len)) {
                LOG(WARNING) << "MysqlReply::Field::Parse (binary): truncated string field length";
                return PARSE_ERROR_ABSOLUTELY_WRONG;
            }
            // is it null?
            if (len == 0 && !(column->_flag & MYSQL_NOT_NULL_FLAG)) {
                _is_nil = true;
                set_parsed();
                return PARSE_OK;
            }
            // field is not null
            if (len > buf.size()) {
                LOG(WARNING) << "MysqlReply::Field::Parse (binary): string field length " << len
                           << " exceeds remaining buffer size " << buf.size();
                return PARSE_ERROR_ABSOLUTELY_WRONG;
            }
            char* d = nullptr;
            MY_ALLOC_CHECK(my_alloc_check(arena, len, d));
            buf.cutn(d, len);
            _data.str.set(d, len);
        } break;
        case MYSQL_FIELD_TYPE_NEWDATE:      // Date YYYY-MM-DD
        case MYSQL_FIELD_TYPE_DATE:         // Date YYYY-MM-DD
        case MYSQL_FIELD_TYPE_DATETIME:     // Timestamp YYYY-MM-DD HH:MM:SS[.fractal]
        case MYSQL_FIELD_TYPE_TIMESTAMP: {  // Timestamp YYYY-MM-DD HH:MM:SS[.fractal]
            ParseError rc = ParseBinaryDataTime(buf, column, _data.str, arena);
            if (rc != PARSE_OK) {
                return rc;
            }
        } break;
        case MYSQL_FIELD_TYPE_TIME: {  // Time [-][H]HH:MM:SS[.fractal]
            ParseError rc = ParseBinaryTime(buf, column, _data.str, arena);
            if (rc != PARSE_OK) {
                return rc;
            }
        } break;
        default:
            LOG(ERROR) << "Unknown field type";
            return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::Field::ParseBinaryTime(butil::IOBuf& buf,
                                              const MysqlReply::Column* column,
                                              butil::StringPiece& str,
                                              butil::Arena* arena) {

    uint64_t len = 0;
    if (!parse_encode_length(buf, &len)) {
        LOG(ERROR) << "invalid TIME packet length";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    // A length of 0, 8 or 12 are the only legal binary TIME encodings. Anything
    // else is a malformed packet -- reject it rather than reading past the value.
    // NOTE: len == 0 is NOT a NULL value (NULL is signalled by the row
    // NULL-bitmap, handled by the caller before we are reached); it is the zero
    // TIME value "00:00:00" with no field bytes on the wire.
    if (len != 0 && len != 8 && len != 12) {
        LOG(ERROR) << "invalid TIME packet length " << len;
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    // Never read more value bytes than the packet actually carries.
    if ((uint64_t)len > buf.size()) {
        LOG(ERROR) << "TIME value length " << len << " exceeds buffer size " << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }

    // Base "HH:MM:SS" is 8 bytes, but MySQL binary TIME spans up to 838 hours
    // and may be negative, so reserve 2 extra bytes for a leading sign and a
    // possible 3rd hour digit ("-838:59:59[.ffffff]").
    uint8_t dstlen;
    switch (column->_decimal) {
        case 0x00:
        case 0x1f:
            dstlen = 8 + 2;
            break;
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
            dstlen = 8 + 2 + 1 + column->_decimal;
            break;
        default:
            LOG(ERROR) << "protocol error, illegal decimals value " << column->_decimal;
            return PARSE_ERROR_ABSOLUTELY_WRONG;
    }

    size_t i = 0;
    char* d = nullptr;
    MY_ALLOC_CHECK(my_alloc_check(arena, dstlen + 2, d));
    d[dstlen] = '\0';
    d[dstlen + 1] = '\0';
    // Read only the fields that are present for this `len`; absent fields are 0.
    // len == 0  -> no bytes: "00:00:00".
    // len == 8  -> is_negative(1) days(4 LE) hour(1) min(1) sec(1), no micros.
    // len == 12 -> + micros(4 LE).
    uint32_t day = 0;
    uint8_t neg = 0, hour = 0, min = 0, sec = 0;

    if (len >= 8) {
        MY_PARSE_CHECK(parse_fixed(buf, &neg, 1));
        MY_PARSE_CHECK(parse_fixed(buf, &day, 4));
        day = mysql_uint4korr((uint8_t*)&day);
        MY_PARSE_CHECK(parse_fixed(buf, &hour, 1));
        MY_PARSE_CHECK(parse_fixed(buf, &min, 1));
        MY_PARSE_CHECK(parse_fixed(buf, &sec, 1));
    }

    // Validate field ranges so the formatted output cannot overflow the buffer
    // and so we never index past digits01/digits10. MySQL caps TIME at 838
    // hours and 59 min/sec; total_hour is at most 3 digits, which dstlen sizes
    // for. A larger total_hour would emit >3 hour digits and overrun `d`.
    if (neg > 1 || min > 59 || sec > 59) {
        LOG(ERROR) << "invalid TIME field value";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    // MySQL binary TIME spans up to 838 hours, so the total can exceed 255 and
    // must be accumulated in a wider type than the 1-byte wire field.
    uint32_t total_hour = (uint32_t)hour + day * 24;
    if (total_hour > 838) {
        LOG(ERROR) << "TIME total hours " << total_hour << " exceeds MySQL max 838";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }

    if (neg == 1) {
        d[i++] = '-';
    }
    if (total_hour >= 100) {
        // total_hour is in [100, 838]: exactly 3 digits, which dstlen reserves
        // space for. Emit hundreds/tens/units directly; the digits01/digits10
        // lookup tables only cover 0..99 so they cannot be indexed by the full
        // value here.
        d[i++] = (char)('0' + total_hour / 100);
        const uint32_t rem = total_hour % 100;
        d[i++] = digits10[rem];
        d[i++] = digits01[rem];
    } else {
        d[i++] = digits10[total_hour];
        d[i++] = digits01[total_hour];
    }

    d[i++] = ':';
    d[i++] = digits10[min];
    d[i++] = digits01[min];
    d[i++] = ':';
    d[i++] = digits10[sec];
    d[i++] = digits01[sec];

    // Microseconds are only present on the wire when len == 12; for len == 0 or
    // len == 8 there are no microsecond bytes even if the column declares
    // decimals.
    ParseError rc;
    if (len == 12) {
        rc = ParseMicrosecs(buf, column->_decimal, d + i);
    } else {
        write_zero_microsecs(column->_decimal, d + i);
        rc = PARSE_OK;
    }
    if (rc == PARSE_OK) {
        // TIME is variable-width (optional sign, 2- or 3+-digit hour), so report
        // the EXACT bytes actually written: i (through ":SS") plus the
        // fractional part -- '.' + decimal digits when decimal is 1..6, else
        // nothing (decimal 0 or 0x1f writes no fractional bytes).
        const size_t micros_len =
            (column->_decimal >= 1 && column->_decimal <= 6) ? (size_t)column->_decimal + 1 : 0;
        str.set(d, i + micros_len);
    }
    return rc;
}

ParseError MysqlReply::Field::ParseBinaryDataTime(butil::IOBuf& buf,
                                                  const MysqlReply::Column* column,
                                                  butil::StringPiece& str,
                                                  butil::Arena* arena) {
    uint64_t len = 0;
    if (!parse_encode_length(buf, &len)) {
        LOG(ERROR) << "illegal date time length";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    // A length of 0, 4, 7 or 11 are the only legal binary DATE/DATETIME/
    // TIMESTAMP encodings. Reject anything else rather than over-reading.
    // NOTE: len == 0 is NOT a NULL value (NULL is signalled by the row
    // NULL-bitmap, handled by the caller before we are reached); it is the zero
    // value "0000-00-00 00:00:00" (or "0000-00-00" for DATE) with no field
    // bytes on the wire.
    if (len != 0 && len != 4 && len != 7 && len != 11) {
        LOG(ERROR) << "illegal date time length " << len;
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    // Never read more value bytes than the packet actually carries.
    if ((uint64_t)len > buf.size()) {
        LOG(ERROR) << "DATETIME value length " << len << " exceeds buffer size " << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    // A DATE column carries only the date part; a time-of-day part on the wire
    // would not fit its 10-byte output buffer, so reject those packets.
    const bool is_date = (column->_type == MYSQL_FIELD_TYPE_DATE ||
                          column->_type == MYSQL_FIELD_TYPE_NEWDATE);
    if (is_date && len != 0 && len != 4) {
        LOG(ERROR) << "illegal DATE length " << len;
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }

    uint8_t dstlen;
    if (is_date) {
        dstlen = 10;
    } else {
        switch (column->_decimal) {
            case 0x00:
            case 0x1f:
                dstlen = 19;
                break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
                dstlen = 19 + 1 + column->_decimal;
                break;
            default:
                LOG(ERROR) << "protocol error, illegal decimal value " << column->_decimal;
                return PARSE_ERROR_ABSOLUTELY_WRONG;
        }
    }

    size_t i = 0;
    char* d = nullptr;
    MY_ALLOC_CHECK(my_alloc_check(arena, dstlen, d));
    // Read only the fields present for this `len`; absent fields are 0.
    // len == 0  -> no bytes (all-zero value).
    // len == 4  -> year(2 LE) month(1) day(1) only -> "YYYY-MM-DD".
    // len == 7  -> + hour(1) min(1) sec(1) -> "YYYY-MM-DD HH:MM:SS".
    // len == 11 -> + micros(4 LE).
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (len >= 4) {
        MY_PARSE_CHECK(parse_fixed(buf, &year, 2));
        year = mysql_uint2korr((uint8_t*)&year);
        MY_PARSE_CHECK(parse_fixed(buf, &month, 1));
        MY_PARSE_CHECK(parse_fixed(buf, &day, 1));
    }
    if (len >= 7) {
        MY_PARSE_CHECK(parse_fixed(buf, &hour, 1));
        MY_PARSE_CHECK(parse_fixed(buf, &min, 1));
        MY_PARSE_CHECK(parse_fixed(buf, &sec, 1));
    }

    // Validate field ranges: year < 10000 keeps the 4-digit year within bounds
    // and keeps every two-digit component inside the digits01/digits10 tables
    // (which only cover 0..99), preventing both buffer overrun and OOB reads.
    if (year > 9999 || month > 99 || day > 99 || hour > 99 || min > 59 || sec > 59) {
        LOG(ERROR) << "invalid DATE/DATETIME field value";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }

    const uint8_t pt = year / 100;
    const uint8_t p1 = year - (100 * pt);
    d[i++] = digits10[pt];
    d[i++] = digits01[pt];
    d[i++] = digits10[p1];
    d[i++] = digits01[p1];
    d[i++] = '-';
    d[i++] = digits10[month];
    d[i++] = digits01[month];
    d[i++] = '-';
    d[i++] = digits10[day];
    d[i++] = digits01[day];

    if (is_date) {
        // DATE column: only "YYYY-MM-DD" (10 bytes) is meaningful.
        str.set(d, i);
        return PARSE_OK;
    }

    // DATETIME/TIMESTAMP column: always emit the full "YYYY-MM-DD HH:MM:SS"
    // form. When len == 4 the time-of-day fields were absent on the wire and
    // default to zero ("00:00:00"); we still write those bytes here so the
    // reported length matches what was actually written.
    d[i++] = ' ';
    d[i++] = digits10[hour];
    d[i++] = digits01[hour];
    d[i++] = ':';
    d[i++] = digits10[min];
    d[i++] = digits01[min];
    d[i++] = ':';
    d[i++] = digits10[sec];
    d[i++] = digits01[sec];

    // Microseconds are only present on the wire when len == 11; for len == 7
    // there are no microsecond bytes even if the column declares decimals.
    ParseError rc;
    if (len == 11) {
        rc = ParseMicrosecs(buf, column->_decimal, d + i);
    } else {
        write_zero_microsecs(column->_decimal, d + i);
        rc = PARSE_OK;
    }
    if (rc == PARSE_OK) {
        // Report the EXACT bytes written: "YYYY-MM-DD HH:MM:SS" (i == 19) plus
        // the fractional part -- '.' + decimal digits when decimal is 1..6, else
        // nothing.
        const size_t micros_len =
            (column->_decimal >= 1 && column->_decimal <= 6) ? (size_t)column->_decimal + 1 : 0;
        str.set(d, i + micros_len);
    }
    return rc;
}

ParseError MysqlReply::Field::ParseMicrosecs(butil::IOBuf& buf, uint8_t decimal, char* d) {
    size_t i = 0;
    uint32_t microsecs;
    uint8_t p1, p2, p3;
    // Always consume the 4 microsecond bytes present on the wire (the caller
    // only invokes this when the value length includes them); format them only
    // when the column declares 1..6 fractional digits (0 / 0x1f == no fraction).
    MY_PARSE_CHECK(parse_fixed(buf, &microsecs, 4));
    if (decimal == 0 || decimal > 6) {
        return PARSE_OK;
    }
    microsecs = mysql_uint4korr((uint8_t*)&microsecs);
    p1 = microsecs / 10000;
    microsecs -= 10000 * p1;
    p2 = microsecs / 100;
    microsecs -= 100 * p2;
    p3 = microsecs;

    switch (decimal) {
        case 1:
            d[i++] = '.';
            d[i++] = digits10[p1];
            break;
        case 2:
            d[i++] = '.';
            d[i++] = digits10[p1];
            d[i++] = digits01[p1];
            break;
        case 3:
            d[i++] = '.';
            d[i++] = digits10[p1];
            d[i++] = digits01[p1];
            d[i++] = digits10[p2];
            break;
        case 4:
            d[i++] = '.';
            d[i++] = digits10[p1];
            d[i++] = digits01[p1];
            d[i++] = digits10[p2];
            d[i++] = digits01[p2];
            break;
        case 5:
            d[i++] = '.';
            d[i++] = digits10[p1];
            d[i++] = digits01[p1];
            d[i++] = digits10[p2];
            d[i++] = digits01[p2];
            d[i++] = digits10[p3];
            break;
        default:
            d[i++] = '.';
            d[i++] = digits10[p1];
            d[i++] = digits01[p1];
            d[i++] = digits10[p2];
            d[i++] = digits01[p2];
            d[i++] = digits10[p3];
            d[i++] = digits01[p3];
    }
    return PARSE_OK;
}

ParseError MysqlReply::ResultSet::Parse(butil::IOBuf& buf, butil::Arena* arena, bool binary) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    // parse header
    MY_PARSE_CHECK(_header.Parse(buf));
    // parse colunms
    MY_ALLOC_CHECK(my_alloc_check(arena, _header._column_count, _columns));
    for (uint64_t i = 0; i < _header._column_count; ++i) {
        MY_PARSE_CHECK(_columns[i].Parse(buf, arena));
    }
    // parse eof1
    MY_PARSE_CHECK(_eof1.Parse(buf));
    // parse row
    std::vector<Row*> rows;
    for (;;) {
        // if not full package reread
        if (!is_full_package(buf)) {
            return PARSE_ERROR_NOT_ENOUGH_DATA;
        }
        // if eof break loops for row
        if (is_an_eof(buf)) {
            break;
        }
        // allocate memory for row and fields
        Row* row = nullptr;
        Field* fields = nullptr;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, row));
        MY_ALLOC_CHECK(my_alloc_check(arena, _header._column_count, fields));
        row->_fields = fields;
        row->_field_count = _header._column_count;
        _last->_next = row;
        _last = row;
        // parse row and fields
        MY_PARSE_CHECK(row->Parse(buf, _columns, _header._column_count, fields, binary, arena));
        // add row count
        ++_row_count;
    }
    // parse eof2
    MY_PARSE_CHECK(_eof2.Parse(buf));
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::PrepareOk::Parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }

    MY_PARSE_CHECK(_header.Parse(buf));

    if (_header._param_count > 0) {
        MY_ALLOC_CHECK(my_alloc_check(arena, _header._param_count, _params));
        for (uint16_t i = 0; i < _header._param_count; ++i) {
            MY_PARSE_CHECK(_params[i].Parse(buf, arena));
        }
        MY_PARSE_CHECK(_eof1.Parse(buf));
    }

    if (_header._column_count > 0) {
        MY_ALLOC_CHECK(my_alloc_check(arena, _header._column_count, _columns));
        for (uint16_t i = 0; i < _header._column_count; ++i) {
            MY_PARSE_CHECK(_columns[i].Parse(buf, arena));
        }
        MY_PARSE_CHECK(_eof2.Parse(buf));
    }
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::PrepareOk::Header::Parse(butil::IOBuf& buf) {
    if (is_parsed()) {
        return PARSE_OK;
    }

    MysqlHeader header;
    butil::IOBuf payload;
    if (!parse_header(buf, &header, &payload)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }

    payload.pop_front(1);
    {
        uint8_t tmp[4];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _stmt_id = mysql_uint4korr(tmp);
    }
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _column_count = mysql_uint2korr(tmp);
    }
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _param_count = mysql_uint2korr(tmp);
    }
    payload.pop_front(1);
    {
        uint8_t tmp[2];
        MY_PARSE_CHECK(parse_fixed(payload, tmp, sizeof(tmp)));
        _warning = mysql_uint2korr(tmp);
    }

    set_parsed();
    return PARSE_OK;
}

}  // namespace brpc
