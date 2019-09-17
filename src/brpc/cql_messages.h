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

// Authors: Daojin, Cai (caidaojin@qiyi.com)

#ifndef BRPC_CQL_MESSAGES_H
#define BRPC_CQL_MESSAGES_H

#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>              // std::is_same
#include <unordered_map>
#include <vector>

#include "butil/iobuf.h"
#include "butil/logging.h"
#include "butil/strings/string_piece.h"
#include "butil/sys_byteorder.h"

namespace brpc {

class CassandraRequest;

inline void CqlEncodeInt16(int16_t v, butil::IOBuf* buf) {
    buf->push_back((v >> 8) & 0xff);
    buf->push_back(v & 0xff);    
}

inline void CqlEncodeUint16(uint16_t v, butil::IOBuf* buf) {
    buf->push_back((v >> 8) & 0xff);
    buf->push_back(v & 0xff);
}

inline void CqlEncodeInt32(int32_t v, butil::IOBuf* buf) {
    buf->push_back((v >> 24) & 0xff);
    buf->push_back((v >> 16) & 0xff);
    buf->push_back((v >> 8) & 0xff);
    buf->push_back(v & 0xff);
}

inline void CqlEncodeUint32(uint32_t v, butil::IOBuf* buf) {
    buf->push_back((v >> 24) & 0xff);
    buf->push_back((v >> 16) & 0xff);
    buf->push_back((v >> 8) & 0xff);
    buf->push_back(v & 0xff);
}

inline void CqlEncodeInt64(int64_t v, butil::IOBuf* buf) {
    CqlEncodeInt32((v >> 32) & 0xffffffff, buf);
    CqlEncodeInt32(v & 0xffffffff, buf);
}

inline int16_t CqlDecodeInt16(butil::IOBufBytesIterator& iter) {
    int16_t v = *iter; ++iter;
    v = ((v << 8) | *iter); ++iter;
    return v;
}

inline uint16_t CqlDecodeUint16(butil::IOBufBytesIterator& iter) {
    uint16_t v = *iter; ++iter;
    v = ((v << 8) | *iter); ++iter;
    return v;
}

inline int32_t CqlDecodeInt32(butil::IOBufBytesIterator& iter) {
    int32_t v = *iter; ++iter;
    v = ((v << 8) | *iter); ++iter;
    v = ((v << 8) | *iter); ++iter;
    v = ((v << 8) | *iter); ++iter;
    return v;
}

inline int64_t CqlDecodeInt64(butil::IOBufBytesIterator& iter) {
    int64_t v = *iter; ++iter;
    v = ((v << 8) | *iter); ++iter;
    v = ((v << 8) | *iter); ++iter;
    v = ((v << 8) | *iter); ++iter;
    v = ((v << 8) | *iter); ++iter;
    v = ((v << 8) | *iter); ++iter;
    v = ((v << 8) | *iter); ++iter;
    v = ((v << 8) | *iter); ++iter;
    return v;
}

// Encode value to buf as [bytes].
// Where [bytes] is: A [int] n, followed by n bytes if n >= 0. If n < 0,
// no byte should follow and the value represented is `null`.
inline void CqlEncodeBytes(bool value, butil::IOBuf* buf) {
    CqlEncodeInt32(1, buf);
    buf->push_back(value ? 0x01 : 0x00);
}

inline void CqlEncodeBytes(int32_t value, butil::IOBuf* buf) {
    CqlEncodeInt32(sizeof(value), buf);
    CqlEncodeInt32(value, buf);
}

inline void CqlEncodeBytes(int64_t value, butil::IOBuf* buf) {
    CqlEncodeInt32(sizeof(value), buf);
    CqlEncodeInt64(value, buf);
}

void CqlEncodeBytes(float value, butil::IOBuf* buf);

void CqlEncodeBytes(double value, butil::IOBuf* buf);

inline void CqlEncodeBytes(const butil::StringPiece& bytes, butil::IOBuf* buf) {
    const int n = bytes.size();
    CqlEncodeInt32(n, buf);
    if (n > 0) {
        buf->append(bytes.data(), n);
    }
}

inline void CqlEncodeBytes(const char* value, butil::IOBuf* buf) {
    CqlEncodeBytes(butil::StringPiece(value), buf);
}

inline void CqlEncodeBytes(const std::string& value, butil::IOBuf* buf) {
    CqlEncodeBytes(butil::StringPiece(value), buf);
}

// Encode cql list and set to buf.
template <typename TValue>
void CqlEncodeBytes(const std::vector<TValue>& value, butil::IOBuf* buf) {
    int32_t len = 0;
    const butil::IOBuf::Area area = buf->reserve(sizeof(len));
    if (area == butil::IOBuf::INVALID_AREA) {
        LOG(FATAL) << "Fail to reserve IOBuf::Area for cql bytes buf.";
        return;
    }
    const size_t begin_size = buf->size();
    if (!value.empty()) {
        CqlEncodeInt32(value.size(), buf);
        for (const auto& e : value) {
            CqlEncodeBytes(e, buf);
        }
    }
    len = butil::HostToNet32(buf->size() - begin_size);
    CHECK(0 == buf->unsafe_assign(area, &len));
}

template <typename TValue>
void CqlEncodeBytes(const std::set<TValue>& value, butil::IOBuf* buf) {
    int32_t len = 0;
    const butil::IOBuf::Area area = buf->reserve(sizeof(len));
    if (area == butil::IOBuf::INVALID_AREA) {
        LOG(FATAL) << "Fail to reserve IOBuf::Area for cql bytes buf.";
        return;
    }
    const size_t begin_size = buf->size();
    if (!value.empty()) {
        CqlEncodeInt32(value.size(), buf);
        for (const auto& e : value) {
            CqlEncodeBytes(e, buf);
        }
    }
    len = butil::HostToNet32(buf->size() - begin_size);
    CHECK(0 == buf->unsafe_assign(area, &len));
}

template <typename TKey, typename TValue>
void CqlEncodeBytes(const std::map<TKey, TValue>& value, butil::IOBuf* buf) {
    int32_t len = 0;
    const butil::IOBuf::Area area = buf->reserve(sizeof(len));
    if (area == butil::IOBuf::INVALID_AREA) {
        LOG(FATAL) << "Fail to reserve IOBuf::Area for cql bytes buf.";
        return;
    }
    const size_t begin_size = buf->size();
    if (!value.empty()) {
        CqlEncodeInt32(value.size(), buf);
        for (const auto& e : value) {
            CqlEncodeBytes(e.first, buf);
            CqlEncodeBytes(e.second, buf);
        }
    }
    len = butil::HostToNet32(buf->size() - begin_size);
    CHECK(0 == buf->unsafe_assign(area, &len));
}

// Decode CQL [bytes] to out.
// Return 0 if success and out is set. 
// Return 1 if success but value is null.
// Return -1 if failure.
int CqlDecodeBytes(butil::IOBufBytesIterator& iter, bool* out);
int CqlDecodeBytes(butil::IOBufBytesIterator& iter, int32_t* out);
int CqlDecodeBytes(butil::IOBufBytesIterator& iter, int64_t* out);
int CqlDecodeBytes(butil::IOBufBytesIterator& iter, float* out);
int CqlDecodeBytes(butil::IOBufBytesIterator& iter, double* out);
int CqlDecodeBytes(butil::IOBufBytesIterator& iter, std::string* out);

template<typename TValue>
int CqlDecodeBytes(butil::IOBufBytesIterator& iter, std::vector<TValue>* value) {
    if (iter.bytes_left() < 4) {
        return -1;
    }
    const int32_t size = CqlDecodeInt32(iter);
    if (size <= 0) {
        return 1;
    }

    if (iter.bytes_left() < 4) {
        return -1;
    }
    const int32_t list_size = CqlDecodeInt32(iter);
    if (value->capacity() < list_size * sizeof(TValue)) {
        value->reserve(list_size * sizeof(TValue));
    }
    for (size_t i = 0; i != list_size; ++i) {
        value->emplace_back();
        if (-1 == CqlDecodeBytes(iter, &value->back())) {
            return -1;
        }
    }
    return 0;
}

template<typename TValue>
int CqlDecodeBytes(butil::IOBufBytesIterator& iter, std::set<TValue>* value) {
    if (iter.bytes_left() < 4) {
        return -1;
    }
    const int32_t size = CqlDecodeInt32(iter);
    if (size <= 0) {
        return 1;
    }

    if (iter.bytes_left() < 4) {
        return -1;
    }
    const int32_t set_size = CqlDecodeInt32(iter);
    for (size_t i = 0; i != set_size; ++i) {
        TValue e;
        if (-1 == CqlDecodeBytes(iter, &e)) {
            return -1;
        }
        value->emplace(std::move(e));
    }
    return 0;
}

template<typename TKey, typename TValue>
int CqlDecodeBytes(butil::IOBufBytesIterator& iter, std::map<TKey, TValue>* value) {
    if (iter.bytes_left() < 4) {
        return -1;
    }
    int32_t size = CqlDecodeInt32(iter);
    if (size <= 0) {
        return 1;
    }
    if (iter.bytes_left() < 4) {
        return -1;
    }
    const int32_t map_size = CqlDecodeInt32(iter);
    for (size_t i = 0; i != map_size; ++i) {
        TKey k;
        TValue v;
        if (-1 == CqlDecodeBytes(iter, &k) || -1 == CqlDecodeBytes(iter, &v)) {
            return -1;
        }
        value->emplace(std::move(k), std::move(v));
    }
    return 0;
}

class CqlQueryParameter;
void CqlEncodeQueryParameter(const CqlQueryParameter& parameter, butil::IOBuf* buf);

enum CqlBatchType {
    CQL_BATCH_LOGGED = 0x00,
    CQL_BATCH_UNLOGGED = 0x01,
    CQL_BATCH_COUNTER = 0x02
};

enum CqlConsistencyLevel {
    CQL_CONSISTENCY_UNKNOWN      = 0xFFFF,
    CQL_CONSISTENCY_ANY          = 0x0000,
    CQL_CONSISTENCY_ONE          = 0x0001,
    CQL_CONSISTENCY_TWO          = 0x0002,
    CQL_CONSISTENCY_THREE        = 0x0003,
    CQL_CONSISTENCY_QUORUM       = 0x0004,
    CQL_CONSISTENCY_ALL          = 0x0005,
    CQL_CONSISTENCY_LOCAL_QUORUM = 0x0006,
    CQL_CONSISTENCY_EACH_QUORUM  = 0x0007,
    CQL_CONSISTENCY_SERIAL       = 0x0008,
    CQL_CONSISTENCY_LOCAL_SERIAL = 0x0009,
    CQL_CONSISTENCY_LOCAL_ONE    = 0x000A
};

enum CassOpCode {
    CQL_OPCODE_ERROR          = 0x00,
    CQL_OPCODE_STARTUP        = 0x01,
    CQL_OPCODE_READY          = 0x02,
    CQL_OPCODE_AUTHENTICATE   = 0x03,
    CQL_OPCODE_DUMMY_WHY      = 0x04,
    CQL_OPCODE_OPTIONS        = 0x05,
    CQL_OPCODE_SUPPORTED      = 0x06,
    CQL_OPCODE_QUERY          = 0x07,
    CQL_OPCODE_RESULT         = 0x08,
    CQL_OPCODE_PREPARE        = 0x09,
    CQL_OPCODE_EXECUTE        = 0x0A,
    CQL_OPCODE_REGISTER       = 0x0B,
    CQL_OPCODE_EVENT          = 0x0C,
    CQL_OPCODE_BATCH          = 0x0D,
    CQL_OPCODE_AUTH_CHALLENGE = 0x0E,
    CQL_OPCODE_AUTH_RESPONSE  = 0x0F,
    CQL_OPCODE_AUTH_SUCCESS   = 0x10,
    CQL_OPCODE_LAST_DUMMY
};

/* The CQL binary protocol is a frame based protocol. Frames are defined as:
  0         8        16         24       32        40
  +---------+---------+---------+---------+---------+
  | version |  flags  |      stream       |  opcode |
  +---------+---------+---------+---------+---------+
  |                 length                |
  +---------+---------+---------+---------+
  |                                       |
  .            ...  body ...              .
  .                                       .
  .                                       .
  +----------------------------------------
*/
struct CqlFrameHead {
    CqlFrameHead() {
        Reset();
    }

