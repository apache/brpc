#include "brpc/mysql_reply.h"
#include <ios>

namespace brpc {

#define MY_ERROR_RET(expr, message) \
    do {                            \
        if ((expr) == true) {       \
            LOG(INFO) << message;   \
            return false;           \
        }                           \
    } while (0)

#define MY_ALLOC_CHECK(expr) MY_ERROR_RET(!(expr), "Fail to arena allocate")
#define MY_PARSE_CHECK(expr) \
    MY_ERROR_RET(!(expr), "Fail to parse mysql protocol")
template <class Type>
inline bool my_alloc_check(butil::Arena* arena, const size_t n,
                           Type*& pointer) {
    if (pointer == NULL) {
        pointer = (Type*)arena->allocate(sizeof(Type) * n);
        if (pointer == NULL) {
            return false;
        }
        for (size_t i = 0; i < n; ++i) {
            new (pointer + i) Type;
        }
    }
    return true;
}

const char* MysqlFieldTypeToString(MysqlFieldType type) {
    switch (type) {
        case MYSQL_FIELD_TYPE_DECIMAL:
        case MYSQL_FIELD_TYPE_TINY:
            return "tiny";
        case MYSQL_FIELD_TYPE_SHORT:
            return "short";
        case MYSQL_FIELD_TYPE_LONG:
            return "long";
        case MYSQL_FIELD_TYPE_FLOAT:
            return "float";
        case MYSQL_FIELD_TYPE_DOUBLE:
            return "double";
        case MYSQL_FIELD_TYPE_NULL:
            return "null";
        case MYSQL_FIELD_TYPE_TIMESTAMP:
            return "timestamp";
        case MYSQL_FIELD_TYPE_LONGLONG:
            return "longlong";
        case MYSQL_FIELD_TYPE_INT24:
            return "int24";
        case MYSQL_FIELD_TYPE_DATE:
            return "date";
        case MYSQL_FIELD_TYPE_TIME:
            return "time";
        case MYSQL_FIELD_TYPE_DATETIME:
            return "datetime";
        case MYSQL_FIELD_TYPE_YEAR:
            return "year";
        case MYSQL_FIELD_TYPE_NEWDATE:
            return "new date";
        case MYSQL_FIELD_TYPE_VARCHAR:
            return "varchar";
        case MYSQL_FIELD_TYPE_BIT:
            return "bit";
        case MYSQL_FIELD_TYPE_JSON:
            return "json";
        case MYSQL_FIELD_TYPE_NEWDECIMAL:
            return "new decimal";
        case MYSQL_FIELD_TYPE_ENUM:
            return "enum";
        case MYSQL_FIELD_TYPE_SET:
            return "set";
        case MYSQL_FIELD_TYPE_TINY_BLOB:
            return "tiny blob";
        case MYSQL_FIELD_TYPE_MEDIUM_BLOB:
            return "blob";
        case MYSQL_FIELD_TYPE_LONG_BLOB:
            return "long blob";
        case MYSQL_FIELD_TYPE_BLOB:
            return "blob";
        case MYSQL_FIELD_TYPE_VAR_STRING:
            return "var string";
        case MYSQL_FIELD_TYPE_STRING:
            return "string";
        case MYSQL_FIELD_TYPE_GEOMETRY:
            return "geometry";
        default:
            return "Unknown Field Type";
    }
}

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
        default:
            return "Unknown Response Type";
    }
}

