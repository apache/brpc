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

#ifndef BRPC_MYSQL_H
#define BRPC_MYSQL_H

#include <string>
#include <vector>
#include <google/protobuf/stubs/common.h>

#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_message_reflection.h>
#include "google/protobuf/descriptor.pb.h"

#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"
#include "butil/arena.h"
#include "parse_result.h"
#include "mysql_command.h"
#include "mysql_reply.h"
#include "mysql_transaction.h"
#include "mysql_statement.h"

namespace brpc {
// Request to mysql.
// Notice that you can pipeline multiple commands in one request and sent
// them to ONE mysql-server together.
// Example:
//   MysqlRequest request;
//   request.Query("select * from table");
//   MysqlResponse response;
//   channel.CallMethod(NULL, &controller, &request, &response, NULL/*done*/);
//   if (!cntl.Failed()) {
//       LOG(INFO) << response.reply(0);
//   }

class MysqlStatementStub {
public:
    MysqlStatementStub(MysqlStatement* stmt);
    MysqlStatement* stmt();
    butil::IOBuf& execute_data();
    butil::Status PackExecuteCommand(butil::IOBuf* outbuf, uint32_t stmt_id);
    // prepare statement null mask
    struct NullMask {
        NullMask() : area(butil::IOBuf::INVALID_AREA) {}
        std::vector<uint8_t> mask;
        butil::IOBuf::Area area;
    };
    // prepare statement param types
    struct ParamTypes {
        ParamTypes() : area(butil::IOBuf::INVALID_AREA) {}
        std::vector<uint8_t> types;
        butil::IOBuf::Area area;
    };
    // null mask and param types
    NullMask& null_mask();
    ParamTypes& param_types();
    // save long data
    void save_long_data(uint16_t param_id, const butil::StringPiece& value);

private:
    MysqlStatement* _stmt;
    butil::IOBuf _execute_data;
    NullMask _null_mask;
    ParamTypes _param_types;
    // long data
    struct LongData {
        uint16_t param_id;
        butil::IOBuf long_data;
    };
    std::vector<LongData> _long_data;
};

inline MysqlStatementStub::MysqlStatementStub(MysqlStatement* stmt) : _stmt(stmt) {}

inline MysqlStatement* MysqlStatementStub::stmt() {
    return _stmt;
}

inline butil::IOBuf& MysqlStatementStub::execute_data() {
    return _execute_data;
}

inline MysqlStatementStub::NullMask& MysqlStatementStub::null_mask() {
    return _null_mask;
}

inline MysqlStatementStub::ParamTypes& MysqlStatementStub::param_types() {
    return _param_types;
}

inline void MysqlStatementStub::save_long_data(uint16_t param_id, const butil::StringPiece& value) {
    LongData d;
    d.param_id = param_id;
    d.long_data.append(value.data(), value.size());
    _long_data.push_back(d);
}

class MysqlRequest : public ::google::protobuf::Message {
public:
    MysqlRequest();
    MysqlRequest(const MysqlTransaction* tx);
    MysqlRequest(MysqlStatement* stmt);
    MysqlRequest(const MysqlTransaction* tx, MysqlStatement* stmt);
    virtual ~MysqlRequest();
    MysqlRequest(const MysqlRequest& from);
    inline MysqlRequest& operator=(const MysqlRequest& from) {
        CopyFrom(from);
        return *this;
    }
    void Swap(MysqlRequest* other);

    // Serialize the request into `buf'. Return true on success.
    bool SerializeTo(butil::IOBuf* buf) const;

    // Protobuf methods.
    MysqlRequest* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const MysqlRequest& from);
    void MergeFrom(const MysqlRequest& from);
    void Clear();

    int ByteSize() const;
    bool MergePartialFromCodedStream(::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
        ::google::protobuf::uint8* output) const;
    int GetCachedSize() const {
        return _cached_size_;
    }

    static const ::google::protobuf::Descriptor* descriptor();
    static const MysqlRequest& default_instance();
    ::google::protobuf::Metadata GetMetadata() const;