    CqlFrameHead(uint8_t v, uint8_t op, int16_t id, 
                 uint32_t len, uint8_t f = 0x00)
        : version(v), flag(f), opcode(op), stream_id(id), length(len) {}

    uint8_t version;
    uint8_t flag;
    uint8_t opcode;
    int16_t stream_id;
    uint32_t length;

    void Reset() {
        version = 0;
        flag = 0x00;
        opcode = CQL_OPCODE_LAST_DUMMY;
        stream_id = 0xFFFF;
        length = 0;
    }
   
    int GetBodyLength() const {
        return length;
    }

    static const size_t kCqlFrameHeadSize;

    // The actual size serialized to buffer as head of cql protocol frame.
    static size_t SerializedSize() {
        return kCqlFrameHeadSize;
    }

};

// <query_parameters> must be
// <consistency><flags>[<n>[name_1]<value_1>...[name_n]<value_n>]
// [<result_page_size>][<paging_state>][<serial_consistency>][<timestamp>]
class CqlQueryParameter {
public:
    enum FLAG_BIT_MASK {
        WITH_VALUES        = 0x01,
        SKIP_META_DATA     = 0x02,
        PAGE_SIZE          = 0x04,
        PAGING_STATE       = 0x08,
        SERIAL_CONSISTENCY = 0x10,
        DEFAULT_TIMESTAMP  = 0x20,
        WITH_NAMES_VALUES  = 0x40
    };

