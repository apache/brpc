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

#ifndef BRPC_FLATBUFFERS_MESSAGE_H
#define BRPC_FLATBUFFERS_MESSAGE_H

#include "butil/config.h"

#if BRPC_WITH_FLATBUFFERS
#include <cstddef>
#include <cstdint>
#include <utility>
#include <flatbuffers/flatbuffers.h>

#if !defined(FLATBUFFERS_VERSION_MAJOR) || \
    !defined(FLATBUFFERS_VERSION_MINOR) || \
    !defined(FLATBUFFERS_VERSION_REVISION) || \
    FLATBUFFERS_VERSION_MAJOR != 25 || \
    FLATBUFFERS_VERSION_MINOR != 2 || \
    FLATBUFFERS_VERSION_REVISION != 10
#error "bRPC FlatBuffers support requires FlatBuffers 25.2.10"
#endif

#include "butil/macros.h"
#include "butil/single_iobuf.h"
#include "brpc/nonreflectable_message.h"

namespace brpc {
namespace flatbuffers {

// Space preceding a payload, available to a future RPC transport.
constexpr uint32_t kDefaultMetaSize = 64;
// IOBuf slices need not start at an aligned address. The allocator corrects it.
constexpr size_t kBufferAlignment = 64;

class MessageBuilder;

// FlatBufferBuilder cannot recover from a null allocation. Allocation failure
// and sizes outside FlatBuffers' offset range are fatal, even in release
// builds.
class SlabAllocator : public ::flatbuffers::Allocator {
public:
    SlabAllocator() : _data(nullptr), _capacity(0) {}
    SlabAllocator(SlabAllocator&& other) noexcept : SlabAllocator() {
        swap(other);
    }
    SlabAllocator& operator=(SlabAllocator&& other) noexcept;

    uint8_t* allocate(size_t size) override;
    void deallocate(uint8_t* p, size_t size) override;
    uint8_t* reallocate_downward(uint8_t* old_p, size_t old_size,
                                 size_t new_size, size_t in_use_back,
                                 size_t in_use_front) override;
    void swap(SlabAllocator& other) noexcept;

private:
    DISALLOW_COPY_AND_ASSIGN(SlabAllocator);
friend class MessageBuilder;
    butil::SingleIOBuf _iobuf;
    uint8_t* _data;
    size_t _capacity;
};

// Construct the allocator before the FlatBufferBuilder base uses it, and
// destroy it after that base has returned its buffer.
struct SlabAllocatorMember {
    SlabAllocator slab_allocator_;
};

// A move-only message for application code. Protobuf CopyFrom/MergeFrom retain
// the same ref-counted IOBuf instead of deep-copying it. Mutable access through
// any alias changes every message sharing that storage and must not race
// readers.
// Parsing checks framing, not the schema: Verify<T>() must succeed before
// reading data received from an untrusted peer.
class Message : public NonreflectableMessage<Message> {
public:
    Message() : _meta_size(0), _msg_size(0) {}
    Message(Message&& other) noexcept : Message() { Swap(other); }
    Message& operator=(Message&& other) noexcept;

    void MergeFrom(const Message&) override;
    void Clear() override;
    void Swap(Message& other) noexcept;

    const uint8_t* data() const;
    // These mutable pointers may refer to storage shared by CopyFrom/MergeFrom.
    void* mutable_data() { return const_cast<uint8_t*>(data()); }
    void* mutable_buf_begin() {
        return const_cast<void*>(_iobuf.get_begin());
    }
    void* reduce_meta_size_and_get_buf(uint32_t new_size);
    uint32_t get_meta_size() const { return _meta_size; }
    size_t size() const { return _msg_size; }

    template <typename T>
    bool Verify() const {
        if (!data() || size() < sizeof(::flatbuffers::uoffset_t) ||
            size() >= FLATBUFFERS_MAX_BUFFER_SIZE) {
            return false;
        }
        ::flatbuffers::Verifier verifier(data(), size());
        return verifier.VerifyBuffer<T>(nullptr);
    }

    // These accessors require a schema-verified buffer. GetMutableRoot may
    // mutate all Message aliases that retain the same IOBuf block.
    template <typename T> const T* GetRoot() const {
        return data() ? ::flatbuffers::GetRoot<T>(data()) : nullptr;
    }
    template <typename T> T* GetMutableRoot() {
        return data()
            ? ::flatbuffers::GetMutableRoot<T>(mutable_data()) : nullptr;
    }

    // Failure leaves the old message unchanged. A fragmented or unaligned
    // payload is copied into aligned storage; aligned contiguous input is
    // shared.
    // When msg_size comes from an untrusted peer, callers must bound it with
    // their own max-message-size policy before parsing. This method does not
    // verify the schema; call Verify<T>() before reading the root.
    bool parse_msg_from_iobuf(const butil::IOBuf& buf, size_t msg_size,
                              size_t meta_size);
    bool append_msg_to_iobuf(butil::IOBuf& buf) const;

private:
    DISALLOW_COPY_AND_ASSIGN(Message);
friend class MessageBuilder;
    Message(const butil::IOBuf::BlockRef& ref, uint32_t meta_size,
            uint32_t msg_size);
    butil::SingleIOBuf _iobuf;
    uint32_t _meta_size;
    uint32_t _msg_size;
};

class MessageBuilder : private SlabAllocatorMember,
                       public ::flatbuffers::FlatBufferBuilder {
public:
    explicit MessageBuilder(size_t initial_size = 1024);
    MessageBuilder(MessageBuilder&& other);
    MessageBuilder& operator=(MessageBuilder&& other);

    // Importing a foreign builder copies its payload and scratch data,
    // retaining build state. Its original allocator frees the source
    // allocation correctly. No free()/delete[] guess or spare-prefix
    // assumption is made.
    explicit MessageBuilder(::flatbuffers::FlatBufferBuilder&& src);
    MessageBuilder& operator=(::flatbuffers::FlatBufferBuilder&& src);

    void Swap(MessageBuilder& other);
    // Requires Finish(). The returned message shares storage without copying;
    // both this builder and a moved-from builder may immediately be reused.
    Message ReleaseMessage();

private:
    DISALLOW_COPY_AND_ASSIGN(MessageBuilder);
    void ClearStringPool();
    // The inherited release methods would retain a pointer to our member
    // allocator after this builder dies. Only ReleaseMessage is supported.
    using ::flatbuffers::FlatBufferBuilder::Release;
    using ::flatbuffers::FlatBufferBuilder::ReleaseRaw;
    void ReleaseBufferPointer() = delete;
    using ::flatbuffers::FlatBufferBuilder::SwapBufAllocator;
};

bool ParseFbFromIOBUF(Message* msg, size_t msg_size, const butil::IOBuf& buf,
                      size_t meta_size = 0);
bool SerializeFbToIOBUF(const Message* msg, butil::IOBuf& buf);

}  // namespace flatbuffers
}  // namespace brpc
#endif  // BRPC_WITH_FLATBUFFERS
#endif  // BRPC_FLATBUFFERS_MESSAGE_H