bool ParseHeader(butil::IOBuf& buf, MysqlHeader* value) {
    // check if the buf is contain a full package
    uint8_t header[4];
    const uint8_t* p = (const uint8_t*)buf.fetch(header, sizeof(header));
    if (p == NULL) {
        LOG(ERROR) << "fetch mysql protocol header failed";
        return false;
    }
    uint32_t payload_size = mysql_uint3korr(p);
    if (buf.size() < payload_size + 4) {
        LOG(INFO) << "IOBuf not enough full mysql message buf size:"
                  << buf.size() << " message size:" << payload_size + 4;
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
bool ParseEncodeLength(butil::IOBuf& buf, uint64_t* value) {
    uint8_t f;
    buf.cut1((char*)&f);
    if (f <= 250) {
        *value = f;
    } else if (f == 251) {
        *value = 0;
    } else if (f == 252) {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        *value = mysql_uint2korr(tmp);
    } else if (f == 253) {
        uint8_t tmp[3];
        buf.cutn(tmp, sizeof(tmp));
        *value = mysql_uint3korr(tmp);
    } else if (f == 254) {
        uint8_t tmp[8];
        buf.cutn(tmp, sizeof(tmp));
        *value = mysql_uint8korr(tmp);
    }
    return true;
}

bool MysqlReply::ConsumePartialIOBuf(butil::IOBuf& buf, butil::Arena* arena,
                                     const bool is_auth, bool* is_multi) {
    *is_multi = false;
    uint8_t header[5];
    const uint8_t* p = (const uint8_t*)buf.fetch(header, sizeof(header));
    if (p == NULL) {
        return false;
    }
    uint8_t type = _type == MYSQL_RSP_UNKNOWN ? p[4] : (uint8_t)_type;
    if (is_auth && type != 0x00 && type != 0xFF) {
        _type = MYSQL_RSP_AUTH;
        Auth* auth = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, auth));
        MY_PARSE_CHECK(auth->parse(buf, arena));
        _data.auth = auth;
        return true;
    }
    if (type == 0x00) {
        _type = MYSQL_RSP_OK;
        Ok* ok = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, ok));
        MY_PARSE_CHECK(ok->parse(buf, arena));
        _data.ok = ok;
        *is_multi = _data.ok->status() & MYSQL_SERVER_MORE_RESULTS_EXISTS;
    } else if (type == 0xFF) {
        _type = MYSQL_RSP_ERROR;
        Error* error = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, error));
        MY_PARSE_CHECK(error->parse(buf, arena));
        _data.error = error;
    } else if (type == 0xFE) {
        _type = MYSQL_RSP_EOF;
        Eof* eof = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, eof));
        MY_PARSE_CHECK(eof->parse(buf));
        _data.eof = eof;
        *is_multi = _data.eof->status() & MYSQL_SERVER_MORE_RESULTS_EXISTS;
    } else if (type >= 0x01 && type <= 0xFA) {
        _type = MYSQL_RSP_RESULTSET;
        MY_ALLOC_CHECK(my_alloc_check(arena, 1, _data.result_set.var));
        ResultSet& r = *_data.result_set.var;
        // parse header
        MY_PARSE_CHECK(r._header.parse(buf));
        // parse colunms
        MY_ALLOC_CHECK(
            my_alloc_check(arena, r._header._column_number, r._columns));
        for (uint64_t i = 0; i < r._header._column_number; ++i) {
            MY_PARSE_CHECK(r._columns[i].parse(buf, arena));
        }
        // parse eof1
        MY_PARSE_CHECK(r._eof1.parse(buf));
        // parse row
        Eof eof;
        std::vector<Row*> rows;
        bool is_first = true;
        while (!eof.isEof(buf)) {
            if (is_first) {
                // we may reenter ConsumePartialIOBuf many times, check the last
                // row
                if (r._last != r._first) {
                    MY_PARSE_CHECK(r._last->parseText(buf));
                    for (uint64_t i = 0; i < r._header._column_number; ++i) {
                        MY_PARSE_CHECK(r._last->_fields[i].parse(
                            buf, r._columns + i, arena));
                    }
                }
                is_first = false;
                continue;
            }

            Row* row = NULL;
            Field* fields = NULL;
            MY_ALLOC_CHECK(my_alloc_check(arena, 1, row));
            MY_ALLOC_CHECK(
                my_alloc_check(arena, r._header._column_number, fields));
            row->_fields = fields;
            row->_field_number = r._header._column_number;
            r._last->_next = row;
            r._last = row;
            // parse row and fields
            MY_PARSE_CHECK(row->parseText(buf));
            for (uint64_t i = 0; i < r._header._column_number; ++i) {
                MY_PARSE_CHECK(fields[i].parse(buf, r._columns + i, arena));
            }
            // row number
            ++r._row_number;
        }
        // parse eof2
        MY_PARSE_CHECK(r._eof2.parse(buf));
        *is_multi = r._eof2.status() & MYSQL_SERVER_MORE_RESULTS_EXISTS;
    } else {
        LOG(ERROR) << "Unknown Response Type";
        return false;
    }
    return true;
}

