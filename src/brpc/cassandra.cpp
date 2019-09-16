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

#define INTERNAL_SUPPRESS_PROTOBUF_FIELD_DEPRECATION
#include <algorithm>

#include <gflags/gflags.h>
#include <google/protobuf/stubs/once.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite_inl.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection_ops.h>
#include <google/protobuf/wire_format.h>

#include "brpc/cassandra.h"
#include "butil/logging.h"
#include "butil/strings/string_piece.h"
#include "butil/string_printf.h"    // string_appendf()

namespace brpc {

DECLARE_bool(enable_cql_prepare);

// Internal implementation detail -- do not call these.
void protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto_impl();
void protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto();
void protobuf_AssignDesc_baidu_2frpc_2fcassandra_5fbase_2eproto();
void protobuf_ShutdownFile_baidu_2frpc_2fcassandra_5fbase_2eproto();

namespace {

const ::google::protobuf::Descriptor* CassandraRequest_descriptor_ = NULL;
const ::google::protobuf::Descriptor* CassandraResponse_descriptor_ = NULL;

}  // namespace

void protobuf_AssignDesc_baidu_2frpc_2fcassandra_5fbase_2eproto() {
    protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto();
    const ::google::protobuf::FileDescriptor* file =
        ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName(
            "baidu/rpc/cassandra_base.proto");
    GOOGLE_CHECK(file != NULL);
    CassandraRequest_descriptor_ = file->message_type(0);
    CassandraResponse_descriptor_ = file->message_type(1);
}

namespace {

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AssignDescriptors_once_);
inline void protobuf_AssignDescriptorsOnce() {
    ::google::protobuf::GoogleOnceInit(&protobuf_AssignDescriptors_once_,
                                       &protobuf_AssignDesc_baidu_2frpc_2fcassandra_5fbase_2eproto);
}

void protobuf_RegisterTypes(const ::std::string&) {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
        CassandraRequest_descriptor_, &CassandraRequest::default_instance());
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
        CassandraResponse_descriptor_, &CassandraResponse::default_instance());
}

}  // namespace

void protobuf_ShutdownFile_baidu_2frpc_2fcassandra_5fbase_2eproto() {
    delete CassandraRequest::default_instance_;
    delete CassandraResponse::default_instance_;
}

void protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto_impl() {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

#if GOOGLE_PROTOBUF_VERSION >= 3002000
    ::google::protobuf::internal::InitProtobufDefaults();
#else
    ::google::protobuf::protobuf_AddDesc_google_2fprotobuf_2fdescriptor_2eproto();
#endif
  ::google::protobuf::DescriptorPool::InternalAddGeneratedFile(
    "\n\036baidu/rpc/cassandra_base.proto\022\tbaidu."
    "rpc\032 google/protobuf/descriptor.proto\"\022\n"
    "\020CassandraRequest\"\023\n\021CassandraResponse", 118);
  ::google::protobuf::MessageFactory::InternalRegisterGeneratedFile(
    "baidu/rpc/cassandra_base.proto", &protobuf_RegisterTypes);
  ::google::protobuf::protobuf_AddDesc_google_2fprotobuf_2fdescriptor_2eproto();
  ::google::protobuf::internal::OnShutdown(&protobuf_ShutdownFile_baidu_2frpc_2fcassandra_5fbase_2eproto);
}

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto_once);
void protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto() {
    ::google::protobuf::GoogleOnceInit(
        &protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto_once,
        &protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto_impl);
}

// Force AddDescriptors() to be called at static initialization time.
struct StaticDescriptorInitializer_baidu_2frpc_2fcassandra_5fbase_2eproto {
    StaticDescriptorInitializer_baidu_2frpc_2fcassandra_5fbase_2eproto() {
        protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto();
    }
} static_descriptor_initializer_baidu_2frpc_2fcassandra_5fbase_2eproto_;

// ===================================================================

#ifndef _MSC_VER
#endif  // !_MSC_VER

CassandraRequest* CassandraRequest::default_instance_ = nullptr;

CassandraRequest::CassandraRequest() {
    SharedCtor();
}

CassandraRequest::CassandraRequest(CqlConsistencyLevel consistency) {
    SharedCtor();
    set_consistency(CQL_CONSISTENCY_LOCAL_ONE);
}

void CassandraRequest::SharedCtor() {
    Clear();
}

CassandraRequest::~CassandraRequest() {
    SharedDtor();
}

void CassandraRequest::SharedDtor() {
    if (this != default_instance_) {
    }
}

void CassandraRequest::SetCachedSize(int size) const {
    GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
    _cached_size_ = size;
    GOOGLE_SAFE_CONCURRENT_WRITES_END();
}

const ::google::protobuf::Descriptor* CassandraRequest::descriptor() {
	protobuf_AssignDescriptorsOnce();
	return CassandraRequest_descriptor_;
}

