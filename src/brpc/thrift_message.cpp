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


#define INTERNAL_SUPPRESS_PROTOBUF_FIELD_DEPRECATION
#include "brpc/thrift_message.h"

#include <algorithm>
#include "butil/logging.h"

#include <google/protobuf/stubs/once.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/reflection_ops.h>
#include <google/protobuf/wire_format.h>

namespace brpc {

ThriftFramedMessage::ThriftFramedMessage()
    : ::google::protobuf::Message() {
    SharedCtor();
}

void ThriftFramedMessage::SharedCtor() {
    field_id = THRIFT_INVALID_FID;
    _own_raw_instance = false;
    _raw_instance = nullptr;
}

ThriftFramedMessage::~ThriftFramedMessage() {
    SharedDtor();
    if (_own_raw_instance) {
        delete _raw_instance;
    }
}

void ThriftFramedMessage::SharedDtor() {
}

const ::google::protobuf::Descriptor* ThriftFramedMessage::descriptor() {
    return ThriftFramedMessageBase::descriptor();
}

ThriftFramedMessage* ThriftFramedMessage::New() const {
    return new ThriftFramedMessage;
}

#if GOOGLE_PROTOBUF_VERSION >= 3006000
ThriftFramedMessage*
ThriftFramedMessage::New(::google::protobuf::Arena* arena) const {
    return CreateMaybeMessage<ThriftFramedMessage>(arena);
}
#endif

void ThriftFramedMessage::Clear() {
    body.clear();
    if (_own_raw_instance) {
        delete _raw_instance;
        _own_raw_instance = false;
        _raw_instance = NULL;
    }
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
    if (_raw_instance) {
        LOG(ERROR) << "ByteSize() is always 0 when _raw_instance is set";
        return 0;
    }
    return body.size();
}

void ThriftFramedMessage::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    LOG(ERROR) << "ThriftFramedMessage does not support MergeFrom";
}

void ThriftFramedMessage::MergeFrom(const ThriftFramedMessage& from) {
    GOOGLE_CHECK_NE(&from, this);
    LOG(ERROR) << "ThriftFramedMessage does not support MergeFrom";
}

void ThriftFramedMessage::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    LOG(ERROR) << "ThriftFramedMessage does not support CopyFrom";
}

void ThriftFramedMessage::CopyFrom(const ThriftFramedMessage& from) {
    if (&from == this) return;
    LOG(ERROR) << "ThriftFramedMessage does not support CopyFrom";
}

bool ThriftFramedMessage::IsInitialized() const {
    return true;
}

void ThriftFramedMessage::Swap(ThriftFramedMessage* other) {
    if (other != this) {
        body.swap(other->body);
        std::swap(field_id, other->field_id);
        std::swap(_own_raw_instance, other->_own_raw_instance);
        std::swap(_raw_instance, other->_raw_instance);
    }
}

::google::protobuf::Metadata ThriftFramedMessage::GetMetadata() const {
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = ThriftFramedMessage::descriptor();
    metadata.reflection = NULL;
    return metadata;
}

void ThriftStub::CallMethod(const char* method_name,
                            Controller* cntl,
                            const ThriftFramedMessage* req,
                            ThriftFramedMessage* res,
                            ::google::protobuf::Closure* done) {
    cntl->_thrift_method_name.assign(method_name);
    _channel->CallMethod(NULL, cntl, req, res, done);
}

} // namespace brpc
