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

#include "brpc/couchbase.h"

#include "brpc/policy/couchbase_protocol.h"
#include "brpc/proto_base.pb.h"
#include "butil/logging.h"
#include "butil/macros.h"
#include "butil/string_printf.h"
#include "butil/sys_byteorder.h"

namespace brpc {

//redefinition
CouchbaseRequest::CouchbaseRequest()
    : NonreflectableMessage<CouchbaseRequest>() {
    SharedCtor();
}

CouchbaseRequest::CouchbaseRequest(const CouchbaseRequest& from)
    : NonreflectableMessage<CouchbaseRequest>(from) {
    SharedCtor();
    MergeFrom(from);
}

void CouchbaseRequest::SharedCtor() {
    _pipelined_count = 0;
    _cached_size_ = 0;
}

CouchbaseRequest::~CouchbaseRequest() {
    SharedDtor();
}

void CouchbaseRequest::SharedDtor() {
}

void CouchbaseRequest::SetCachedSize(int size) const {
    _cached_size_ = size;
}

void CouchbaseRequest::Clear() {
    _buf.clear();
    _pipelined_count = 0;
}

// Get the Scope ID for a given scope name
bool CouchbaseRequest::GetScopeId(const butil::StringPiece& scope_name) {
    if (scope_name.empty()) {
        LOG(ERROR) << "Empty scope name";
        return false;
    }
    // Opcode 0xBC for Get Scope ID (see Collections.md)
    const policy::CouchbaseRequestHeader header = {
        policy::CB_MAGIC_REQUEST,
        policy::CB_GET_SCOPE_ID,
        butil::HostToNet16(scope_name.size()),
        0, // no extras
        policy::CB_BINARY_RAW_BYTES,
        0, // no vbucket
        butil::HostToNet32(scope_name.size()),
        0, // opaque
        0  // no CAS
    };
    if (_buf.append(&header, sizeof(header))) {
        return false;
    }
    if (_buf.append(scope_name.data(), scope_name.size())) {
        return false;
    }
    ++_pipelined_count;
    return true;
}

bool CouchbaseRequest::SelectBucket(const butil::StringPiece &bucket_name) {
    if (bucket_name.empty()) {
        LOG(ERROR) << "Empty bucket name";
        return false;
    }
    //construct the request header
    const policy::CouchbaseRequestHeader header = {
        policy::CB_MAGIC_REQUEST,
        policy::CB_SELECT_BUCKET,
        butil::HostToNet16(bucket_name.size()),
        0,
        policy::CB_BINARY_RAW_BYTES,
        0,
        butil::HostToNet32(bucket_name.size()),
        0,
        0
    };
    if (_buf.append(&header, sizeof(header))) {
        std::cout<<"Failed to append header to buffer"<<std::endl;
        return false;
    }
    if (_buf.append(bucket_name.data(), bucket_name.size())) {
        std::cout<<"Failed to append bucket name to buffer"<<std::endl;
        return false;
    }
    ++_pipelined_count;
    return true;
}

bool CouchbaseRequest::Authenticate(const butil::StringPiece &username,
                                      const butil::StringPiece &password) {
    if (username.empty() || password.empty()) {
        LOG(ERROR) << "Empty username or password";
        return false;
    }
    // Construct the request header
    constexpr char kPlainAuthCommand[] = "PLAIN";
    constexpr char kPadding[1] = {'\0'};
    const brpc::policy::CouchbaseRequestHeader header = {
        brpc::policy::CB_MAGIC_REQUEST, brpc::policy::CB_BINARY_SASL_AUTH,
        butil::HostToNet16(sizeof(kPlainAuthCommand) - 1), 0, 0, 0,
        butil::HostToNet32(sizeof(kPlainAuthCommand) + 1 +
                           username.length() * 2 + password.length()),
        0, 0};
    std::string* auth_str = new std::string();
    auth_str->clear();
    auth_str->append(reinterpret_cast<const char*>(&header), sizeof(header));
    auth_str->append(kPlainAuthCommand, sizeof(kPlainAuthCommand) - 1);
    auth_str->append(username.data());
    auth_str->append(kPadding, sizeof(kPadding));
    auth_str->append(username.data());
    auth_str->append(kPadding, sizeof(kPadding));
    auth_str->append(password.data());
    if(_buf.append(auth_str->data(), auth_str->size())) {
        std::cout<<"Failed to append auth string to buffer"<<std::endl;
        delete auth_str;
        return false;
    }
    ++_pipelined_count;
    return true;
}

bool CouchbaseRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
    LOG(WARNING) << "You're not supposed to parse a CouchbaseRequest";
    
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
        char aux_buf[sizeof(policy::CouchbaseRequestHeader)];
        const policy::CouchbaseRequestHeader* header =
            (const policy::CouchbaseRequestHeader*)tmp.fetch(aux_buf, sizeof(aux_buf));
        if (header == NULL) {
            return false;
        }
        if (header->magic != (uint8_t)policy::CB_MAGIC_REQUEST) {
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

void CouchbaseRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
    LOG(WARNING) << "You're not supposed to serialize a CouchbaseRequest";

    // simple approach just making it work.
    butil::IOBufAsZeroCopyInputStream wrapper(_buf);
    const void* data = NULL;
    int size = 0;
    while (wrapper.Next(&data, &size)) {
        output->WriteRaw(data, size);
    }
}

size_t CouchbaseRequest::ByteSizeLong() const {
    int total_size =  static_cast<int>(_buf.size());
    _cached_size_ = total_size;
    return total_size;
}

void CouchbaseRequest::MergeFrom(const CouchbaseRequest& from) {
    CHECK_NE(&from, this);
    _buf.append(from._buf);
    _pipelined_count += from._pipelined_count;
}

bool CouchbaseRequest::IsInitialized() const {
    return _pipelined_count != 0;
}

void CouchbaseRequest::Swap(CouchbaseRequest* other) {
    if (other != this) {
        _buf.swap(other->_buf);
        std::swap(_pipelined_count, other->_pipelined_count);
        std::swap(_cached_size_, other->_cached_size_);
    }
}

::google::protobuf::Metadata CouchbaseRequest::GetMetadata() const {
    ::google::protobuf::Metadata metadata{};
    metadata.descriptor = CouchbaseRequestBase::descriptor();
    metadata.reflection = nullptr;
    return metadata;
}
//redifinition
CouchbaseResponse::CouchbaseResponse()
    : NonreflectableMessage<CouchbaseResponse>() {
    SharedCtor();
}

//redifinition
CouchbaseResponse::CouchbaseResponse(const CouchbaseResponse& from)
    : NonreflectableMessage<CouchbaseResponse>(from) {
    SharedCtor();
    MergeFrom(from);
}

void CouchbaseResponse::SharedCtor() {
    _cached_size_ = 0;
}

//redifinition
CouchbaseResponse::~CouchbaseResponse() {
    SharedDtor();
}

void CouchbaseResponse::SharedDtor() {
}

void CouchbaseResponse::SetCachedSize(int size) const {
    _cached_size_ = size;
}

void CouchbaseResponse::Clear() {
}

bool CouchbaseResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
    LOG(WARNING) << "You're not supposed to parse a CouchbaseResponse";

