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

#ifndef BRPC_MYSQL_REPLY_H
#define BRPC_MYSQL_REPLY_H

#include <vector>
#include "butil/iobuf.h"  // butil::IOBuf
#include "butil/arena.h"
#include "butil/sys_byteorder.h"
#include "butil/logging.h"  // LOG()
#include "parse_result.h"

namespace brpc {

class CheckParsed {
public:
    CheckParsed() : _is_parsed(false) {}
    bool is_parsed() const {
        return _is_parsed;
    }
    void set_parsed() {
        _is_parsed = true;
    }

private:
    bool _is_parsed;
};

struct MysqlHeader {
    uint32_t payload_size;
    uint32_t seq;
};

enum MysqlRspType : uint8_t {
    MYSQL_RSP_OK = 0x00,
    MYSQL_RSP_ERROR = 0xFF,
    MYSQL_RSP_RESULTSET = 0x01,
    MYSQL_RSP_EOF = 0xFE,
    MYSQL_RSP_AUTH = 0xFB,     // add for mysql auth
    MYSQL_RSP_UNKNOWN = 0xFC,  // add for other case
};

enum MysqlFieldType : uint8_t {
    MYSQL_FIELD_TYPE_DECIMAL = 0x00,
    MYSQL_FIELD_TYPE_TINY = 0x01,
    MYSQL_FIELD_TYPE_SHORT = 0x02,
    MYSQL_FIELD_TYPE_LONG = 0x03,
    MYSQL_FIELD_TYPE_FLOAT = 0x04,
    MYSQL_FIELD_TYPE_DOUBLE = 0x05,
    MYSQL_FIELD_TYPE_NULL = 0x06,
    MYSQL_FIELD_TYPE_TIMESTAMP = 0x07,
    MYSQL_FIELD_TYPE_LONGLONG = 0x08,
    MYSQL_FIELD_TYPE_INT24 = 0x09,
    MYSQL_FIELD_TYPE_DATE = 0x0A,
    MYSQL_FIELD_TYPE_TIME = 0x0B,
    MYSQL_FIELD_TYPE_DATETIME = 0x0C,
    MYSQL_FIELD_TYPE_YEAR = 0x0D,
    MYSQL_FIELD_TYPE_NEWDATE = 0x0E,
    MYSQL_FIELD_TYPE_VARCHAR = 0x0F,
    MYSQL_FIELD_TYPE_BIT = 0x10,
    MYSQL_FIELD_TYPE_JSON = 0xF5,
    MYSQL_FIELD_TYPE_NEWDECIMAL = 0xF6,
    MYSQL_FIELD_TYPE_ENUM = 0xF7,
    MYSQL_FIELD_TYPE_SET = 0xF8,
    MYSQL_FIELD_TYPE_TINY_BLOB = 0xF9,
    MYSQL_FIELD_TYPE_MEDIUM_BLOB = 0xFA,
    MYSQL_FIELD_TYPE_LONG_BLOB = 0xFB,
    MYSQL_FIELD_TYPE_BLOB = 0xFC,
    MYSQL_FIELD_TYPE_VAR_STRING = 0xFD,
    MYSQL_FIELD_TYPE_STRING = 0xFE,
    MYSQL_FIELD_TYPE_GEOMETRY = 0xFF,
};

enum MysqlFieldFlag : uint16_t {
    MYSQL_NOT_NULL_FLAG = 0x0001,
    MYSQL_PRI_KEY_FLAG = 0x0002,
    MYSQL_UNIQUE_KEY_FLAG = 0x0004,
    MYSQL_MULTIPLE_KEY_FLAG = 0x0008,
    MYSQL_BLOB_FLAG = 0x0010,
    MYSQL_UNSIGNED_FLAG = 0x0020,
    MYSQL_ZEROFILL_FLAG = 0x0040,
    MYSQL_BINARY_FLAG = 0x0080,
    MYSQL_ENUM_FLAG = 0x0100,
    MYSQL_AUTO_INCREMENT_FLAG = 0x0200,
    MYSQL_TIMESTAMP_FLAG = 0x0400,
    MYSQL_SET_FLAG = 0x0800,
};

enum MysqlServerStatus : uint16_t {
    MYSQL_SERVER_STATUS_IN_TRANS = 1,
    MYSQL_SERVER_STATUS_AUTOCOMMIT = 2,   /* Server in auto_commit mode */
    MYSQL_SERVER_MORE_RESULTS_EXISTS = 8, /* Multi query - next query exists */
    MYSQL_SERVER_QUERY_NO_GOOD_INDEX_USED = 16,
    MYSQL_SERVER_QUERY_NO_INDEX_USED = 32,
    /**
      The server was able to fulfill the clients request and opened a
      read-only non-scrollable cursor for a query. This flag comes
      in reply to COM_STMT_EXECUTE and COM_STMT_FETCH commands.
    */
    MYSQL_SERVER_STATUS_CURSOR_EXISTS = 64,
    /**
      This flag is sent when a read-only cursor is exhausted, in reply to
      COM_STMT_FETCH command.
    */
    MYSQL_SERVER_STATUS_LAST_ROW_SENT = 128,
    MYSQL_SERVER_STATUS_DB_DROPPED = 256, /* A database was dropped */
    MYSQL_SERVER_STATUS_NO_BACKSLASH_ESCAPES = 512,
    /**
      Sent to the client if after a prepared statement reprepare
      we discovered that the new statement returns a different
      number of result set columns.
    */
    MYSQL_SERVER_STATUS_METADATA_CHANGED = 1024,
    MYSQL_SERVER_QUERY_WAS_SLOW = 2048,

