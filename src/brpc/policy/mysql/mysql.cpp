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

#define INTERNAL_SUPPRESS_PROTOBUF_FIELD_DEPRECATION
#include <algorithm>
#include <gflags/gflags.h>
#include "butil/string_printf.h"
#include "butil/macros.h"
#include "butil/logging.h"
#include "brpc/controller.h"
#include "brpc/policy/mysql/mysql.h"
#include "brpc/policy/mysql/mysql_common.h"

namespace brpc {

DEFINE_int32(mysql_multi_replies_size, 10, "multi replies size in one MysqlResponse");

butil::Status MysqlStatementStub::PackExecuteCommand(butil::IOBuf* outbuf, uint32_t stmt_id) {
    butil::Status st;
    // long data
    for (const auto& i : _long_data) {
        st = MysqlMakeLongDataPacket(outbuf, stmt_id, i.param_id, i.long_data);
        if (!st.ok()) {
            LOG(ERROR) << "make long data header error " << st;
            return st;
        }
    }
    _long_data.clear();
    // execute data
    st = MysqlMakeExecutePacket(outbuf, stmt_id, _execute_data);
    if (!st.ok()) {
        LOG(ERROR) << "make execute header error " << st;
        return st;
    }
    _execute_data.clear();
    _null_mask.mask.clear();
    _null_mask.area = butil::IOBuf::INVALID_AREA;
    _param_types.types.clear();
    _param_types.area = butil::IOBuf::INVALID_AREA;

    return st;
}

MysqlRequest::MysqlRequest()
    : NonreflectableMessage<MysqlRequest>() {
    SharedCtor();
}

MysqlRequest::MysqlRequest(const MysqlTransaction* tx)
    : NonreflectableMessage<MysqlRequest>() {
    SharedCtor();
    _tx = tx;
}

MysqlRequest::MysqlRequest(MysqlStatement* stmt)
    : NonreflectableMessage<MysqlRequest>() {
    SharedCtor();
    _stmt = new MysqlStatementStub(stmt);
}

MysqlRequest::MysqlRequest(const MysqlTransaction* tx, MysqlStatement* stmt)
    : NonreflectableMessage<MysqlRequest>() {
    SharedCtor();
    _tx = tx;
    _stmt = new MysqlStatementStub(stmt);
}

MysqlRequest::MysqlRequest(const MysqlRequest& from)
    : NonreflectableMessage<MysqlRequest>(from) {
    SharedCtor();
    MergeFrom(from);
}

void MysqlRequest::SharedCtor() {
    _has_error = false;
    _cached_size_ = 0;
    _has_command = false;
    _tx = NULL;
    _stmt = NULL;
    _param_index = 0;
}

MysqlRequest::~MysqlRequest() {
    SharedDtor();
    if (_stmt != NULL) {
        delete _stmt;
    }
    _stmt = NULL;
}

void MysqlRequest::SharedDtor() {
}

void MysqlRequest::SetCachedSize(int size) const {
    _cached_size_ = size;
}

void MysqlRequest::Clear() {
    _has_error = false;
    _buf.clear();
    _has_command = false;
    _tx = NULL;
    if (_stmt) {
        delete _stmt;
        _stmt = NULL;
    }
    _param_index = 0;
}

size_t MysqlRequest::ByteSizeLong() const {
    int total_size = _buf.size();
    _cached_size_ = total_size;
    return total_size;
}

void MysqlRequest::MergeFrom(const MysqlRequest& from) {
    if (&from == this) {
        return;
    }
    _has_command = from._has_command;
    _has_error = from._has_error;
    _buf = from._buf;
    _cached_size_ = from._cached_size_;
    _param_index = from._param_index;
    // _tx is a non-owning pointer (never deleted by MysqlRequest): shallow copy.
    _tx = from._tx;
    // _stmt is owned (deleted in the dtor): deep-copy to avoid double free.
    if (_stmt != NULL) {
        delete _stmt;
        _stmt = NULL;
    }
    if (from._stmt != NULL) {
        _stmt = new MysqlStatementStub(*from._stmt);
    }
}

void MysqlRequest::Swap(MysqlRequest* other) {
    if (other != this) {
        _buf.swap(other->_buf);
        std::swap(_has_error, other->_has_error);
        std::swap(_cached_size_, other->_cached_size_);
        std::swap(_has_command, other->_has_command);
        std::swap(_tx, other->_tx);
        std::swap(_stmt, other->_stmt);
        std::swap(_param_index, other->_param_index);
    }
}

bool MysqlRequest::SerializeTo(butil::IOBuf* buf) const {
    if (_has_error) {
        LOG(ERROR) << "Reject serialization due to error in CommandXXX[V]";
        return false;
    }
    *buf = _buf;
    return true;
}

bool MysqlRequest::Query(const butil::StringPiece& command) {
    if (_has_error) {
        return false;
    }

    if (_has_command) {
        LOG(WARNING) << "MysqlRequest::Query: a command was already set on this request";
        return false;
    }

    const butil::Status st = MysqlMakeCommand(&_buf, MYSQL_COM_QUERY, command);
    if (st.ok()) {
        _has_command = true;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}

bool MysqlRequest::AddParam(int8_t p) {
    if (_has_error) {
        return false;
    }
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(int8_t): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st = MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_TINY);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}
bool MysqlRequest::AddParam(uint8_t p) {
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(uint8_t): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st =
        MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_TINY, true);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}
bool MysqlRequest::AddParam(int16_t p) {
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(int16_t): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st = MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_SHORT);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}
bool MysqlRequest::AddParam(uint16_t p) {
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(uint16_t): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st =
        MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_SHORT, true);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}
