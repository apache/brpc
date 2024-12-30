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

#include "brpc/cql_messages.h"

#include <array>
#include <iomanip>

#include "butil/logging.h"
#include "butil/string_printf.h"    // string_appendf()

namespace brpc {

namespace {

union FloatIntCast {
    float f;
    int32_t i;
};

union DoubleIntCast {
    double d;
    int64_t i;
};

std::array<const char*, CQL_OPCODE_LAST_DUMMY> kCqlOpcodeNames = 
    {"'Error response'", "'Startup request'", "'Ready response'",
     "'Authenticate response'", "Not existing message", "'Options request'",
     "'Supported request'", "'Query request'", "'Result response'",
     "'Prepare request'", "'Exexute request'", "'Register request'",
     "'Event response'", "'Batch request'", "'Auth_challenge response'",
     "'Auth_response request'", "'Auth_success response'"};

} // namespace


void CqlEncodeBytes(float value, butil::IOBuf* buf) {
    FloatIntCast fi;
    fi.f = value;
    CqlEncodeBytes(fi.i, buf);
}

void CqlEncodeBytes(double value, butil::IOBuf* buf) {
    DoubleIntCast di;
    di.d = value;
    CqlEncodeBytes(di.i, buf);
}

inline void CqlEncodeString(const std::string& s, butil::IOBuf* buf) {
    if (!s.empty()) {
        CqlEncodeUint16(s.size(), buf);
        buf->append(s);
    }
}

// Encode CQL [string map] to buf.
// Return bytes encoded into buf.
void CqlEncodeStringMap(
    const std::map<std::string, std::string>& pairs, 
    butil::IOBuf* buf) {
    if (pairs.empty()) {
        return;
    }
    CqlEncodeUint16(pairs.size(), buf);
    for (const std::pair<std::string, std::string>& pair : pairs) {
        CqlEncodeString(pair.first, buf);
        CqlEncodeString(pair.second, buf);
    }
}

// Encode cql [long string] type to buffer.
// [long string]: An [int] n, followed by n bytes representing an UTF-8 string.
inline void CqlEncodeLongStringTo(const butil::StringPiece s, butil::IOBuf* buf) {
    CqlEncodeUint32(s.size(), buf);
    buf->append(s.data(), s.size());
}

inline void CqlEncodeConsistency(CqlConsistencyLevel consistency, butil::IOBuf* buf) {
    return CqlEncodeUint16(consistency, buf);
}

void CqlEncodeQueryParameter(const CqlQueryParameter& parameter, butil::IOBuf* buf) {
    CqlEncodeConsistency(parameter._consistency, buf);
    buf->push_back(parameter._flags);
    if (parameter.has_flag(CqlQueryParameter::WITH_VALUES)) {
        if (parameter._num_name_value > 0) {
            CqlEncodeUint16(parameter._num_name_value, buf);
            buf->append(parameter._name_value);
        }
    }
    if (parameter.has_flag(CqlQueryParameter::SERIAL_CONSISTENCY)) {
        CqlEncodeConsistency(parameter._serial_consistency, buf);
    }
    if (parameter.has_flag(CqlQueryParameter::DEFAULT_TIMESTAMP)) {
        CqlEncodeInt64(parameter._timestamp, buf);
    }
    // TODO(caidaojin): Process other flags if needed.
}

void CqlEncodeQuery(const std::string& query,
                    const CqlQueryParameter& query_parameter,
                    butil::IOBuf* buf) {
    if (query.empty()) {
        return;
    }
    CqlEncodeUint32(query.size(), buf);
    buf->append(query);
    CqlEncodeQueryParameter(query_parameter, buf);
}

void CqlEncodeQuery(const butil::IOBuf& query,
                    const CqlQueryParameter& query_parameter,
                    butil::IOBuf* buf) {
    if (query.empty()) {
        return;
    }
    CqlEncodeUint32(query.size(), buf);
    buf->append(query);
    CqlEncodeQueryParameter(query_parameter, buf);
}

void CqlEncodeHead(const CqlFrameHead& header, butil::IOBuf* buf) {
    buf->push_back(header.version);
    buf->push_back(header.flag);
    CqlEncodeInt16(header.stream_id, buf);
    buf->push_back(header.opcode);
    CqlEncodeUint32(header.length, buf);
}

void CqlEncodeStartup(CqlFrameHead& head,
                      const std::map<std::string, std::string>& options,
                      butil::IOBuf* buf) {
    butil::IOBuf body;
    CqlEncodeStringMap(options, &body);
    head.length = body.size();
    CqlEncodeHead(head, buf);
    buf->append(body);
}

// CQL [string] : A [short] n, followed by n bytes representing an UTF-8 string.
bool CqlDecodeString(butil::IOBufBytesIterator& iter, std::string* out) {
    if (iter.bytes_left() < 3) {
        return false;
    } 
    uint16_t ssize = CqlDecodeUint16(iter);
    if (iter.bytes_left() >= ssize) {
        if (out != nullptr) {
            if (static_cast<int>(out->capacity()) < ssize) {
                out->reserve(ssize);
            }
            iter.copy_and_forward(out, ssize);
        } else {
            iter.forward(ssize);
        }
        return true;
    }
    return false;
}

// Just forword IOBufBytesIterator number of CQL [string].
// CQL [string]: A [short] n, followed by n bytes representing an UTF-8 string.
inline bool CqlForwardStrings(butil::IOBufBytesIterator& iter, size_t num) {
    for (size_t i = 0; i != num; ++i) {
        if (!CqlDecodeString(iter, nullptr)) {
            return false;
        }
    }
    return true;
}

int CqlDecodeBytes(butil::IOBufBytesIterator& iter, std::string* out) {
    if (iter.bytes_left() < sizeof(int32_t)) {
        return -1;
    }
    int32_t nbytes = CqlDecodeInt32(iter);
    if (nbytes <= 0) {
        return 1;
    }
    if ((int)iter.bytes_left() < nbytes) {
        return -1;
    }
    if (out != nullptr) {
        if (static_cast<int>(out->capacity()) < nbytes) {
            out->reserve(nbytes);
        }		
        iter.copy_and_forward(out, nbytes);
    } else {
        iter.forward(nbytes);
    }

    return 0;
}

int CqlDecodeBytes(butil::IOBufBytesIterator& iter, bool* out) {
    if (iter.bytes_left() < sizeof(int32_t)) {
        return -1;
    }
    int32_t nbytes = CqlDecodeInt32(iter);
    if (nbytes == 1) {
        if (iter.bytes_left() < 1) {
            return -1;
        } 
        *out = *iter == 0x01;
        return 0;
    } else if (nbytes <= 0) {
        return 1;
    }
    return -1;
}

int CqlDecodeBytes(butil::IOBufBytesIterator& iter, int32_t* out) {
    if (iter.bytes_left() < sizeof(int32_t)) {
        return -1;
    }
    int32_t nbytes = CqlDecodeInt32(iter);
    if (nbytes == 4) {
        if (iter.bytes_left() < 4) {
            return -1;
        } 
        *out = CqlDecodeInt32(iter);
        return 0;
    } else if (nbytes <= 0) {
        return 1;
    }
    return -1;
}

int CqlDecodeBytes(butil::IOBufBytesIterator& iter, int64_t* out) {
    if (iter.bytes_left() < sizeof(int32_t)) {
        return -1;
    }
    int32_t nbytes = CqlDecodeInt32(iter);
    if (nbytes == 8) {
        if (iter.bytes_left() < 8) {
            return -1;
        } 
        *out = CqlDecodeInt64(iter);
        return 0;
    } else if (nbytes <= 0) {
        return 1;
    }
    return -1;
}

int CqlDecodeBytes(butil::IOBufBytesIterator& iter, float* out) {
    if (iter.bytes_left() < sizeof(int32_t)) {
        return -1;
    }
    int32_t nbytes = CqlDecodeInt32(iter);
    if (nbytes == sizeof(float)) {
        if (iter.bytes_left() < sizeof(float)) {
            return -1;
        }
        FloatIntCast fi;
        fi.i = CqlDecodeInt32(iter);
        *out = fi.f;
        return 0;
    } else if (nbytes <= 0) {
        return 1;
    }
    return -1;
}

int CqlDecodeBytes(butil::IOBufBytesIterator& iter, double* out) {
    if (iter.bytes_left() < sizeof(int32_t)) {
        return -1;
    }
    int32_t nbytes = CqlDecodeInt32(iter);
    if (nbytes == sizeof(double)) {
        if (iter.bytes_left() < sizeof(double)) {
            return -1;
        }
        DoubleIntCast di;
        di.i = CqlDecodeInt64(iter);
        *out = di.d;
        return 0;
    } else if (nbytes <= 0) {
        return 1;
    }
    return -1;
}

bool CqlDecodeHead(butil::IOBufBytesIterator& iter, CqlFrameHead* head) {
    if (iter.bytes_left() < head->SerializedSize()) {
        return false;
    }
    head->version = *iter; ++iter;
    head->flag = *iter; ++iter;
    head->stream_id = CqlDecodeInt16(iter);
    head->opcode = *iter; ++iter;
    head->length = CqlDecodeInt32(iter);
    return true;
}

const size_t CqlFrameHead::kCqlFrameHeadSize = 9;

void CqlQueryParameter::MoveValuesTo(butil::IOBuf* buf) {
    remove_flag(WITH_VALUES);
    CqlEncodeUint16(_num_name_value, buf);
    if (_num_name_value > 0) {
        buf->append(_name_value.movable());
    }
    _num_name_value = 0;
}

const std::string& CqlErrorDecoder::GetErrorDetails() {
    if (!has_decoded()) {
        return _error_details;
    }
    if (_error_details.empty()) {
        CqlDecodeString(_decoded_body, &_error_details);
    }
    return _error_details;
}

bool CqlErrorDecoder::Decode() {
    if (has_decoded()) {
        return has_decoded_success();
    }
    if (!DecodeErrorCode()) {
        return false;
    }
    return CqlDecodeString(_decoded_body, nullptr);
}

bool CqlErrorDecoder::DecodeErrorCode() {
    if (_decoded_body.bytes_left() <= 4) {
        _error = "No enough buffer to parse cql ERROR.";
        return false;
    }
    _error_code = CqlDecodeInt32(_decoded_body);

    const char* e = GetErrorBrief();
    if(e != nullptr) {
        _error = e;
        return true;
    }
    return false;
}

const char* CqlErrorDecoder::GetErrorBrief() {
    switch (_error_code) {
    case 0x0000:
        return "Server error";
    case 0x000A:
        return "Protocol error";
    case 0x0100:
        return "Authentication error";
    case 0x1000:
        return "Unavailable exception";
    case 0x1001:
        return "Overloaded";
    case 0x1002:
        return "Is_bootstrapping";
    case 0x1003:
        return "Truncate_error";
    case 0x1100:
        return "Write_timeout";
    case 0x1200:
        return "Read_timeout";
    case 0x1300:
        return "Read_failure";
    case 0x1400:
        return "Function_failure";
    case 0x1500:
        return "Write_failure";
    case 0x2000:
        return "Syntax_error";
    case 0x2100:
        return "Unauthorized";
    case 0x2200:
        return "Invalid";
    case 0x2300:
        return "Config_error";
    case 0x2400:
        return "Already_exists";
    case 0x2500:
        return "Unprepared";
    default:
        break;
    }

    return nullptr;
}

bool CqlAuthenticateDecoder::Decode() {
    if (has_decoded()) {
        return has_decoded_success();
    }
    butil::IOBufBytesIterator iter(_body_iter);
    if (!CqlDecodeString(iter, &_authenticator)) {
        _error = "Fail to decode cql authenticate response.";
        return false;
    }
    if (iter.bytes_left() != 0) {
        _error = "Invalid body length of cql authenticate response.";
        return false;
    }
    return true;
}

bool CqlAuthSuccessDecoder::Decode() {
    if (has_decoded()) {
        return has_decoded_success();
    }

    butil::IOBufBytesIterator iter(_body_iter);
    if (CqlDecodeBytes(iter, reinterpret_cast<std::string*>(0)) != -1
        && iter.bytes_left() == 0) {
        return true;
    }
    _error = "Invalid cql auth success body.";
    return false;
}

bool CqlAuthSuccessDecoder::DecodeAuthToken(std::string* out) {
    butil::IOBufBytesIterator iter(_body_iter);
    if (CqlDecodeBytes(iter, out) != -1) {
        return true;
    }
    return false;
}

bool CqlResultDecoder::Decode() {
    butil::IOBufBytesIterator iter(_body_iter);
    _type = CqlDecodeResultType(iter);
    return _type != CQL_RESULT_LAST_DUMMY && DecodeCqlResult(iter);
}

bool CqlSetKeyspaceResultDecoder::DecodeCqlResult(
    butil::IOBufBytesIterator& iter) {
    if (!CqlDecodeString(iter, &_keyspace)) {
        _error = "Fail to parse keyspace from cql result body.";
        return false;
    }
    if (iter.bytes_left() != 0) {
        _error = "Invalid body length of cql use keyspace result.";
        return false;
    }
    return true;
}

// The meta data is composed of :
// <flags><columns_count>[<paging_state>][<global_table_spec>?<col_spec_1>...<col_spec_n>]
bool CqlMetaData::Decode(butil::IOBufBytesIterator& iter) {
    if (!DecodeFlags(iter) || !DecodeColumnsCount(iter)) {
        return false;
    }
    if (has_paging_state()) {
        CHECK(false) << "TODO(caidaojin): Now not support paging_state.";
    }

    if (has_meta_data()) {
        // Handle global_table_spec. 
        if (has_global_tables_spec() && !DecodeGlobalTabelSpec(iter)) {
            return false;
        }
        return DecodeColumnsSpec(iter);
    }

    return true;
}

// Flags are composed following bit mask: 0x0001, 0x0002 and 0x0004.
bool CqlMetaData::DecodeFlags(butil::IOBufBytesIterator& iter) {
    if (iter.bytes_left() >= sizeof(_flags)) {
        _flags = CqlDecodeInt32(iter);
    }
    bool ret = _flags & ~(0x0007);
    if (ret) {
       butil::string_appendf(
           &_error, "Invalid flags(0x%02X\n) in meta data of cql rows result", _flags);
    }
    return !ret;
}

bool CqlMetaData::DecodeColumnsCount(butil::IOBufBytesIterator& iter) {
    if (iter.bytes_left() >= sizeof(_columns_count)) {
        _columns_count = CqlDecodeInt32(iter);
        if (_columns_count >= 0) {
            return true;
        }
        _error = "Negative columns count in meta data of cql rows result.";
        return false;
    }
    _error = "Fail to parse columns count due to be short of remain bytes";
    return false;
}

// It is composed of two [string] representing the
// (unique) keyspace name and table name the columns belong to.
bool CqlMetaData::DecodeGlobalTabelSpec(butil::IOBufBytesIterator& iter) {
    if (CqlDecodeString(iter, &_keyspace)
        && CqlDecodeString(iter, &_table)) {
        return true;
    }
    _error = "Fail to parse global table spec of cql rows result.";
    return false;
}

inline CqlValueTypeId CqlMetaData::CqlDecodeCqlOption(butil::IOBufBytesIterator& iter) {
    if (iter.bytes_left() < 2) {
        return CQL_DUMMY_TYPE;
    }
    CqlValueTypeId id = static_cast<CqlValueTypeId>(CqlDecodeUint16(iter));
    CHECK(id != CQL_CUSTOM && id != CQL_UDT)
        << "Do not support cql UDT and custom type now!";
    return id;
}

bool CqlMetaData::CqlDecodeColumnType(butil::IOBufBytesIterator& iter) {
    CqlColumnSpec& column_spec = _column_specs.back();
    CHECK(!column_spec.IsValid());
    CqlValueTypeId type_id = CqlDecodeCqlOption(iter);
    if (type_id == CQL_DUMMY_TYPE) {
        return false;
    }
    column_spec.type_id = type_id;
    CqlValueTypeId element_type_id;
    switch (type_id) {
    case CQL_LIST:
    case CQL_SET:
        element_type_id = CqlDecodeCqlOption(iter);
        if(element_type_id != CQL_DUMMY_TYPE) {
            column_spec.elements_type_id.emplace_back(element_type_id);
            return true;
        }
        return false;
    case CQL_MAP:
        for (int i = 0; i != 2; ++i) {
            element_type_id = CqlDecodeCqlOption(iter);
            if(element_type_id == CQL_DUMMY_TYPE) {
                return false;
            }
            column_spec.elements_type_id.emplace_back(element_type_id);
        }
        return true;
    case CQL_TUPLE: 
        if (iter.bytes_left() > 2) {
            const size_t n = CqlDecodeUint16(iter);
            for (size_t i = 0; i != n; ++i) {
                element_type_id = CqlDecodeCqlOption(iter);
                if (element_type_id == CQL_DUMMY_TYPE) {
                    return false;
                }
                column_spec.elements_type_id.emplace_back(element_type_id);
            }
            return true;
        }
        return false;
    case CQL_CUSTOM:
    case CQL_UDT:
        return false;
    default:
        return true;
    }
    return false;
}

// <col_spec_i> specifies the columns returned in the query. There are
// <column_count> such column specifications that are composed of:
// (<ksname><tablename>)?<name><type>
bool CqlMetaData::DecodeColumnsSpec(butil::IOBufBytesIterator& iter) {
    const bool global_spec = has_global_tables_spec();
    for (int i = 0; i != _columns_count; ++i) {
        if (!global_spec) {
            if (_keyspace.empty() && _table.empty()) {
                if (!DecodeGlobalTabelSpec(iter)) {
                    butil::string_appendf(
                        &_error, 
                        "Fail to decode keyspace and table name of the %dth column spec.",
                        i + 1);
                    return false;
                }
            } else if (!CqlForwardStrings(iter, 2)) {
                butil::string_appendf(
                    &_error, 
                    "Fail to skip keyspace and table name of the %dth column spec.",
                    i + 1);
                return false;
            }
        }
        _column_specs.emplace_back();
        if (!CqlDecodeString(iter, &_column_specs.back().name)) {
            butil::string_appendf(
                &_error,
                "Fail to decode column name of the %dth column spec.",
                i + 1);
            return false;
        }
        if (!CqlDecodeColumnType(iter)) {
            butil::string_appendf(
                &_error,
                "Fail to decode column type of the %dth column spec.",
                i + 1);
            return false;
        }
        _column_specs_map.emplace(_column_specs.back().name, _column_specs.size() - 1);
    }
    return true;
}

int CqlRowsResultDecoder::GetColumnValueSpecOffset(
    const butil::StringPiece name) {
    auto iter = _meta_data._column_specs_map.find(name);
    if (iter != _meta_data._column_specs_map.end()) {
        return iter->second;
    }
    return -1;
}

bool CqlRowsResultDecoder::DecodeCqlResult(butil::IOBufBytesIterator& iter) {
    if (!_meta_data.Decode(iter)) {
        set_error(_meta_data.error());
        return false;
    }

    if (!DecodeRowsCount(iter)) {
        set_error("Fail to decode cql result rows count.");
        return false;
    }

    _rows_iter.emplace_back(iter);

    return true;
}

bool CqlRowsResultDecoder::DecodeRowsCount(butil::IOBufBytesIterator& iter) {
    if (iter.bytes_left() >= 4) {
        _rows_count = CqlDecodeInt32(iter);
        return true;
    }
    return false;
}

bool CqlRowsResultDecoder::ForwardToColumnOfRow(
    const butil::StringPiece column_name, butil::IOBufBytesIterator& row_iter) {
    int offset = GetColumnValueSpecOffset(column_name);
    if (offset < 0) {
        return false;
    }
    return ForwardToColumnOfRow((size_t)offset, row_iter);
}

bool CqlRowsResultDecoder::ForwardToColumnOfRow(
    size_t offset, butil::IOBufBytesIterator& row_iter) {
    for (size_t i = 0; i != offset; ++i) {
        if (-1 == CqlDecodeBytes(row_iter, reinterpret_cast<std::string*>(0))) {
            set_error("Fail to find cql column position in response");
            return false;
        }
    }
    return true;
}

const butil::IOBufBytesIterator* CqlRowsResultDecoder::GetRowIter(size_t index) {
    if (rows_count() <= index) {
        set_error("Invalid cql row index");
        return nullptr;
    }
    while (_rows_iter.size() <= index) {
        if (!BuildNextRowIter()) {
            set_error("Fail to find cql row buf");
            return nullptr;
        }
    }
    return &_rows_iter[index];
}

bool CqlRowsResultDecoder::BuildNextRowIter() {
    butil::IOBufBytesIterator last_iter = _rows_iter.back();
    for (size_t i = 0; i != columns_count(); ++i) {
        if (-1 == CqlDecodeBytes(last_iter, reinterpret_cast<std::string*>(0))) {
            return false;
        }
    }
    _rows_iter.emplace_back(last_iter);
    return true;
}

const char* GetCqlOpcodeName(uint8_t opcode) {
    return opcode < kCqlOpcodeNames.size() ? kCqlOpcodeNames[opcode] : "Unknow cql opcode";
}

int32_t CqlDecodeResultType(butil::IOBufBytesIterator& iter) {
    if (iter.bytes_left() >= 4) {
        return CqlDecodeInt32(iter);
    }
    return CQL_RESULT_LAST_DUMMY;
}

bool NewAndDecodeCqlResult(const butil::IOBuf& result_body,
                           std::unique_ptr<CqlResponseDecoder>* decoder) {
    butil::IOBufBytesIterator iter(result_body);
    switch (CqlDecodeResultType(iter)) {
    case CQL_RESULT_ROWS: {
        CqlRowsResultDecoder* rows_result =
            new (std::nothrow) CqlRowsResultDecoder(result_body);
        if (rows_result != nullptr && rows_result->DecodeResponse()) {
            decoder->reset(rows_result);
            return true;
        }
        break;
    }
    case CQL_RESULT_VOID: {
        CqlVoidResultDecoder* void_result =
            new (std::nothrow) CqlVoidResultDecoder(result_body);
        if (void_result != nullptr && void_result->DecodeResponse()) {
            decoder->reset(void_result);
            return true;
        }
        break;
    }
    case CQL_RESULT_SET_KEYSPACE:
    case CQL_RESULT_PREPARED:
    case CQL_RESULT_SCHEMA_CHANGE:
    default:
        break;
    }
    return false;
}
  
bool NewAndDecodeCqlError(const butil::IOBuf& error_body,
    std::unique_ptr<CqlResponseDecoder>* decoder) {
    decoder->reset(new (std::nothrow) CqlErrorDecoder(error_body));
    return *decoder != nullptr && (*decoder)->DecodeResponse();
}

std::ostream& operator<<(std::ostream& os, const CqlFrameHead& head) {
    os << "CqlFrameHead: [" << GetCqlOpcodeName(head.opcode)
			 << " version=" << (int)(head.version & ~0x80)
       << " flag=" << (int)head.flag
       << " opcode=" << (int)head.opcode 
       << " stream_id=" << (int)head.stream_id 
       << " length=" << (int)head.length << ']';
    return os;
}

std::ostream& operator<<(std::ostream& os,
                        const CqlMessageOsWrapper& cql_msg_wrapper) {
    const butil::IOBuf& body = cql_msg_wrapper.body;
    os << cql_msg_wrapper.head << "\nCqlFrameBody: size=" << body.size() 
       <<"\n[ASSIC:" << body << "]";
    
    if (cql_msg_wrapper.verbose) {
        os << "\n[HEX:";
        butil::IOBufBytesIterator it(body);
        for (size_t i = 0; i != body.size(); ++i) {
            os << ' ' << std::setw(2) << std::setfill('0') << std::hex << (int)*it;
            it++;
        }
        os << ']';
    }
    return os;
}

} // namespace brpc