    /**
      To mark ResultSet containing output parameter values.
    */
    MYSQL_SERVER_PS_OUT_PARAMS = 4096,

    /**
      Set at the same time as MYSQL_SERVER_STATUS_IN_TRANS if the started
      multi-statement transaction is a read-only transaction. Cleared
      when the transaction commits or aborts. Since this flag is sent
      to clients in OK and EOF packets, the flag indicates the
      transaction status at the end of command execution.
    */
    MYSQL_SERVER_STATUS_IN_TRANS_READONLY = 8192,
    MYSQL_SERVER_SESSION_STATE_CHANGED = 1UL << 14,
};
// Msql Collation
enum MysqlCollation : uint16_t {
    MYSQL_big5_chinese_ci = 1,
    MYSQL_latin2_czech_cs = 2,
    MYSQL_dec8_swedish_ci = 3,
    MYSQL_cp850_general_ci = 4,
    MYSQL_latin1_german1_ci = 5,
    MYSQL_hp8_english_ci = 6,
    MYSQL_koi8r_general_ci = 7,
    MYSQL_latin1_swedish_ci = 8,
    MYSQL_latin2_general_ci = 9,
    MYSQL_swe7_swedish_ci = 10,
    MYSQL_ascii_general_ci = 11,
    MYSQL_ujis_japanese_ci = 12,
    MYSQL_sjis_japanese_ci = 13,
    MYSQL_cp1251_bulgarian_ci = 14,
    MYSQL_latin1_danish_ci = 15,
    MYSQL_hebrew_general_ci = 16,
    MYSQL_tis620_thai_ci = 18,
    MYSQL_euckr_korean_ci = 19,
    MYSQL_latin7_estonian_cs = 20,
    MYSQL_latin2_hungarian_ci = 21,
    MYSQL_koi8u_general_ci = 22,
    MYSQL_cp1251_ukrainian_ci = 23,
    MYSQL_gb2312_chinese_ci = 24,
    MYSQL_greek_general_ci = 25,
    MYSQL_cp1250_general_ci = 26,
    MYSQL_latin2_croatian_ci = 27,
    MYSQL_gbk_chinese_ci = 28,
    MYSQL_cp1257_lithuanian_ci = 29,
    MYSQL_latin5_turkish_ci = 30,
    MYSQL_latin1_german2_ci = 31,
    MYSQL_armscii8_general_ci = 32,
    MYSQL_utf8_general_ci = 33,
    MYSQL_cp1250_czech_cs = 34,
    MYSQL_ucs2_general_ci = 35,
    MYSQL_cp866_general_ci = 36,
    MYSQL_keybcs2_general_ci = 37,
    MYSQL_macce_general_ci = 38,
    MYSQL_macroman_general_ci = 39,
    MYSQL_cp852_general_ci = 40,
    MYSQL_latin7_general_ci = 41,
    MYSQL_latin7_general_cs = 42,
    MYSQL_macce_bin = 43,
    MYSQL_cp1250_croatian_ci = 44,
    MYSQL_utf8mb4_general_ci = 45,
    MYSQL_utf8mb4_bin = 46,
    MYSQL_latin1_bin = 47,
    MYSQL_latin1_general_ci = 48,
    MYSQL_latin1_general_cs = 49,
    MYSQL_cp1251_bin = 50,
    MYSQL_cp1251_general_ci = 51,
    MYSQL_cp1251_general_cs = 52,
    MYSQL_macroman_bin = 53,
    MYSQL_utf16_general_ci = 54,
    MYSQL_utf16_bin = 55,
    MYSQL_utf16le_general_ci = 56,
    MYSQL_cp1256_general_ci = 57,
    MYSQL_cp1257_bin = 58,
    MYSQL_cp1257_general_ci = 59,
    MYSQL_utf32_general_ci = 60,
    MYSQL_utf32_bin = 61,
    MYSQL_utf16le_bin = 62,
    MYSQL_binary = 63,
    MYSQL_armscii8_bin = 64,
    MYSQL_ascii_bin = 65,
    MYSQL_cp1250_bin = 66,
    MYSQL_cp1256_bin = 67,
    MYSQL_cp866_bin = 68,
    MYSQL_dec8_bin = 69,
    MYSQL_greek_bin = 70,
    MYSQL_hebrew_bin = 71,
    MYSQL_hp8_bin = 72,
    MYSQL_keybcs2_bin = 73,
    MYSQL_koi8r_bin = 74,
    MYSQL_koi8u_bin = 75,
    MYSQL_latin2_bin = 77,
    MYSQL_latin5_bin = 78,
    MYSQL_latin7_bin = 79,
    MYSQL_cp850_bin = 80,
    MYSQL_cp852_bin = 81,
    MYSQL_swe7_bin = 82,
    MYSQL_utf8_bin = 83,
    MYSQL_big5_bin = 84,
    MYSQL_euckr_bin = 85,
    MYSQL_gb2312_bin = 86,
    MYSQL_gbk_bin = 87,
    MYSQL_sjis_bin = 88,
    MYSQL_tis620_bin = 89,
    MYSQL_ucs2_bin = 90,
    MYSQL_ujis_bin = 91,
    MYSQL_geostd8_general_ci = 92,
    MYSQL_geostd8_bin = 93,
    MYSQL_latin1_spanish_ci = 94,
    MYSQL_cp932_japanese_ci = 95,
    MYSQL_cp932_bin = 96,
    MYSQL_eucjpms_japanese_ci = 97,
    MYSQL_eucjpms_bin = 98,
    MYSQL_cp1250_polish_ci = 99,
    MYSQL_utf16_unicode_ci = 101,
    MYSQL_utf16_icelandic_ci = 102,
    MYSQL_utf16_latvian_ci = 103,
    MYSQL_utf16_romanian_ci = 104,
    MYSQL_utf16_slovenian_ci = 105,
    MYSQL_utf16_polish_ci = 106,
    MYSQL_utf16_estonian_ci = 107,
    MYSQL_utf16_spanish_ci = 108,
    MYSQL_utf16_swedish_ci = 109,
    MYSQL_utf16_turkish_ci = 110,
    MYSQL_utf16_czech_ci = 111,
    MYSQL_utf16_danish_ci = 112,
    MYSQL_utf16_lithuanian_ci = 113,
    MYSQL_utf16_slovak_ci = 114,
    MYSQL_utf16_spanish2_ci = 115,
    MYSQL_utf16_roman_ci = 116,
    MYSQL_utf16_persian_ci = 117,
    MYSQL_utf16_esperanto_ci = 118,
    MYSQL_utf16_hungarian_ci = 119,
    MYSQL_utf16_sinhala_ci = 120,
    MYSQL_utf16_german2_ci = 121,
    MYSQL_utf16_croatian_ci = 122,
    MYSQL_utf16_unicode_520_ci = 123,
    MYSQL_utf16_vietnamese_ci = 124,
    MYSQL_ucs2_unicode_ci = 128,
    MYSQL_ucs2_icelandic_ci = 129,
    MYSQL_ucs2_latvian_ci = 130,
    MYSQL_ucs2_romanian_ci = 131,
    MYSQL_ucs2_slovenian_ci = 132,
    MYSQL_ucs2_polish_ci = 133,
    MYSQL_ucs2_estonian_ci = 134,
    MYSQL_ucs2_spanish_ci = 135,
    MYSQL_ucs2_swedish_ci = 136,
    MYSQL_ucs2_turkish_ci = 137,
    MYSQL_ucs2_czech_ci = 138,
    MYSQL_ucs2_danish_ci = 139,
    MYSQL_ucs2_lithuanian_ci = 140,
    MYSQL_ucs2_slovak_ci = 141,
    MYSQL_ucs2_spanish2_ci = 142,
    MYSQL_ucs2_roman_ci = 143,
    MYSQL_ucs2_persian_ci = 144,
    MYSQL_ucs2_esperanto_ci = 145,
    MYSQL_ucs2_hungarian_ci = 146,
    MYSQL_ucs2_sinhala_ci = 147,
    MYSQL_ucs2_german2_ci = 148,
    MYSQL_ucs2_croatian_ci = 149,
    MYSQL_ucs2_unicode_520_ci = 150,
    MYSQL_ucs2_vietnamese_ci = 151,
    MYSQL_ucs2_general_mysql500_ci = 159,
    MYSQL_utf32_unicode_ci = 160,
    MYSQL_utf32_icelandic_ci = 161,
    MYSQL_utf32_latvian_ci = 162,
    MYSQL_utf32_romanian_ci = 163,
    MYSQL_utf32_slovenian_ci = 164,
    MYSQL_utf32_polish_ci = 165,
    MYSQL_utf32_estonian_ci = 166,
    MYSQL_utf32_spanish_ci = 167,
    MYSQL_utf32_swedish_ci = 168,
    MYSQL_utf32_turkish_ci = 169,
    MYSQL_utf32_czech_ci = 170,
    MYSQL_utf32_danish_ci = 171,
    MYSQL_utf32_lithuanian_ci = 172,
    MYSQL_utf32_slovak_ci = 173,
    MYSQL_utf32_spanish2_ci = 174,
    MYSQL_utf32_roman_ci = 175,
    MYSQL_utf32_persian_ci = 176,
    MYSQL_utf32_esperanto_ci = 177,
    MYSQL_utf32_hungarian_ci = 178,
    MYSQL_utf32_sinhala_ci = 179,
    MYSQL_utf32_german2_ci = 180,
    MYSQL_utf32_croatian_ci = 181,
    MYSQL_utf32_unicode_520_ci = 182,
    MYSQL_utf32_vietnamese_ci = 183,
    MYSQL_utf8_unicode_ci = 192,
    MYSQL_utf8_icelandic_ci = 193,
    MYSQL_utf8_latvian_ci = 194,
    MYSQL_utf8_romanian_ci = 195,
    MYSQL_utf8_slovenian_ci = 196,
    MYSQL_utf8_polish_ci = 197,
    MYSQL_utf8_estonian_ci = 198,
    MYSQL_utf8_spanish_ci = 199,
    MYSQL_utf8_swedish_ci = 200,
    MYSQL_utf8_turkish_ci = 201,
    MYSQL_utf8_czech_ci = 202,
    MYSQL_utf8_danish_ci = 203,
    MYSQL_utf8_lithuanian_ci = 204,
    MYSQL_utf8_slovak_ci = 205,
    MYSQL_utf8_spanish2_ci = 206,
    MYSQL_utf8_roman_ci = 207,
    MYSQL_utf8_persian_ci = 208,
    MYSQL_utf8_esperanto_ci = 209,
    MYSQL_utf8_hungarian_ci = 210,
    MYSQL_utf8_sinhala_ci = 211,
    MYSQL_utf8_german2_ci = 212,
    MYSQL_utf8_croatian_ci = 213,
    MYSQL_utf8_unicode_520_ci = 214,
    MYSQL_utf8_vietnamese_ci = 215,
    MYSQL_utf8_general_mysql500_ci = 223,
    MYSQL_utf8mb4_unicode_ci = 224,
    MYSQL_utf8mb4_icelandic_ci = 225,
    MYSQL_utf8mb4_latvian_ci = 226,
    MYSQL_utf8mb4_romanian_ci = 227,
    MYSQL_utf8mb4_slovenian_ci = 228,
    MYSQL_utf8mb4_polish_ci = 229,
    MYSQL_utf8mb4_estonian_ci = 230,
    MYSQL_utf8mb4_spanish_ci = 231,
    MYSQL_utf8mb4_swedish_ci = 232,
    MYSQL_utf8mb4_turkish_ci = 233,
    MYSQL_utf8mb4_czech_ci = 234,
    MYSQL_utf8mb4_danish_ci = 235,
    MYSQL_utf8mb4_lithuanian_ci = 236,
    MYSQL_utf8mb4_slovak_ci = 237,
    MYSQL_utf8mb4_spanish2_ci = 238,
    MYSQL_utf8mb4_roman_ci = 239,
    MYSQL_utf8mb4_persian_ci = 240,
    MYSQL_utf8mb4_esperanto_ci = 241,
    MYSQL_utf8mb4_hungarian_ci = 242,
    MYSQL_utf8mb4_sinhala_ci = 243,
    MYSQL_utf8mb4_german2_ci = 244,
    MYSQL_utf8mb4_croatian_ci = 245,
    MYSQL_utf8mb4_unicode_520_ci = 246,
    MYSQL_utf8mb4_vietnamese_ci = 247,
};

const char* MysqlFieldTypeToString(MysqlFieldType);
const char* MysqlRspTypeToString(MysqlRspType);

class MysqlReply {
public:
    // Mysql Auth package
    class Auth : private CheckParsed {
    public:
        Auth();
        uint8_t protocol() const;
        butil::StringPiece version() const;
        uint32_t thread_id() const;
        butil::StringPiece salt() const;
        uint16_t capability() const;
        uint8_t language() const;
        uint16_t status() const;
        uint16_t extended_capability() const;
        uint8_t auth_plugin_length() const;
        butil::StringPiece salt2() const;
        butil::StringPiece auth_plugin() const;