    template<typename TValue>
    void AppendValue(const TValue& value) {
        CqlEncodeBytes(value, &_name_value);
        ++_num_name_value;
    }

    void set_serial_consistency(CqlConsistencyLevel consistency) {
        _serial_consistency = consistency;
    }

    void set_timestamp(int64_t ts) {
        _timestamp = ts;
    }

    void add_flag(uint8_t flag) {
        _flags |= flag;
    }

    void remove_flag(uint8_t flag) {
        _flags &= ~flag;
    }

    bool has_flag(uint8_t flag) const {
        return _flags & flag;
    }

    void Encode(butil::IOBuf* buf) {
        CqlEncodeQueryParameter(*this, buf);
    }

    void MoveValuesTo(butil::IOBuf* buf);

    void Reset() {
        _flags = 0x00;
        _num_name_value = 0;
        _page_size = -1;
        _consistency = CQL_CONSISTENCY_LOCAL_ONE;
        _serial_consistency = CQL_CONSISTENCY_SERIAL;
        _timestamp = 0;
        _name_value.clear();
    }

private:
friend void CqlEncodeQueryParameter(const CqlQueryParameter& parameter,
                                    butil::IOBuf* buf);
friend class CassandraRequest;

    uint8_t _flags = 0x00;
    uint16_t _num_name_value = 0;
    uint32_t _page_size = -1;
    CqlConsistencyLevel _consistency = CQL_CONSISTENCY_LOCAL_ONE;
    CqlConsistencyLevel _serial_consistency = CQL_CONSISTENCY_SERIAL;
    int64_t _timestamp = 0;
    butil::IOBuf _name_value;
};

enum CqlResultType {
    CQL_RESULT_VOID          = 0x0001,
    CQL_RESULT_ROWS          = 0x0002,
    CQL_RESULT_SET_KEYSPACE  = 0x0003,
    CQL_RESULT_PREPARED      = 0x0004,
    CQL_RESULT_SCHEMA_CHANGE = 0x0005,
    CQL_RESULT_LAST_DUMMY    = 0x0006
};

struct CqlResponse {
    size_t ByteSize() const {
        return head.SerializedSize() + body.length();
    }
	
