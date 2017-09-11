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
#include "brpc/serialized_request.h"

#include <algorithm>

#include <google/protobuf/stubs/once.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite_inl.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection_ops.h>
#include <google/protobuf/wire_format.h>

#include "butil/logging.h"


namespace brpc {

namespace {

const ::google::protobuf::Descriptor* SerializedRequest_descriptor_ = NULL;

}  // namespace

void protobuf_AssignDesc_baidu_2frpc_2fserialized_5frequest_2eproto() {
    protobuf_AddDesc_baidu_2frpc_2fserialized_5frequest_2eproto();
    const ::google::protobuf::FileDescriptor* file =
        ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName(
            "baidu/rpc/serialized_request.proto");
    GOOGLE_CHECK(file != NULL);
    SerializedRequest_descriptor_ = file->message_type(0);
}

namespace {

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AssignDescriptors_once_);
inline void protobuf_AssignDescriptorsOnce() {
    ::google::protobuf::GoogleOnceInit(&protobuf_AssignDescriptors_once_,
                                       &protobuf_AssignDesc_baidu_2frpc_2fserialized_5frequest_2eproto);
}

void protobuf_RegisterTypes(const ::std::string&) {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
        SerializedRequest_descriptor_, &SerializedRequest::default_instance());
}

}  // namespace

void protobuf_ShutdownFile_baidu_2frpc_2fserialized_5frequest_2eproto() {
    delete SerializedRequest::default_instance_;
}

void protobuf_AddDesc_baidu_2frpc_2fserialized_5frequest_2eproto() {
    static bool already_here = false;
    if (already_here) return;
    already_here = true;
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    ::google::protobuf::DescriptorPool::InternalAddGeneratedFile(
        "\n\"baidu/rpc/serialized_request.proto\022\tba"
        "idu.rpc\"\023\n\021SerializedRequest", 68);
    ::google::protobuf::MessageFactory::InternalRegisterGeneratedFile(
        "baidu/rpc/serialized_request.proto", &protobuf_RegisterTypes);
    SerializedRequest::default_instance_ = new SerializedRequest();
    SerializedRequest::default_instance_->InitAsDefaultInstance();
    ::google::protobuf::internal::OnShutdown(&protobuf_ShutdownFile_baidu_2frpc_2fserialized_5frequest_2eproto);
}

// Force AddDescriptors() to be called at static initialization time.
struct StaticDescriptorInitializer_baidu_2frpc_2fserialized_5frequest_2eproto {
    StaticDescriptorInitializer_baidu_2frpc_2fserialized_5frequest_2eproto() {
        protobuf_AddDesc_baidu_2frpc_2fserialized_5frequest_2eproto();
    }
} static_descriptor_initializer_baidu_2frpc_2fserialized_5frequest_2eproto_;


// ===================================================================

#ifndef _MSC_VER
#endif  // !_MSC_VER

SerializedRequest::SerializedRequest()
    : ::google::protobuf::Message() {
    SharedCtor();
}

void SerializedRequest::InitAsDefaultInstance() {
}

SerializedRequest::SerializedRequest(const SerializedRequest& from)
    : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void SerializedRequest::SharedCtor() {
}

SerializedRequest::~SerializedRequest() {
    SharedDtor();
}

void SerializedRequest::SharedDtor() {
    if (this != default_instance_) {
    }
}

void SerializedRequest::SetCachedSize(int /*size*/) const {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
}
const ::google::protobuf::Descriptor* SerializedRequest::descriptor() {
    protobuf_AssignDescriptorsOnce();
    return SerializedRequest_descriptor_;
}

const SerializedRequest& SerializedRequest::default_instance() {
    if (default_instance_ == NULL)
        protobuf_AddDesc_baidu_2frpc_2fserialized_5frequest_2eproto();
    return *default_instance_;
}

SerializedRequest* SerializedRequest::default_instance_ = NULL;

SerializedRequest* SerializedRequest::New() const {
    return new SerializedRequest;
}

void SerializedRequest::Clear() {
    _serialized.clear();
}

bool SerializedRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream*) {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
    return false;
}

void SerializedRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream*) const {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
}

::google::protobuf::uint8* SerializedRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
    return target;
}

int SerializedRequest::ByteSize() const {
    return (int)_serialized.size();
}

void SerializedRequest::MergeFrom(const ::google::protobuf::Message&) {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
}

void SerializedRequest::MergeFrom(const SerializedRequest&) {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
}

void SerializedRequest::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    const SerializedRequest* source =
        ::google::protobuf::internal::dynamic_cast_if_available<const SerializedRequest*>(
            &from);
    if (source == NULL) {
        CHECK(false) << "SerializedRequest can only CopyFrom SerializedRequest";
    } else {
        _serialized = source->_serialized;
    }
}

void SerializedRequest::CopyFrom(const SerializedRequest& from) {
    if (&from == this) return;
    _serialized = from._serialized;
}

bool SerializedRequest::IsInitialized() const {
    // Always true because it's already serialized.
    return true;
}

void SerializedRequest::Swap(SerializedRequest* other) {
    if (other != this) {
        _serialized.swap(other->_serialized);
    }
}

::google::protobuf::Metadata SerializedRequest::GetMetadata() const {
    protobuf_AssignDescriptorsOnce();
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = SerializedRequest_descriptor_;
    metadata.reflection = NULL;
    return metadata;
}

} // namespace brpc
