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


#include <algorithm>
#include <google/protobuf/reflection_ops.h>
#include <google/protobuf/wire_format.h>
#include "butil/string_printf.h"
#include "butil/macros.h"
#include "butil/sys_byteorder.h"
#include "butil/logging.h"
#include "brpc/memcache.h"
#include "brpc/policy/memcache_binary_header.h"

namespace brpc {

MemcacheRequest::MemcacheRequest()
    : ::google::protobuf::Message() {
    SharedCtor();
}

MemcacheRequest::MemcacheRequest(const MemcacheRequest& from)
    : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void MemcacheRequest::SharedCtor() {
    _pipelined_count = 0;
    _cached_size_ = 0;
}

MemcacheRequest::~MemcacheRequest() {
    SharedDtor();
}

void MemcacheRequest::SharedDtor() {
}

void MemcacheRequest::SetCachedSize(int size) const {
    _cached_size_ = size;
}

const ::google::protobuf::Descriptor* MemcacheRequest::descriptor() {
    return MemcacheRequestBase::descriptor();
}

MemcacheRequest* MemcacheRequest::New() const {
    return new MemcacheRequest;
}

#if GOOGLE_PROTOBUF_VERSION >= 3006000
MemcacheRequest* MemcacheRequest::New(::google::protobuf::Arena* arena) const {
    return CreateMaybeMessage<MemcacheRequest>(arena);
}
#endif

void MemcacheRequest::Clear() {
    _buf.clear();
    _pipelined_count = 0;
}

bool MemcacheRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
    LOG(WARNING) << "You're not supposed to parse a MemcacheRequest";
    
    // simple approach just making it work.
    butil::IOBuf tmp;
    const void* data = NULL;
    int size = 0;
    while (input->GetDirectBufferPointer(&data, &size)) {
        tmp.append(data, size);
        input->Skip(size);
    }
    const butil::IOBuf saved = tmp;
    int count = 0;
    for (; !tmp.empty(); ++count) {
        char aux_buf[sizeof(policy::MemcacheRequestHeader)];
        const policy::MemcacheRequestHeader* header =
            (const policy::MemcacheRequestHeader*)tmp.fetch(aux_buf, sizeof(aux_buf));
        if (header == NULL) {
            return false;
        }
        if (header->magic != (uint8_t)policy::MC_MAGIC_REQUEST) {
            return false;
        }
        uint32_t total_body_length = butil::NetToHost32(header->total_body_length);
        if (tmp.size() < sizeof(*header) + total_body_length) {
            return false;
        }
        tmp.pop_front(sizeof(*header) + total_body_length);
    }
    _buf.append(saved);
    _pipelined_count += count;
    return true;
}

void MemcacheRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
    LOG(WARNING) << "You're not supposed to serialize a MemcacheRequest";

    // simple approach just making it work.
    butil::IOBufAsZeroCopyInputStream wrapper(_buf);
    const void* data = NULL;
    int size = 0;
    while (wrapper.Next(&data, &size)) {
        output->WriteRaw(data, size);
    }
}

::google::protobuf::uint8* MemcacheRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

int MemcacheRequest::ByteSize() const {
    int total_size =  _buf.size();
    _cached_size_ = total_size;
    return total_size;
}

void MemcacheRequest::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const MemcacheRequest* source = dynamic_cast<const MemcacheRequest*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void MemcacheRequest::MergeFrom(const MemcacheRequest& from) {
    GOOGLE_CHECK_NE(&from, this);
    _buf.append(from._buf);
    _pipelined_count += from._pipelined_count;
}

void MemcacheRequest::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