    CqlFrameHead head;
    butil::IOBuf body;  
};

class CqlResponseDecoder {
public:
    CqlResponseDecoder(const butil::IOBuf& buf) : _body_iter(buf) {}
    CqlResponseDecoder(const butil::IOBufBytesIterator& iter) : _body_iter(iter) {}
    virtual ~CqlResponseDecoder() = default;

    bool DecodeResponse() {
        if (has_decoded()) {
            return has_decoded_success();        
        }
        if (Decode()) {
            set_decoded_success();
        } else {
            set_decoded_failure();
        }
        return has_decoded_success();
    }

    bool has_decoded() const {
        return _decode_result >= 0;
    }

    bool has_decoded_success() const {
        return _decode_result == 0;
    }

    const std::string& error() const {
        return _error;
    }

protected:
    virtual bool Decode() = 0;

    void set_decoded_success() { _decode_result = 0; }
    void set_decoded_failure() { _decode_result = 1; }

    int _decode_result = -1;
    // Save decode errors. 
    std::string _error;
    // The butil::IOBuf to decode.
    // The butil::IOBuf MUST be available and can not be changed.  
    const butil::IOBufBytesIterator _body_iter;

    DISALLOW_COPY_AND_ASSIGN(CqlResponseDecoder);
};

class CqlReadyDecoder final : public CqlResponseDecoder {
public:
    CqlReadyDecoder(const butil::IOBuf& body) : CqlResponseDecoder(body) {}

private:
    virtual bool Decode() {
        return _body_iter.bytes_left() == 0;
    }
};

// CQL ERROR response: Indicates an error processing a request.
// The body of the message will be an error code ([int]) followed 
// by a [string] error message.
class CqlErrorDecoder final : public CqlResponseDecoder {
public:
    CqlErrorDecoder(const butil::IOBuf& body)
        : CqlResponseDecoder(body), _decoded_body(body) {}

