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


#include "brpc/serialized_request.h"
#include "butil/logging.h"

namespace brpc {

SerializedRequest::SerializedRequest()
    : ::google::protobuf::Message() {
    SharedCtor();
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
}

void SerializedRequest::SetCachedSize(int /*size*/) const {
    CHECK(false) << "You're not supposed to call " << __FUNCTION__;
}
const ::google::protobuf::Descriptor* SerializedRequest::descriptor() {
    return SerializedRequestBase::descriptor();
}

SerializedRequest* SerializedRequest::New() const {
    return new SerializedRequest;
}

#if GOOGLE_PROTOBUF_VERSION >= 3006000
SerializedRequest*
SerializedRequest::New(::google::protobuf::Arena* arena) const {
    return CreateMaybeMessage<SerializedRequest>(arena);
}
#endif

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
    const SerializedRequest* source = dynamic_cast<const SerializedRequest*>(&from);
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
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = SerializedRequest::descriptor();
    metadata.reflection = NULL;
    return metadata;
}

} // namespace brpc