void MemcacheRequest::CopyFrom(const MemcacheRequest& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

bool MemcacheRequest::IsInitialized() const {
    return _pipelined_count != 0;
}

void MemcacheRequest::Swap(MemcacheRequest* other) {
    if (other != this) {
        _buf.swap(other->_buf);
        std::swap(_pipelined_count, other->_pipelined_count);
        std::swap(_cached_size_, other->_cached_size_);
    }
}

::google::protobuf::Metadata MemcacheRequest::GetMetadata() const {
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = MemcacheRequest::descriptor();
    metadata.reflection = NULL;
    return metadata;
}

MemcacheResponse::MemcacheResponse()
    : ::google::protobuf::Message() {
    SharedCtor();
}

MemcacheResponse::MemcacheResponse(const MemcacheResponse& from)
    : ::google::protobuf::Message() {
    SharedCtor();
    MergeFrom(from);
}

void MemcacheResponse::SharedCtor() {
    _cached_size_ = 0;
}

MemcacheResponse::~MemcacheResponse() {
    SharedDtor();
}

void MemcacheResponse::SharedDtor() {
}

void MemcacheResponse::SetCachedSize(int size) const {
    _cached_size_ = size;
}
const ::google::protobuf::Descriptor* MemcacheResponse::descriptor() {
    return MemcacheResponseBase::descriptor();
}

MemcacheResponse* MemcacheResponse::New() const {
    return new MemcacheResponse;
}

#if GOOGLE_PROTOBUF_VERSION >= 3006000
MemcacheResponse*
MemcacheResponse::New(::google::protobuf::Arena* arena) const {
    return CreateMaybeMessage<MemcacheResponse>(arena);
}
#endif

void MemcacheResponse::Clear() {
}

bool MemcacheResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
    LOG(WARNING) << "You're not supposed to parse a MemcacheResponse";

    // simple approach just making it work.
    const void* data = NULL;
    int size = 0;
    while (input->GetDirectBufferPointer(&data, &size)) {
        _buf.append(data, size);
        input->Skip(size);
    }
    return true;
}

void MemcacheResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
    LOG(WARNING) << "You're not supposed to serialize a MemcacheResponse";
    
    // simple approach just making it work.
    butil::IOBufAsZeroCopyInputStream wrapper(_buf);
    const void* data = NULL;
    int size = 0;
    while (wrapper.Next(&data, &size)) {
        output->WriteRaw(data, size);
    }
}

::google::protobuf::uint8* MemcacheResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
    return target;
}

int MemcacheResponse::ByteSize() const {
    int total_size = _buf.size();
    _cached_size_ = total_size;
    return total_size;
}

void MemcacheResponse::MergeFrom(const ::google::protobuf::Message& from) {
    GOOGLE_CHECK_NE(&from, this);
    const MemcacheResponse* source = dynamic_cast<const MemcacheResponse*>(&from);
    if (source == NULL) {
        ::google::protobuf::internal::ReflectionOps::Merge(from, this);
    } else {
        MergeFrom(*source);
    }
}

void MemcacheResponse::MergeFrom(const MemcacheResponse& from) {
    GOOGLE_CHECK_NE(&from, this);
    _err = from._err;
    // responses of memcached according to their binary layout, should be
    // directly concatenatible.
    _buf.append(from._buf);
}

