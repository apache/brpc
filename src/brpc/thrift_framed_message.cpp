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

#ifdef ENABLE_THRIFT_FRAMED_PROTOCOL

#define INTERNAL_SUPPRESS_PROTOBUF_FIELD_DEPRECATION
#include "brpc/thrift_framed_message.h"

#include <algorithm>
#include "butil/logging.h"

#include <brpc/protocol.h>                          // RegisterProtocol
#include <brpc/policy/thrift_protocol.h>

#include <google/protobuf/stubs/once.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite_inl.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection_ops.h>
#include <google/protobuf/wire_format.h>


namespace brpc {

namespace {
const ::google::protobuf::Descriptor* ThriftFramedMessage_descriptor_ = NULL;
}  // namespace


void protobuf_AssignDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto() {
    protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
    const ::google::protobuf::FileDescriptor* file =
        ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName(
            "baidu/rpc/thrift_framed_message.proto");
    GOOGLE_CHECK(file != NULL);
    ThriftFramedMessage_descriptor_ = file->message_type(0);
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
        ThriftFramedMessage_descriptor_, &ThriftFramedMessage::default_instance());
}

}  // namespace

void protobuf_ShutdownFile_baidu_2frpc_2fthrift_binary_5fmessage_2eproto() {
    delete ThriftFramedMessage::default_instance_;
}

void protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto_impl() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

#if GOOGLE_PROTOBUF_VERSION >= 3002000
    ::google::protobuf::internal::InitProtobufDefaults();
#else
    ::google::protobuf::protobuf_AddDesc_google_2fprotobuf_2fdescriptor_2eproto();
#endif
    ::google::protobuf::DescriptorPool::InternalAddGeneratedFile(
       "\n\033thrift_framed_message.proto\022\004brpc\"\025\n\023T"
       "hriftBinaryMessage", 58);
     ::google::protobuf::MessageFactory::InternalRegisterGeneratedFile(
       "thrift_framed_message.proto", &protobuf_RegisterTypes);
     ThriftFramedMessage::default_instance_ = new ThriftFramedMessage();
     ThriftFramedMessage::default_instance_->InitAsDefaultInstance();
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

ThriftFramedMessage::ThriftFramedMessage()
    : ::google::protobuf::Message() {
    SharedCtor();
}

void ThriftFramedMessage::InitAsDefaultInstance() {
}

ThriftFramedMessage::ThriftFramedMessage(const ThriftFramedMessage& from)
    : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void ThriftFramedMessage::SharedCtor() {
    memset(&head, 0, sizeof(head));
    thrift_raw_instance_deleter = nullptr;
    thrift_raw_instance = nullptr;
    thrift_message_seq_id = 0;
    method_name = "";    
    //RegisterThriftFramedProtocolDummy dummy;
}

ThriftFramedMessage::~ThriftFramedMessage() {
    SharedDtor();
    if (thrift_raw_instance && thrift_raw_instance_deleter) {
        thrift_raw_instance_deleter(thrift_raw_instance);
    }
}

void ThriftFramedMessage::SharedDtor() {
    if (this != default_instance_) {
    }
}

const ::google::protobuf::Descriptor* ThriftFramedMessage::descriptor() {
    protobuf_AssignDescriptorsOnce();
    return ThriftFramedMessage_descriptor_;
}

const ThriftFramedMessage& ThriftFramedMessage::default_instance() {
    if (default_instance_ == NULL)
        protobuf_AddDesc_baidu_2frpc_2fthrift_binary_5fmessage_2eproto();
    return *default_instance_;
}

ThriftFramedMessage* ThriftFramedMessage::default_instance_ = NULL;

ThriftFramedMessage* ThriftFramedMessage::New() const {
    return new ThriftFramedMessage;
}

void ThriftFramedMessage::Clear() {
    memset(&head, 0, sizeof(head));
    body.clear();
}

bool ThriftFramedMessage::MergePartialFromCodedStream(
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

void ThriftFramedMessage::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream*) const {
}

::google::protobuf::uint8* ThriftFramedMessage::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

int ThriftFramedMessage::ByteSize() const {
    return sizeof(thrift_binary_head_t) + body.size();
}

void ThriftFramedMessage::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const ThriftFramedMessage* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const ThriftFramedMessage*>(
            &from);
    if (source == NULL) {
        LOG(ERROR) << "Can only merge from ThriftFramedMessage";
        return;
    } else {
        MergeFrom(*source);
    }
}

void ThriftFramedMessage::MergeFrom(const ThriftFramedMessage& from) {
    GOOGLE_CHECK_NE(&from, this);
    head = from.head;
    body = from.body;
}

void ThriftFramedMessage::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

void ThriftFramedMessage::CopyFrom(const ThriftFramedMessage& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

bool ThriftFramedMessage::IsInitialized() const {
    return true;
}

void ThriftFramedMessage::Swap(ThriftFramedMessage* other) {
    if (other != this) {
        const thrift_binary_head_t tmp = other->head;
        other->head = head;
        head = tmp;
        body.swap(other->body);
    }
}

::google::protobuf::Metadata ThriftFramedMessage::GetMetadata() const {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = ThriftFramedMessage_descriptor_;
    metadata.reflection = NULL;
    return metadata;
}

} // namespace brpc

#endif