    private:
        ParseError Parse(butil::IOBuf& buf, butil::Arena* arena);

        DISALLOW_COPY_AND_ASSIGN(Auth);
        friend class MysqlReply;

        uint8_t _protocol;
        butil::StringPiece _version;
        uint32_t _thread_id;
        butil::StringPiece _salt;
        uint16_t _capability;
        uint8_t _language;
        uint16_t _status;
        uint16_t _extended_capability;
        uint8_t _auth_plugin_length;
        butil::StringPiece _salt2;
        butil::StringPiece _auth_plugin;
    };
    // Mysql Ok package
    class Ok : private CheckParsed {
    public:
        Ok();
        uint64_t affect_row() const;
        uint64_t index() const;
        uint16_t status() const;
        uint16_t warning() const;
        butil::StringPiece msg() const;

    private:
        ParseError Parse(butil::IOBuf& buf, butil::Arena* arena);

        DISALLOW_COPY_AND_ASSIGN(Ok);
        friend class MysqlReply;

        uint64_t _affect_row;
        uint64_t _index;
        uint16_t _status;
        uint16_t _warning;
        butil::StringPiece _msg;
    };
    // Mysql Error package
    class Error : private CheckParsed {
    public:
        Error();
        uint16_t errcode() const;
        butil::StringPiece status() const;
        butil::StringPiece msg() const;

