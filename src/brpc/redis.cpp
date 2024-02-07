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


#include <google/protobuf/reflection_ops.h>     // ReflectionOps::Merge
#include <gflags/gflags.h>
#include "butil/status.h"
#include "butil/strings/string_util.h"          // StringToLowerASCII
#include "brpc/redis.h"
#include "brpc/redis_command.h"

namespace brpc {

DEFINE_bool(redis_verbose_crlf2space, false, "[DEBUG] Show \\r\\n as a space");

RedisRequest::RedisRequest()
    : ::google::protobuf::Message() {
    SharedCtor();
}

RedisRequest::RedisRequest(const RedisRequest& from)
    : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void RedisRequest::SharedCtor() {
    _ncommand = 0;
    _has_error = false;
    _cached_size_ = 0;
}

RedisRequest::~RedisRequest() {
    SharedDtor();
}

void RedisRequest::SharedDtor() {
}

void RedisRequest::SetCachedSize(int size) const {
    _cached_size_ = size;
}

RedisRequest* RedisRequest::New() const {
    return new RedisRequest;
}

#if GOOGLE_PROTOBUF_VERSION >= 3006000
RedisRequest* RedisRequest::New(::google::protobuf::Arena* arena) const {
    return CreateMaybeMessage<RedisRequest>(arena);
}
#endif

void RedisRequest::Clear() {
    _ncommand = 0;
    _has_error = false;
    _buf.clear();
}

bool RedisRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream*) {
    LOG(WARNING) << "You're not supposed to parse a RedisRequest";
    return true;
}

void RedisRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream*) const {
    LOG(WARNING) << "You're not supposed to serialize a RedisRequest";
}

::google::protobuf::uint8* RedisRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

int RedisRequest::ByteSize() const {
    int total_size =  _buf.size();
    _cached_size_ = total_size;
    return total_size;
}