    // Get a brief comments about this cql error response. 
    const char* GetErrorBrief();

    // Get error detail description in the message body.
    const std::string& GetErrorDetails();

    int error_code() const {
        return _error_code;
    }

private:
    virtual bool Decode();

    // Decode error code([int]) and set to '_error_code'.  
    bool DecodeErrorCode();

    int _error_code = -1;
    std::string _error_details;
    butil::IOBufBytesIterator _decoded_body; 
};

// Indicates that the server requires authentication, and which authentication
// mechanism to use. The body consists of a single [string] indicating the full 
// class name of the IAuthenticator in use.
class CqlAuthenticateDecoder final : public CqlResponseDecoder {
public:
    CqlAuthenticateDecoder(const butil::IOBuf& body) : CqlResponseDecoder(body) {}

    const std::string& authenticator() const {
        return _authenticator;
    }

private:
    virtual bool Decode();

    std::string _authenticator;
};

class CqlAuthSuccessDecoder final : public CqlResponseDecoder {
public:
    CqlAuthSuccessDecoder(const butil::IOBuf& body) : CqlResponseDecoder(body) {}

    bool DecodeAuthToken(std::string* out);

private:
    virtual bool Decode();
};

// The result to a query (QUERY, PREPARE, EXECUTE or BATCH messages).
class CqlResultDecoder : public CqlResponseDecoder {
public:
    CqlResultDecoder(const butil::IOBuf& body) : CqlResponseDecoder(body) {}
    virtual ~CqlResultDecoder() = default;

    const std::string& LastError() {
        return _error;
    }

protected:
     // @CqlResponseDecoder
    virtual bool Decode() final;

    virtual bool DecodeCqlResult(butil::IOBufBytesIterator& it) = 0;

    int32_t _type;
};

// CQL VOID result: The rest of the body for a Void result is empty. 
// It indicates that a query was successful without providing more information.
class CqlVoidResultDecoder final : public CqlResultDecoder {
public:
    CqlVoidResultDecoder(const butil::IOBuf& body) : CqlResultDecoder(body) {}

private:
    virtual bool DecodeCqlResult(butil::IOBufBytesIterator& it) {
        if (it.bytes_left() == 0) {
            return true;
        }
        _error = "Cql void result body should be NULL.";
        return false;
    }
};

// CQL use keyspace result: The result to a `use` query. 
// The body (after the kind [int]) is a single [string] indicating 
// the name of the keyspace that has been set.
class CqlSetKeyspaceResultDecoder final : public CqlResultDecoder {
public:
    CqlSetKeyspaceResultDecoder(const butil::IOBuf& body)
        : CqlResultDecoder(body) {}

    const std::string& keyspace() const {
        return _keyspace;
    }

private:
    virtual bool DecodeCqlResult(butil::IOBufBytesIterator& iter);

