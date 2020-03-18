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


#include <algorithm>
#include <google/protobuf/reflection_ops.h>                 // ReflectionOps::Merge
#include <google/protobuf/wire_format.h>
#include "brpc/nshead_message.h"
#include "butil/logging.h"

namespace brpc {

NsheadMessage::NsheadMessage()
    : ::google::protobuf::Message() {
    SharedCtor();
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
}

const ::google::protobuf::Descriptor* NsheadMessage::descriptor() {
    return NsheadMessageBase::descriptor();
}

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
    const NsheadMessage* source = dynamic_cast<const NsheadMessage*>(&from);
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
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = NsheadMessage::descriptor();
    metadata.reflection = NULL;
    return metadata;
}

} // namespace brpc