    // simple approach just making it work.
    const void* data = NULL;
    int size = 0;
    while (input->GetDirectBufferPointer(&data, &size)) {
        _buf.append(data, size);
        input->Skip(size);
    }
    return true;
}

void CouchbaseResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
    LOG(WARNING) << "You're not supposed to serialize a CouchbaseResponse";
    
    // simple approach just making it work.
    butil::IOBufAsZeroCopyInputStream wrapper(_buf);
    const void* data = NULL;
    int size = 0;
    while (wrapper.Next(&data, &size)) {
        output->WriteRaw(data, size);
    }
}

size_t CouchbaseResponse::ByteSizeLong() const {
    int total_size = static_cast<int>(_buf.size());
    _cached_size_ = total_size;
    return total_size;
}

void CouchbaseResponse::MergeFrom(const CouchbaseResponse& from) {
    CHECK_NE(&from, this);
    _err = from._err;
    // responses of memcached according to their binary layout, should be
    // directly concatenatible.
    _buf.append(from._buf);
}

bool CouchbaseResponse::IsInitialized() const {
    return !_buf.empty();
}

void CouchbaseResponse::Swap(CouchbaseResponse* other) {
    if (other != this) {
        _buf.swap(other->_buf);
        std::swap(_cached_size_, other->_cached_size_);
    }
}

::google::protobuf::Metadata CouchbaseResponse::GetMetadata() const {
    ::google::protobuf::Metadata metadata{};
    metadata.descriptor = CouchbaseResponseBase::descriptor();
    metadata.reflection = nullptr;
    return metadata;
}

// ===================================================================