void MemcacheResponse::CopyFrom(const ::google::protobuf::Message& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

void MemcacheResponse::CopyFrom(const MemcacheResponse& from) {
    if (&from == this) return;
    Clear();
    MergeFrom(from);
}

bool MemcacheResponse::IsInitialized() const {
    return !_buf.empty();
}

void MemcacheResponse::Swap(MemcacheResponse* other) {
    if (other != this) {
        _buf.swap(other->_buf);
        std::swap(_cached_size_, other->_cached_size_);
    }
}

::google::protobuf::Metadata MemcacheResponse::GetMetadata() const {
    ::google::protobuf::Metadata metadata;
    metadata.descriptor = MemcacheResponse::descriptor();
    metadata.reflection = NULL;
    return metadata;
}

// ===================================================================

const char* MemcacheResponse::status_str(Status st) {
    switch (st) {
    case STATUS_SUCCESS:
        return "SUCCESS";
    case STATUS_KEY_ENOENT:
        return "The key does not exist";
    case STATUS_KEY_EEXISTS:
        return "The key exists";
    case STATUS_E2BIG:
        return "Arg list is too long";
    case STATUS_EINVAL:
        return "Invalid argument";
    case STATUS_NOT_STORED:
        return "Not stored";
    case STATUS_DELTA_BADVAL:
        return "Bad delta";
    case STATUS_AUTH_ERROR:
        return "authentication error";
    case STATUS_AUTH_CONTINUE:
        return "authentication continue";
    case STATUS_UNKNOWN_COMMAND:
        return "Unknown command";
    case STATUS_ENOMEM:
        return "Out of memory";
    }
    return "Unknown status";
}

// MUST NOT have extras.
// MUST have key.
// MUST NOT have value.
bool MemcacheRequest::GetOrDelete(uint8_t command, const butil::StringPiece& key) {
    const policy::MemcacheRequestHeader header = {
        policy::MC_MAGIC_REQUEST,
        command,
        butil::HostToNet16(key.size()),
        0,
        policy::MC_BINARY_RAW_BYTES,
        0,
        butil::HostToNet32(key.size()),
        0,
        0
    };
    if (_buf.append(&header, sizeof(header))) {
        return false;
    }
    if (_buf.append(key.data(), key.size())) {
        return false;
    }
    ++_pipelined_count;
    return true;
}

bool MemcacheRequest::Get(const butil::StringPiece& key) {
    return GetOrDelete(policy::MC_BINARY_GET, key);
}

bool MemcacheRequest::Delete(const butil::StringPiece& key) {
    return GetOrDelete(policy::MC_BINARY_DELETE, key);
}

struct FlushHeaderWithExtras {
    policy::MemcacheRequestHeader header;
    uint32_t exptime;
} __attribute__((packed));
BAIDU_CASSERT(sizeof(FlushHeaderWithExtras) == 28, must_match);

// MAY have extras.
// MUST NOT have key.
// MUST NOT have value.
// Extra data for flush:
//    Byte/     0       |       1       |       2       |       3       |
//       /              |               |               |               |
//      |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
//      +---------------+---------------+---------------+---------------+
//     0| Expiration                                                    |
//      +---------------+---------------+---------------+---------------+
//    Total 4 bytes
bool MemcacheRequest::Flush(uint32_t timeout) {
    const uint8_t FLUSH_EXTRAS = (timeout == 0 ? 0 : 4);
    FlushHeaderWithExtras header_with_extras = {{
            policy::MC_MAGIC_REQUEST,
            policy::MC_BINARY_FLUSH,
            0,
            FLUSH_EXTRAS,
            policy::MC_BINARY_RAW_BYTES,
            0,
            butil::HostToNet32(FLUSH_EXTRAS),
            0,
            0 }, butil::HostToNet32(timeout) };
    if (FLUSH_EXTRAS == 0) {
        if (_buf.append(&header_with_extras.header,
                       sizeof(policy::MemcacheRequestHeader))) {
            return false;
        }
    } else {
        if (_buf.append(&header_with_extras, sizeof(header_with_extras))) {
            return false;
        }
    }
    ++_pipelined_count;
    return true;
}

// (if found):
// MUST have extras.
// MAY have key.
// MAY have value.
// Extra data for the get commands:
// Byte/     0       |       1       |       2       |       3       |
//    /              |               |               |               |
//   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
//   +---------------+---------------+---------------+---------------+
//  0| Flags                                                         |
//   +---------------+---------------+---------------+---------------+
//   Total 4 bytes
bool MemcacheResponse::PopGet(
    butil::IOBuf* value, uint32_t* flags, uint64_t* cas_value) {
    const size_t n = _buf.size();
    policy::MemcacheResponseHeader header;
    if (n < sizeof(header)) {
        butil::string_printf(&_err, "buffer is too small to contain a header");
        return false;
    }
    _buf.copy_to(&header, sizeof(header));
    if (header.command != (uint8_t)policy::MC_BINARY_GET) {
        butil::string_printf(&_err, "not a GET response");
        return false;
    }
    if (n < sizeof(header) + header.total_body_length) {
        butil::string_printf(&_err, "response=%u < header=%u + body=%u",
                  (unsigned)n, (unsigned)sizeof(header), header.total_body_length);
        return false;
    }
    if (header.status != (uint16_t)STATUS_SUCCESS) {
        LOG_IF(ERROR, header.extras_length != 0) << "GET response must not have flags";
        LOG_IF(ERROR, header.key_length != 0) << "GET response must not have key";
        const int value_size = (int)header.total_body_length - (int)header.extras_length
            - (int)header.key_length;
        if (value_size < 0) {
            butil::string_printf(&_err, "value_size=%d is non-negative", value_size);
            return false;
        }
        _buf.pop_front(sizeof(header) + header.extras_length +
                      header.key_length);
        _err.clear();
        _buf.cutn(&_err, value_size);
        return false;
    }
    if (header.extras_length != 4u) {
        butil::string_printf(&_err, "GET response must have flags as extras, actual length=%u",
                  header.extras_length);
        return false;
    }
    if (header.key_length != 0) {
        butil::string_printf(&_err, "GET response must not have key");
        return false;
    }
    const int value_size = (int)header.total_body_length - (int)header.extras_length
        - (int)header.key_length;
    if (value_size < 0) {
        butil::string_printf(&_err, "value_size=%d is non-negative", value_size);
        return false;
    }
    _buf.pop_front(sizeof(header));
    uint32_t raw_flags = 0;
    _buf.cutn(&raw_flags, sizeof(raw_flags));
    if (flags) {
        *flags = butil::NetToHost32(raw_flags);
    }
    if (value) {
        value->clear();
        _buf.cutn(value, value_size);
    }
    if (cas_value) {
        *cas_value = header.cas_value;
    }
    _err.clear();
    return true;
}

bool MemcacheResponse::PopGet(
    std::string* value, uint32_t* flags, uint64_t* cas_value) {
    butil::IOBuf tmp;
    if (PopGet(&tmp, flags, cas_value)) {
        tmp.copy_to(value);
        return true;
    }
    return false;
}

// MUST NOT have extras
// MUST NOT have key
// MUST NOT have value
bool MemcacheResponse::PopDelete() {
    return PopStore(policy::MC_BINARY_DELETE, NULL);
}
bool MemcacheResponse::PopFlush() {
    return PopStore(policy::MC_BINARY_FLUSH, NULL);
}

struct StoreHeaderWithExtras {
    policy::MemcacheRequestHeader header;
    uint32_t flags;
    uint32_t exptime;
} __attribute__((packed));
BAIDU_CASSERT(sizeof(StoreHeaderWithExtras) == 32, must_match);
const size_t STORE_EXTRAS = sizeof(StoreHeaderWithExtras) -
                                                    sizeof(policy::MemcacheRequestHeader);
// MUST have extras.
// MUST have key.
// MAY have value.
// Extra data for set/add/replace:
// Byte/     0       |       1       |       2       |       3       |
//    /              |               |               |               |
//   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
//   +---------------+---------------+---------------+---------------+
//  0| Flags                                                         |
//   +---------------+---------------+---------------+---------------+
//  4| Expiration                                                    |
//   +---------------+---------------+---------------+---------------+
//   Total 8 bytes
bool MemcacheRequest::Store(
    uint8_t command, const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    StoreHeaderWithExtras header_with_extras = {{
            policy::MC_MAGIC_REQUEST,
            command,
            butil::HostToNet16(key.size()),
            STORE_EXTRAS,
            policy::MC_BINARY_RAW_BYTES,
            0,
            butil::HostToNet32(STORE_EXTRAS + key.size() + value.size()),
            0,
            butil::HostToNet64(cas_value)
        }, butil::HostToNet32(flags), butil::HostToNet32(exptime)};
    if (_buf.append(&header_with_extras, sizeof(header_with_extras))) {
        return false;
    }
    if (_buf.append(key.data(), key.size())) {
        return false;
    }
    if (_buf.append(value.data(), value.size())) {
        return false;
    }
    ++_pipelined_count;
    return true;
}

// MUST have CAS
// MUST NOT have extras
// MUST NOT have key
// MUST NOT have value
bool MemcacheResponse::PopStore(uint8_t command, uint64_t* cas_value) {
    const size_t n = _buf.size();
    policy::MemcacheResponseHeader header;
    if (n < sizeof(header)) {
        butil::string_printf(&_err, "buffer is too small to contain a header");
        return false;
    }
    _buf.copy_to(&header, sizeof(header));
    if (header.command != command) {
        butil::string_printf(&_err, "Not a STORE response");
        return false;
    }
    if (n < sizeof(header) + header.total_body_length) {
        butil::string_printf(&_err, "Not enough data");
        return false;
    }
    LOG_IF(ERROR, header.extras_length != 0) << "STORE response must not have flags";
    LOG_IF(ERROR, header.key_length != 0) << "STORE response must not have key";
    int value_size = (int)header.total_body_length - (int)header.extras_length
        - (int)header.key_length;
    if (header.status != (uint16_t)STATUS_SUCCESS) {
        _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
        _err.clear();
        _buf.cutn(&_err, value_size);
        return false;
    }
    LOG_IF(ERROR, value_size != 0) << "STORE response must not have value, actually="
                                   << value_size;
    _buf.pop_front(sizeof(header) + header.total_body_length);
    if (cas_value) {
        CHECK(header.cas_value);
        *cas_value = header.cas_value;
    }
    _err.clear();
    return true;
}

bool MemcacheRequest::Set(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    return Store(policy::MC_BINARY_SET, key, value, flags, exptime, cas_value);
}

bool MemcacheRequest::Add(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    return Store(policy::MC_BINARY_ADD, key, value, flags, exptime, cas_value);
}

bool MemcacheRequest::Replace(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    return Store(policy::MC_BINARY_REPLACE, key, value, flags, exptime, cas_value);
}
    
bool MemcacheRequest::Append(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    if (value.empty()) {
        LOG(ERROR) << "value to append must be non-empty";
        return false;
    }
    return Store(policy::MC_BINARY_APPEND, key, value, flags, exptime, cas_value);
}

bool MemcacheRequest::Prepend(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    if (value.empty()) {
        LOG(ERROR) << "value to prepend must be non-empty";
        return false;
    }
    return Store(policy::MC_BINARY_PREPEND, key, value, flags, exptime, cas_value);
}

bool MemcacheResponse::PopSet(uint64_t* cas_value) {
    return PopStore(policy::MC_BINARY_SET, cas_value);
}
bool MemcacheResponse::PopAdd(uint64_t* cas_value) {
    return PopStore(policy::MC_BINARY_ADD, cas_value);
}
bool MemcacheResponse::PopReplace(uint64_t* cas_value) {
    return PopStore(policy::MC_BINARY_REPLACE, cas_value);
}
bool MemcacheResponse::PopAppend(uint64_t* cas_value) {
    return PopStore(policy::MC_BINARY_APPEND, cas_value);
}
bool MemcacheResponse::PopPrepend(uint64_t* cas_value) {
    return PopStore(policy::MC_BINARY_PREPEND, cas_value);
}

struct IncrHeaderWithExtras {
    policy::MemcacheRequestHeader header;
    uint64_t delta;
    uint64_t initial_value;
    uint32_t exptime;
} __attribute__((packed));
BAIDU_CASSERT(sizeof(IncrHeaderWithExtras) == 44, must_match);

const size_t INCR_EXTRAS = sizeof(IncrHeaderWithExtras) -
    sizeof(policy::MemcacheRequestHeader);

// MUST have extras.
// MUST have key.
// MUST NOT have value.
// Extra data for incr/decr:
// Byte/     0       |       1       |       2       |       3       |
//    /              |               |               |               |
//   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
//   +---------------+---------------+---------------+---------------+
//  0| Delta to add / subtract                                      |
//   |                                                               |
//   +---------------+---------------+---------------+---------------+
//  8| Initial value                                                 |
//   |                                                               |
//   +---------------+---------------+---------------+---------------+
// 16| Expiration                                                    |
//   +---------------+---------------+---------------+---------------+
//   Total 20 bytes
bool MemcacheRequest::Counter(
    uint8_t command, const butil::StringPiece& key, uint64_t delta,
    uint64_t initial_value, uint32_t exptime) {
    IncrHeaderWithExtras header_with_extras = {{
            policy::MC_MAGIC_REQUEST,
            command,
            butil::HostToNet16(key.size()),
            INCR_EXTRAS,
            policy::MC_BINARY_RAW_BYTES,
            0,
            butil::HostToNet32(INCR_EXTRAS + key.size()),
            0,
            0 }, butil::HostToNet64(delta), butil::HostToNet64(initial_value), butil::HostToNet32(exptime) };
    if (_buf.append(&header_with_extras, sizeof(header_with_extras))) {
        return false;
    }
    if (_buf.append(key.data(), key.size())) {
        return false;
    }
    ++_pipelined_count;
    return true;
}

bool MemcacheRequest::Increment(const butil::StringPiece& key, uint64_t delta,
                                uint64_t initial_value, uint32_t exptime) {
    return Counter(policy::MC_BINARY_INCREMENT, key, delta, initial_value, exptime);
}

bool MemcacheRequest::Decrement(const butil::StringPiece& key, uint64_t delta,
                                uint64_t initial_value, uint32_t exptime) {
    return Counter(policy::MC_BINARY_DECREMENT, key, delta, initial_value, exptime);
}

// MUST NOT have extras.
// MUST NOT have key.
// MUST have value.
// Byte/     0       |       1       |       2       |       3       |
//    /              |               |               |               |
//   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
//   +---------------+---------------+---------------+---------------+
//  0| 64-bit unsigned response.                                     |
//   |                                                               |
//   +---------------+---------------+---------------+---------------+
//   Total 8 bytes
bool MemcacheResponse::PopCounter(
    uint8_t command, uint64_t* new_value, uint64_t* cas_value) {
    const size_t n = _buf.size();
    policy::MemcacheResponseHeader header;
    if (n < sizeof(header)) {
        butil::string_printf(&_err, "buffer is too small to contain a header");
        return false;
    }
    _buf.copy_to(&header, sizeof(header));
    if (header.command != command) {
        butil::string_printf(&_err, "not a INCR/DECR response");
        return false;
    }
    if (n < sizeof(header) + header.total_body_length) {
        butil::string_printf(&_err, "response=%u < header=%u + body=%u",
                  (unsigned)n, (unsigned)sizeof(header), header.total_body_length);
        return false;
    }
    LOG_IF(ERROR, header.extras_length != 0) << "INCR/DECR response must not have flags";
    LOG_IF(ERROR, header.key_length != 0) << "INCR/DECR response must not have key";
    const int value_size = (int)header.total_body_length - (int)header.extras_length
        - (int)header.key_length;
    _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);

    if (header.status != (uint16_t)STATUS_SUCCESS) {
        if (value_size < 0) {
            butil::string_printf(&_err, "value_size=%d is negative", value_size);
        } else {
            _err.clear();
            _buf.cutn(&_err, value_size);
        }
        return false;
    }
    if (value_size != 8) {
        butil::string_printf(&_err, "value_size=%d is not 8", value_size);
        return false;
    }
    uint64_t raw_value = 0;
    _buf.cutn(&raw_value, sizeof(raw_value));
    *new_value = butil::NetToHost64(raw_value);
    if (cas_value) {
        *cas_value = header.cas_value;
    }
    _err.clear();
    return true;
}

