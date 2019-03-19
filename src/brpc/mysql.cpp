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
#include "brpc/mysql.h"
#include "brpc/mysql_command.h"
#include "brpc/mysql_reply.h"

namespace brpc {

DEFINE_int32(mysql_max_replies, 50, "maximum replies size in one MysqlResponse");
DEFINE_bool(mysql_verbose_crlf2space, false, "[DEBUG] Show \\r\\n as a space");

// Internal implementation detail -- do not call these.
void protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto_impl();
void protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto();
void protobuf_AssignDesc_baidu_2frpc_2fmysql_5fbase_2eproto();
void protobuf_ShutdownFile_baidu_2frpc_2fmysql_5fbase_2eproto();

namespace {

const ::google::protobuf::Descriptor* MysqlRequest_descriptor_ = NULL;
const ::google::protobuf::Descriptor* MysqlResponse_descriptor_ = NULL;

}  // namespace

void protobuf_AssignDesc_baidu_2frpc_2fmysql_5fbase_2eproto() {
    protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto();
    const ::google::protobuf::FileDescriptor* file =
        ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName(
            "baidu/rpc/mysql_base.proto");
    GOOGLE_CHECK(file != NULL);
    MysqlRequest_descriptor_ = file->message_type(0);
    MysqlResponse_descriptor_ = file->message_type(1);
}

namespace {

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AssignDescriptors_once_);
inline void protobuf_AssignDescriptorsOnce() {
    ::google::protobuf::GoogleOnceInit(&protobuf_AssignDescriptors_once_,
                                       &protobuf_AssignDesc_baidu_2frpc_2fmysql_5fbase_2eproto);
}

void protobuf_RegisterTypes(const ::std::string&) {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
        MysqlRequest_descriptor_, &MysqlRequest::default_instance());
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
        MysqlResponse_descriptor_, &MysqlResponse::default_instance());
}

}  // namespace

void protobuf_ShutdownFile_baidu_2frpc_2fmysql_5fbase_2eproto() {
    delete MysqlRequest::default_instance_;
    delete MysqlResponse::default_instance_;
}

void protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto_impl() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

#if GOOGLE_PROTOBUF_VERSION >= 3002000
    ::google::protobuf::internal::InitProtobufDefaults();
#else
    ::google::protobuf::protobuf_AddDesc_google_2fprotobuf_2fdescriptor_2eproto();
#endif
    ::google::protobuf::DescriptorPool::InternalAddGeneratedFile(
        "\n\032baidu/rpc/mysql_base.proto\022\tbaidu.rpc\032"
        " google/protobuf/descriptor.proto\"\016\n\014Mys"
        "qlRequest\"\017\n\rMysqlResponseB\003\200\001\001",
        111);
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedFile("baidu/rpc/mysql_base.proto",
                                                                      &protobuf_RegisterTypes);
    MysqlRequest::default_instance_ = new MysqlRequest();
    MysqlResponse::default_instance_ = new MysqlResponse();
    MysqlRequest::default_instance_->InitAsDefaultInstance();
    MysqlResponse::default_instance_->InitAsDefaultInstance();
    ::google::protobuf::internal::OnShutdown(
        &protobuf_ShutdownFile_baidu_2frpc_2fmysql_5fbase_2eproto);
}

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto_once);
void protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto() {
    ::google::protobuf::GoogleOnceInit(&protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto_once,
                                       &protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto_impl);
}

// Force AddDescriptors() to be called at static initialization time.
struct StaticDescriptorInitializer_baidu_2frpc_2fmysql_5fbase_2eproto {
    StaticDescriptorInitializer_baidu_2frpc_2fmysql_5fbase_2eproto() {
        protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto();
    }
} static_descriptor_initializer_baidu_2frpc_2fmysql_5fbase_2eproto_;


// ===================================================================

#ifndef _MSC_VER
#endif  // !_MSC_VER

MysqlRequest::MysqlRequest() : ::google::protobuf::Message() {
    SharedCtor();
}

