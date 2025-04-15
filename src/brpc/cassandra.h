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

#ifndef BRPC_CASSANDRA_H
#define BRPC_CASSANDRA_H

#include <cmath>              // std::abs()
#include <string>

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_message_reflection.h>
#include "google/protobuf/descriptor.pb.h"

#include "brpc/cql_messages.h"
#include "butil/iobuf.h"
#include "butil/logging.h"
#include "butil/strings/string_piece.h"
#include "butil/sys_byteorder.h"

namespace brpc {

class Controller;
class InputMessageBase;

namespace policy {
class CqlParsingContext;
void SerializeCqlRequest(butil::IOBuf* buf, brpc::Controller* cntl,
                         const google::protobuf::Message* request);
void ProcessCqlResponse(InputMessageBase* msg_base);
} // namespace policy

class CassandraRequest : public ::google::protobuf::Message {
public:
    CassandraRequest();
    CassandraRequest(CqlConsistencyLevel consistency);
	
    ~CassandraRequest();   

    template<typename TKey>
    void Get(const butil::StringPiece key_name, const TKey& key,
             const butil::StringPiece column_name, const butil::StringPiece table) {
        _opcode = CQL_OPCODE_QUERY;
        uint32_t query_len = 0;
        const butil::IOBuf::Area area = _query.reserve(sizeof(query_len));
        if (area == butil::IOBuf::INVALID_AREA) {
            LOG(FATAL) << "Fail to reserve IOBuf::Area for cql query length.";
            return;
        }
        const size_t begin_size = _query.size();
			  // SELECT column_name FROM table WHERE key_name=?	
        _query.append("SELECT ", sizeof("SELECT ") - 1);
        _query.append(column_name.data(), column_name.size());
        _query.append(" FROM ", sizeof(" FROM ") - 1);
        _query.append(table.data(), table.size());
        _query.append(" WHERE ", sizeof(" WHERE ") - 1);
        _query.append(key_name.data(), key_name.size());
        _query.append("=?", sizeof("=?") - 1);

        query_len = butil::HostToNet32(_query.size() - begin_size);
        CHECK(0 == _query.unsafe_assign(area, &query_len));

        _query_parameter.add_flag(CqlQueryParameter::WITH_VALUES);
        _query_parameter.AppendValue(key);
    }
 
    template<typename TKey>
    void Get(const butil::StringPiece key_name, const TKey& key,
             const butil::StringPiece table) {
        Get(key_name, key, '*', table);
    }

    template<typename TKey, typename TValue>
    void Insert(const butil::StringPiece key_name, const TKey& key, 
                const butil::StringPiece column_name, const TValue& column_value,
                const butil::StringPiece table, int32_t ttl = -1) {
        HandleBatchQueryAtBeginningIfNeeded();
        uint32_t query_len = 0;
        const butil::IOBuf::Area area = _query.reserve(sizeof(query_len));
        if (area == butil::IOBuf::INVALID_AREA) {
            LOG(FATAL) << "Fail to reserve IOBuf::Area for cql query length.";
            return;
        }
        const size_t begin_size = _query.size();

        // INSERT INTO table (key_name, column_name) VALUES (?, ?) USING TTL ttl
        _query.append("INSERT INTO ", sizeof("INSERT INTO ") - 1);
        _query.append(table.data(), table.size());
        _query.append(" (", sizeof(" (") - 1);
        _query.append(key_name.data(), key_name.size());
        _query.append(", ", sizeof(", ") - 1);
        _query.append(column_name.data(), column_name.size());
        _query.append(") VALUES (?, ?)", sizeof(") VALUES (?, ?)") - 1);
        if (ttl > 0) {
            _query.append("USING TTL ", sizeof("USING TTL ") - 1);
            _query.append(std::to_string(ttl));
        }
        query_len = butil::HostToNet32(_query.size() - begin_size);
        CHECK(0 == _query.unsafe_assign(area, &query_len));

        if (!is_batch_mode()) {
            _query_parameter.add_flag(CqlQueryParameter::WITH_VALUES);
            _query_parameter.AppendValue(key);
            _query_parameter.AppendValue(column_value);
        } else {
            CqlEncodeUint16(2, &_query);
            CqlEncodeBytes(key, &_query);
            CqlEncodeBytes(column_value, &_query);
        }
    }