bool MemcacheResponse::PopIncrement(uint64_t* new_value, uint64_t* cas_value) {
    return PopCounter(policy::MC_BINARY_INCREMENT, new_value, cas_value);
}
bool MemcacheResponse::PopDecrement(uint64_t* new_value, uint64_t* cas_value) {
    return PopCounter(policy::MC_BINARY_DECREMENT, new_value, cas_value);
}

// MUST have extras.
// MUST have key.
// MUST NOT have value.
struct TouchHeaderWithExtras {
    policy::MemcacheRequestHeader header;
    uint32_t exptime;
} __attribute__((packed));
BAIDU_CASSERT(sizeof(TouchHeaderWithExtras) == 28, must_match);
const size_t TOUCH_EXTRAS = sizeof(TouchHeaderWithExtras) - sizeof(policy::MemcacheRequestHeader);

// MAY have extras.
// MUST NOT have key.
// MUST NOT have value.
// Extra data for touch:
//    Byte/     0       |       1       |       2       |       3       |
//       /              |               |               |               |
//      |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
//      +---------------+---------------+---------------+---------------+
//     0| Expiration                                                    |
//      +---------------+---------------+---------------+---------------+
//    Total 4 bytes
bool MemcacheRequest::Touch(const butil::StringPiece& key, uint32_t exptime) {
    TouchHeaderWithExtras header_with_extras = {{
            policy::MC_MAGIC_REQUEST,
            policy::MC_BINARY_TOUCH,
            butil::HostToNet16(key.size()),
            TOUCH_EXTRAS,
            policy::MC_BINARY_RAW_BYTES,
            0,
            butil::HostToNet32(TOUCH_EXTRAS + key.size()),
            0,
            0 }, butil::HostToNet32(exptime) };
    if (_buf.append(&header_with_extras, sizeof(header_with_extras))) {
        return false;
    }
    if (_buf.append(key.data(), key.size())) {
        return false;
    }
    ++_pipelined_count;
    return true;
}

