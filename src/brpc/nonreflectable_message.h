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

#ifndef BRPC_NONREFLECTABLE_MESSAGE_H
#define BRPC_NONREFLECTABLE_MESSAGE_H

#include <google/protobuf/generated_message_reflection.h>
#include <google/protobuf/message.h>

#include "pb_compat.h"

namespace brpc {

//
// In bRPC, some non-Protobuf based protocol messages are also designed to implement
// Protobuf Message interfaces, to provide a unified protocol message.
// The API of Protobuf Message changes frequently, and these non-Protobuf based protocol
// messages do not rely on the reflection functionality of Protobuf.
//
// NonreflectableMessage is designed to isolate upstream API changes and
// provides basic implementations to simplify the adaptation process.
//
// Function implementations are kept order with the upstream,
// and use only #if version_check #endif, to make maintenance easier.
//
template <typename T>
class NonreflectableMessage : public ::google::protobuf::Message {
public:
    inline NonreflectableMessage() = default;
    inline NonreflectableMessage(const NonreflectableMessage&) : NonreflectableMessage() {}
    inline NonreflectableMessage& operator=(const NonreflectableMessage& other) {
        CopyFrom(other);
        return *this;
    }
    inline NonreflectableMessage& operator=(const T& other) {
        CopyFrom(other);
        return *this;
    }

#if GOOGLE_PROTOBUF_VERSION >= 5029000
    const ::google::protobuf::internal::ClassData* GetClassData() const override {
        return nullptr;
    }
#elif GOOGLE_PROTOBUF_VERSION >= 5026000
    const ClassData* GetClassData() const override {
        return nullptr;
    }
#endif

#if GOOGLE_PROTOBUF_VERSION < 3019000
    Message* New() const override {
        return new T();
    }
#endif

#if GOOGLE_PROTOBUF_VERSION >= 3000000 && GOOGLE_PROTOBUF_VERSION < 5029000
    Message* New(::google::protobuf::Arena* arena) const override {
        return ::google::protobuf::Arena::Create<T>(arena);
    }
#endif

    void CopyFrom(const ::google::protobuf::Message& other) PB_321_OVERRIDE {
        if (&other == this) {
            return;
        }
        Clear();
        MergeFrom(other);
    }

    inline void CopyFrom(const NonreflectableMessage& other) {
        if (&other == this) {
            return;
        }
        Clear();
        MergeFrom(other);
    }

    void MergeFrom(const ::google::protobuf::Message& other) PB_526_OVERRIDE {
        if (&other == this) {
            return;
        }

        // Cross-type merging is meaningless, call implementation of subclass
#if GOOGLE_PROTOBUF_VERSION >= 3007000
        const T* same_type_other = ::google::protobuf::DynamicCastToGenerated<T>(&other);
#elif GOOGLE_PROTOBUF_VERSION >= 3000000
        const T* same_type_other = ::google::protobuf::internal::DynamicCastToGenerated<const T>(&other);
#endif // GOOGLE_PROTOBUF_VERSION
        if (same_type_other != nullptr) {
            MergeFrom(*same_type_other);
        } else {
            Message::MergeFrom(other);
        }
    }

    virtual void MergeFrom(const T&) = 0;

#if GOOGLE_PROTOBUF_VERSION > 3019000 && GOOGLE_PROTOBUF_VERSION < 5026000
    // Unsupported by default.
    std::string InitializationErrorString() const override {
        return "unknown error";
    }
#endif

#if GOOGLE_PROTOBUF_VERSION < 3019000
    // Unsupported by default.
    void DiscardUnknownFields() override {}
#endif

#if GOOGLE_PROTOBUF_VERSION < 5026000
    // Unsupported by default.
    size_t SpaceUsedLong() const override {
        return 0;
    }
#endif

    // Unsupported by default.
    ::std::string GetTypeName() const PB_526_OVERRIDE {
        return {};
    }

    void Clear() override {}

#if GOOGLE_PROTOBUF_VERSION < 3010000
    bool MergePartialFromCodedStream(::google::protobuf::io::CodedInputStream*) override {
        return true;
    }
#endif

    // Quickly check if all required fields have values set.
    // Unsupported by default.
    bool IsInitialized() const PB_527_OVERRIDE {
        return true;
    }

#if GOOGLE_PROTOBUF_VERSION >= 3010000 && GOOGLE_PROTOBUF_VERSION <= 5026000
    const char* _InternalParse(
            const char* ptr, ::google::protobuf::internal::ParseContext*) override {
        return ptr;
    }
#endif

    // Size of bytes after serialization.
    size_t ByteSizeLong() const override {
        return 0;
    }

#if GOOGLE_PROTOBUF_VERSION >= 3007000 && GOOGLE_PROTOBUF_VERSION < 3010000
    void SerializeWithCachedSizes(::google::protobuf::io::CodedOutputStream*) const override {}
#endif

#if GOOGLE_PROTOBUF_VERSION >= 3010000 && GOOGLE_PROTOBUF_VERSION < 3011000
    uint8_t* InternalSerializeWithCachedSizesToArray(
            uint8_t* ptr, ::google::protobuf::io::EpsCopyOutputStream*) const override {
        return ptr;
    }
#endif

#if GOOGLE_PROTOBUF_VERSION >= 3011000
    uint8_t* _InternalSerialize(
            uint8_t* ptr, ::google::protobuf::io::EpsCopyOutputStream*) const override {
        return ptr;
    }
#endif

#if GOOGLE_PROTOBUF_VERSION < 4025000
    // Unnecessary for Nonreflectable message.
    int GetCachedSize() const override {
        return 0;
    }
#endif

#if GOOGLE_PROTOBUF_VERSION < 4025000
    // Unnecessary for Nonreflectable message.
    void SetCachedSize(int) const override {}
#endif

public:
    // Only can be used to determine whether the Types are the same.
    ::google::protobuf::Metadata GetMetadata() const PB_527_OVERRIDE {
        ::google::protobuf::Metadata metadata{};
        // can only be used to
        metadata.descriptor = reinterpret_cast<const ::google::protobuf::Descriptor*>(&_instance);
        metadata.reflection = reinterpret_cast<const ::google::protobuf::Reflection*>(&_instance);
        return metadata;
    }

    // Only can be used to determine whether the Types are the same.
    inline static const ::google::protobuf::Descriptor* descriptor() noexcept {
        return default_instance().GetMetadata().descriptor;
    }

    inline static const T& default_instance() noexcept {
        return _instance;
    }

private:
    static T _instance;
};

template <typename T>
T NonreflectableMessage<T>::_instance;

} // namespace brpc

#endif // BRPC_NONREFLECTABLE_MESSAGE_H