const CassandraRequest& CassandraRequest::default_instance() {
    if (default_instance_ == nullptr) {
        protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto();
    }
    return *default_instance_;
}

::google::protobuf::Metadata CassandraRequest::GetMetadata() const {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = CassandraRequest_descriptor_;
    metadata.reflection = nullptr;
    return metadata;
}

void CassandraRequest::InitAsDefaultInstance() {
}

int CassandraRequest::ByteSize() const {
    int total_size = _query.size() + CqlFrameHead::SerializedSize();
    GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
    _cached_size_ = total_size;
    GOOGLE_SAFE_CONCURRENT_WRITES_END();
    return total_size;
}

bool CassandraRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
    LOG(WARNING) << "You're not supposed to parse a CassandraRequest";
    return true;
}

void CassandraRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
    LOG(WARNING) << "You're not supposed to serialize a CassandraRequest";
}

::google::protobuf::uint8* CassandraRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

// Protobuf methods.
CassandraRequest* CassandraRequest::New() const {
    return new (std::nothrow) CassandraRequest();
}

void CassandraRequest::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    Clear();
    const CassandraRequest* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const CassandraRequest*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        CopyFrom(*source);
    }
}

void CassandraRequest::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const CassandraRequest* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const CassandraRequest*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void CassandraRequest::CopyFrom(const CassandraRequest& from) {
    if (&from == this) {
        return;
    }
    _cached_size_ = from._cached_size_;
    _opcode = from._opcode;
    _batch_parameter = from._batch_parameter;
    _query = from._query;
    _query_parameter = from._query_parameter;
}

void CassandraRequest::MergeFrom(const CassandraRequest& from) {
    LOG(WARNING) << "CassandraRequest does not support merge from another.";
}

inline void CassandraRequest::EncodeValuesNumberOfLastBatchedQuery() {
    if (_batch_parameter.curr_query_values > 0) {
        CHECK(_batch_parameter.curr_query_values_area != butil::IOBuf::INVALID_AREA)
            << "Invalid cql batch query values IOBuf area";
        const uint16_t len = butil::HostToNet16(_batch_parameter.curr_query_values);
        CHECK(0 == _query.unsafe_assign(_batch_parameter.curr_query_values_area, &len));
        _batch_parameter.curr_query_values = 0;
    }
    _batch_parameter.curr_query_values_area = butil::IOBuf::INVALID_AREA;
}

void CassandraRequest::HandleBatchQueryAtBeginningIfNeeded() {
    if (_batch_parameter.count == 0) {
        _opcode = CQL_OPCODE_QUERY;
    } else {
        if (_batch_parameter.count == 1) {			
            _opcode = CQL_OPCODE_BATCH;
            // Append query values for first batched query.	
            _query_parameter.MoveValuesTo(&_query);
        } else {
            EncodeValuesNumberOfLastBatchedQuery();
        }
        // Set query kind for current query.
        _query.push_back(_batch_parameter.kind);
    }

    ++_batch_parameter.count;
}

// The batch body must be:
// <type><n><query_1>...<query_n><consistency><flags>[<serial_consistency>][<timestamp>]
// Where a <query_i> must be of the form:
// <kind><string_or_id><n>[<name_1>]<value_1>...[<name_n>]<value_n>
void CassandraRequest::EncodeBatchQueries(butil::IOBuf* buf) {
    buf->push_back(_batch_parameter.type);
    CqlEncodeInt16(_batch_parameter.count, buf);
    // Set query kind for the first query.
    buf->push_back(_batch_parameter.kind);
    EncodeValuesNumberOfLastBatchedQuery();		
}

void CassandraRequest::Encode(butil::IOBuf* buf) {
    if (_query.empty()) {
        return;
    }
    if (is_batch_mode()) {
        EncodeBatchQueries(buf);
    }
    buf->append(_query);
    CqlEncodeQueryParameter(_query_parameter, buf);
}

void CassandraRequest::Clear() {
    _opcode = CQL_OPCODE_LAST_DUMMY;
    _cached_size_ = 0;
    _batch_parameter.clear();
    _query.clear();
    _query_parameter.Reset();
}

bool CassandraRequest::IsInitialized() const {
    return opcode() != CQL_OPCODE_LAST_DUMMY;
}

void CassandraRequest::Print(std::ostream& os) const {
    os << "Cassandra request: [\r\n" 
       << "  opcode=" << GetCqlOpcodeName(opcode()) << "\r\n";
    if (is_batch_mode()) {
        os << "  batch_size=" << _batch_parameter.count << "\r\n";
    }
    os << "]";
}

std::ostream& operator<<(std::ostream& os, const CassandraRequest& r) {
    r.Print(os);
    return os;
}