bool MysqlRequest::AddParam(int32_t p) {
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(int32_t): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st = MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_LONG);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}
bool MysqlRequest::AddParam(uint32_t p) {
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(uint32_t): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st =
        MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_LONG, true);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}
bool MysqlRequest::AddParam(int64_t p) {
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(int64_t): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st =
        MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_LONGLONG);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}
bool MysqlRequest::AddParam(uint64_t p) {
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(uint64_t): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st =
        MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_LONGLONG, true);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}
bool MysqlRequest::AddParam(float p) {
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(float): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st = MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_FLOAT);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}
bool MysqlRequest::AddParam(double p) {
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(double): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st = MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_DOUBLE);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}
bool MysqlRequest::AddParam(const butil::StringPiece& p) {
    if (_stmt == NULL || _stmt->stmt() == NULL) {
        LOG(WARNING) << "MysqlRequest::AddParam(StringPiece): no prepared statement bound to request";
        _has_error = true;
        return false;
    }
    const butil::Status st = MysqlMakeExecuteData(_stmt, _param_index, &p, MYSQL_FIELD_TYPE_STRING);
    if (st.ok()) {
        ++_param_index;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}

void MysqlRequest::Print(std::ostream& os) const {
    butil::IOBuf cp = _buf;
    {
        uint8_t buf[3];
        cp.cutn(buf, 3);
        os << "size:" << mysql_uint3korr(buf) << ",";
    }
    {
        uint8_t buf;
        cp.cut1((char*)&buf);
        os << "sequence:" << (unsigned)buf << ",";
    }
    os << "payload(hex):";
    while (!cp.empty()) {
        uint8_t buf;
        cp.cut1((char*)&buf);
        os << std::hex << (unsigned)buf;
    }
}

std::ostream& operator<<(std::ostream& os, const MysqlRequest& r) {
    r.Print(os);
    return os;
}

MysqlResponse::MysqlResponse()
    : NonreflectableMessage<MysqlResponse>() {
    SharedCtor();
}

void MysqlResponse::SharedCtor() {
    _nreply = 0;
    _cached_size_ = 0;
}

MysqlResponse::~MysqlResponse() {
    SharedDtor();
}

void MysqlResponse::SharedDtor() {
}

void MysqlResponse::SetCachedSize(int size) const {
    _cached_size_ = size;
}

void MysqlResponse::Clear() {
    MysqlReply empty_reply;
    _first_reply.Swap(empty_reply);
    _other_replies.clear();
    _arena.clear();
    _nreply = 0;
    _cached_size_ = 0;
}

size_t MysqlResponse::ByteSizeLong() const {
    return _cached_size_;
}

void MysqlResponse::MergeFrom(const MysqlResponse&) {
    CHECK(false) << "MysqlResponse does not support MergeFrom/CopyFrom";
}

bool MysqlResponse::IsInitialized() const {
    return true;
}

void MysqlResponse::Swap(MysqlResponse* other) {
    if (other != this) {
        _first_reply.Swap(other->_first_reply);
        std::swap(_other_replies, other->_other_replies);
        _arena.swap(other->_arena);
        std::swap(_nreply, other->_nreply);
        std::swap(_cached_size_, other->_cached_size_);
    }
}

ParseError MysqlResponse::ConsumePartialIOBuf(butil::IOBuf& buf,
                                              bool is_auth,
                                              MysqlStmtType stmt_type) {
    bool more_results = true;
    size_t oldsize = 0;
    while (more_results) {
        oldsize = buf.size();
        if (reply_size() == 0) {
            ParseError err =
                _first_reply.ConsumePartialIOBuf(buf, &_arena, is_auth, stmt_type, &more_results);
            if (err != PARSE_OK) {
                return err;
            }
        } else {
            const int32_t replies_size =
                FLAGS_mysql_multi_replies_size > 1 ? FLAGS_mysql_multi_replies_size : 10;
            if (_other_replies.size() < reply_size()) {
                MysqlReply* replies =
                    (MysqlReply*)_arena.allocate(sizeof(MysqlReply) * (replies_size - 1));
                if (replies == NULL) {
                    LOG(ERROR) << "Fail to allocate MysqlReply[" << replies_size - 1 << "]";
                    return PARSE_ERROR_ABSOLUTELY_WRONG;
                }
                _other_replies.reserve(replies_size - 1);
                for (int i = 0; i < replies_size - 1; ++i) {
                    new (&replies[i]) MysqlReply;
                    _other_replies.push_back(&replies[i]);
                }
            }
            ParseError err = _other_replies[_nreply - 1]->ConsumePartialIOBuf(
                buf, &_arena, is_auth, stmt_type, &more_results);
            if (err != PARSE_OK) {
                return err;
            }
        }

        const size_t newsize = buf.size();
        _cached_size_ += oldsize - newsize;
        oldsize = newsize;
        ++_nreply;
    }

    if (oldsize == 0) {
        return PARSE_OK;
    } else {
        LOG(ERROR) << "Parse protocol finished, but IOBuf has more data";
        return PARSE_ERROR_ABSOLUTELY_WRONG;
    }
}

std::ostream& operator<<(std::ostream& os, const MysqlResponse& response) {
    os << "\n-----MYSQL REPLY BEGIN-----\n";
    if (response.reply_size() == 0) {
        os << "<empty response>";
    } else if (response.reply_size() == 1) {
        os << response.reply(0);
    } else {
        for (size_t i = 0; i < response.reply_size(); ++i) {
            os << "\nreply(" << i << ")----------";
            os << response.reply(i);
        }
    }
    os << "\n-----MYSQL REPLY END-----\n";

    return os;
}

}  // namespace brpc
