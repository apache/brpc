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


#include "brpc/serialized_response.h"
#include "butil/logging.h"

namespace brpc {

SerializedResponse::SerializedResponse()
    : ::google::protobuf::Message() {
    SharedCtor();
}

SerializedResponse::SerializedResponse(const SerializedResponse& from)
    : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void SerializedResponse::SharedCtor() {
}

SerializedResponse::~SerializedResponse() {
    SharedDtor();
}

void SerializedResponse::SharedDtor() {
}

void SerializedResponse::SetCachedSize(int /*size*/) const {
    CHECK(false) << "You're not supposed to call " << __func__;
}
const ::google::protobuf::Descriptor* SerializedResponse::descriptor() {
    return SerializedResponseBase::descriptor();
}

SerializedResponse* SerializedResponse::New() const {
    return new SerializedResponse;
}

#if GOOGLE_PROTOBUF_VERSION >= 3006000
SerializedResponse*
SerializedResponse::New(::google::protobuf::Arena* arena) const {
    return CreateMaybeMessage<SerializedResponse>(arena);
}
#endif

void SerializedResponse::Clear() {
    _serialized.clear();
}

bool SerializedResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream*) {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
    return false;
}

void SerializedResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream*) const {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
}

::google::protobuf::uint8* SerializedResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
    return target;
}

int SerializedResponse::ByteSize() const {
    return (int)_serialized.size();
}

void SerializedResponse::MergeFrom(const ::google::protobuf::Message&) {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
}

void SerializedResponse::MergeFrom(const SerializedResponse&) {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
}

void SerializedResponse::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    const SerializedResponse* source = dynamic_cast<const SerializedResponse*>(&from);
    if (source == NULL) {
        CHECK(false) << "SerializedResponse can only CopyFrom SerializedResponse";
    } else {
        _serialized = source->_serialized;
    }
}

void SerializedResponse::CopyFrom(const SerializedResponse& from) {
    if (&from == this) return;
    _serialized = from._serialized;
}

bool SerializedResponse::IsInitialized() const {
    // Always true because it's already serialized.
    return true;
}

void SerializedResponse::Swap(SerializedResponse* other) {
    if (other != this) {
        _serialized.swap(other->_serialized);
    }
}

::google::protobuf::Metadata SerializedResponse::GetMetadata() const {
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = SerializedResponse::descriptor();
    metadata.reflection = NULL;
    return metadata;
}

} // namespace brpc