const char* CouchbaseResponse::status_str(Status st) {
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
bool CouchbaseRequest::GetOrDelete(uint8_t command, const butil::StringPiece& key) {
    const policy::CouchbaseRequestHeader header = {
        policy::CB_MAGIC_REQUEST,
        command,
        butil::HostToNet16(key.size()),
        0,
        policy::CB_BINARY_RAW_BYTES,
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

bool CouchbaseRequest::Get(const butil::StringPiece& key) {
    return GetOrDelete(policy::CB_BINARY_GET, key);
}

bool CouchbaseRequest::Delete(const butil::StringPiece& key) {
    return GetOrDelete(policy::CB_BINARY_DELETE, key);
}

struct FlushHeaderWithExtras {
    policy::CouchbaseRequestHeader header;
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
bool CouchbaseRequest::Flush(uint32_t timeout) {
    const uint8_t FLUSH_EXTRAS = (timeout == 0 ? 0 : 4);
    FlushHeaderWithExtras header_with_extras = {{
            policy::CB_MAGIC_REQUEST,
            policy::CB_BINARY_FLUSH,
            0,
            FLUSH_EXTRAS,
            policy::CB_BINARY_RAW_BYTES,
            0,
            butil::HostToNet32(FLUSH_EXTRAS),
            0,
            0 }, butil::HostToNet32(timeout) };
    if (FLUSH_EXTRAS == 0) {
        if (_buf.append(&header_with_extras.header,
                       sizeof(policy::CouchbaseRequestHeader))) {
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
bool CouchbaseResponse::PopGet(
    butil::IOBuf* value, uint32_t* flags, uint64_t* cas_value) {
    const size_t n = _buf.size();
    policy::CouchbaseResponseHeader header;
    if (n < sizeof(header)) {
        butil::string_printf(&_err, "buffer is too small to contain a header");
        return false;
    }
    _buf.copy_to(&header, sizeof(header));
    if (header.command != (uint8_t)policy::CB_BINARY_GET) {
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

bool CouchbaseResponse::PopGet(
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
bool CouchbaseResponse::PopDelete() {
    return PopStore(policy::CB_BINARY_DELETE, NULL);
}
bool CouchbaseResponse::PopFlush() {
    return PopStore(policy::CB_BINARY_FLUSH, NULL);
}

struct StoreHeaderWithExtras {
    policy::CouchbaseRequestHeader header;
    uint32_t flags;
    uint32_t exptime;
} __attribute__((packed));
BAIDU_CASSERT(sizeof(StoreHeaderWithExtras) == 32, must_match);
const size_t STORE_EXTRAS = sizeof(StoreHeaderWithExtras) -
                                                    sizeof(policy::CouchbaseRequestHeader);
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
bool CouchbaseRequest::Store(
    uint8_t command, const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    StoreHeaderWithExtras header_with_extras = {{
            policy::CB_MAGIC_REQUEST,
            command,
            butil::HostToNet16(key.size()),
            STORE_EXTRAS,
            policy::CB_JSON,
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
bool CouchbaseResponse::PopStore(uint8_t command, uint64_t* cas_value) {
    const size_t n = _buf.size();
    policy::CouchbaseResponseHeader header;
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

bool CouchbaseRequest::Set(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    return Store(policy::CB_BINARY_SET, key, value, flags, exptime, cas_value);
}

// Collection Management Methods
bool CouchbaseRequest::GetCollectionsManifest() {
    const policy::CouchbaseRequestHeader header = {
        policy::CB_MAGIC_REQUEST,
        policy::CB_GET_COLLECTIONS_MANIFEST,
        0, // no key
        0, // no extras
        policy::CB_BINARY_RAW_BYTES,
        0, // no vbucket
        0, // no body
        0, // opaque
        0  // no CAS
    };
    if (_buf.append(&header, sizeof(header))) {
        return false;
    }
    ++_pipelined_count;
    return true;
}

bool CouchbaseRequest::GetCollectionId(const butil::StringPiece& scope_name, 
                                     const butil::StringPiece& collection_name) {
    // Format the collection path as "scope.collection"
    std::string collection_path = scope_name.as_string() + "." + collection_name.as_string();
    
    const policy::CouchbaseRequestHeader header = {
        policy::CB_MAGIC_REQUEST,
        policy::CB_COLLECTIONS_GET_CID,
        butil::HostToNet16(collection_path.size()),
        0, // no extras
        policy::CB_BINARY_RAW_BYTES,
        0, // no vbucket
        butil::HostToNet32(collection_path.size()),
        0, // opaque
        0  // no CAS
    };
    if (_buf.append(&header, sizeof(header))) {
        return false;
    }
    if (_buf.append(collection_path.data(), collection_path.size())) {
        return false;
    }
    ++_pipelined_count;
    return true;
}

// Collection-aware document operations
bool CouchbaseRequest::GetFromCollection(const butil::StringPiece& key,
                                       const butil::StringPiece& scope_name,
                                       const butil::StringPiece& collection_name) {
    // First, we need to construct a collection-aware key
    // For simplicity, we'll use the format: scope.collection::key
    std::string scoped_key = scope_name.as_string() + "." + 
                           collection_name.as_string() + "::" + 
                           key.as_string();
    
    return GetOrDelete(policy::CB_BINARY_GET, butil::StringPiece(scoped_key));
}

bool CouchbaseRequest::SetToCollection(const butil::StringPiece& key, 
                                     const butil::StringPiece& value,
                                     uint32_t flags, uint32_t exptime, uint64_t cas_value,
                                     const butil::StringPiece& scope_name,
                                     const butil::StringPiece& collection_name) {
    // Construct a collection-aware key
    std::string scoped_key = scope_name.as_string() + "." + 
                           collection_name.as_string() + "::" + 
                           key.as_string();
    
    return Store(policy::CB_BINARY_SET, butil::StringPiece(scoped_key), value, flags, exptime, cas_value);
}

bool CouchbaseRequest::Add(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    return Store(policy::CB_BINARY_ADD, key, value, flags, exptime, cas_value);
}

bool CouchbaseRequest::Replace(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    return Store(policy::CB_BINARY_REPLACE, key, value, flags, exptime, cas_value);
}
    
bool CouchbaseRequest::Append(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    if (value.empty()) {
        LOG(ERROR) << "value to append must be non-empty";
        return false;
    }
    return Store(policy::CB_BINARY_APPEND, key, value, flags, exptime, cas_value);
}

bool CouchbaseRequest::Prepend(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value) {
    if (value.empty()) {
        LOG(ERROR) << "value to prepend must be non-empty";
        return false;
    }
    return Store(policy::CB_BINARY_PREPEND, key, value, flags, exptime, cas_value);
}

bool CouchbaseResponse::PopSet(uint64_t* cas_value) {
    return PopStore(policy::CB_BINARY_SET, cas_value);
}
bool CouchbaseResponse::PopAdd(uint64_t* cas_value) {
    return PopStore(policy::CB_BINARY_ADD, cas_value);
}
bool CouchbaseResponse::PopReplace(uint64_t* cas_value) {
    return PopStore(policy::CB_BINARY_REPLACE, cas_value);
}
bool CouchbaseResponse::PopAppend(uint64_t* cas_value) {
    return PopStore(policy::CB_BINARY_APPEND, cas_value);
}
bool CouchbaseResponse::PopPrepend(uint64_t* cas_value) {
    return PopStore(policy::CB_BINARY_PREPEND, cas_value);
}

// Collection-related response methods
bool CouchbaseResponse::PopCollectionsManifest(std::string* manifest_json) {
    const size_t n = _buf.size();
    policy::CouchbaseResponseHeader header;
    if (n < sizeof(header)) {
        butil::string_printf(&_err, "buffer is too small to contain a header");
        return false;
    }
    _buf.copy_to(&header, sizeof(header));
    if (header.command != policy::CB_GET_COLLECTIONS_MANIFEST) {
        butil::string_printf(&_err, "Not a collections manifest response");
        return false;
    }
    if (n < sizeof(header) + header.total_body_length) {
        butil::string_printf(&_err, "Not enough data");
        return false;
    }
    if (header.status != 0) {
        _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
        _err.clear();
        int value_size = (int)header.total_body_length - (int)header.extras_length - (int)header.key_length;
        _buf.cutn(&_err, value_size);
        return false;
    }

    // Skip header and extras/key if any
    _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
    
    // Get the manifest JSON
    int value_size = (int)header.total_body_length - (int)header.extras_length - (int)header.key_length;
    if (manifest_json && value_size > 0) {
        _buf.cutn(manifest_json, value_size);
    } else {
        _buf.pop_front(value_size);
    }
    
    _err.clear();
    return true;
}

bool CouchbaseResponse::PopCollectionId(uint32_t* collection_id) {
    const size_t n = _buf.size();
    policy::CouchbaseResponseHeader header;
    if (n < sizeof(header)) {
        butil::string_printf(&_err, "buffer is too small to contain a header");
        return false;
    }
    _buf.copy_to(&header, sizeof(header));
    if (header.command != policy::CB_COLLECTIONS_GET_CID) {
        butil::string_printf(&_err, "Not a collection ID response");
        return false;
    }
    if (n < sizeof(header) + header.total_body_length) {
        butil::string_printf(&_err, "Not enough data");
        return false;
    }
    if (header.status != 0) {
        _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
        _err.clear();
        int value_size = (int)header.total_body_length - (int)header.extras_length - (int)header.key_length;
        _buf.cutn(&_err, value_size);
        return false;
    }

    // Skip header and extras/key if any
    _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
    
    // Get the collection ID (typically 4 bytes)
    int value_size = (int)header.total_body_length - (int)header.extras_length - (int)header.key_length;
    if (collection_id && value_size >= 4) {
        uint32_t cid;
        _buf.copy_to(&cid, 4);
        *collection_id = butil::NetToHost32(cid);
        _buf.pop_front(value_size);
    } else {
        _buf.pop_front(value_size);
    }
    
    _err.clear();
    return true;
}

// Collection-aware response methods
bool CouchbaseResponse::PopGetFromCollection(butil::IOBuf* value, uint32_t* flags, uint64_t* cas_value) {
    // Same implementation as PopGet, just aliased for clarity
    return PopGet(value, flags, cas_value);
}

bool CouchbaseResponse::PopSetToCollection(uint64_t* cas_value) {
    // Same implementation as PopSet, just aliased for clarity
    return PopSet(cas_value);
}

struct IncrHeaderWithExtras {
    policy::CouchbaseRequestHeader header;
    uint64_t delta;
    uint64_t initial_value;
    uint32_t exptime;
} __attribute__((packed));
BAIDU_CASSERT(sizeof(IncrHeaderWithExtras) == 44, must_match);

const size_t INCR_EXTRAS = sizeof(IncrHeaderWithExtras) -
    sizeof(policy::CouchbaseRequestHeader);

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
bool CouchbaseRequest::Counter(
    uint8_t command, const butil::StringPiece& key, uint64_t delta,
    uint64_t initial_value, uint32_t exptime) {
    IncrHeaderWithExtras header_with_extras = {{
            policy::CB_MAGIC_REQUEST,
            command,
            butil::HostToNet16(key.size()),
            INCR_EXTRAS,
            policy::CB_BINARY_RAW_BYTES,
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

bool CouchbaseRequest::Increment(const butil::StringPiece& key, uint64_t delta,
                                uint64_t initial_value, uint32_t exptime) {
    return Counter(policy::CB_BINARY_INCREMENT, key, delta, initial_value, exptime);
}

bool CouchbaseRequest::Decrement(const butil::StringPiece& key, uint64_t delta,
                                uint64_t initial_value, uint32_t exptime) {
    return Counter(policy::CB_BINARY_DECREMENT, key, delta, initial_value, exptime);
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
bool CouchbaseResponse::PopCounter(
    uint8_t command, uint64_t* new_value, uint64_t* cas_value) {
    const size_t n = _buf.size();
    policy::CouchbaseResponseHeader header;
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

bool CouchbaseResponse::PopIncrement(uint64_t* new_value, uint64_t* cas_value) {
    return PopCounter(policy::CB_BINARY_INCREMENT, new_value, cas_value);
}
bool CouchbaseResponse::PopDecrement(uint64_t* new_value, uint64_t* cas_value) {
    return PopCounter(policy::CB_BINARY_DECREMENT, new_value, cas_value);
}

// MUST have extras.
// MUST have key.
// MUST NOT have value.
struct TouchHeaderWithExtras {
    policy::CouchbaseRequestHeader header;
    uint32_t exptime;
} __attribute__((packed));
BAIDU_CASSERT(sizeof(TouchHeaderWithExtras) == 28, must_match);
const size_t TOUCH_EXTRAS = sizeof(TouchHeaderWithExtras) - sizeof(policy::CouchbaseRequestHeader);

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
bool CouchbaseRequest::Touch(const butil::StringPiece& key, uint32_t exptime) {
    TouchHeaderWithExtras header_with_extras = {{
            policy::CB_MAGIC_REQUEST,
            policy::CB_BINARY_TOUCH,
            butil::HostToNet16(key.size()),
            TOUCH_EXTRAS,
            policy::CB_BINARY_RAW_BYTES,
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
bool CouchbaseRequest::Version() {
    const policy::CouchbaseRequestHeader header = {
        policy::CB_MAGIC_REQUEST,
        policy::CB_BINARY_VERSION,
        0,
        0,
        policy::CB_BINARY_RAW_BYTES,
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
bool CouchbaseResponse::PopVersion(std::string* version) {
    const size_t n = _buf.size();
    policy::CouchbaseResponseHeader header;
    if (n < sizeof(header)) {
        butil::string_printf(&_err, "buffer is too small to contain a header");
        return false;
    }
    _buf.copy_to(&header, sizeof(header));
    if (header.command != policy::CB_BINARY_VERSION) {
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