void MysqlReply::Print(std::ostream& os) const {
    if (_type == MYSQL_RSP_AUTH) {
        const Auth& auth = *_data.auth;
        os << "\nprotocol:" << (unsigned)auth._protocol
           << "\nversion:" << auth._version.as_string()
           << "\nthread_id:" << auth._thread_id
           << "\nsalt:" << auth._salt.as_string()
           << "\ncapacity:" << auth._capability
           << "\nlanguage:" << (unsigned)auth._language
           << "\nstatus:" << auth._status
           << "\nextended_capacity:" << auth._extended_capability
           << "\nauth_plugin_length:" << auth._auth_plugin_length
           << "\nsalt2:" << auth._salt2.as_string()
           << "\nauth_plugin:" << auth._auth_plugin.as_string();
    } else if (_type == MYSQL_RSP_OK) {
        const Ok& ok = *_data.ok;
        os << "\naffect_row:" << ok._affect_row << "\nindex:" << ok._index
           << "\nstatus:" << ok._status << "\nwarning:" << ok._warning
           << "\nmessage:" << ok._msg.as_string();
    } else if (_type == MYSQL_RSP_ERROR) {
        const Error& err = *_data.error;
        os << "\nerrcode:" << err._errcode
           << "\nstatus:" << err._status.as_string()
           << "\nmessage:" << err._msg.as_string();
    } else if (_type == MYSQL_RSP_RESULTSET) {
        const ResultSet& r = *_data.result_set.const_var;
        os << "\nheader.column_number:" << r._header._column_number;
        for (uint64_t i = 0; i < r._header._column_number; ++i) {
            os << "\ncolumn[" << i
               << "].catalog:" << r._columns[i]._catalog.as_string()
               << "\ncolumn[" << i
               << "].database:" << r._columns[i]._database.as_string()
               << "\ncolumn[" << i
               << "].table:" << r._columns[i]._table.as_string() << "\ncolumn["
               << i
               << "].origin_table:" << r._columns[i]._origin_table.as_string()
               << "\ncolumn[" << i
               << "].name:" << r._columns[i]._name.as_string() << "\ncolumn["
               << i
               << "].origin_name:" << r._columns[i]._origin_name.as_string()
               << "\ncolumn[" << i
               << "].collation:" << (uint16_t)r._columns[i]._collation
               << "\ncolumn[" << i << "].length:" << r._columns[i]._length
               << "\ncolumn[" << i << "].type:" << (unsigned)r._columns[i]._type
               << "\ncolumn[" << i << "].flag:" << (unsigned)r._columns[i]._flag
               << "\ncolumn[" << i
               << "].decimal:" << (unsigned)r._columns[i]._decimal;
        }
        os << "\neof1.warning:" << r._eof1._warning;
        os << "\neof1.status:" << r._eof1._status;
        int n = 0;
        for (const Row* row = r._first->_next; row != r._last->_next;
             row = row->_next) {
            os << "\nrow(" << n++ << "):";
            for (uint64_t j = 0; j < r._header._column_number; ++j) {
                switch (r._columns[j]._type) {
                    case MYSQL_FIELD_TYPE_TINY:
                        if (r._columns[j]._flag & MYSQL_UNSIGNED_FLAG) {
                            os << row->field(j).tiny();
                        } else {
                            os << row->field(j).stiny();
                        }
                        break;
                    case MYSQL_FIELD_TYPE_SHORT:
                    case MYSQL_FIELD_TYPE_YEAR:
                        if (r._columns[j]._flag & MYSQL_UNSIGNED_FLAG) {
                            os << row->field(j).small();
                        } else {
                            os << row->field(j).ssmall();
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
    } else {
        os << "Unknown response type";
    }
}

bool MysqlReply::Auth::parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return true;
    }
    const std::string delim(1, 0x00);
    MysqlHeader header;
    if (!ParseHeader(buf, &header)) {
        return false;
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
    buf.cut1((char*)&_language);
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
        char* d = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, _auth_plugin_length, d));
        buf.cutn(d, _auth_plugin_length);
        _auth_plugin.set(d, _auth_plugin_length);
    }
    buf.clear();  // consume all buf
    set_parsed();
    return true;
}

bool MysqlReply::ResultSetHeader::parse(butil::IOBuf& buf) {
    if (is_parsed()) {
        return true;
    }
    MysqlHeader header;
    if (!ParseHeader(buf, &header)) {
        return false;
    }
    uint64_t old_size, new_size;
    old_size = buf.size();
    ParseEncodeLength(buf, &_column_number);
    new_size = buf.size();
    if (old_size - new_size < header.payload_size) {
        ParseEncodeLength(buf, &_extra_msg);
    } else {
        _extra_msg = 0;
    }
    set_parsed();
    return true;
}

bool MysqlReply::Column::parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return true;
    }
    MysqlHeader header;
    if (!ParseHeader(buf, &header)) {
        return false;
    }

    uint64_t len;
    ParseEncodeLength(buf, &len);
    char* catalog = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, catalog));
    buf.cutn(catalog, len);
    _catalog.set(catalog, len);

    ParseEncodeLength(buf, &len);
    char* database = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, database));
    buf.cutn(database, len);
    _database.set(database, len);

    ParseEncodeLength(buf, &len);
    char* table = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, table));
    buf.cutn(table, len);
    _table.set(table, len);

    ParseEncodeLength(buf, &len);
    char* origin_table = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, origin_table));
    buf.cutn(origin_table, len);
    _origin_table.set(origin_table, len);

    ParseEncodeLength(buf, &len);
    char* name = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, name));
    buf.cutn(name, len);
    _name.set(name, len);

    ParseEncodeLength(buf, &len);
    char* origin_name = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, origin_name));
    buf.cutn(origin_name, len);
    _origin_name.set(origin_name, len);
    buf.pop_front(1);
    {
        uint8_t tmp[2];
        buf.cutn(tmp, sizeof(tmp));
        _collation = (MysqlCollation)mysql_uint2korr(tmp);
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
    return true;
}

