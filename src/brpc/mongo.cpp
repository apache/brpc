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

#include "brpc/mongo.h"

#include <google/protobuf/reflection_ops.h>     // ReflectionOps::Merge

#include "brpc/proto_base.pb.h"
#include "butil/logging.h"
#include "mongo_head.h"

namespace brpc {

bool MongoMessage::SerializeToIOBuf(butil::IOBuf *buf) const {
    // flag bits
    uint32_t flag_bits = butil::ByteSwapToLE32(_flag_bits);
    buf->append(&flag_bits, sizeof(flag_bits));

    // body section
    uint8_t payload_type = MONGO_PAYLOAD_TYPE_BODY;
    buf->append(&payload_type, sizeof(payload_type));
    buf->append(bson_get_data(_body.get()), _body->len);

    // data section
    if (_key.empty() || _doc_sequence.empty()) {
        return true;
    }
    if (bson_has_field(_body.get(), _key.c_str())) {
        return false;
    }

    payload_type = MONGO_PAYLOAD_TYPE_DOC_SEQUENCE;
    uint8_t section_length = 4 + _key.size() + 1;
    for (const UniqueBsonPtr& doc : _doc_sequence) {
        section_length += doc->len;
    }
    section_length = butil::ByteSwapToLE32(section_length);
    buf->append(&payload_type, sizeof(payload_type));
    buf->append(&section_length, sizeof(section_length));
    buf->append(_key.c_str(), _key.size() + 1);
    for (const UniqueBsonPtr& doc : _doc_sequence) {
        buf->append(bson_get_data(doc.get()), doc->len);
    }
    return true;
}

bool MongoMessage::ParseFromIOBuf(butil::IOBuf* payload) {
    bool success = false;
    do {
        // TODO: support checksum
        payload->cutn(&_flag_bits, sizeof(_flag_bits));
        _flag_bits = butil::ByteSwapToLE32(_flag_bits);
        if (_flag_bits != 0) {
            LOG(WARNING) << "current only zero flag is supported";
            break;
        }

        // parse first section
        uint8_t payload_type;
        payload->cutn(&payload_type, sizeof(payload_type));
        if (payload_type != MONGO_PAYLOAD_TYPE_BODY) {
            break;
        }

        _body = butil::bson::ExtractBsonFromIOBuf(*payload);
        if (!_body) {
            break;
        }
        payload->pop_front(_body->len);

        if (payload->size() == 0) {
            success = true;
            break;
        }

        // parse second section
        payload->cutn(&payload_type, sizeof(payload_type));
        if (payload_type != MONGO_PAYLOAD_TYPE_DOC_SEQUENCE) {
            break;
        }

        uint32_t section_length = 0;
        payload->cutn(&section_length, sizeof(section_length));
        section_length = butil::ByteSwapToLE32(section_length);
        if (section_length != sizeof(section_length) + payload->size()) {
            break;
        }

        char c = '\0';
        while (payload->cut1(&c) && c != '\0') {
            _key.push_back(c);
        }
        if (c == '\0') {
            _key.push_back(c);
        } else {
            break;
        }

        butil::bson::BsonEnumerator enumerator(payload);
        while (const bson_t* doc = enumerator.Next()) {
            _doc_sequence.emplace_back(bson_copy(doc));
        };
        if (!enumerator.HasError()) {
            success = true;
        }
    } while (false);

    return success;
}

void MongoMessage::Print(std::ostream& os) const {
    os << "{";
    do {
        if (!_body) {
            break;
        }

        if (_head.message_length != 0) {
            os << " \"message_length\" : " << _head.message_length << ",";
            os << " \"request_id\" : " << _head.request_id << ",";
            os << " \"response_to\" : " << _head.response_to << ",";
            os << " \"op_code\" : " << _head.op_code << ",";
        }

        os << " \"flag_bits\" : " << _flag_bits << ",";
        os << " \"body\" : ";
        char *json = bson_as_json(_body.get(), nullptr);
        os << json;
        bson_free(json);

        if (_key.empty() || _doc_sequence.empty()) {
            break;
        }
        os << ", \"" << _key << "\" : [";
        size_t i = 0;
        for (const UniqueBsonPtr& doc : _doc_sequence) {
            json = bson_as_json(doc.get(), nullptr);
            os << json;
            ++i;
            if (i != _doc_sequence.size()) {
                os << ", ";
            }
            bson_free(json);
        }
        os << "]";
    } while (false);
    os << "}";
}

MongoMessage::MongoMessage()
    : ::google::protobuf::Message() {
    SharedCtor();
}

MongoMessage::MongoMessage(const MongoMessage& from)
    : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

MongoMessage::~MongoMessage() {
    SharedDtor();
}

void MongoMessage::SharedCtor() {
    _cached_size = 0;
    memset(&_head, 0, sizeof(_head));
    _flag_bits = 0;
}

void MongoMessage::SharedDtor() {
}

void MongoMessage::SetCachedSize(int size) const {
    _cached_size = size;
}

MongoMessage* MongoMessage::New() const {
    return new MongoMessage;
}

#if GOOGLE_PROTOBUF_VERSION >= 3006000
MongoMessage* MongoMessage::New(::google::protobuf::Arena* arena) const {
    return CreateMaybeMessage<MongoMessage>(arena);
}
#endif

void MongoMessage::Clear() {
    _cached_size = 0;
    memset(&_head, 0, sizeof(_head));
    _flag_bits = 0;
    _body.reset();
    _key.clear();
    _doc_sequence.clear();
}

bool MongoMessage::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream*) {
    LOG(WARNING) << "You're not supposed to parse a MongoMessage";
    return true;
}