// MUST NOT have extras.
// MUST NOT have key.
// MUST NOT have value.
bool MemcacheRequest::Version() {
    const policy::MemcacheRequestHeader header = {
        policy::MC_MAGIC_REQUEST,
        policy::MC_BINARY_VERSION,
        0,
        0,
        policy::MC_BINARY_RAW_BYTES,
        0,
        0,
        0,
        0
    };
    if (_buf.append(&header, sizeof(header))) {
        return false;
    }
    ++_pipelined_count;
    return true;
}

// MUST NOT have extras.
// MUST NOT have key.
// MUST have value.
bool MemcacheResponse::PopVersion(std::string* version) {
    const size_t n = _buf.size();
    policy::MemcacheResponseHeader header;
    if (n < sizeof(header)) {
        butil::string_printf(&_err, "buffer is too small to contain a header");
        return false;
    }
    _buf.copy_to(&header, sizeof(header));
    if (header.command != policy::MC_BINARY_VERSION) {
        butil::string_printf(&_err, "not a VERSION response");
        return false;
    }
    if (n < sizeof(header) + header.total_body_length) {
        butil::string_printf(&_err, "response=%u < header=%u + body=%u",
                  (unsigned)n, (unsigned)sizeof(header), header.total_body_length);
        return false;
    }
    LOG_IF(ERROR, header.extras_length != 0) << "VERSION response must not have flags";
    LOG_IF(ERROR, header.key_length != 0) << "VERSION response must not have key";
    const int value_size = (int)header.total_body_length - (int)header.extras_length
        - (int)header.key_length;
    _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
    if (value_size < 0) {
        butil::string_printf(&_err, "value_size=%d is negative", value_size);
        return false;
    }
    if (header.status != (uint16_t)STATUS_SUCCESS) {
        _err.clear();
        _buf.cutn(&_err, value_size);
        return false;
    }
    if (version) {
        version->clear();
        _buf.cutn(version, value_size);
    }
    _err.clear();
    return true;
}
 
} // namespace brpc