    std::string _keyspace;
};

class CqlRowsResultDecoder;

enum CqlValueTypeId {
    CQL_CUSTOM    = 0x00000000,
    CQL_ASCII     = 0x00000001,
    CQL_BIGINT    = 0x00000002,
    CQL_BLOB      = 0x00000003,
    CQL_BOOLEAN   = 0x00000004,
    CQL_COUNTER   = 0x00000005,
    CQL_DECIMAL   = 0x00000006,
    CQL_DOUBLE    = 0x00000007,
    CQL_FLOAT     = 0x00000008,
    CQL_INT       = 0x00000009,
    CQL_TIMESTAMP = 0x0000000B,
    CQL_UUID      = 0x0000000C,
    CQL_VARCHAR   = 0x0000000D,
    CQL_VAEINT    = 0x0000000E,
    CQL_TIMEUUID  = 0x0000000F,
    CQL_INET      = 0x00000010,
    CQL_LIST      = 0x00000020,
    CQL_MAP       = 0x00000021,
    CQL_SET       = 0x00000022,
    CQL_UDT       = 0x00000030,
    CQL_TUPLE     = 0x00000031,
    CQL_DUMMY_TYPE = 0x0000FFFF
};

template<typename TValue>
bool CheckCppTypeCategories(CqlValueTypeId type_id, TValue*) {
    if (std::is_same<TValue, std::string>::value) {
        return true;
    }
    switch (type_id) {
    case CQL_BOOLEAN:
        return std::is_same<TValue, bool>::value;
    case CQL_INT:
        return std::is_same<TValue, int32_t>::value;
    case CQL_BIGINT:
        return std::is_same<TValue, int64_t>::value;
    case CQL_FLOAT:
        return std::is_same<TValue, float>::value;
    case CQL_DOUBLE:
        return std::is_same<TValue, double>::value;
    default:
        break; 
    }
    return false;
}

struct CqlColumnSpec {
    CqlValueTypeId type_id = CQL_DUMMY_TYPE;
    std::vector<CqlValueTypeId> elements_type_id;
    std::string name;

    bool IsValid() const {
        return type_id != CQL_DUMMY_TYPE;
    }    
};

// The meta data is composed of :
// <flags><columns_count>[<paging_state>][<global_table_spec>?<col_spec_1>...<col_spec_n>]
class CqlMetaData {
public:
    virtual bool Decode(butil::IOBufBytesIterator& iter);

    bool DecodeFlags(butil::IOBufBytesIterator& iter);
    bool DecodeColumnsCount(butil::IOBufBytesIterator& iter);
    bool DecodeGlobalTabelSpec(butil::IOBufBytesIterator& iter);
    bool CqlDecodeColumnType(butil::IOBufBytesIterator& iter);
    bool DecodeColumnsSpec(butil::IOBufBytesIterator& iter);

    const std::string& error() const { 
        return _error;
    }

private:
friend class CqlRowsResultDecoder;

    bool has_paging_state() const { return _flags & 0x0002; }
    bool has_meta_data() const { return ~_flags & 0x0004; }
    bool has_global_tables_spec() const { return _flags & 0x0001; }

    CqlValueTypeId CqlDecodeCqlOption(butil::IOBufBytesIterator& iter);

    std::string _error;
		
    int32_t _flags = 0;
    int32_t _columns_count = 0;
    int32_t _paging_state = 0;
    std::string _keyspace;
    std::string _table;

    std::vector<CqlColumnSpec> _column_specs;
    std::map<butil::StringPiece, size_t> _column_specs_map;
};

// Indicates a set of rows. The rest of body of a Rows result is:
// <metadata><rows_count><rows_content>
class CqlRowsResultDecoder final : public CqlResultDecoder {
public:
    CqlRowsResultDecoder(const butil::IOBuf& body) : CqlResultDecoder(body) {}

