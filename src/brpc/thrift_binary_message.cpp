// Copyright (c) 2014 Baidu, Inc.
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

// Authors: wangxuefeng (wangxuefeng@didichuxing.com)

#define INTERNAL_SUPPRESS_PROTOBUF_FIELD_DEPRECATION
#include "brpc/thrift_binary_message.h"

#include <algorithm>
#include "butil/logging.h"

#include <google/protobuf/stubs/once.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite_inl.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection_ops.h>
#include <google/protobuf/wire_format.h>


namespace brpc {

namespace {
const ::google::protobuf::Descriptor* ThriftBinaryMessage_descriptor_ = NULL;
}  // namespace


void protobuf_AssignDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto() {
    protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
    const ::google::protobuf::FileDescriptor* file =
        ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName(
            "baidu/rpc/thrift_binary_message.proto");
    GOOGLE_CHECK(file != NULL);
    ThriftBinaryMessage_descriptor_ = file->message_type(0);
}

namespace {

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AssignDescriptors_once_);
inline void protobuf_AssignDescriptorsOnce() {
    ::google::protobuf::GoogleOnceInit(&protobuf_AssignDescriptors_once_,
                                       &protobuf_AssignDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto);
}

void protobuf_RegisterTypes(const ::std::string&) {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
        ThriftBinaryMessage_descriptor_, &ThriftBinaryMessage::default_instance());
}

}  // namespace

void protobuf_ShutdownFile_baidu_2frpc_2fthrift_binary_5fmessage_2eproto() {
    delete ThriftBinaryMessage::default_instance_;
}

void protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto_impl() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

#if GOOGLE_PROTOBUF_VERSION >= 3002000
    ::google::protobuf::internal::InitProtobufDefaults();
#else
    ::google::protobuf::protobuf_AddDesc_google_2fprotobuf_2fdescriptor_2eproto();
#endif
    ::google::protobuf::DescriptorPool::InternalAddGeneratedFile(
       "\n\033thrift_binary_message.proto\022\004brpc\"\025\n\023T"
       "hriftBinaryMessage", 58);
     ::google::protobuf::MessageFactory::InternalRegisterGeneratedFile(
       "thrift_binary_message.proto", &protobuf_RegisterTypes);
     ThriftBinaryMessage::default_instance_ = new ThriftBinaryMessage();
     ThriftBinaryMessage::default_instance_->InitAsDefaultInstance();
     ::google::protobuf::internal::OnShutdown(&protobuf_ShutdownFile_baidu_2frpc_2fthrift_binary_5fmessage_2eproto);

}

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto_once);
void protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto() {
    ::google::protobuf::GoogleOnceInit(
        &protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto_once,
        &protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto_impl);
}

// Force AddDescriptors() to be called at static initialization time.
struct StaticDescriptorInitializer_baidu_2frpc_2fthrift_binary_5fmessage_2eproto {
    StaticDescriptorInitializer_baidu_2frpc_2fthrift_binary_5fmessage_2eproto() {
        protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
    }
} static_descriptor_initializer_baidu_2frpc_2fthrift_binary_5fmessage_2eproto_;


// ===================================================================

#ifndef _MSC_VER
#endif  // !_MSC_VER

ThriftBinaryMessage::ThriftBinaryMessage()
    : ::google::protobuf::Message() {
    SharedCtor();
}

void ThriftBinaryMessage::InitAsDefaultInstance() {
}

ThriftBinaryMessage::ThriftBinaryMessage(const ThriftBinaryMessage& from)
    : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void ThriftBinaryMessage::SharedCtor() {
    memset(&head, 0, sizeof(head));
}

ThriftBinaryMessage::~ThriftBinaryMessage() {
    SharedDtor();
}

void ThriftBinaryMessage::SharedDtor() {
    if (this != default_instance_) {
    }
}

const ::google::protobuf::Descriptor* ThriftBinaryMessage::descriptor() {
    protobuf_AssignDescriptorsOnce();
    return ThriftBinaryMessage_descriptor_;
}

const ThriftBinaryMessage& ThriftBinaryMessage::default_instance() {
    if (default_instance_ == NULL)
        protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
    return *default_instance_;
}

ThriftBinaryMessage* ThriftBinaryMessage::default_instance_ = NULL;

ThriftBinaryMessage* ThriftBinaryMessage::New() const {
    return new ThriftBinaryMessage;
}

void ThriftBinaryMessage::Clear() {
    memset(&head, 0, sizeof(head));
    body.clear();
}

bool ThriftBinaryMessage::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
#define DO_(EXPRESSION) if (!(EXPRESSION)) return false
    ::google::protobuf::uint32 tag;
    while ((tag = input->ReadTag()) != 0) {
        if (::google::protobuf::internal::WireFormatLite::GetTagWireType(tag) ==
            ::google::protobuf::internal::WireFormatLite::WIRETYPE_END_GROUP) {
            return true;
        }
    }
    return true;
#undef DO_
}

void ThriftBinaryMessage::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream*) const {
}

::google::protobuf::uint8* ThriftBinaryMessage::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

int ThriftBinaryMessage::ByteSize() const {
    return sizeof(thrift_binary_head_t) + body.size();
}

void ThriftBinaryMessage::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const ThriftBinaryMessage* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const ThriftBinaryMessage*>(
            &from);
    if (source == NULL) {
        LOG(ERROR) << "Can only merge from ThriftBinaryMessage";
        return;
    } else {
        MergeFrom(*source);
    }
}

void ThriftBinaryMessage::MergeFrom(const ThriftBinaryMessage& from) {
    GOOGLE_CHECK_NE(&from, this);
    head = from.head;
    body = from.body;
}

void ThriftBinaryMessage::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

void ThriftBinaryMessage::CopyFrom(const ThriftBinaryMessage& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

bool ThriftBinaryMessage::IsInitialized() const {
    return true;
}

void ThriftBinaryMessage::Swap(ThriftBinaryMessage* other) {
    if (other != this) {
        const thrift_binary_head_t tmp = other->head;
        other->head = head;
        head = tmp;
        body.swap(other->body);
    }
}

::google::protobuf::Metadata ThriftBinaryMessage::GetMetadata() const {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = ThriftBinaryMessage_descriptor_;
    metadata.reflection = NULL;
    return metadata;
}

} // namespace brpc