void MongoMessage::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream*) const {
    LOG(WARNING) << "You're not supposed to serialize a MongoMessage";
}

::google::protobuf::uint8* MongoMessage::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    LOG(WARNING) << "You're not supposed to serialize a MongoMessage";
    return target;
}

int MongoMessage::ByteSize() const {
    int total_size = 0;
    do {
        if (!_body) {
            break;
        }
        // header + flag_bits(4) + section_kind(1) + body
        total_size += sizeof(_head) + 5 + _body->len;
        // section_kind(1) + section_length(4) + doc_sequence
        if (!_key.empty() && !_doc_sequence.empty()) {
            total_size += 5;
            for (const UniqueBsonPtr& doc : _doc_sequence) {
                total_size += doc->len;
            }
        }
    } while (false);
    _cached_size = total_size;
    return total_size;
}

void MongoMessage::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const MongoMessage* source = dynamic_cast<const MongoMessage*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void MongoMessage::MergeFrom(const MongoMessage& from) {
    GOOGLE_CHECK_NE(&from, this);
    if (_body || !_doc_sequence.empty()) {
        LOG(WARNING) << "You're not supposed to merge a non-empty MongoMessage";
        return;
    }
    _head = from._head;
    _flag_bits = from._flag_bits;
    _body.reset(bson_copy(from._body.get()));
    _key = from._key;
    for (const UniqueBsonPtr& doc : from._doc_sequence) {
        _doc_sequence.emplace_back(bson_copy(doc.get()));
    }
    _cached_size = from._cached_size;
}

void MongoMessage::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

void MongoMessage::CopyFrom(const MongoMessage& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

bool MongoMessage::IsInitialized() const {
    return _body != nullptr;
}

void MongoMessage::Swap(MongoMessage* other) {
    if (other != this) {
        std::swap(_head, other->_head);
        std::swap(_flag_bits, other->_flag_bits);
        std::swap(_body, other->_body);
        std::swap(_key, other->_key);
        std::swap(_doc_sequence, other->_doc_sequence);
    }
}

const ::google::protobuf::Descriptor* MongoMessage::descriptor() {
    return MongoMessageBase::descriptor();
}

::google::protobuf::Metadata MongoMessage::GetMetadata() const {
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = MongoMessage::descriptor();
    metadata.reflection = NULL;
    return metadata;
}

std::ostream& operator<<(std::ostream& os, const MongoMessage& msg) {
    msg.Print(os);
    return os;
}

}  // namespace brpc