    private:
        ParseError Parse(butil::IOBuf& buf, butil::Arena* arena);

        DISALLOW_COPY_AND_ASSIGN(Error);
        friend class MysqlReply;

        uint16_t _errcode;
        butil::StringPiece _status;
        butil::StringPiece _msg;
    };
    // Mysql Eof package
    class Eof : private CheckParsed {
    public:
        Eof();
        uint16_t warning() const;
        uint16_t status() const;

    private:
        ParseError Parse(butil::IOBuf& buf);

        DISALLOW_COPY_AND_ASSIGN(Eof);
        friend class MysqlReply;

        uint16_t _warning;
        uint16_t _status;
    };
    // Mysql Column
    class Column : private CheckParsed {
    public:
        Column();
        butil::StringPiece catalog() const;
        butil::StringPiece database() const;
        butil::StringPiece table() const;
        butil::StringPiece origin_table() const;
        butil::StringPiece name() const;
        butil::StringPiece origin_name() const;
        MysqlCollation collation() const;
        uint32_t length() const;
        MysqlFieldType type() const;
        MysqlFieldFlag flag() const;
        uint8_t decimal() const;

    private:
        ParseError Parse(butil::IOBuf& buf, butil::Arena* arena);

        DISALLOW_COPY_AND_ASSIGN(Column);
        friend class MysqlReply;

