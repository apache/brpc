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

#include "brpc/flatbuffers/message.h"

#if BRPC_WITH_FLATBUFFERS
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include "butil/logging.h"

namespace brpc {
namespace flatbuffers {
namespace {

void CheckOrAbort(bool condition, const char* error) {
    if (BAIDU_UNLIKELY(!condition)) {
        // brpc's crash_on_fatal_log flag is false by default. A CHECK alone
        // cannot enforce FlatBuffers' non-null allocator contract.
        LOG(ERROR) << error;
        std::abort();
    }
}

uint8_t* AlignPayload(uint8_t* data) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(data);
    const size_t padding = (kBufferAlignment - address % kBufferAlignment) %
                          kBufferAlignment;
    return data + padding;
}

}  // namespace

SlabAllocator& SlabAllocator::operator=(SlabAllocator&& other) noexcept {
    if (this != &other) {
        SlabAllocator tmp(std::move(other));
        swap(tmp);
    }
    return *this;
}

void SlabAllocator::swap(SlabAllocator& other) noexcept {
    _iobuf.swap(other._iobuf);
    std::swap(_data, other._data);
    std::swap(_capacity, other._capacity);
}

uint8_t* SlabAllocator::allocate(size_t size) {
    CheckOrAbort(size > 0 && size < FLATBUFFERS_MAX_BUFFER_SIZE,
                 "Invalid FlatBuffers allocation size");
    CheckOrAbort(_data == nullptr, "Allocator already has a live buffer");
    butil::SingleIOBuf storage;
    const uint32_t allocation_size = static_cast<uint32_t>(
        size + kDefaultMetaSize + kBufferAlignment - 1);
    uint8_t* raw = static_cast<uint8_t*>(storage.allocate(allocation_size));
    CheckOrAbort(raw != nullptr, "Fail to allocate FlatBuffers storage");
    _data = AlignPayload(raw + kDefaultMetaSize);
    _capacity = size;
    _iobuf.swap(storage);
    return _data;
}

void SlabAllocator::deallocate(uint8_t* p, size_t /*size*/) {
    if (!p) {
        return;
    }
    CheckOrAbort(p == _data, "Invalid FlatBuffers deallocation pointer");
    _iobuf.reset();
    _data = nullptr;
    _capacity = 0;
}

uint8_t* SlabAllocator::reallocate_downward(
    uint8_t* old_p, size_t old_size, size_t new_size,
    size_t in_use_back, size_t in_use_front) {
    CheckOrAbort(old_p != nullptr && old_p == _data && old_size == _capacity,
                 "Invalid FlatBuffers reallocation buffer");
    CheckOrAbort(new_size > old_size, "FlatBuffers reallocation must grow");
    CheckOrAbort(in_use_back <= old_size && in_use_front <= old_size - in_use_back,
                 "Invalid FlatBuffers scratch or payload size");
    // Keep the old allocation alive until BOTH data and scratch are copied.
    SlabAllocator replacement;
    uint8_t* data = replacement.allocate(new_size);
    if (in_use_back) {
        memcpy(data + new_size - in_use_back,
               old_p + old_size - in_use_back, in_use_back);
    }
    if (in_use_front) {
        memcpy(data, old_p, in_use_front);
    }
    swap(replacement);
    return _data;
}

Message::Message(const butil::IOBuf::BlockRef& ref, uint32_t meta_size,
                 uint32_t msg_size)
    : _iobuf(ref), _meta_size(meta_size), _msg_size(msg_size) {}

Message& Message::operator=(Message&& other) noexcept {
    if (this != &other) {
        Clear();
        Swap(other);
    }
    return *this;
}

void Message::Swap(Message& other) noexcept {
    _iobuf.swap(other._iobuf);
    std::swap(_meta_size, other._meta_size);
    std::swap(_msg_size, other._msg_size);
}

void Message::MergeFrom(const Message& /*other*/) {
    CheckOrAbort(false, "FlatBuffers Message is move-only; use move assignment");
}

void Message::Clear() {
    _iobuf.reset();
    _meta_size = 0;
    _msg_size = 0;
}

const uint8_t* Message::data() const {
    const uint8_t* raw = static_cast<const uint8_t*>(_iobuf.get_begin());
    return raw ? raw + _meta_size : nullptr;
}

bool Message::parse_msg_from_iobuf(const butil::IOBuf& buf, size_t msg_size,
                                  size_t meta_size) {
    const size_t total = buf.size();
    // Subtraction avoids overflow, and the limits cover SingleIOBuf's uint32_t
    // sizes, including allocation overhead, before any narrowing conversion.
    if (msg_size == 0 || total >= FLATBUFFERS_MAX_BUFFER_SIZE ||
        meta_size > total || msg_size != total - meta_size) {
        return false;
    }
    butil::SingleIOBuf storage;
    const butil::StringPiece first = buf.backing_block(0);
    if (first.size() == total &&
        reinterpret_cast<uintptr_t>(first.data() + meta_size) %
            kBufferAlignment == 0) {
        if (!storage.assign(buf, static_cast<uint32_t>(total))) {
            return false;
        }
    } else {
        uint8_t* raw = static_cast<uint8_t*>(storage.allocate(
            static_cast<uint32_t>(total + kBufferAlignment - 1)));
        if (!raw) {
            return false;
        }
        uint8_t* begin = AlignPayload(raw + meta_size) - meta_size;
        buf.copy_to(begin, total);
        const butil::IOBuf::BlockRef& ref = storage.get_cur_ref();
        butil::IOBuf::BlockRef aligned_ref = {
            ref.offset + static_cast<uint32_t>(begin - raw),
            static_cast<uint32_t>(total), ref.block};
        butil::SingleIOBuf aligned(aligned_ref);
        storage.swap(aligned);
    }
    _iobuf.swap(storage);
    _meta_size = static_cast<uint32_t>(meta_size);
    _msg_size = static_cast<uint32_t>(msg_size);
    return true;
}

bool Message::append_msg_to_iobuf(butil::IOBuf& buf) const {
    if (!data() || !_msg_size) {
        return false;
    }
    _iobuf.append_to(&buf);
    return true;
}

void* Message::reduce_meta_size_and_get_buf(uint32_t new_size) {
    if (!data() || new_size > _meta_size) {
        errno = EINVAL;
        return nullptr;
    }
    if (new_size != _meta_size) {
        const uint32_t offset = _meta_size - new_size;
        const butil::IOBuf::BlockRef& ref = _iobuf.get_cur_ref();
        butil::IOBuf::BlockRef sub_ref = {
            ref.offset + offset, ref.length - offset, ref.block};
        butil::SingleIOBuf slice(sub_ref);
        _iobuf.swap(slice);
        _meta_size = new_size;
    }
    return mutable_buf_begin();
}

MessageBuilder::MessageBuilder(size_t initial_size)
    : ::flatbuffers::FlatBufferBuilder(
          initial_size, &slab_allocator_, false, kBufferAlignment) {
    CheckOrAbort(initial_size > 0 && initial_size < FLATBUFFERS_MAX_BUFFER_SIZE,
                 "Invalid FlatBuffers initial size");
}

MessageBuilder::MessageBuilder(MessageBuilder&& other) : MessageBuilder() {
    Swap(other);
}

MessageBuilder& MessageBuilder::operator=(MessageBuilder&& other) {
    if (this != &other) {
        MessageBuilder tmp(std::move(other));
        Swap(tmp);
    }
    return *this;
}

void MessageBuilder::ClearStringPool() {
    // FlatBuffers' shared-string comparator stores a vector_downward pointer.
    // Discard only this optional cache when moving between vector objects.
    delete string_pool;
    string_pool = nullptr;
}

void MessageBuilder::Swap(MessageBuilder& other) {
    if (this == &other) {
        return;
    }
    ClearStringPool();
    other.ClearStringPool();
    slab_allocator_.swap(other.slab_allocator_);
    ::flatbuffers::FlatBufferBuilder::Swap(other);
    // Each builder must continue to refer to its OWN allocator member.
    buf_.swap_allocator(other.buf_);
}

MessageBuilder::MessageBuilder(::flatbuffers::FlatBufferBuilder&& src)
    : ::flatbuffers::FlatBufferBuilder(std::move(src)) {
    ClearStringPool();
    CheckOrAbort(minalign_ <= kBufferAlignment,
                 "Unsupported FlatBuffers alignment");
    decltype(buf_) replacement(
        buf_.capacity() ? buf_.capacity() : 1024,
        &slab_allocator_, false, kBufferAlignment);
    if (buf_.capacity()) {
        replacement.push(buf_.data(), buf_.size());
        for (::flatbuffers::uoffset_t i = 0; i < buf_.scratch_size(); ++i) {
            replacement.scratch_push_small(buf_.scratch_data()[i]);
        }
    }
    // The temporary returns the original buffer to its actual allocator.
    buf_.swap(replacement);
}

MessageBuilder& MessageBuilder::operator=(::flatbuffers::FlatBufferBuilder&& src) {
    if (static_cast<::flatbuffers::FlatBufferBuilder*>(this) != &src) {
        MessageBuilder tmp(std::move(src));
        Swap(tmp);
    }
    return *this;
}

Message MessageBuilder::ReleaseMessage() {
    CheckOrAbort(finished && buf_.size() > 0,
                 "Finish the FlatBuffer before releasing a message");
    const uint32_t msg_size = static_cast<uint32_t>(buf_.size());
    const uint8_t* msg_data = buf_.data();
    const butil::SingleIOBuf& storage = slab_allocator_._iobuf;
    const uint8_t* raw = static_cast<const uint8_t*>(storage.get_begin());
    CheckOrAbort(raw != nullptr, "Missing FlatBuffers storage");
    CheckOrAbort(msg_data >= raw + kDefaultMetaSize,
                 "Missing FlatBuffers metadata prefix");
    const size_t begin = msg_data - raw - kDefaultMetaSize;
    CheckOrAbort(begin + kDefaultMetaSize + msg_size <= storage.get_length(),
                 "FlatBuffers message exceeds storage");
    const butil::IOBuf::BlockRef& ref = storage.get_cur_ref();
    const butil::IOBuf::BlockRef sub_ref = {
        ref.offset + static_cast<uint32_t>(begin),
        kDefaultMetaSize + msg_size, ref.block};
    Message msg(sub_ref, kDefaultMetaSize, msg_size);
    // Never expose stale scratch or allocator bytes in the reserved prefix.
    memset(msg.mutable_buf_begin(), 0, kDefaultMetaSize);
    Reset();
    return msg;
}

bool ParseFbFromIOBUF(Message* msg, size_t msg_size, const butil::IOBuf& buf,
                      size_t meta_size) {
    return msg && msg->parse_msg_from_iobuf(buf, msg_size, meta_size);
}

bool SerializeFbToIOBUF(const Message* msg, butil::IOBuf& buf) {
    return msg && msg->append_msg_to_iobuf(buf);
}

}  // namespace flatbuffers
}  // namespace brpc
#endif  // BRPC_WITH_FLATBUFFERS
