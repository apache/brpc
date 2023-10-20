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

#ifndef BRPC_MONGO_H
#define BRPC_MONGO_H

#include <google/protobuf/message.h>

#include "brpc/mongo_head.h"
#include "brpc/pb_compat.h"
#include "brpc/parse_result.h"
#include "butil/iobuf.h"
#include "butil/bson_util.h"

namespace brpc {


class MongoMessage : public ::google::protobuf::Message {
public:
    MongoMessage();
    virtual ~MongoMessage();
    MongoMessage(const MongoMessage& from);
    MongoMessage& operator=(const MongoMessage& from) {
        CopyFrom(from);
        return *this;
    };
    void Swap(MongoMessage* other);

    mongo_head_t& head() {
        return _head;
    }

    const mongo_head_t& head() const {
        return _head;
    }

    const uint32_t& flag_bits() const {
        return _flag_bits;
    }

    void set_flag_bits(int32_t flag_bits) {
        _flag_bits = flag_bits;
    }

    const std::string& key() const {
        return _key;
    }

    void set_key(std::string key) {
        _key = std::move(key);
    }

    const bson_t* body() const {
        return _body.get();
    }

    void set_body(bson_t* body) {
        _body.reset(bson_copy(body));
    }

    void set_body(butil::bson::UniqueBsonPtr&& body) {
        _body.reset(body.release());
    }

    bson_t* doc_sequence(size_t i) {
        return _doc_sequence[i].get();
    }

    size_t doc_sequence_size() {
        return _doc_sequence.size();
    }

    void add_doc_sequence() {
        _doc_sequence.emplace_back(bson_new());
    }

    void add_doc_sequence(butil::bson::UniqueBsonPtr&& doc) {
        _doc_sequence.emplace_back(std::move(doc));
    }

    // Serialize/Parse |Payload| from IOBuf
    bool ParseFromIOBuf(butil::IOBuf* payload);
    bool SerializeToIOBuf(butil::IOBuf* buf) const;

    // Protobuf methods.
    MongoMessage* New() const PB_319_OVERRIDE;
#if GOOGLE_PROTOBUF_VERSION >= 3006000
    MongoMessage* New(::google::protobuf::Arena* arena) const override;
#endif
    void CopyFrom(const ::google::protobuf::Message& from) PB_321_OVERRIDE;
    void MergeFrom(const ::google::protobuf::Message& from) override;
    void CopyFrom(const MongoMessage& from);
    void MergeFrom(const MongoMessage& from);
    void Clear() override;
    bool IsInitialized() const override;

    int ByteSize() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input) PB_310_OVERRIDE;
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const PB_310_OVERRIDE;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const PB_310_OVERRIDE;
    int GetCachedSize() const override { return _cached_size; }

    static const ::google::protobuf::Descriptor* descriptor();

    void Print(std::ostream&) const;

protected:
    ::google::protobuf::Metadata GetMetadata() const override;

private:
    using UniqueBsonPtr = butil::bson::UniqueBsonPtr;

    void SharedCtor();
    void SharedDtor();
    virtual void SetCachedSize(int size) const override;

    mongo_head_t _head;
    uint32_t _flag_bits;
    UniqueBsonPtr _body;
    std::string _key;
    std::vector<UniqueBsonPtr> _doc_sequence;

    mutable int _cached_size;   // ByteSize
};

std::ostream& operator<<(std::ostream& os, const MongoMessage&);

}  // namespace brpc
#endif  // BRPC_MONGO_H
