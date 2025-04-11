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


#ifndef BRPC_COMPRESS_H
#define BRPC_COMPRESS_H

#include <google/protobuf/message.h>              // Message
#include "butil/iobuf.h"                           // butil::IOBuf
#include "butil/logging.h"
#include "brpc/options.pb.h"                     // CompressType
#include "brpc/nonreflectable_message.h"

namespace brpc {

// Serializer can be used to implement custom serialization
// before compression with user callback.
class Serializer : public NonreflectableMessage<Serializer> {
public:
    using Callback = std::function<bool(google::protobuf::io::ZeroCopyOutputStream*)>;

    Serializer() :Serializer(NULL) {}

    explicit Serializer(Callback callback)
        :_callback(std::move(callback)) {
        SharedCtor();
    }

    ~Serializer() override {
        SharedDtor();
    }

    Serializer(const Serializer& from)
        : NonreflectableMessage(from) {
        SharedCtor();
        MergeFrom(from);
    }

    Serializer& operator=(const Serializer& from) {
        CopyFrom(from);
        return *this;
    }

    void Swap(Serializer* other) {
        if (other != this) {
        }
    }

    void MergeFrom(const Serializer& from) override {
        CHECK_NE(&from, this);
    }

    // implements Message ----------------------------------------------
    void Clear() override {
        _callback = nullptr;
    }
    size_t ByteSizeLong() const override { return 0; }
    int GetCachedSize() const PB_425_OVERRIDE { return ByteSize(); }

    ::google::protobuf::Metadata GetMetadata() const PB_527_OVERRIDE;

    // Converts the data into `output' for later compression.
    bool SerializeTo(google::protobuf::io::ZeroCopyOutputStream* output) const {
        if (!_callback) {
            LOG(WARNING) << "Serializer::SerializeTo() called without callback";
            return false;
        }
        return _callback(output);
    }

    void SetCallback(Callback callback) {
        _callback = std::move(callback);
    }

private:
    void SharedCtor() {}
    void SharedDtor() {}

    Callback _callback;
};

// Deserializer can be used to implement custom deserialization
// after decompression with user callback.
class Deserializer : public NonreflectableMessage<Deserializer> {
public:
public:
    using Callback = std::function<bool(google::protobuf::io::ZeroCopyInputStream*)>;

    Deserializer() :Deserializer(NULL) {}

    explicit Deserializer(Callback callback) : _callback(std::move(callback)) {
        SharedCtor();
    }

    ~Deserializer() override {
        SharedDtor();
    }

    Deserializer(const Deserializer& from)
        : NonreflectableMessage(from) {
        SharedCtor();
        MergeFrom(from);
    }

    Deserializer& operator=(const Deserializer& from) {
        CopyFrom(from);
        return *this;
    }

    void Swap(Deserializer* other) {
        if (other != this) {
            _callback.swap(other->_callback);
        }
    }

    void MergeFrom(const Deserializer& from) override {
        CHECK_NE(&from, this);
        _callback = from._callback;
    }

    // implements Message ----------------------------------------------
    void Clear() override { _callback = nullptr; }
    size_t ByteSizeLong() const override { return 0; }
    int GetCachedSize() const PB_425_OVERRIDE { return ByteSize(); }

    ::google::protobuf::Metadata GetMetadata() const PB_527_OVERRIDE;

    // Converts the decompressed `input'.
    bool DeserializeFrom(google::protobuf::io::ZeroCopyInputStream* intput) const {
        if (!_callback) {
            LOG(WARNING) << "Deserializer::DeserializeFrom() called without callback";
            return false;
        }
        return _callback(intput);
    }
    void SetCallback(Callback callback) {
        _callback = std::move(callback);
    }

private:
    void SharedCtor() {}
    void SharedDtor() {}

    Callback _callback;
};

struct CompressHandler {
    // Compress serialized `msg' into `buf'.
    // Returns true on success, false otherwise
    bool (*Compress)(const google::protobuf::Message& msg, butil::IOBuf* buf);

    // Parse decompressed `data' as `msg'.
    // Returns true on success, false otherwise
    bool (*Decompress)(const butil::IOBuf& data, google::protobuf::Message* msg);

    // Name of the compression algorithm, must be string constant.
    const char* name;
};

// [NOT thread-safe] Register `handler' using key=`type'
// Returns 0 on success, -1 otherwise
int RegisterCompressHandler(CompressType type, CompressHandler handler);

// Returns CompressHandler pointer of `type' if registered, NULL otherwise.
const CompressHandler* FindCompressHandler(CompressType type);

// Returns the `name' of the CompressType if registered
const char* CompressTypeToCStr(CompressType type);

// Put all registered handlers into `vec'.
void ListCompressHandler(std::vector<CompressHandler>* vec);

// Parse decompressed `data' as `msg' using registered `compress_type'.
// Returns true on success, false otherwise
bool ParseFromCompressedData(const butil::IOBuf& data,
                             google::protobuf::Message* msg,
                             CompressType compress_type);

// Compress serialized `msg' into `buf' using registered `compress_type'.
// Returns true on success, false otherwise
bool SerializeAsCompressedData(const google::protobuf::Message& msg,
                               butil::IOBuf* buf,
                               CompressType compress_type);

} // namespace brpc


#endif // BRPC_COMPRESS_H