    // Increase and decrease a counter.
    template<typename TKey>
    void Add(const butil::StringPiece key_name, const TKey& key,
             const butil::StringPiece counter_name, int count,
             const butil::StringPiece table) {
        HandleBatchQueryAtBeginningIfNeeded();
        uint32_t query_len = 0;
        const butil::IOBuf::Area area = _query.reserve(sizeof(query_len));
        if (area == butil::IOBuf::INVALID_AREA) {
            LOG(FATAL) << "Fail to reserve IOBuf::Area for cql query length.";
            return;
        }
        const size_t begin_size = _query.size();
		
        // UPDATE table set counter_name = counter_name + count WHERE key_name = ?.
        _query.append("UPDATE ", sizeof("UPDATE ") - 1);
        _query.append(table.data(), table.size());
        _query.append(" set ", sizeof(" set ") - 1);
        _query.append(counter_name.data(), counter_name.size());
        _query.push_back('=');
        _query.append(counter_name.data(), counter_name.size());
        _query.push_back(count > 0 ? '+' : '-');
        _query.append(std::to_string(std::abs(count)));
        _query.append(" WHERE ", sizeof(" WHERE ") - 1);
        _query.append(key_name.data(), key_name.size());
        _query.append("=?", sizeof("=?") - 1);

        query_len = butil::HostToNet32(_query.size() - begin_size);
        CHECK(0 == _query.unsafe_assign(area, &query_len));
				
        if (!is_batch_mode()) {
            _query_parameter.add_flag(CqlQueryParameter::WITH_VALUES);
            _query_parameter.AppendValue(key);
        } else {
            CqlEncodeUint16(1, &_query);
            CqlEncodeBytes(key, &_query);
            set_batch_type(CQL_BATCH_COUNTER);
        }
    }

    template<typename TKey>
    void Delete(const butil::StringPiece key_name, const TKey& key,
                const butil::StringPiece column_name, const butil::StringPiece table) {
        HandleBatchQueryAtBeginningIfNeeded();
        uint32_t query_len = 0;
        const butil::IOBuf::Area area = _query.reserve(sizeof(query_len));
        if (area == butil::IOBuf::INVALID_AREA) {
            LOG(FATAL) << "Fail to reserve IOBuf::Area for cql query length.";
            return;
        }
        const size_t begin_size = _query.size();

        // Delete column_name FROM table where key_name=?.
        _query.append("Delete ", sizeof("Delete ") - 1);
        _query.append(column_name.data(), column_name.size());
        _query.append(" FROM ", sizeof(" FROM ") - 1);
        _query.append(table.data(), table.size());
        _query.append(" WHERE ", sizeof(" WHERE ") - 1);
        _query.append(key_name.data(), key_name.size());
        _query.append("=?", sizeof("=?") - 1);

        query_len = butil::HostToNet32(_query.size() - begin_size);
        CHECK(0 == _query.unsafe_assign(area, &query_len));

        if (!is_batch_mode()) {
            _query_parameter.add_flag(CqlQueryParameter::WITH_VALUES);
            _query_parameter.AppendValue(key);
        } else {
            CqlEncodeUint16(1, &_query);
            CqlEncodeBytes(key, &_query);
        }
    }

    template<typename TKey>
    void Delete(const butil::StringPiece key_name, const TKey& key,
                const butil::StringPiece table) {
        Delete(key_name, key, "", table);
    }

    void ExecuteCql3Query(const butil::StringPiece query) {
        HandleBatchQueryAtBeginningIfNeeded();
        CqlEncodeUint32(query.size(), &_query);
        _query.append(query.data(), query.size());
        // Batch query is always need a value number even if no values followed.
        if (is_batch_mode()) {
            CHECK(_batch_parameter.curr_query_values == 0)
                << "Invalid cql batched query values number. "
                   "Most likely reason: you did not call methods in the right way.";
            _batch_parameter.curr_query_values_area = _query.reserve(
                sizeof(_batch_parameter.curr_query_values));  
            CHECK(0 == _query.unsafe_assign(_batch_parameter.curr_query_values_area,
                                            &_batch_parameter.curr_query_values));
        }
    }

