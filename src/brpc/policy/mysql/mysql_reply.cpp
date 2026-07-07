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
    if (pointer == NULL) {
        pointer = (Type*)arena->allocate(sizeof(Type) * n);
        if (pointer == NULL) {
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
    if (pointer == NULL) {
        pointer = (char*)arena->allocate(sizeof(char) * n);
        if (pointer == NULL) {
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
    if (p == NULL) {
        return false;
    }
    uint32_t payload_size = mysql_uint3korr(p);
    if (buf.size() < payload_size + 4) {
        return false;
    }
    return true;
}
// if is eof package
inline bool is_an_eof(const butil::IOBuf& buf) {
    uint8_t tmp[5];
    const uint8_t* p = (const uint8_t*)buf.fetch(tmp, sizeof(tmp));
    if (p == NULL) {
        return false;
    }
    uint8_t type = p[4];
    if (type == MYSQL_RSP_EOF) {
        return true;
    } else {
        return false;
    }
}
// parse header
inline bool parse_header(butil::IOBuf& buf, MysqlHeader* value) {
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
    return true;
}
// use this carefully, we depending on parse_header for checking IOBuf contain full package
inline uint64_t parse_encode_length(butil::IOBuf& buf) {
    if (buf.size() == 0) {
        return 0;
    }

    uint64_t value = 0;
    uint8_t f = 0;
    buf.cut1((char*)&f);
    if (f <= 250) {
        value = f;
    } else if (f == 251) {
        value = 0;
    } else if (f == 252) {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        value = mysql_uint2korr(tmp);
    } else if (f == 253) {
        uint8_t tmp[3];
        buf.cutn(tmp, sizeof(tmp));
        value = mysql_uint3korr(tmp);
    } else if (f == 254) {
        uint8_t tmp[8];
        buf.cutn(tmp, sizeof(tmp));
        value = mysql_uint8korr(tmp);
    }
    return value;
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
        const bool fast_auth_success = (sp != NULL && sp[5] == 0x03);
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
    if (type == 0x00 && (is_auth || stmt_type != MYSQL_NEED_PREPARE)) {
        _type = MYSQL_RSP_OK;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.ok));
        MY_PARSE_CHECK(_data.ok->Parse(buf, arena));
        *more_results = _data.ok->status() & MYSQL_SERVER_MORE_RESULTS_EXISTS;
    } else if ((type == 0x00 && stmt_type == MYSQL_NEED_PREPARE) || type == MYSQL_RSP_PREPARE_OK) {
        _type = MYSQL_RSP_PREPARE_OK;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.prepare_ok));
        MY_PARSE_CHECK(_data.prepare_ok->Parse(buf, arena));
    } else if (type == 0xFF) {
        _type = MYSQL_RSP_ERROR;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.error));
        MY_PARSE_CHECK(_data.error->Parse(buf, arena));
    } else if (type == 0xFE) {
        _type = MYSQL_RSP_EOF;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.eof));
        MY_PARSE_CHECK(_data.eof->Parse(buf));
        *more_results = _data.eof->status() & MYSQL_SERVER_MORE_RESULTS_EXISTS;
    } else if (type >= 0x01 && type <= 0xFA) {
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
    if (!parse_header(buf, &header)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    buf.cut1((char*)&_protocol);
    {
        butil::IOBuf version;
        buf.cut_until(&version, delim);
        char* d = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, version.size(), d));
        version.copy_to(d);
        _version.set(d, version.size());
    }
    {
        uint8_t tmp[4];
        buf.cutn(tmp, sizeof(tmp));
        _thread_id = mysql_uint4korr(tmp);
    }
    {
        butil::IOBuf salt;
        buf.cut_until(&salt, delim);
        char* d = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, salt.size(), d));
        salt.copy_to(d);
        _salt.set(d, salt.size());
    }
    {
        uint8_t tmp[2];
        buf.cutn(&tmp, sizeof(tmp));
        _capability = mysql_uint2korr(tmp);
    }
    buf.cut1((char*)&_collation);
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _status = mysql_uint2korr(tmp);
    }
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _extended_capability = mysql_uint2korr(tmp);
    }
    buf.cut1((char*)&_auth_plugin_length);
    buf.pop_front(10);
    {
        butil::IOBuf salt2;
        buf.cut_until(&salt2, delim);
        char* d = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, salt2.size(), d));
        salt2.copy_to(d);
        _salt2.set(d, salt2.size());
    }
    {
        if (_auth_plugin_length > buf.size()) {
            LOG(ERROR) << "MysqlReply::Auth::Parse: auth_plugin length " << _auth_plugin_length
                       << " exceeds remaining buffer size " << buf.size();
            return PARSE_ERROR_ABSOLUTELY_WRONG;
        }
        char* d = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, _auth_plugin_length, d));
        buf.cutn(d, _auth_plugin_length);
        _auth_plugin.set(d, _auth_plugin_length);
    }
    buf.clear();  // consume all buf
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::AuthMoreData::Parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    MysqlHeader header;
    if (!parse_header(buf, &header)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    _seq = (uint8_t)header.seq;
    // Drop the 0x01 AuthMoreData tag; expose only the bytes after it (a
    // single status byte 0x03/0x04, or the PEM-encoded RSA public key).
    buf.pop_front(1);
    const int64_t len = (int64_t)header.payload_size - 1;
    if (len > 0) {
        char* d = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, len, d));
        buf.cutn(d, len);
        _data.set(d, len);
    } else {
        _data.set(NULL, 0);
    }
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::ResultSetHeader::Parse(butil::IOBuf& buf) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    MysqlHeader header;
    if (!parse_header(buf, &header)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    uint64_t old_size, new_size;
    old_size = buf.size();
    _column_count = parse_encode_length(buf);
    // Guard against an absurd/malicious column count driving unbounded
    // allocations downstream (per-column arrays and the row NULL-bitmap).
    // MySQL's hard limit is 4096 columns per table; 65535 is a generous cap
    // that no legitimate result set exceeds.
    if (_column_count > 65535) {
        LOG(ERROR) << "illegal column count " << _column_count;
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    new_size = buf.size();
    if (old_size - new_size < header.payload_size) {
        _extra_msg = parse_encode_length(buf);
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
    if (!parse_header(buf, &header)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }

    // Each length-encoded string must fit within the remaining buffer; an
    // oversized length would otherwise drive my_alloc_check/cutn/.set past the
    // packet (mirrors the hardened auth_plugin path above).
    uint64_t len = parse_encode_length(buf);
    if (len > buf.size()) {
        LOG(WARNING) << "MysqlReply::Column::Parse: catalog length " << len
                   << " exceeds remaining buffer size " << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    char* catalog = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, catalog));
    buf.cutn(catalog, len);
    _catalog.set(catalog, len);

    len = parse_encode_length(buf);
    if (len > buf.size()) {
        LOG(WARNING) << "MysqlReply::Column::Parse: database length " << len
                   << " exceeds remaining buffer size " << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    char* database = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, database));
    buf.cutn(database, len);
    _database.set(database, len);

    len = parse_encode_length(buf);
    if (len > buf.size()) {
        LOG(WARNING) << "MysqlReply::Column::Parse: table length " << len
                   << " exceeds remaining buffer size " << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    char* table = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, table));
    buf.cutn(table, len);
    _table.set(table, len);

    len = parse_encode_length(buf);
    if (len > buf.size()) {
        LOG(WARNING) << "MysqlReply::Column::Parse: origin_table length " << len
                   << " exceeds remaining buffer size " << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    char* origin_table = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, origin_table));
    buf.cutn(origin_table, len);
    _origin_table.set(origin_table, len);

    len = parse_encode_length(buf);
    if (len > buf.size()) {
        LOG(WARNING) << "MysqlReply::Column::Parse: name length " << len
                   << " exceeds remaining buffer size " << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    char* name = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, name));
    buf.cutn(name, len);
    _name.set(name, len);

    len = parse_encode_length(buf);
    if (len > buf.size()) {
        LOG(WARNING) << "MysqlReply::Column::Parse: origin_name length " << len
                   << " exceeds remaining buffer size " << buf.size();
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    char* origin_name = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, origin_name));
    buf.cutn(origin_name, len);
    _origin_name.set(origin_name, len);
    buf.pop_front(1);
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _charset = mysql_uint2korr(tmp);
    }
    {
        uint8_t tmp[4];
        buf.cutn(tmp, sizeof(tmp));
        _length = mysql_uint4korr(tmp);
    }
    buf.cut1((char*)&_type);
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _flag = (MysqlFieldFlag)mysql_uint2korr(tmp);
    }
    buf.cut1((char*)&_decimal);
    buf.pop_front(2);
    set_parsed();
    return PARSE_OK;
}