        butil::StringPiece _catalog;
        butil::StringPiece _database;
        butil::StringPiece _table;
        butil::StringPiece _origin_table;
        butil::StringPiece _name;
        butil::StringPiece _origin_name;
        MysqlCollation _collation;
        uint32_t _length;
        MysqlFieldType _type;
        MysqlFieldFlag _flag;
        uint8_t _decimal;
    };
    // Mysql Field
    class Field : private CheckParsed {
    public:
        Field();
        int8_t stiny() const;
        uint8_t tiny() const;
        int16_t ssmall() const;
        uint16_t small() const;
        int32_t sinteger() const;
        uint32_t integer() const;
        int64_t sbigint() const;
        uint64_t bigint() const;
        float float32() const;
        double float64() const;
        butil::StringPiece string() const;
        bool is_stiny() const;
        bool is_tiny() const;
        bool is_ssmall() const;
        bool is_small() const;
        bool is_sinteger() const;
        bool is_integer() const;
        bool is_sbigint() const;
        bool is_bigint() const;
        bool is_float32() const;
        bool is_float64() const;
        bool is_string() const;
        bool is_nil() const;

    private:
        ParseError Parse(butil::IOBuf& buf, const MysqlReply::Column* column, butil::Arena* arena);

        DISALLOW_COPY_AND_ASSIGN(Field);
        friend class MysqlReply;

        union {
            int8_t stiny;
            uint8_t tiny;
            int16_t ssmall;
            uint16_t small;
            int32_t sinteger;
            uint32_t integer;
            int64_t sbigint;
            uint64_t bigint;
            float float32;
            double float64;
            butil::StringPiece str;
        } _data = {.str = NULL};
        MysqlFieldType _type;
        bool _is_unsigned;
        bool _is_nil;
    };
    // Mysql Row
    class Row : private CheckParsed {
    public:
        Row();
        uint64_t field_number() const;
        const Field& field(const uint64_t index) const;

    private:
        ParseError ParseText(butil::IOBuf& buf);

        DISALLOW_COPY_AND_ASSIGN(Row);
        friend class MysqlReply;

        Field* _fields;
        uint64_t _field_number;
    };

public:
    MysqlReply();
    ParseError ConsumePartialIOBuf(butil::IOBuf& buf,
                                   butil::Arena* arena,
                                   const bool is_auth,
                                   bool* more_results);
    void Swap(MysqlReply& other);
    void Print(std::ostream& os) const;
    // response type
    MysqlRspType type() const;
    // get auth
    const Auth& auth() const;
    const Ok& ok() const;
    const Error& error() const;
    const Eof& eof() const;
    // get column number
    uint64_t column_number() const;
    // get one column
    const Column& column(const uint64_t index) const;
    // get row number
    uint64_t row_number() const;
    // get one row
    const Row& row(const uint64_t index) const;
    bool is_auth() const;
    bool is_ok() const;
    bool is_error() const;
    bool is_eof() const;
    bool is_resultset() const;

private:
    // Mysql result set header
    struct ResultSetHeader : private CheckParsed {
        ResultSetHeader() : _column_number(0), _extra_msg(0) {}
        ParseError Parse(butil::IOBuf& buf);
        uint64_t _column_number;
        uint64_t _extra_msg;