    template<typename TValue>
    void AppendQueryParameterValue(const TValue& value) {
        if (!is_batch_mode()) {
            _query_parameter.add_flag(CqlQueryParameter::WITH_VALUES);
            _query_parameter.AppendValue(value);
            return;
        }
        CqlEncodeBytes(value, &_query);
        ++_batch_parameter.curr_query_values;
    }

    void set_consistency(CqlConsistencyLevel consistency) {
        _query_parameter._consistency = consistency;
    }

    void set_serial_consistency(CqlConsistencyLevel consistency) {
        _query_parameter.add_flag(CqlQueryParameter::SERIAL_CONSISTENCY);
        _query_parameter.set_serial_consistency(consistency);
    }

    void set_timestamp(int64_t timestamp) {
        _query_parameter.add_flag(CqlQueryParameter::DEFAULT_TIMESTAMP);
        _query_parameter.set_timestamp(timestamp);
    }

    void set_batch_type(CqlBatchType type) {
        _batch_parameter.type = type;
    }

    CassOpCode opcode() const { return _opcode; } 

    void Print(std::ostream& os) const;

    // Protobuf methods.
    CassandraRequest* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const CassandraRequest& from);
    void MergeFrom(const CassandraRequest& from);
    void Clear();
    bool IsInitialized() const;

    int ByteSize() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
        ::google::protobuf::uint8* target) const;
    int GetCachedSize() const { return _cached_size_; }

    static const ::google::protobuf::Descriptor* descriptor();
    static const CassandraRequest& default_instance();
    ::google::protobuf::Metadata GetMetadata() const;

private:
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;

friend void protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto_impl();
friend void protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fcassandra_5fbase_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fcassandra_5fbase_2eproto();
  
    void InitAsDefaultInstance();
    static CassandraRequest* default_instance_;

private:
friend void policy::SerializeCqlRequest(
    butil::IOBuf* buf, brpc::Controller* cntl,
    const google::protobuf::Message* request);

    struct BatchParameter {
        // Batch query kind. '0' is normal query and '1' is prepare query.
        // Now we only support normal query for batch request.
        uint8_t kind = 0;
        // The type of batch to use.
        uint8_t type = CQL_BATCH_LOGGED;		
        // Number of batched queries. If count > 1, it is batch query.		
        uint16_t count = 0;
        uint16_t curr_query_values = 0;
        butil::IOBuf::Area curr_query_values_area = butil::IOBuf::INVALID_AREA;

        void clear() {
            count = curr_query_values = kind = 0;
            type = CQL_BATCH_LOGGED;
            curr_query_values_area = butil::IOBuf::INVALID_AREA;
        }
    };

    CqlConsistencyLevel consistency() const {
        return _query_parameter._consistency;
    }

    const CqlQueryParameter& query_parameter() const {
        return _query_parameter;
    }

    bool is_batch_mode() const {
        return _batch_parameter.count > 1;
    }

    bool IsValidOpcode() const {
        return _opcode == CQL_OPCODE_QUERY 
            || _opcode == CQL_OPCODE_PREPARE
            || _opcode == CQL_OPCODE_BATCH;
    }

    uint8_t flags() const { return _query_parameter._flags; }

    const butil::IOBuf& query() const { return _query; }

    void EncodeBatchQueries(butil::IOBuf* buf);

    void Encode(butil::IOBuf* buf);

    void HandleBatchQueryAtBeginningIfNeeded();

    void EncodeValuesNumberOfLastBatchedQuery();

    CassOpCode _opcode;
    mutable int _cached_size_;
    BatchParameter _batch_parameter;
    butil::IOBuf _query;
    CqlQueryParameter _query_parameter;
};

class CassandraResponse : public ::google::protobuf::Message {
public:
    // The response error code. If response is CQL ERROR. The error code
    // is the ERROR result.
    enum ErrorCode {
        CASS_UNDECODED = 0xF000,
        CASS_DECODED_FAULT = 0xF001,
        CASS_SUCCESS = 0xFFFF
    };