    // call query command
    bool Query(const butil::StringPiece& command);
    // add statement params
    bool AddParam(int8_t p);
    bool AddParam(uint8_t p);
    bool AddParam(int16_t p);
    bool AddParam(uint16_t p);
    bool AddParam(int32_t p);
    bool AddParam(uint32_t p);
    bool AddParam(int64_t p);
    bool AddParam(uint64_t p);
    bool AddParam(float p);
    bool AddParam(double p);
    bool AddParam(const butil::StringPiece& p);

    // True if previous command failed.
    bool has_error() const {
        return _has_error;
    }

    const MysqlTransaction* get_tx() const {
        return _tx;
    }

    MysqlStatementStub* get_stmt() const {
        return _stmt;
    }

    void Print(std::ostream&) const;

private:
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;

    bool _has_command;            // request has command
    bool _has_error;              // previous AddCommand had error
    butil::IOBuf _buf;            // the serialized request.
    mutable int _cached_size_;    // ByteSize
    const MysqlTransaction* _tx;  // transaction
    MysqlStatementStub* _stmt;    // statement
    uint16_t _param_index;        // statement param index

    friend void protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto_impl();
    friend void protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto();
    friend void protobuf_AssignDesc_baidu_2frpc_2fmysql_5fbase_2eproto();
    friend void protobuf_ShutdownFile_baidu_2frpc_2fmysql_5fbase_2eproto();

    void InitAsDefaultInstance();
    static MysqlRequest* default_instance_;
};

// Response from Mysql.
// Notice that a MysqlResponse instance may contain multiple replies
// due to pipelining.
class MysqlResponse : public ::google::protobuf::Message {
public:
    MysqlResponse();
    virtual ~MysqlResponse();
    MysqlResponse(const MysqlResponse& from);
    inline MysqlResponse& operator=(const MysqlResponse& from) {
        CopyFrom(from);
        return *this;
    }
    void Swap(MysqlResponse* other);
    // Parse and consume intact replies from the buf, actual reply size may less then max_count, if
    // some command execute failed
    // Returns PARSE_OK on success.
    // Returns PARSE_ERROR_NOT_ENOUGH_DATA if data in `buf' is not enough to parse.
    // Returns PARSE_ERROR_ABSOLUTELY_WRONG if the parsing
    // failed.
    ParseError ConsumePartialIOBuf(butil::IOBuf& buf, bool is_auth, MysqlStmtType stmt_type);

    // Number of replies in this response.
    // (May have more than one reply due to pipeline)
    size_t reply_size() const {
        return _nreply;
    }

    const MysqlReply& reply(size_t index) const {
        if (index < reply_size()) {
            return (index == 0 ? _first_reply : *_other_replies[index - 1]);
        }
        static MysqlReply mysql_nil;
        return mysql_nil;
    }
    // implements Message ----------------------------------------------

    MysqlResponse* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const MysqlResponse& from);
    void MergeFrom(const MysqlResponse& from);
    void Clear();
    bool IsInitialized() const;

    int ByteSize() const;
    bool MergePartialFromCodedStream(::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
        ::google::protobuf::uint8* output) const;
    int GetCachedSize() const {
        return 0;
    }

    static const ::google::protobuf::Descriptor* descriptor();
    static const MysqlResponse& default_instance();
    ::google::protobuf::Metadata GetMetadata() const;

private:
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;

    MysqlReply _first_reply;
    std::vector<MysqlReply*> _other_replies;
    butil::Arena _arena;
    size_t _nreply;
    mutable int _cached_size_;

    friend void protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto_impl();
    friend void protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto();
    friend void protobuf_AssignDesc_baidu_2frpc_2fmysql_5fbase_2eproto();
    friend void protobuf_ShutdownFile_baidu_2frpc_2fmysql_5fbase_2eproto();

    void InitAsDefaultInstance();
    static MysqlResponse* default_instance_;
};

std::ostream& operator<<(std::ostream& os, const MysqlRequest&);
std::ostream& operator<<(std::ostream& os, const MysqlResponse&);

}  // namespace brpc

#endif  // BRPC_MYSQL_H
