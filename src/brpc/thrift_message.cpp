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

#include "butil/logging.h"

namespace brpc {

ThriftFramedMessage::ThriftFramedMessage()
    : NonreflectableMessage<ThriftFramedMessage>() {
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

void ThriftFramedMessage::Clear() {
    body.clear();
    if (_own_raw_instance) {
        delete _raw_instance;
        _own_raw_instance = false;
        _raw_instance = NULL;
    }
}

size_t ThriftFramedMessage::ByteSizeLong() const {
    if (_raw_instance) {
        LOG(ERROR) << "ByteSize() is always 0 when _raw_instance is set";
        return 0;
    }
    return body.size();
}

void ThriftFramedMessage::MergeFrom(const ThriftFramedMessage& from) {
    CHECK_NE(&from, this);
    LOG(ERROR) << "ThriftFramedMessage does not support MergeFrom";
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
    ::google::protobuf::Metadata metadata{};
    metadata.descriptor = ThriftFramedMessageBase::descriptor();
    metadata.reflection = nullptr;
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