    // Get value of column column_name in the row_index row.
    template<typename TValue>
    int DecodeColumnValueOfRow(const butil::StringPiece column_name,
                             size_t row_index, TValue* column_value) {
        const butil::IOBufBytesIterator* p_iter = GetRowIter(row_index);
        if (p_iter == nullptr) {
            return -1;
        }
        butil::IOBufBytesIterator iter = *p_iter;
        const int offset = GetColumnValueSpecOffset(column_name);
        if (offset < 0) {
            return -1;
        }  
        if (!CheckCppTypeCategories(
            _meta_data._column_specs.at(offset).type_id, column_value)) {
            set_error("Invalid value type");
            return -1;
        }

        LOG(ERROR) << "debug column_name=" << column_name << " offset=" << offset;
        if (ForwardToColumnOfRow(offset, iter)) {
            const int ret = CqlDecodeBytes(iter, column_value);
            if (ret == -1) {
                set_error("Fail to decode cql value.");
            }
            return ret;
        }
        return -1;
    }

    template<typename TValue>
    int DecodeColumnValueOfRow(const butil::StringPiece column_name,
                               size_t row_index, std::vector<TValue>* column_value) {
        const butil::IOBufBytesIterator* p_iter = GetRowIter(row_index);
        if (p_iter == nullptr) {
            return -1;
        }
        butil::IOBufBytesIterator iter = *p_iter;
        const int offset = GetColumnValueSpecOffset(column_name);
        if (offset < 0) {
            return -1;
        }
				const CqlColumnSpec& column_spec = _meta_data._column_specs.at(offset);
				if (column_spec.type_id != CQL_LIST) {
            set_error("Not a cql list type");
            return -1;
        }
        if (!CheckCppTypeCategories(column_spec.elements_type_id.at(0), (TValue*)0)) {
            set_error("Invalid cql list element type");
            return -1;
        }
        if (ForwardToColumnOfRow(offset, iter)) {
            const int ret = CqlDecodeBytes(iter, column_value);
            if (ret == -1) {
                set_error("Fail to decode cql list.");
            }
            return ret;
        }
        return -1;
    }

    template<typename TValue>
    int DecodeColumnValueOfRow(const butil::StringPiece column_name,
                               size_t row_index, std::set<TValue>* column_value) {
        const butil::IOBufBytesIterator* p_iter = GetRowIter(row_index);
        if (p_iter == nullptr) {
            return -1;
        }
        butil::IOBufBytesIterator iter = *p_iter;
        const int offset = GetColumnValueSpecOffset(column_name);
        if (offset < 0) {
            return -1;
        }
				const CqlColumnSpec& column_spec = _meta_data._column_specs.at(offset);
				if (column_spec.type_id != CQL_SET) {
            set_error("Not a cql set type");
            return -1;
        }
        if (!CheckCppTypeCategories(column_spec.elements_type_id.at(0), (TValue*)0)) {
            set_error("Invalid cql set element type");
            return -1;
        }
        if (ForwardToColumnOfRow(offset, iter)) {
            const int ret = CqlDecodeBytes(iter, column_value);
            if (ret == -1) {
                set_error("Fail to decode cql set.");
            }
            return ret;
        }
        return -1;
    }

    template<typename TKey, typename TValue>
    int DecodeColumnValueOfRow(const butil::StringPiece column_name,
                               size_t row_index, std::map<TKey, TValue>* column_value) {
        const butil::IOBufBytesIterator* p_iter = GetRowIter(row_index);
        if (p_iter == nullptr) {
            return -1;
        }
        butil::IOBufBytesIterator iter = *p_iter;
        const int offset = GetColumnValueSpecOffset(column_name);
        if (offset < 0) {
            return -1;
        }
				const CqlColumnSpec& column_spec = _meta_data._column_specs.at(offset);
				if (column_spec.type_id != CQL_MAP) {
            set_error("Not a cql map type");
            return -1;
        }
        if (!CheckCppTypeCategories(column_spec.elements_type_id.at(0), (TKey*)0)) {
            set_error("Invalid cql map key type");
            return -1;
        }
        if (!CheckCppTypeCategories(column_spec.elements_type_id.at(1), (TValue*)0)) {
            set_error("Invalid cql map value type");
            return -1;
        }				
        if (ForwardToColumnOfRow(offset, iter)) {
            const int ret = CqlDecodeBytes(iter, column_value);
            if (ret == -1) {
                set_error("Fail to decode cql map.");
            }
            return ret;
        }
        return -1;
    }