void MysqlRequest::InitAsDefaultInstance() {}

MysqlRequest::MysqlRequest(const MysqlRequest& from) : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void MysqlRequest::SharedCtor() {
    _has_error = false;
    _cached_size_ = 0;
}

MysqlRequest::~MysqlRequest() {
    SharedDtor();
}

void MysqlRequest::SharedDtor() {
    if (this != default_instance_) {
    }
}

void MysqlRequest::SetCachedSize(int size) const {
    GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
    _cached_size_ = size;
    GOOGLE_SAFE_CONCURRENT_WRITES_END();
}
const ::google::protobuf::Descriptor* MysqlRequest::descriptor() {
    protobuf_AssignDescriptorsOnce();
    return MysqlRequest_descriptor_;
}

const MysqlRequest& MysqlRequest::default_instance() {
    if (default_instance_ == NULL) {
        protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto();
    }
    return *default_instance_;
}

MysqlRequest* MysqlRequest::default_instance_ = NULL;

MysqlRequest* MysqlRequest::New() const {
    return new MysqlRequest;
}

void MysqlRequest::Clear() {
    _has_error = false;
    _buf.clear();
}

bool MysqlRequest::MergePartialFromCodedStream(::google::protobuf::io::CodedInputStream*) {
    LOG(WARNING) << "You're not supposed to parse a MysqlRequest";
    return true;
}

void MysqlRequest::SerializeWithCachedSizes(::google::protobuf::io::CodedOutputStream*) const {
    LOG(WARNING) << "You're not supposed to serialize a MysqlRequest";
}

::google::protobuf::uint8* MysqlRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

int MysqlRequest::ByteSize() const {
    int total_size = _buf.size();
    GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
    _cached_size_ = total_size;
    GOOGLE_SAFE_CONCURRENT_WRITES_END();
    return total_size;
}

void MysqlRequest::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const MysqlRequest* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const MysqlRequest*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void MysqlRequest::MergeFrom(const MysqlRequest& from) {
    GOOGLE_CHECK_NE(&from, this);
    _has_error = _has_error || from._has_error;
    _buf.append(from._buf);
}

void MysqlRequest::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this)
        return;
    Clear();
    MergeFrom(from);
}

void MysqlRequest::CopyFrom(const MysqlRequest& from) {
    if (&from == this)
        return;
    Clear();
    MergeFrom(from);
}

void MysqlRequest::Swap(MysqlRequest* other) {
    if (other != this) {
        _buf.swap(other->_buf);
        std::swap(_has_error, other->_has_error);
        std::swap(_cached_size_, other->_cached_size_);
    }
}

bool MysqlRequest::SerializeTo(butil::IOBuf* buf) const {
    if (_has_error) {
        LOG(ERROR) << "Reject serialization due to error in AddCommand[V]";
        return false;
    }
    *buf = _buf;
    return true;
}

::google::protobuf::Metadata MysqlRequest::GetMetadata() const {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = MysqlRequest_descriptor_;
    metadata.reflection = NULL;
    return metadata;
}