    private:
        DISALLOW_COPY_AND_ASSIGN(ResultSetHeader);
    };
    // Mysql result set
    struct ResultSet : private CheckParsed {
        ResultSet() : _columns(NULL) {
            _rows.reserve(10);
        }
        ParseError Parse(butil::IOBuf& buf, butil::Arena* arena);
        ResultSetHeader _header;
        Column* _columns;
        Eof _eof1;
        std::vector<Row*> _rows;
        Eof _eof2;

    private:
        DISALLOW_COPY_AND_ASSIGN(ResultSet);
    };
    // member values
    MysqlRspType _type;
    union {
        Auth* auth;
        ResultSet* result_set;
        Ok* ok;
        Error* error;
        Eof* eof;
        uint64_t padding;  // For swapping, must cover all bytes.
    } _data;

    DISALLOW_COPY_AND_ASSIGN(MysqlReply);
};

// mysql reply
inline MysqlReply::MysqlReply() {
    _type = MYSQL_RSP_UNKNOWN;
    _data.padding = 0;
}
inline void MysqlReply::Swap(MysqlReply& other) {
    std::swap(_type, other._type);
    std::swap(_data.padding, other._data.padding);
}
inline std::ostream& operator<<(std::ostream& os, const MysqlReply& r) {
    r.Print(os);
    return os;
}
inline MysqlRspType MysqlReply::type() const {
    return _type;
}
inline const MysqlReply::Auth& MysqlReply::auth() const {
    if (is_auth()) {
        return *_data.auth;
    }
    CHECK(false) << "The reply is " << MysqlRspTypeToString(_type) << ", not an auth";
    static Auth auth_nil;
    return auth_nil;
}
inline const MysqlReply::Ok& MysqlReply::ok() const {
    if (is_ok()) {
        return *_data.ok;
    }
    CHECK(false) << "The reply is " << MysqlRspTypeToString(_type) << ", not an ok";
    static Ok ok_nil;
    return ok_nil;
}
inline const MysqlReply::Error& MysqlReply::error() const {
    if (is_error()) {
        return *_data.error;
    }
    CHECK(false) << "The reply is " << MysqlRspTypeToString(_type) << ", not an error";
    static Error error_nil;
    return error_nil;
}
inline const MysqlReply::Eof& MysqlReply::eof() const {
    if (is_eof()) {
        return *_data.eof;
    }
    CHECK(false) << "The reply is " << MysqlRspTypeToString(_type) << ", not an eof";
    static Eof eof_nil;
    return eof_nil;
}
inline uint64_t MysqlReply::column_number() const {
    if (is_resultset()) {
        return _data.result_set->_header._column_number;
    }
    CHECK(false) << "The reply is " << MysqlRspTypeToString(_type) << ", not an resultset";
    return 0;
}
inline const MysqlReply::Column& MysqlReply::column(const uint64_t index) const {
    static Column column_nil;
    if (is_resultset()) {
        if (index < _data.result_set->_header._column_number) {
            return _data.result_set->_columns[index];
        }
        CHECK(false) << "index " << index << " out of bound [0,"
                     << _data.result_set->_header._column_number << ")";
        return column_nil;
    }
    CHECK(false) << "The reply is " << MysqlRspTypeToString(_type) << ", not an resultset";
    return column_nil;
}
inline uint64_t MysqlReply::row_number() const {
    if (is_resultset()) {
        return _data.result_set->_rows.size();
    }
    CHECK(false) << "The reply is " << MysqlRspTypeToString(_type) << ", not an resultset";
    return 0;
}
inline const MysqlReply::Row& MysqlReply::row(const uint64_t index) const {
    static Row row_nil;
    if (is_resultset()) {
        if (index < _data.result_set->_rows.size()) {
            return *(_data.result_set->_rows[index]);
        }
        CHECK(false) << "index " << index << " out of bound [0," << _data.result_set->_rows.size()
                     << ")";
        return row_nil;
    }
    CHECK(false) << "The reply is " << MysqlRspTypeToString(_type) << ", not an resultset";
    return row_nil;
}
inline bool MysqlReply::is_auth() const {
    return _type == MYSQL_RSP_AUTH;
}
inline bool MysqlReply::is_ok() const {
    return _type == MYSQL_RSP_OK;
}
inline bool MysqlReply::is_error() const {
    return _type == MYSQL_RSP_ERROR;
}
inline bool MysqlReply::is_eof() const {
    return _type == MYSQL_RSP_EOF;
}
inline bool MysqlReply::is_resultset() const {
    return _type == MYSQL_RSP_RESULTSET;
}
// mysql auth
inline MysqlReply::Auth::Auth()
    : _protocol(0),
      _thread_id(0),
      _capability(0),
      _language(0),
      _status(0),
      _extended_capability(0),
      _auth_plugin_length(0) {}
inline uint8_t MysqlReply::Auth::protocol() const {
    return _protocol;
}
inline butil::StringPiece MysqlReply::Auth::version() const {
    return _version;
}
inline uint32_t MysqlReply::Auth::thread_id() const {
    return _thread_id;
}
inline butil::StringPiece MysqlReply::Auth::salt() const {
    return _salt;
}
inline uint16_t MysqlReply::Auth::capability() const {
    return _capability;
}
inline uint8_t MysqlReply::Auth::language() const {
    return _language;
}
inline uint16_t MysqlReply::Auth::status() const {
    return _status;
}
inline uint16_t MysqlReply::Auth::extended_capability() const {
    return _extended_capability;
}
inline uint8_t MysqlReply::Auth::auth_plugin_length() const {
    return _auth_plugin_length;
}
inline butil::StringPiece MysqlReply::Auth::salt2() const {
    return _salt2;
}
inline butil::StringPiece MysqlReply::Auth::auth_plugin() const {
    return _auth_plugin;
}
// mysql reply ok
inline MysqlReply::Ok::Ok() : _affect_row(0), _index(0), _status(0), _warning(0) {}
inline uint64_t MysqlReply::Ok::affect_row() const {
    return _affect_row;
}
inline uint64_t MysqlReply::Ok::index() const {
    return _index;
}
inline uint16_t MysqlReply::Ok::status() const {
    return _status;
}
inline uint16_t MysqlReply::Ok::warning() const {
    return _warning;
}
inline butil::StringPiece MysqlReply::Ok::msg() const {
    return _msg;
}
// mysql reply error
inline MysqlReply::Error::Error() : _errcode(0) {}
inline uint16_t MysqlReply::Error::errcode() const {
    return _errcode;
}
inline butil::StringPiece MysqlReply::Error::status() const {
    return _status;
}
inline butil::StringPiece MysqlReply::Error::msg() const {
    return _msg;
}
// mysql reply eof
inline MysqlReply::Eof::Eof() : _warning(0), _status(0) {}
inline uint16_t MysqlReply::Eof::warning() const {
    return _warning;
}
inline uint16_t MysqlReply::Eof::status() const {
    return _status;
}
// mysql reply column
inline MysqlReply::Column::Column() : _length(0), _type(MYSQL_FIELD_TYPE_NULL), _decimal(0) {}
inline butil::StringPiece MysqlReply::Column::catalog() const {
    return _catalog;
}
inline butil::StringPiece MysqlReply::Column::database() const {
    return _database;
}
inline butil::StringPiece MysqlReply::Column::table() const {
    return _table;
}
inline butil::StringPiece MysqlReply::Column::origin_table() const {
    return _origin_table;
}
inline butil::StringPiece MysqlReply::Column::name() const {
    return _name;
}
inline butil::StringPiece MysqlReply::Column::origin_name() const {
    return _origin_name;
}
inline MysqlCollation MysqlReply::Column::collation() const {
    return _collation;
}
inline uint32_t MysqlReply::Column::length() const {
    return _length;
}
inline MysqlFieldType MysqlReply::Column::type() const {
    return _type;
}
inline MysqlFieldFlag MysqlReply::Column::flag() const {
    return _flag;
}
inline uint8_t MysqlReply::Column::decimal() const {
    return _decimal;
}
// mysql reply row
inline MysqlReply::Row::Row() : _fields(NULL), _field_number(0) {}
inline uint64_t MysqlReply::Row::field_number() const {
    return _field_number;
}
inline const MysqlReply::Field& MysqlReply::Row::field(const uint64_t index) const {
    if (index < _field_number) {
        return _fields[index];
    }
    CHECK(false) << "index " << index << " out of bound [0," << _field_number << ")";
    static Field field_nil;
    return field_nil;
}
// mysql reply field
inline MysqlReply::Field::Field()
    : _type(MYSQL_FIELD_TYPE_NULL), _is_unsigned(false), _is_nil(false) {}
inline int8_t MysqlReply::Field::stiny() const {
    if (is_stiny()) {
        return _data.stiny;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an stiny";
    return 0;
}
inline uint8_t MysqlReply::Field::tiny() const {
    if (is_tiny()) {
        return _data.tiny;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an tiny";
    return 0;
}
inline int16_t MysqlReply::Field::ssmall() const {
    if (is_ssmall()) {
        return _data.ssmall;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an ssmall";
    return 0;
}
inline uint16_t MysqlReply::Field::small() const {
    if (is_small()) {
        return _data.small;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an small";
    return 0;
}
inline int32_t MysqlReply::Field::sinteger() const {
    if (is_sinteger()) {
        return _data.sinteger;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an sinteger";
    return 0;
}
inline uint32_t MysqlReply::Field::integer() const {
    if (is_integer()) {
        return _data.integer;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an integer";
    return 0;
}
inline int64_t MysqlReply::Field::sbigint() const {
    if (is_sbigint()) {
        return _data.sbigint;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an sbigint";
    return 0;
}
inline uint64_t MysqlReply::Field::bigint() const {
    if (is_bigint()) {
        return _data.bigint;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an bigint";
    return 0;
}
inline float MysqlReply::Field::float32() const {
    if (is_float32()) {
        return _data.float32;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an float32";
    return 0;
}
inline double MysqlReply::Field::float64() const {
    if (is_float64()) {
        return _data.float64;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an float64";
    return 0;
}
inline butil::StringPiece MysqlReply::Field::string() const {
    if (is_string()) {
        return _data.str;
    }
    CHECK(false) << "The reply is " << MysqlFieldTypeToString(_type) << " and "
                 << (_is_nil ? "NULL" : "NOT NULL") << ", not an string";
    return butil::StringPiece();
}
inline bool MysqlReply::Field::is_stiny() const {
    return _type == MYSQL_FIELD_TYPE_TINY && !_is_unsigned && !_is_nil;
}
inline bool MysqlReply::Field::is_tiny() const {
    return _type == MYSQL_FIELD_TYPE_TINY && _is_unsigned && !_is_nil;
}
inline bool MysqlReply::Field::is_ssmall() const {
    return (_type == MYSQL_FIELD_TYPE_SHORT || _type == MYSQL_FIELD_TYPE_YEAR) && !_is_unsigned &&
        !_is_nil;
}
inline bool MysqlReply::Field::is_small() const {
    return (_type == MYSQL_FIELD_TYPE_SHORT || _type == MYSQL_FIELD_TYPE_YEAR) && _is_unsigned &&
        !_is_nil;
}
inline bool MysqlReply::Field::is_sinteger() const {
    return (_type == MYSQL_FIELD_TYPE_INT24 || _type == MYSQL_FIELD_TYPE_LONG) && !_is_unsigned &&
        !_is_nil;
}
inline bool MysqlReply::Field::is_integer() const {
    return (_type == MYSQL_FIELD_TYPE_INT24 || _type == MYSQL_FIELD_TYPE_LONG) && _is_unsigned &&
        !_is_nil;
}
inline bool MysqlReply::Field::is_sbigint() const {
    return _type == MYSQL_FIELD_TYPE_LONGLONG && !_is_unsigned && !_is_nil;
}
inline bool MysqlReply::Field::is_bigint() const {
    return _type == MYSQL_FIELD_TYPE_LONGLONG && _is_unsigned && !_is_nil;
}
inline bool MysqlReply::Field::is_float32() const {
    return _type == MYSQL_FIELD_TYPE_FLOAT && !_is_nil;
}
inline bool MysqlReply::Field::is_float64() const {
    return _type == MYSQL_FIELD_TYPE_DOUBLE && !_is_nil;
}
inline bool MysqlReply::Field::is_string() const {
    return (_type == MYSQL_FIELD_TYPE_DECIMAL || _type == MYSQL_FIELD_TYPE_NEWDECIMAL ||
            _type == MYSQL_FIELD_TYPE_VARCHAR || _type == MYSQL_FIELD_TYPE_BIT ||
            _type == MYSQL_FIELD_TYPE_ENUM || _type == MYSQL_FIELD_TYPE_SET ||
            _type == MYSQL_FIELD_TYPE_TINY_BLOB || _type == MYSQL_FIELD_TYPE_MEDIUM_BLOB ||
            _type == MYSQL_FIELD_TYPE_LONG_BLOB || _type == MYSQL_FIELD_TYPE_BLOB ||
            _type == MYSQL_FIELD_TYPE_VAR_STRING || _type == MYSQL_FIELD_TYPE_STRING ||
            _type == MYSQL_FIELD_TYPE_GEOMETRY || _type == MYSQL_FIELD_TYPE_JSON ||
            _type == MYSQL_FIELD_TYPE_TIME || _type == MYSQL_FIELD_TYPE_DATE ||
            _type == MYSQL_FIELD_TYPE_NEWDATE || _type == MYSQL_FIELD_TYPE_TIMESTAMP ||
            _type == MYSQL_FIELD_TYPE_DATETIME) &&
        !_is_nil;
}
inline bool MysqlReply::Field::is_nil() const {
    return _is_nil;
}

}  // namespace brpc

#endif  // BRPC_MYSQL_REPLY_H
