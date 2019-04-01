// Copyright (c) 2015 Baidu, Inc.
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

// Authors: Ge,Jun (gejun@baidu.com)

#define INTERNAL_SUPPRESS_PROTOBUF_FIELD_DEPRECATION
#include <algorithm>
#include <gflags/gflags.h>
#include <google/protobuf/stubs/once.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite_inl.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection_ops.h>
#include <google/protobuf/wire_format.h>
#include "butil/string_printf.h"
#include "butil/macros.h"
#include "brpc/controller.h"
#include "brpc/redis.h"
#include "brpc/redis_command.h"


namespace brpc {

DEFINE_bool(redis_verbose_crlf2space, false, "[DEBUG] Show \\r\\n as a space");

// Internal implementation detail -- do not call these.
void protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto_impl();
void protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto();
void protobuf_AssignDesc_baidu_2frpc_2fredis_5fbase_2eproto();
void protobuf_ShutdownFile_baidu_2frpc_2fredis_5fbase_2eproto();

namespace {

const ::google::protobuf::Descriptor* RedisRequest_descriptor_ = NULL;
const ::google::protobuf::Descriptor* RedisResponse_descriptor_ = NULL;

}  // namespace

void protobuf_AssignDesc_baidu_2frpc_2fredis_5fbase_2eproto() {
    protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto();
    const ::google::protobuf::FileDescriptor* file =
        ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName(
            "baidu/rpc/redis_base.proto");
    GOOGLE_CHECK(file != NULL);
    RedisRequest_descriptor_ = file->message_type(0);
    RedisResponse_descriptor_ = file->message_type(1);
}

namespace {

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AssignDescriptors_once_);
inline void protobuf_AssignDescriptorsOnce() {
    ::google::protobuf::GoogleOnceInit(&protobuf_AssignDescriptors_once_,
                                       &protobuf_AssignDesc_baidu_2frpc_2fredis_5fbase_2eproto);
}

void protobuf_RegisterTypes(const ::std::string&) {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
        RedisRequest_descriptor_, &RedisRequest::default_instance());
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
        RedisResponse_descriptor_, &RedisResponse::default_instance());
}

}  // namespace

void protobuf_ShutdownFile_baidu_2frpc_2fredis_5fbase_2eproto() {
    delete RedisRequest::default_instance_;
    delete RedisResponse::default_instance_;
}

void protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto_impl() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

#if GOOGLE_PROTOBUF_VERSION >= 3002000
    ::google::protobuf::internal::InitProtobufDefaults();
#else
    ::google::protobuf::protobuf_AddDesc_google_2fprotobuf_2fdescriptor_2eproto();
#endif
  ::google::protobuf::DescriptorPool::InternalAddGeneratedFile(
    "\n\032baidu/rpc/redis_base.proto\022\tbaidu.rpc\032"
    " google/protobuf/descriptor.proto\"\016\n\014Red"
    "isRequest\"\017\n\rRedisResponseB\003\200\001\001", 111);
  ::google::protobuf::MessageFactory::InternalRegisterGeneratedFile(
    "baidu/rpc/redis_base.proto", &protobuf_RegisterTypes);
  RedisRequest::default_instance_ = new RedisRequest();
  RedisResponse::default_instance_ = new RedisResponse();
  RedisRequest::default_instance_->InitAsDefaultInstance();
  RedisResponse::default_instance_->InitAsDefaultInstance();
  ::google::protobuf::internal::OnShutdown(&protobuf_ShutdownFile_baidu_2frpc_2fredis_5fbase_2eproto);
}

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto_once);
void protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto() {
    ::google::protobuf::GoogleOnceInit(
        &protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto_once,
        &protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto_impl);
}

// Force AddDescriptors() to be called at static initialization time.
struct StaticDescriptorInitializer_baidu_2frpc_2fredis_5fbase_2eproto {
    StaticDescriptorInitializer_baidu_2frpc_2fredis_5fbase_2eproto() {
        protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto();
    }
} static_descriptor_initializer_baidu_2frpc_2fredis_5fbase_2eproto_;


// ===================================================================

#ifndef _MSC_VER
#endif  // !_MSC_VER

RedisRequest::RedisRequest()
    : ::google::protobuf::Message() {
    SharedCtor();
}