bool MysqlRequest::Query(const std::string& stmt) {
    if (_has_error) {
        return false;
    }

    const butil::Status st = MysqlMakeCommand(&_buf, COM_QUERY, stmt);
    if (st.ok()) {
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

// ===================================================================

#ifndef _MSC_VER
#endif  // !_MSC_VER

MysqlResponse::MysqlResponse() : ::google::protobuf::Message() {
    SharedCtor();
}

void MysqlResponse::InitAsDefaultInstance() {}

MysqlResponse::MysqlResponse(const MysqlResponse& from) : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void MysqlResponse::SharedCtor() {
    _other_replies = NULL;
    _nreply = 0;
    _cached_size_ = 0;
}

MysqlResponse::~MysqlResponse() {
    SharedDtor();
}

void MysqlResponse::SharedDtor() {
    if (this != default_instance_) {
    }
}

void MysqlResponse::SetCachedSize(int size) const {
    _cached_size_ = size;
}
const ::google::protobuf::Descriptor* MysqlResponse::descriptor() {
    protobuf_AssignDescriptorsOnce();
    return MysqlResponse_descriptor_;
}

const MysqlResponse& MysqlResponse::default_instance() {
    if (default_instance_ == NULL) {
        protobuf_AddDesc_baidu_2frpc_2fmysql_5fbase_2eproto();
    }
    return *default_instance_;
}

MysqlResponse* MysqlResponse::default_instance_ = NULL;

MysqlResponse* MysqlResponse::New() const {
    return new MysqlResponse;
}

void MysqlResponse::Clear() {}

bool MysqlResponse::MergePartialFromCodedStream(::google::protobuf::io::CodedInputStream*) {
    LOG(WARNING) << "You're not supposed to parse a MysqlResponse";
    return true;
}

void MysqlResponse::SerializeWithCachedSizes(::google::protobuf::io::CodedOutputStream*) const {
    LOG(WARNING) << "You're not supposed to serialize a MysqlResponse";
}

::google::protobuf::uint8* MysqlResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

int MysqlResponse::ByteSize() const {
    return _cached_size_;
}

void MysqlResponse::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const MysqlResponse* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const MysqlResponse*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void MysqlResponse::MergeFrom(const MysqlResponse& from) {
    GOOGLE_CHECK_NE(&from, this);
}

void MysqlResponse::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this)
        return;
    Clear();
    MergeFrom(from);
}

void MysqlResponse::CopyFrom(const MysqlResponse& from) {
    if (&from == this)
        return;
    Clear();
    MergeFrom(from);
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

::google::protobuf::Metadata MysqlResponse::GetMetadata() const {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = MysqlResponse_descriptor_;
    metadata.reflection = NULL;
    return metadata;
}

// ===================================================================

bool MysqlResponse::ConsumePartialIOBuf(butil::IOBuf& buf, const bool is_auth) {
    bool is_multi;

label_reparse:
    size_t oldsize = buf.size();
    if (reply_size() == 0) {
        if (!_first_reply.ConsumePartialIOBuf(buf, &_arena, is_auth, &is_multi)) {
            LOG(INFO) << "mysql reply parse error";
            return false;
        }

        const size_t newsize = buf.size();
        _cached_size_ += oldsize - newsize;
        oldsize = newsize;
        ++_nreply;
    } else {
        if (_nreply >= FLAGS_mysql_max_replies) {
            LOG(ERROR) << "current max reply size is " << FLAGS_mysql_max_replies
                       << ", use --mysql_max_replies=XXX to adjust";
            return false;
        }
        if (_other_replies == NULL) {
            _other_replies =
                (MysqlReply*)_arena.allocate(sizeof(MysqlReply) * FLAGS_mysql_max_replies - 1);
            if (_other_replies == NULL) {
                LOG(ERROR) << "Fail to allocate MysqlReply";
                return false;
            }
        }
        new (&_other_replies[_nreply - 1]) MysqlReply;
        if (!_other_replies[_nreply - 1].ConsumePartialIOBuf(buf, &_arena, is_auth, &is_multi)) {
            LOG(INFO) << "mysql reply parse error";
            return false;
        }

        const size_t newsize = buf.size();
        _cached_size_ += oldsize - newsize;
        oldsize = newsize;
        ++_nreply;
    }

    if (oldsize > 0) {
        goto label_reparse;
    }

    if (!is_multi) {
        return true;
    } else {
        return false;
    }
}

std::ostream& operator<<(std::ostream& os, const MysqlResponse& response) {
    os << "\n-----MYSQL REPLY BEGIN-----\n";
    if (response.reply_size() == 0) {
        os << "<empty response>";
    } else if (response.reply_size() == 1) {
        os << response.reply(0);
    } else {
        for (int i = 0; i < response.reply_size(); ++i) {
            os << "\nreply(" << i << ")----------";
            os << response.reply(i);
        }
    }
    os << "\n-----MYSQL REPLY END-----\n";

    return os;
}

}  // namespace brpc