CassandraResponse::CassandraResponse() {
    SharedCtor();
}

void CassandraResponse::SharedCtor() {
    Clear();
}

CassandraResponse::~CassandraResponse() {
    SharedDtor();
}

void CassandraResponse::SharedDtor() {
    if (this != default_instance_) {
    }
}

void CassandraResponse::SetCachedSize(int size) const {
    GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
    _cached_size_ = size;
    GOOGLE_SAFE_CONCURRENT_WRITES_END();
}

CassandraResponse* CassandraResponse::default_instance_ = nullptr;

::google::protobuf::Metadata CassandraResponse::GetMetadata() const {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = CassandraResponse_descriptor_;
    metadata.reflection = nullptr;
    return metadata;
}

CqlRowsResultDecoder* CassandraResponse::GetRowsResult() {
    if (opcode() != CQL_OPCODE_RESULT) {
        return nullptr;
    }
    return dynamic_cast<CqlRowsResultDecoder*>(_decoder.get());
}

inline int CassandraResponse::GetRowsCount() {
    CqlRowsResultDecoder* rows_result = GetRowsResult();
    if (rows_result != nullptr) {
        return rows_result->rows_count();
    }
    return -1;
}

const std::vector<CqlColumnSpec>* CassandraResponse::GetColumnSpecs() {
    CqlRowsResultDecoder* rows_result = GetRowsResult();
    if (rows_result != nullptr) {
        return &(rows_result->GetColumnSpecs());
    }
    return nullptr;
}

void CassandraResponse::Print(std::ostream& os) const {
    os << "Cassandra response: [\r\n";
    os << head();
}

std::ostream& operator<<(std::ostream& os, const CassandraResponse& response) {
    response.Print(os);
    return os;
}

bool CassandraResponse::Decode() {
    if (_error_code != CASS_UNDECODED) {
        return _error_code != CASS_SUCCESS;
    }
    _error_code = CASS_SUCCESS;
    switch (opcode()) {
    case CQL_OPCODE_RESULT:
        if (NewAndDecodeCqlResult(body(), &_decoder)) {
            return true;
        }
        break;
    case CQL_OPCODE_ERROR:
        if (NewAndDecodeCqlError(body(), &_decoder)) {
            _error = _decoder->error();
            _error_code = 
                static_cast<CqlErrorDecoder*>(_decoder.get())->error_code();
            return true;
        }
        break;
    case CQL_OPCODE_LAST_DUMMY:        
        _error = "Decode a dummy response.";
        break;
    default:
        butil::string_appendf(
            &_error, "Unknown response opcode=`%d`.", opcode());
        break;
    }
    if (_decoder != nullptr) {
        _error = _decoder->error();
    }
    _error_code = CASS_DECODED_FAULT;
    return false;
}

const CassandraResponse& CassandraResponse::default_instance() {
    if (default_instance_ == nullptr) {
        protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto();
    }
    return *default_instance_;
}

void CassandraResponse::InitAsDefaultInstance() {
}

const ::google::protobuf::Descriptor* CassandraResponse::descriptor() {
	protobuf_AssignDescriptorsOnce();
	return CassandraResponse_descriptor_;
}

int CassandraResponse::ByteSize() const {
    int total_size = _cql_response.ByteSize();
    GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
    _cached_size_ = total_size;
    GOOGLE_SAFE_CONCURRENT_WRITES_END();
    return total_size;
}

bool CassandraResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
    LOG(WARNING) << "You're not supposed to parse a CassandraResponse";
    return true;
}

void CassandraResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
    LOG(WARNING) << "You're not supposed to serialize a CassandraResponse";
}

::google::protobuf::uint8* CassandraResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

// Protobuf methods.
CassandraResponse* CassandraResponse::New() const {
    return new (std::nothrow) CassandraResponse();
}

void CassandraResponse::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    Clear();
    const CassandraResponse* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const CassandraResponse*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        CopyFrom(*source);
    }
}

void CassandraResponse::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const CassandraResponse* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const CassandraResponse*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void CassandraResponse::CopyFrom(const CassandraResponse& from) {
    if (&from == this) {
        return;
    }
    _cached_size_ = from._cached_size_;
    _error = from._error;
    _cql_response = from._cql_response;
    Decode(); 
}

void CassandraResponse::MergeFrom(const CassandraResponse& from) {
    LOG(WARNING) << "CassandraResponse does not support merge from another.";
}

void CassandraResponse::Clear() {
    _cached_size_ = 0;
    _error_code = CASS_UNDECODED;
    _error.clear();
    _cql_response.head.Reset();
    _cql_response.body.clear();
    _decoder.reset();
}

bool CassandraResponse::IsInitialized() const {
    return opcode() != CQL_OPCODE_LAST_DUMMY;
}

} // namespace brpc
