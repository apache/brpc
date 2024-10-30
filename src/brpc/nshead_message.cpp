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

#include "brpc/nshead_message.h"

#include "brpc/proto_base.pb.h"
#include "butil/logging.h"

namespace brpc {

NsheadMessage::NsheadMessage()
    : NonreflectableMessage<NsheadMessage>() {
    SharedCtor();
}

NsheadMessage::NsheadMessage(const NsheadMessage& from)
    : NonreflectableMessage<NsheadMessage>() {
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

void NsheadMessage::Clear() {
    memset(&head, 0, sizeof(head));
    body.clear();
}

size_t NsheadMessage::ByteSizeLong() const {
    return sizeof(nshead_t) + body.size();
}

void NsheadMessage::MergeFrom(const ::google::protobuf::Message& from) {
    CHECK_NE(&from, this);
    const NsheadMessage* source = dynamic_cast<const NsheadMessage*>(&from);
    if (source == NULL) {
        LOG(ERROR) << "Can only merge from NsheadMessage";
        return;
    } else {
        MergeFrom(*source);
    }
}

void NsheadMessage::MergeFrom(const NsheadMessage& from) {
    CHECK_NE(&from, this);
    // No way to merge two nshead messages, just overwrite.
    head = from.head;
    body = from.body;
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
    ::google::protobuf::Metadata metadata{};
    metadata.descriptor = NsheadMessageBase::descriptor();
    metadata.reflection = nullptr;
    return metadata;
}

} // namespace brpc