ParseError MysqlReply::Ok::Parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return PARSE_OK;
    }
    MysqlHeader header;
    if (!parse_header(buf, &header)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }

    uint64_t old_size, new_size;
    old_size = buf.size();
    buf.pop_front(1);

    _affect_row = parse_encode_length(buf);
    _index = parse_encode_length(buf);
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _status = mysql_uint2korr(tmp);
    }
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _warning = mysql_uint2korr(tmp);
    }

    new_size = buf.size();
    if (old_size - new_size < header.payload_size) {
        const int64_t len = header.payload_size - (old_size - new_size);
        char* msg = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, len, msg));
        buf.cutn(msg, len);
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
    if (!parse_header(buf, &header)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    buf.pop_front(1);
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _warning = mysql_uint2korr(tmp);
    }
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
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
    if (!parse_header(buf, &header)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    buf.pop_front(1);  // 0xFF
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _errcode = mysql_uint2korr(tmp);
    }
    buf.pop_front(1);  // '#'
    // 5 byte server status
    char* status = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, 5, status));
    buf.cutn(status, 5);
    _status.set(status, 5);
    // error message, Null-Terminated string.
    // payload layout consumed so far: 0xFF(1) + errcode(2) + '#'(1) +
    // sql_state(5) = 9 bytes; guard against a malformed short packet to avoid
    // an unsigned underflow producing a huge length.
    if (header.payload_size < 9) {
        LOG(WARNING) << "MysqlReply::Error::Parse: truncated ERR packet, payload_size "
                   << header.payload_size << " < 9 (0xFF+errcode+'#'+sql_state)";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
    uint64_t len = header.payload_size - 9;
    char* msg = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, msg));
    buf.cutn(msg, len);
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
    if (!parse_header(buf, &header)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }
    if (!binary) {  // mysql text protocol
        for (uint64_t i = 0; i < column_count; ++i) {
            MY_PARSE_CHECK(fields[i].Parse(buf, columns + i, arena));
        }
    } else {  // mysql binary protocol
        uint8_t hdr = 0;
        buf.cut1((char*)&hdr);
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
        uint8_t* null_mask = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, (size_t)size, null_mask));
        for (uint64_t i = 0; i < size; ++i) {
            null_mask[i] = 0;
        }
        buf.cutn(null_mask, size);
        for (uint64_t i = 0; i < column_count; ++i) {
            MY_PARSE_CHECK(fields[i].Parse(buf, columns + i, i, column_count, null_mask, arena));
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
    const uint64_t len = parse_encode_length(buf);
    // is it null?
    if (len == 0 && !(column->_flag & MYSQL_NOT_NULL_FLAG)) {
        _is_nil = true;
        set_parsed();
        return PARSE_OK;
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
                _data.tiny = strtoul(str.to_string().c_str(), NULL, 10);
            } else {
                _data.stiny = strtol(str.to_string().c_str(), NULL, 10);
            }
            break;
        case MYSQL_FIELD_TYPE_SHORT:
        case MYSQL_FIELD_TYPE_YEAR:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                _data.small = strtoul(str.to_string().c_str(), NULL, 10);
            } else {
                _data.ssmall = strtol(str.to_string().c_str(), NULL, 10);
            }
            break;
        case MYSQL_FIELD_TYPE_INT24:
        case MYSQL_FIELD_TYPE_LONG:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                _data.integer = strtoul(str.to_string().c_str(), NULL, 10);
            } else {
                _data.sinteger = strtol(str.to_string().c_str(), NULL, 10);
            }
            break;
        case MYSQL_FIELD_TYPE_LONGLONG:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                _data.bigint = strtoul(str.to_string().c_str(), NULL, 10);
            } else {
                _data.sbigint = strtol(str.to_string().c_str(), NULL, 10);
            }
            break;
        case MYSQL_FIELD_TYPE_FLOAT:
            _data.float32 = strtof(str.to_string().c_str(), NULL);
            break;
        case MYSQL_FIELD_TYPE_DOUBLE:
            _data.float64 = strtod(str.to_string().c_str(), NULL);
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
            char* d = NULL;
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
                buf.cut1((char*)&_data.tiny);
            } else {
                buf.cut1((char*)&_data.stiny);
            }
            break;
        case MYSQL_FIELD_TYPE_SHORT:
        case MYSQL_FIELD_TYPE_YEAR:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                uint8_t* p = (uint8_t*)&_data.small;
                buf.cutn(p, 2);
                _data.small = mysql_uint2korr(p);
            } else {
                uint8_t* p = (uint8_t*)&_data.ssmall;
                buf.cutn(p, 2);
                _data.ssmall = (int16_t)mysql_uint2korr(p);
            }
            break;
        case MYSQL_FIELD_TYPE_INT24:
        case MYSQL_FIELD_TYPE_LONG:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                uint8_t* p = (uint8_t*)&_data.integer;
                buf.cutn(p, 4);
                _data.integer = mysql_uint4korr(p);
            } else {
                uint8_t* p = (uint8_t*)&_data.sinteger;
                buf.cutn(p, 4);
                _data.sinteger = (int32_t)mysql_uint4korr(p);
            }
            break;
        case MYSQL_FIELD_TYPE_LONGLONG:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                uint8_t* p = (uint8_t*)&_data.bigint;
                buf.cutn(p, 8);
                _data.bigint = mysql_uint8korr(p);
            } else {
                uint8_t* p = (uint8_t*)&_data.sbigint;
                buf.cutn(p, 8);
                _data.sbigint = (int64_t)mysql_uint8korr(p);
            }
            break;
        case MYSQL_FIELD_TYPE_FLOAT: {
            uint8_t* p = (uint8_t*)&_data.float32;
            buf.cutn(p, 4);
        } break;
        case MYSQL_FIELD_TYPE_DOUBLE: {
            uint8_t* p = (uint8_t*)&_data.float64;
            buf.cutn(p, 8);
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
            const uint64_t len = parse_encode_length(buf);
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
            char* d = NULL;
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

    const uint64_t len = parse_encode_length(buf);
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
    if (len > buf.size()) {
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
    char* d = NULL;
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
        buf.cut1((char*)&neg);
        buf.cutn(&day, 4);
        day = mysql_uint4korr((uint8_t*)&day);
        buf.cut1((char*)&hour);
        buf.cut1((char*)&min);
        buf.cut1((char*)&sec);
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
    const uint64_t len = parse_encode_length(buf);
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
    if (len > buf.size()) {
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
    char* d = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, dstlen, d));
    // Read only the fields present for this `len`; absent fields are 0.
    // len == 0  -> no bytes (all-zero value).
    // len == 4  -> year(2 LE) month(1) day(1) only -> "YYYY-MM-DD".
    // len == 7  -> + hour(1) min(1) sec(1) -> "YYYY-MM-DD HH:MM:SS".
    // len == 11 -> + micros(4 LE).
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (len >= 4) {
        buf.cutn(&year, 2);
        year = mysql_uint2korr((uint8_t*)&year);
        buf.cut1((char*)&month);
        buf.cut1((char*)&day);
    }
    if (len >= 7) {
        buf.cut1((char*)&hour);
        buf.cut1((char*)&min);
        buf.cut1((char*)&sec);
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
    buf.cutn((char*)&microsecs, 4);
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
        Row* row = NULL;
        Field* fields = NULL;
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
    if (!parse_header(buf, &header)) {
        return PARSE_ERROR_NOT_ENOUGH_DATA;
    }

    buf.pop_front(1);
    {
        uint8_t tmp[4];
        buf.cutn(tmp, sizeof(tmp));
        _stmt_id = mysql_uint4korr(tmp);
    }
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _column_count = mysql_uint2korr(tmp);
    }
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _param_count = mysql_uint2korr(tmp);
    }
    buf.pop_front(1);
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _warning = mysql_uint2korr(tmp);
    }

    set_parsed();
    return PARSE_OK;
}

}  // namespace brpc