    CassandraResponse();
    virtual ~CassandraResponse();

    // If response is replied by server and not a ERROR.
    bool Failed() const {
        return error_code() != CASS_SUCCESS;
    }

    int error_code() const {
        return _error_code;
    }

    // Get value of the column. 
    // row_index is the index of rows in query content.
    // Return 0 if success, 1 if the column is null and -1 if failure.
    template<typename TValue>    
    int GetColumnValueOfRow(const butil::StringPiece column_name,
                            size_t row_index,
                            TValue* column_value) {
        CqlRowsResultDecoder* rows_result = GetRowsResult();
        if (rows_result == nullptr) {
            _error = "Null cql rows result.";
            return -1;
        }
        if (rows_result->rows_count() == 0 || rows_result->columns_count() == 0) {
            _error = "No rows in cql rows result.";
            return -1;
        }
        const int ret = rows_result->DecodeColumnValueOfRow(
            column_name, row_index, column_value);
        if (ret == -1) {
            rows_result->MoveErrorTo(&_error);
        }
        return ret;
    }

    // Get value of column of the first row.
    template<typename TValue>
    int GetColumnValue(const butil::StringPiece column_name,
                       TValue* column_value) {  
        return GetColumnValueOfRow(column_name, 0, column_value);
    }

    // Return the number of row in the query rows result.
    // Return -1 if the response is the default dummy value.
    int GetRowsCount();

    // Get column specs of cql rows result.
    // Return valid pointer if success. Otherwise, return nullptr.
    const std::vector<CqlColumnSpec>* GetColumnSpecs();

    const std::string& LastError() const {
        return _error;
    }

    const CqlFrameHead& head() const {
        return _cql_response.head;
    }

    const butil::IOBuf& body() const {
        return _cql_response.body;
    }

    void Print(std::ostream& os) const;

    void MoveFrom(CqlResponse& response) {
        _cql_response.head = response.head;
        _cql_response.body.swap(response.body);
    }

    void Swap(CassandraResponse* response) {
        if (response != this) {
            CqlFrameHead head = _cql_response.head;
            _cql_response.head = response->_cql_response.head;
            response->_cql_response.head = head;
            _cql_response.body.swap(response->_cql_response.body);
        }
    }

    // Protobuf methods.
    CassandraResponse* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const CassandraResponse& from);
    void MergeFrom(const CassandraResponse& from);
    void Clear();
    bool IsInitialized() const;

    int ByteSize() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
        ::google::protobuf::uint8* target) const;
    int GetCachedSize() const { return _cached_size_; }

    static const ::google::protobuf::Descriptor* descriptor();
    static const CassandraResponse& default_instance();
    ::google::protobuf::Metadata GetMetadata() const;

private:
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;

friend void protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto_impl();
friend void protobuf_AddDesc_baidu_2frpc_2fcassandra_5fbase_2eproto();
friend void protobuf_AssignDesc_baidu_2frpc_2fcassandra_5fbase_2eproto();
friend void protobuf_ShutdownFile_baidu_2frpc_2fcassandra_5fbase_2eproto();
  
    void InitAsDefaultInstance();
    static CassandraResponse* default_instance_;

private:
friend policy::CqlParsingContext;
friend void policy::ProcessCqlResponse(InputMessageBase* msg_base);

    CassOpCode opcode() const {
        return (CassOpCode)_cql_response.head.opcode;
    }

    const CqlResponse& cql_response() const {
        return _cql_response;
    }

    CqlRowsResultDecoder* GetRowsResult();

    bool Decode();

    mutable int _cached_size_;

    int _error_code = CASS_UNDECODED;
    std::string _error;

    CqlResponse _cql_response;
    std::unique_ptr<CqlResponseDecoder> _decoder;
};

std::ostream& operator<<(std::ostream& os, const CassandraRequest& r);
std::ostream& operator<<(std::ostream& os, const CassandraResponse& r);

} // namespace brpc

#endif  // BRPC_CASSANDRA_H
