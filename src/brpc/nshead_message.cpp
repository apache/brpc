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

// Authors: Ge,Jun (gejun@baidu.com)

#define INTERNAL_SUPPRESS_PROTOBUF_FIELD_DEPRECATION
#include "brpc/nshead_message.h"

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
const ::google::protobuf::Descriptor* NsheadMessage_descriptor_ = NULL;
}  // namespace


void protobuf_AssignDesc_baidu_2frpc_2fnshead_5fmessage_2eproto() {
    protobuf_AddDesc_baidu_2frpc_2fnshead_5fmessage_2eproto();
    const ::google::protobuf::FileDescriptor* file =
        ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName(
            "baidu/rpc/nshead_message.proto");
    GOOGLE_CHECK(file != NULL);
    NsheadMessage_descriptor_ = file->message_type(0);
}

namespace {

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AssignDescriptors_once_);
inline void protobuf_AssignDescriptorsOnce() {
    ::google::protobuf::GoogleOnceInit(&protobuf_AssignDescriptors_once_,
                                       &protobuf_AssignDesc_baidu_2frpc_2fnshead_5fmessage_2eproto);
}

void protobuf_RegisterTypes(const ::std::string&) {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
        NsheadMessage_descriptor_, &NsheadMessage::default_instance());
}

}  // namespace

void protobuf_ShutdownFile_baidu_2frpc_2fnshead_5fmessage_2eproto() {
    delete NsheadMessage::default_instance_;
}

void protobuf_AddDesc_baidu_2frpc_2fnshead_5fmessage_2eproto_impl() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

#if GOOGLE_PROTOBUF_VERSION >= 3002000
    ::google::protobuf::internal::InitProtobufDefaults();
#else
    ::google::protobuf::protobuf_AddDesc_google_2fprotobuf_2fdescriptor_2eproto();
#endif
    ::google::protobuf::DescriptorPool::InternalAddGeneratedFile(
        "\n\036baidu/rpc/nshead_message.proto\022\tbaidu."
        "rpc\032 google/protobuf/descriptor.proto\"\017\n"
        "\rNsheadMessageB\003\200\001\001", 99);
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedFile(
        "baidu/rpc/nshead_message.proto", &protobuf_RegisterTypes);
    NsheadMessage::default_instance_ = new NsheadMessage();
    NsheadMessage::default_instance_->InitAsDefaultInstance();
    ::google::protobuf::internal::OnShutdown(
        &protobuf_ShutdownFile_baidu_2frpc_2fnshead_5fmessage_2eproto);
}

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AddDesc_baidu_2frpc_2fnshead_5fmessage_2eproto_once);
void protobuf_AddDesc_baidu_2frpc_2fnshead_5fmessage_2eproto() {
    ::google::protobuf::GoogleOnceInit(
        &protobuf_AddDesc_baidu_2frpc_2fnshead_5fmessage_2eproto_once,
        &protobuf_AddDesc_baidu_2frpc_2fnshead_5fmessage_2eproto_impl);
}

// Force AddDescriptors() to be called at static initialization time.
struct StaticDescriptorInitializer_baidu_2frpc_2fnshead_5fmessage_2eproto {
    StaticDescriptorInitializer_baidu_2frpc_2fnshead_5fmessage_2eproto() {
        protobuf_AddDesc_baidu_2frpc_2fnshead_5fmessage_2eproto();
    }
} static_descriptor_initializer_baidu_2frpc_2fnshead_5fmessage_2eproto_;


// ===================================================================

#ifndef _MSC_VER
#endif  // !_MSC_VER

NsheadMessage::NsheadMessage()
    : ::google::protobuf::Message() {
    SharedCtor();
}

void NsheadMessage::InitAsDefaultInstance() {
}

NsheadMessage::NsheadMessage(const NsheadMessage& from)
    : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void NsheadMessage::SharedCtor() {
    memset(&head, 0, sizeof(head));
}

NsheadMessage::~NsheadMessage() {
    SharedDtor();
}

void NsheadMessage::SharedDtor() {
    if (this != default_instance_) {
    }
}

const ::google::protobuf::Descriptor* NsheadMessage::descriptor() {
    protobuf_AssignDescriptorsOnce();
    return NsheadMessage_descriptor_;
}

const NsheadMessage& NsheadMessage::default_instance() {
    if (default_instance_ == NULL)
        protobuf_AddDesc_baidu_2frpc_2fnshead_5fmessage_2eproto();
    return *default_instance_;
}

NsheadMessage* NsheadMessage::default_instance_ = NULL;

NsheadMessage* NsheadMessage::New() const {
    return new NsheadMessage;
}

void NsheadMessage::Clear() {
    memset(&head, 0, sizeof(head));
    body.clear();
}

bool NsheadMessage::MergePartialFromCodedStream(
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

void NsheadMessage::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream*) const {
}

::google::protobuf::uint8* NsheadMessage::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

int NsheadMessage::ByteSize() const {
    return sizeof(nshead_t) + body.size();
}

void NsheadMessage::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const NsheadMessage* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const NsheadMessage*>(
            &from);
    if (source == NULL) {
        LOG(ERROR) << "Can only merge from NsheadMessage";
        return;
    } else {
        MergeFrom(*source);
    }
}

void NsheadMessage::MergeFrom(const NsheadMessage& from) {
    GOOGLE_CHECK_NE(&from, this);
    // No way to merge two nshead messages, just overwrite.
    head = from.head;
    body = from.body;
}

void NsheadMessage::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

void NsheadMessage::CopyFrom(const NsheadMessage& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

bool NsheadMessage::IsInitialized() const {
    return true;
}

void NsheadMessage::Swap(NsheadMessage* other) {
    if (other != this) {
        const nshead_t tmp = other->head;
        other->head = head;
        head = tmp;
        body.swap(other->body);
    }
}

::google::protobuf::Metadata NsheadMessage::GetMetadata() const {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = NsheadMessage_descriptor_;
    metadata.reflection = NULL;
    return metadata;
}

} // namespace brpc