void RedisRequest::MergeFrom(const ::google::protobuf::Message& from) {
    CHECK_NE(&from, this);
    const RedisRequest* source = dynamic_cast<const RedisRequest*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void RedisRequest::MergeFrom(const RedisRequest& from) {
    CHECK_NE(&from, this);
    _has_error = _has_error || from._has_error;
    _buf.append(from._buf);
    _ncommand += from._ncommand;
}

void RedisRequest::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

void RedisRequest::CopyFrom(const RedisRequest& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

bool RedisRequest::IsInitialized() const {
    return _ncommand != 0;
}

void RedisRequest::Swap(RedisRequest* other) {
    if (other != this) {
        _buf.swap(other->_buf);
        std::swap(_ncommand, other->_ncommand);
        std::swap(_has_error, other->_has_error);
        std::swap(_cached_size_, other->_cached_size_);
    }
}

bool RedisRequest::AddCommand(const butil::StringPiece& command) {
    if (_has_error) {
        return false;
    }
    const butil::Status st = RedisCommandNoFormat(&_buf, command);
    if (st.ok()) {
        ++_ncommand;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }    
}

bool RedisRequest::AddCommandByComponents(const butil::StringPiece* components, 
                                         size_t n) {
    if (_has_error) {
        return false;
    }
    const butil::Status st = RedisCommandByComponents(&_buf, components, n);
    if (st.ok()) {
        ++_ncommand;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }        
}

bool RedisRequest::AddCommandWithArgs(const char* fmt, ...) {
    if (_has_error) {
        return false;
    }
    va_list ap;
    va_start(ap, fmt);
    const butil::Status st = RedisCommandFormatV(&_buf, fmt, ap);
    va_end(ap);
    if (st.ok()) {
        ++_ncommand;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}

bool RedisRequest::AddCommandV(const char* fmt, va_list ap) {
    if (_has_error) {
        return false;
    }
    const butil::Status st = RedisCommandFormatV(&_buf, fmt, ap);
    if (st.ok()) {
        ++_ncommand;
        return true;
    } else {
        CHECK(st.ok()) << st;
        _has_error = true;
        return false;
    }
}

bool RedisRequest::SerializeTo(butil::IOBuf* buf) const {
    if (_has_error) {
        LOG(ERROR) << "Reject serialization due to error in AddCommand[V]";
        return false;
    }
    *buf = _buf;
    return true;
}

const ::google::protobuf::Descriptor* RedisRequest::descriptor() {
    return RedisRequestBase::descriptor();
}

::google::protobuf::Metadata RedisRequest::GetMetadata() const {
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = RedisRequest::descriptor();
    metadata.reflection = NULL;
    return metadata;
}

void RedisRequest::Print(std::ostream& os) const {
    butil::IOBuf cp = _buf;
    butil::IOBuf seg;
    while (cp.cut_until(&seg, "\r\n") == 0) {
        os << seg;
        if (FLAGS_redis_verbose_crlf2space) {
            os << ' ';
        } else {
            os << "\\r\\n";
        }
        seg.clear();
    }
    if (!cp.empty()) {
        os << cp;
    }
    if (_has_error) {
        os << "[ERROR]";
    }
}

std::ostream& operator<<(std::ostream& os, const RedisRequest& r) {
    r.Print(os);
    return os;
}

RedisResponse::RedisResponse()
    : ::google::protobuf::Message()
    , _first_reply(&_arena) {
    SharedCtor();
}
RedisResponse::RedisResponse(const RedisResponse& from)
    : ::google::protobuf::Message()
    , _first_reply(&_arena) {
    SharedCtor();
    MergeFrom(from);
}

void RedisResponse::SharedCtor() {
    _other_replies = NULL;
    _cached_size_ = 0;
    _nreply = 0;
}

RedisResponse::~RedisResponse() {
    SharedDtor();
}

void RedisResponse::SharedDtor() {
}

void RedisResponse::SetCachedSize(int size) const {
    _cached_size_ = size;
}

RedisResponse* RedisResponse::New() const {
    return new RedisResponse;
}

#if GOOGLE_PROTOBUF_VERSION >= 3006000
RedisResponse* RedisResponse::New(::google::protobuf::Arena* arena) const {
    return CreateMaybeMessage<RedisResponse>(arena);
}
#endif

void RedisResponse::Clear() {
    _first_reply.Reset();
    _other_replies = NULL;
    _arena.clear();
    _nreply = 0;
    _cached_size_ = 0;
}

bool RedisResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream*) {
    LOG(WARNING) << "You're not supposed to parse a RedisResponse";
    return true;
}

void RedisResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream*) const {
    LOG(WARNING) << "You're not supposed to serialize a RedisResponse";
}

::google::protobuf::uint8* RedisResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

int RedisResponse::ByteSize() const {
    return _cached_size_;
}

void RedisResponse::MergeFrom(const ::google::protobuf::Message& from) {
    CHECK_NE(&from, this);
    const RedisResponse* source = dynamic_cast<const RedisResponse*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void RedisResponse::MergeFrom(const RedisResponse& from) {
    CHECK_NE(&from, this);
    if (from._nreply == 0) {
        return;
    }
    _cached_size_ += from._cached_size_;
    if (_nreply == 0) {
        _first_reply.CopyFromDifferentArena(from._first_reply);
    }
    const int new_nreply = _nreply + from._nreply;
    if (new_nreply == 1) {
        _nreply = new_nreply;
        return;
    }
    RedisReply* new_others =
        (RedisReply*)_arena.allocate(sizeof(RedisReply) * (new_nreply - 1));
    for (int i = 0; i < new_nreply - 1; ++i) {
        new (new_others + i) RedisReply(&_arena);
    }
    int new_other_index = 0;
    for (int i = 1; i < _nreply; ++i) {
        new_others[new_other_index++].CopyFromSameArena(
            _other_replies[i - 1]);
    }
    for (int i = !_nreply; i < from._nreply; ++i) {
        new_others[new_other_index++].CopyFromDifferentArena(from.reply(i));
    }
    DCHECK_EQ(new_nreply - 1, new_other_index);
    _other_replies = new_others;
    _nreply = new_nreply;
}

void RedisResponse::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

void RedisResponse::CopyFrom(const RedisResponse& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

bool RedisResponse::IsInitialized() const {
    return reply_size() > 0;
}

void RedisResponse::Swap(RedisResponse* other) {
    if (other != this) {
        _first_reply.Swap(other->_first_reply);
        std::swap(_other_replies, other->_other_replies);
        _arena.swap(other->_arena);
        std::swap(_nreply, other->_nreply);
        std::swap(_cached_size_, other->_cached_size_);
    }
}

const ::google::protobuf::Descriptor* RedisResponse::descriptor() {
    return RedisResponseBase::descriptor();
}

::google::protobuf::Metadata RedisResponse::GetMetadata() const {
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = RedisResponse::descriptor();
    metadata.reflection = NULL;
    return metadata;
}

// ===================================================================

ParseError RedisResponse::ConsumePartialIOBuf(butil::IOBuf& buf, int reply_count) {
    size_t oldsize = buf.size();
    if (reply_size() == 0) {
        ParseError err = _first_reply.ConsumePartialIOBuf(buf);
        if (err != PARSE_OK) {
            return err;
        }
        const size_t newsize = buf.size();
        _cached_size_ += oldsize - newsize;
        oldsize = newsize;
        ++_nreply;
    }
    if (reply_count > 1) {
        if (_other_replies == NULL) {
            _other_replies = (RedisReply*)_arena.allocate(
                sizeof(RedisReply) * (reply_count - 1));
            if (_other_replies == NULL) {
                LOG(ERROR) << "Fail to allocate RedisReply[" << reply_count -1 << "]";
                return PARSE_ERROR_ABSOLUTELY_WRONG;
            }
            for (int i = 0; i < reply_count - 1; ++i) {
                new (&_other_replies[i]) RedisReply(&_arena);
            }
        }
        for (int i = reply_size(); i < reply_count; ++i) {
            ParseError err = _other_replies[i - 1].ConsumePartialIOBuf(buf);
            if (err != PARSE_OK) {
                return err;
            }
            const size_t newsize = buf.size();
            _cached_size_ += oldsize - newsize;
            oldsize = newsize;
            ++_nreply;
        }
    }
    return PARSE_OK;
}

std::ostream& operator<<(std::ostream& os, const RedisResponse& response) {
    if (response.reply_size() == 0) {
        return os << "<empty response>";
    } else if (response.reply_size() == 1) {
        return os << response.reply(0);
    } else {
        os << '[';
        for (int i = 0; i < response.reply_size(); ++i) {
            if (i) {
                os << ", ";
            }
            os << response.reply(i);
        }
        os << ']';
    }
    return os;
}

bool RedisService::AddCommandHandler(const std::string& name, RedisCommandHandler* handler) {
    std::string lcname = StringToLowerASCII(name);
    auto it = _command_map.find(lcname);
    if (it != _command_map.end()) {
        LOG(ERROR) << "redis command name=" << name << " exist";
        return false;
    }
    _command_map[lcname] = handler;
    return true;
}
 
RedisCommandHandler* RedisService::FindCommandHandler(const butil::StringPiece& name) const {
    auto it = _command_map.find(name.as_string());
    if (it != _command_map.end()) {
        return it->second;
    }
    return NULL;
}

RedisCommandHandler* RedisCommandHandler::NewTransactionHandler() {
    LOG(ERROR) << "NewTransactionHandler is not implemented";
    return NULL;
}

} // namespace brpc
