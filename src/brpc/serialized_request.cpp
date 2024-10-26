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

#include "brpc/proto_base.pb.h"
#include "butil/logging.h"

namespace brpc {

SerializedRequest::SerializedRequest()
    : NonreflectableMessage<SerializedRequest>() {
    SharedCtor();
}

SerializedRequest::SerializedRequest(const SerializedRequest& from)
    : NonreflectableMessage<SerializedRequest>() {
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

void SerializedRequest::Clear() {
    _serialized.clear();
}

size_t SerializedRequest::ByteSizeLong() const {
    return _serialized.size();
}

void SerializedRequest::MergeFrom(const SerializedRequest& from) {
    CHECK_NE(&from, this);
    _serialized = from._serialized;
}

void SerializedRequest::Swap(SerializedRequest* other) {
    if (other != this) {
        _serialized.swap(other->_serialized);
    }
}

::google::protobuf::Metadata SerializedRequest::GetMetadata() const {
    ::google::protobuf::Metadata metadata{};
    metadata.descriptor = SerializedRequestBase::descriptor();
    metadata.reflection = nullptr;
    return metadata;
}

} // namespace brpc
