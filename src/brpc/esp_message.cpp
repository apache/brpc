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

#include "esp_message.h"

#include <google/protobuf/reflection_ops.h>     // ReflectionOps::Merge
#include <google/protobuf/wire_format.h>        // WireFormatLite::GetTagWireType

namespace brpc {

EspMessage::EspMessage()
    : ::google::protobuf::Message() {
    SharedCtor();
}

EspMessage::EspMessage(const EspMessage& from)
    : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void EspMessage::SharedCtor() {
    memset(&head, 0, sizeof(head));
}

EspMessage::~EspMessage() {
    SharedDtor();
}

void EspMessage::SharedDtor() {
}

const ::google::protobuf::Descriptor* EspMessage::descriptor() {
    return EspMessageBase::descriptor();
}

EspMessage* EspMessage::New() const {
    return new EspMessage;
}

#if GOOGLE_PROTOBUF_VERSION >= 3006000
EspMessage* EspMessage::New(::google::protobuf::Arena* arena) const {
    return CreateMaybeMessage<EspMessage>(arena);
}
#endif

void EspMessage::Clear() {
    head.body_len = 0;
    body.clear();
}

bool EspMessage::MergePartialFromCodedStream(
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

void EspMessage::SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream*) const {
}

::google::protobuf::uint8* EspMessage::SerializeWithCachedSizesToArray(
        ::google::protobuf::uint8* target) const {
    return target;
}

int EspMessage::ByteSize() const {
    return sizeof(head) + body.size();
}

void EspMessage::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const EspMessage* source = dynamic_cast<const EspMessage*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void EspMessage::MergeFrom(const EspMessage& from) {
    GOOGLE_CHECK_NE(&from, this);
    head = from.head;
    body = from.body;
}

void EspMessage::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) {
        return;
    }

    Clear();
    MergeFrom(from);
}

void EspMessage::CopyFrom(const EspMessage& from) {
    if (&from == this) {
        return;
    }

    Clear();
    MergeFrom(from);
}

bool EspMessage::IsInitialized() const {
    return true;
}

void EspMessage::Swap(EspMessage* other) {
    if (other != this) {
        const EspHead tmp = other->head;
        other->head = head;
        head = tmp;
        body.swap(other->body);
    }
}

::google::protobuf::Metadata EspMessage::GetMetadata() const {
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = EspMessage::descriptor();
    metadata.reflection = NULL;
    return metadata;
}

} // namespace brpc
