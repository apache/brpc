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

#include "brpc/proto_base.pb.h"
#include "butil/logging.h"

namespace brpc {

EspMessage::EspMessage()
    : NonreflectableMessage<EspMessage>() {
    SharedCtor();
}

void EspMessage::SharedCtor() {
    memset(&head, 0, sizeof(head));
}

EspMessage::~EspMessage() {
    SharedDtor();
}

void EspMessage::SharedDtor() {
}

void EspMessage::Clear() {
    head.body_len = 0;
    body.clear();
}

size_t EspMessage::ByteSizeLong() const {
    return sizeof(head) + body.size();
}

void EspMessage::MergeFrom(const EspMessage& from) {
    CHECK_NE(&from, this);
    head = from.head;
    body = from.body;
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
    ::google::protobuf::Metadata metadata{};
    metadata.descriptor = EspMessageBase::descriptor();
    metadata.reflection = nullptr;
    return metadata;
}

} // namespace brpc