    size_t columns_count() const {
        return _meta_data._columns_count;    
    }

    size_t rows_count() const {
        return _rows_count;
    }

    const std::vector<CqlColumnSpec>& GetColumnSpecs() const {
        return _meta_data._column_specs;
    }

    void set_error(const butil::StringPiece e) {
        _error.assign(e.data(), e.size());
    }

    void MoveErrorTo(std::string* error) {
        error->swap(_error);
        _error.clear();
    }

private:
    virtual bool DecodeCqlResult(butil::IOBufBytesIterator& iter);

    bool DecodeRowsCount(butil::IOBufBytesIterator& iter);

    const butil::IOBufBytesIterator* GetRowIter(size_t index);

    int GetColumnValueSpecOffset(const butil::StringPiece column_name);

    bool ForwardToColumnOfRow(const butil::StringPiece column_name,
                              butil::IOBufBytesIterator& row_iter);
    bool ForwardToColumnOfRow(size_t offset, butil::IOBufBytesIterator& row_iter);

    bool BuildNextRowIter();

    // The number of rows present in this result.
    int32_t _rows_count = 0;
    CqlMetaData _meta_data;
    // Point to iobuf of <rows_content>.
    std::vector<butil::IOBufBytesIterator> _rows_iter;
};

bool CqlDecodeHead(butil::IOBufBytesIterator& iter, CqlFrameHead* head);

inline bool IsResponseOpcode(uint8_t opcode) {
    return opcode == CQL_OPCODE_RESULT ||
           opcode == CQL_OPCODE_ERROR ||
           opcode == CQL_OPCODE_READY ||
           opcode == CQL_OPCODE_AUTHENTICATE ||
           opcode == CQL_OPCODE_SUPPORTED ||
           opcode == CQL_OPCODE_EVENT ||
           opcode == CQL_OPCODE_AUTH_CHALLENGE ||
           opcode == CQL_OPCODE_AUTH_SUCCESS;
}

void CqlEncodeHead(const CqlFrameHead& header, butil::IOBuf* buf);

void CqlEncodeStartup(CqlFrameHead& head,
                      const std::map<std::string, std::string>& options,
                      butil::IOBuf* buf);

inline void CqlEncodePlainTextAuthenticate(
    const std::string& user_name,
    const std::string& password,
    butil::IOBuf* buf) {
    buf->push_back(0);
    buf->append(user_name);
    buf->push_back(0);
    buf->append(password);
}

void CqlEncodeConsistency(CqlConsistencyLevel consistency, butil::IOBuf* buf);

void CqlEncodeQuery(const std::string& query,
                    const CqlQueryParameter& query_paramter,
                    butil::IOBuf* buf);

void CqlEncodeQuery(const butil::IOBuf& query,
                    const CqlQueryParameter& query_paramter,
                    butil::IOBuf* buf);

inline void CqlEncodeUseKeyspace(
    const std::string& query, butil::IOBuf* buf) {
    CqlEncodeQuery(query, CqlQueryParameter(), buf);   
}

const char* GetCqlOpcodeName(uint8_t opcode);

int32_t CqlDecodeResultType(butil::IOBufBytesIterator& iter);

bool NewAndDecodeCqlResult(const butil::IOBuf& cql_body,
                           std::unique_ptr<CqlResponseDecoder>* decoder);
bool NewAndDecodeCqlError(const butil::IOBuf& cql_body,
                          std::unique_ptr<CqlResponseDecoder>* decoder);

struct CqlMessageOsWrapper {
    CqlMessageOsWrapper(
        const CqlFrameHead& h, const butil::IOBuf& b, bool vbs = false)
        : verbose(vbs), head(h), body(b) {}

    CqlMessageOsWrapper(const CqlResponse& response, bool vbs = false)
        : CqlMessageOsWrapper(response.head, response.body, vbs) {}

    bool verbose = false;
    const CqlFrameHead& head;
    const butil::IOBuf& body;
};

std::ostream& operator<<(std::ostream& os, const CqlFrameHead& head);
std::ostream& operator<<(std::ostream& os, const CqlMessageOsWrapper& msg);

} // namespace brpc

#endif  // BRPC_CQL_MESSAGES_H