bool MysqlReply::Ok::parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return true;
    }
    MysqlHeader header;
    if (!ParseHeader(buf, &header)) {
        return false;
    }

    uint64_t len, old_size, new_size;
    old_size = buf.size();
    buf.pop_front(1);

    ParseEncodeLength(buf, &len);
    _affect_row = len;

    ParseEncodeLength(buf, &len);
    _index = len;

    buf.cutn(&_status, 2);
    buf.cutn(&_warning, 2);

    new_size = buf.size();
    if (old_size - new_size < header.payload_size) {
        len = header.payload_size - (old_size - new_size);
        char* msg = NULL;
        MY_ALLOC_CHECK(my_alloc_check(arena, len, msg));
        buf.cutn(msg, len);
        _msg.set(msg, len);
        // buf.pop_front(1);  // Null
    }
    set_parsed();
    return true;
}

bool MysqlReply::Eof::isEof(const butil::IOBuf& buf) {
    uint8_t tmp[5];
    const uint8_t* p = (const uint8_t*)buf.fetch(tmp, sizeof(tmp));
    uint8_t type = p[4];
    if (type == MYSQL_RSP_EOF) {
        buf.copy_to(&_warning, 2, 5);
        buf.copy_to(&_status, 2, 7);
        return true;
    }
    return false;
}

bool MysqlReply::Eof::parse(butil::IOBuf& buf) {
    if (is_parsed()) {
        return true;
    }
    MysqlHeader header;
    if (!ParseHeader(buf, &header)) {
        return false;
    }
    buf.pop_front(1);
    buf.cutn(&_warning, 2);
    buf.cutn(&_status, 2);
    set_parsed();
    return true;
}

bool MysqlReply::Error::parse(butil::IOBuf& buf, butil::Arena* arena) {
    if (is_parsed()) {
        return true;
    }
    MysqlHeader header;
    if (!ParseHeader(buf, &header)) {
        return false;
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
    // error message, Null-Terminated string
    uint64_t len = header.payload_size - 9;
    char* msg = NULL;
    MY_ALLOC_CHECK(my_alloc_check(arena, len, msg));
    buf.cutn(msg, len);
    _msg.set(msg, len);
    // buf.pop_front(1);  // Null
    set_parsed();
    return true;
}

bool MysqlReply::Row::parseText(butil::IOBuf& buf) {
    if (is_parsed()) {
        return true;
    }
    MysqlHeader header;
    if (!ParseHeader(buf, &header)) {
        return false;
    }
    set_parsed();
    return true;
}

bool MysqlReply::Field::parse(butil::IOBuf& buf,
                              const MysqlReply::Column* column,
                              butil::Arena* arena) {
    if (is_parsed()) {
        return true;
    }
    uint64_t len;
    ParseEncodeLength(buf, &len);
    // is it null?
    if (len == 0 && !(column->_flag & MYSQL_NOT_NULL_FLAG)) {
        _type = MYSQL_FIELD_TYPE_NULL;
        set_parsed();
        return true;
    }
    // field type
    _type = column->_type;
    // is unsigned flag set
    _is_unsigned = column->_flag & MYSQL_UNSIGNED_FLAG;
    // field is not null
    butil::IOBuf str;
    buf.cutn(&str, len);
    switch (_type) {
        case MYSQL_FIELD_TYPE_TINY:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                std::istringstream(str.to_string()) >> _data.tiny;
            } else {
                std::istringstream(str.to_string()) >> _data.stiny;
            }
            break;
        case MYSQL_FIELD_TYPE_SHORT:
        case MYSQL_FIELD_TYPE_YEAR:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                std::istringstream(str.to_string()) >> _data.small;
            } else {
                std::istringstream(str.to_string()) >> _data.ssmall;
            }
            break;
        case MYSQL_FIELD_TYPE_INT24:
        case MYSQL_FIELD_TYPE_LONG:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                std::istringstream(str.to_string()) >> _data.integer;
            } else {
                std::istringstream(str.to_string()) >> _data.sinteger;
            }
            break;
        case MYSQL_FIELD_TYPE_LONGLONG:
            if (column->_flag & MYSQL_UNSIGNED_FLAG) {
                std::istringstream(str.to_string()) >> _data.bigint;
            } else {
                std::istringstream(str.to_string()) >> _data.sbigint;
            }
            break;
        case MYSQL_FIELD_TYPE_FLOAT:
            std::istringstream(str.to_string()) >> _data.float32;
            break;
        case MYSQL_FIELD_TYPE_DOUBLE:
            std::istringstream(str.to_string()) >> _data.float64;
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
            return false;
    }
    set_parsed();
    return true;
}

}  // namespace brpc
