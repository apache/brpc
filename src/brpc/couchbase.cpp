// Copyright (c) 2018 Qiyi, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Daojin Cai (caidaojin@qiyi.com)

#include "brpc/couchbase.h"
#include "brpc/policy/memcache_binary_header.h"
#include "butil/string_printf.h"
#include "butil/sys_byteorder.h"

namespace brpc {

int CouchbaseRequest::ParseRequest(
    std::string* key, policy::MemcacheBinaryCommand* command) const {
    const size_t n = _buf.size();
    policy::MemcacheRequestHeader header;
    if (n < sizeof(header)) {
        return -1;
    }
    _buf.copy_to(&header, sizeof(header));
    const uint16_t key_len = butil::NetToHost16(header.key_length);
    if (key_len == 0) {
        return 1;
    }
    *command = static_cast<policy::MemcacheBinaryCommand>(header.command);
    _buf.copy_to(key, key_len, sizeof(header) + header.extras_length);
    return 0;
}

bool CouchbaseRequest::BuildNewWithVBucketId(CouchbaseRequest* request, 
                                             const size_t vbucket_id) const {
    if (this == request) {
        return false;
    }
    const size_t n = _buf.size();
    policy::MemcacheRequestHeader header;
    if (n < sizeof(header)) {
        return false;
    }
    _buf.copy_to(&header, sizeof(header));
    header.vbucket_id = butil::HostToNet16(vbucket_id);
    request->Clear();
    if (request->_buf.append(&header, sizeof(header)) != 0) {
        return false;
    }
    _buf.append_to(&request->_buf, n - sizeof(header), sizeof(header));
    request->_pipelined_count = _pipelined_count;
    return true;
}

bool CouchbaseRequest::ReplicasGet(const butil::StringPiece& key) {
    const policy::MemcacheRequestHeader header = {
        policy::MC_MAGIC_REQUEST,
        0x83,
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

bool CouchbaseResponse::GetStatus(Status* st) {
    const size_t n = _buf.size();
    policy::MemcacheResponseHeader header;
    if (n < sizeof(header)) {
        butil::string_printf(&_err, "buffer is too small to contain a header");
        return false;
    }
    _buf.copy_to(&header, sizeof(header));
    if (n < sizeof(header) + header.total_body_length) {
        butil::string_printf(&_err, "response=%u < header=%u + body=%u",
            (unsigned)n, (unsigned)sizeof(header), header.total_body_length);
        return false;
    }
    *st = static_cast<Status>(header.status);
    return true;
}

} // namespace brpc