void RedisRequest::InitAsDefaultInstance() {
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
    if (this != default_instance_) {
    }
}

void RedisRequest::SetCachedSize(int size) const {
    GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
    _cached_size_ = size;
    GOOGLE_SAFE_CONCURRENT_WRITES_END();
}
const ::google::protobuf::Descriptor* RedisRequest::descriptor() {
    protobuf_AssignDescriptorsOnce();
    return RedisRequest_descriptor_;
}

const RedisRequest& RedisRequest::default_instance() {
    if (default_instance_ == NULL) {
        protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto();
    }
    return *default_instance_;
}

RedisRequest* RedisRequest::default_instance_ = NULL;

RedisRequest* RedisRequest::New() const {
    return new RedisRequest;
}

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
    GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
    _cached_size_ = total_size;
    GOOGLE_SAFE_CONCURRENT_WRITES_END();
    return total_size;
}

void RedisRequest::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const RedisRequest* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const RedisRequest*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void RedisRequest::MergeFrom(const RedisRequest& from) {
    GOOGLE_CHECK_NE(&from, this);
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

::google::protobuf::Metadata RedisRequest::GetMetadata() const {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = RedisRequest_descriptor_;
    metadata.reflection = NULL;
    return metadata;
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

// ===================================================================

#ifndef _MSC_VER
#endif  // !_MSC_VER

RedisResponse::RedisResponse()
    : ::google::protobuf::Message() {
    SharedCtor();
}

void RedisResponse::InitAsDefaultInstance() {
}

RedisResponse::RedisResponse(const RedisResponse& from)
    : ::google::protobuf::Message() {
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
    if (this != default_instance_) {
    }
}

void RedisResponse::SetCachedSize(int size) const {
    _cached_size_ = size;
}
const ::google::protobuf::Descriptor* RedisResponse::descriptor() {
    protobuf_AssignDescriptorsOnce();
    return RedisResponse_descriptor_;
}

const RedisResponse& RedisResponse::default_instance() {
    if (default_instance_ == NULL) {
        protobuf_AddDesc_baidu_2frpc_2fredis_5fbase_2eproto();
    }
    return *default_instance_;
}

RedisResponse* RedisResponse::default_instance_ = NULL;

RedisResponse* RedisResponse::New() const {
    return new RedisResponse;
}

void RedisResponse::Clear() {
    _first_reply.Clear();
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
    GOOGLE_CHECK_NE(&from, this);
    const RedisResponse* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const RedisResponse*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void RedisResponse::MergeFrom(const RedisResponse& from) {
    GOOGLE_CHECK_NE(&from, this);
    if (from._nreply == 0) {
        return;
    }
    _cached_size_ += from._cached_size_;
    if (_nreply == 0) {
        _first_reply.CopyFromDifferentArena(from._first_reply, &_arena);
    }
    const int new_nreply = _nreply + from._nreply;
    if (new_nreply == 1) {
        _nreply = new_nreply;
        return;
    }
    RedisReply* new_others =
        (RedisReply*)_arena.allocate(sizeof(RedisReply) * (new_nreply - 1));
    for (int i = 0; i < new_nreply - 1; ++i) {
        new (new_others + i) RedisReply;
    }
    int new_other_index = 0;
    for (int i = 1; i < _nreply; ++i) {
        new_others[new_other_index++].CopyFromSameArena(
            _other_replies[i - 1]);
    }
    for (int i = !_nreply; i < from._nreply; ++i) {
        new_others[new_other_index++].CopyFromDifferentArena(
            from.reply(i), &_arena);
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

::google::protobuf::Metadata RedisResponse::GetMetadata() const {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = RedisResponse_descriptor_;
    metadata.reflection = NULL;
    return metadata;
}

// ===================================================================

ParseError RedisResponse::ConsumePartialIOBuf(butil::IOBuf& buf, int reply_count) {
    size_t oldsize = buf.size();
    if (reply_size() == 0) {
        ParseError err = _first_reply.ConsumePartialIOBuf(buf, &_arena);
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
                new (&_other_replies[i]) RedisReply;
            }
        }
        for (int i = reply_size(); i < reply_count; ++i) {
            ParseError err = _other_replies[i - 1].ConsumePartialIOBuf(buf, &_arena);
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
 
} // namespace brpc
