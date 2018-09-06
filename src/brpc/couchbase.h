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

#ifndef BRPC_COUCHBASE_H
#define BRPC_COUCHBASE_H

#include "brpc/memcache.h"
#include "brpc/policy/memcache_binary_header.h"

namespace brpc {

// Request to couchbase.
// Do not support pipeline multiple operations in one request and sent now.
// Do not support Flush/Version
class CouchbaseRequest : public MemcacheRequest {
friend class CouchbaseChannel;
friend class CouchbaseRetryPolicy;
public:
    void Swap(CouchbaseRequest* other) {
        MemcacheRequest::Swap(other);
        if (this != other) {
            std::swap(_read_replicas, other->_read_replicas);
        }
    }

    bool Get(const butil::StringPiece& key, bool read_replicas = false) {
        MemcacheRequest::Clear();
        _read_replicas = read_replicas;
        return MemcacheRequest::Get(key);
    }

    bool Set(const butil::StringPiece& key, const butil::StringPiece& value,
             uint32_t flags, uint32_t exptime, uint64_t cas_value) {
        MemcacheRequest::Clear();
        return MemcacheRequest::Set(key, value, flags, exptime, cas_value);
    }
    
    bool Add(const butil::StringPiece& key, const butil::StringPiece& value,
             uint32_t flags, uint32_t exptime, uint64_t cas_value) {
        MemcacheRequest::Clear();
        return MemcacheRequest::Add(key, value, flags, exptime, cas_value); 
    }

    bool Replace(const butil::StringPiece& key, const butil::StringPiece& value,
                 uint32_t flags, uint32_t exptime, uint64_t cas_value) {
        MemcacheRequest::Clear();
        return MemcacheRequest::Replace(key, value, flags, exptime, cas_value);
    }
    
    bool Append(const butil::StringPiece& key, const butil::StringPiece& value,
                uint32_t flags, uint32_t exptime, uint64_t cas_value) {
        MemcacheRequest::Clear();
        return MemcacheRequest::Append(key, value, flags, exptime, cas_value);
    }

    bool Prepend(const butil::StringPiece& key, const butil::StringPiece& value,
                 uint32_t flags, uint32_t exptime, uint64_t cas_value) {
        MemcacheRequest::Clear();
        return MemcacheRequest::Prepend(key, value, flags, exptime, cas_value);
    }

    bool Delete(const butil::StringPiece& key) {
        MemcacheRequest::Clear();
        return MemcacheRequest::Delete(key);
    }

    bool Flush(uint32_t timeout) {
        MemcacheRequest::Clear();
        return MemcacheRequest::Flush(timeout);
    }

    bool Increment(const butil::StringPiece& key, uint64_t delta,
                   uint64_t initial_value, uint32_t exptime) {
        MemcacheRequest::Clear();
        return MemcacheRequest::Increment(key, delta, initial_value, exptime);
    }

    bool Decrement(const butil::StringPiece& key, uint64_t delta,
                   uint64_t initial_value, uint32_t exptime) {
        MemcacheRequest::Clear();
        return MemcacheRequest::Decrement(key, delta, initial_value, exptime);
    }
    
    bool Touch(const butil::StringPiece& key, uint32_t exptime) { 
        MemcacheRequest::Clear();
        return MemcacheRequest::Touch(key, exptime);
    }

    bool Version() {
        MemcacheRequest::Clear();
        return MemcacheRequest::Version();
    }
    
    CouchbaseRequest* New() const { return new CouchbaseRequest;}

    void CopyFrom(const CouchbaseRequest& from) {
        MemcacheRequest::CopyFrom(from);
        _read_replicas = from._read_replicas;
    }

private:
    int ParseRequest(std::string* key, 
                     policy::MemcacheBinaryCommand* command) const;

    bool BuildVBucketId(const size_t vbucket_id,
                        CouchbaseRequest* request) const;

    bool ReplicasGet(const butil::StringPiece& key, const size_t vbucket_id);

    void MergeFrom(const CouchbaseRequest& from);

    int pipelined_count();
    
    bool read_replicas() const { return _read_replicas; } 

    bool _read_replicas = false;
};

// Response from couchbase.
class CouchbaseResponse : public MemcacheResponse {
public:
    void Swap(CouchbaseResponse* other) {
        MemcacheResponse::Swap(other);
    }

    CouchbaseResponse* New() const { return new CouchbaseResponse;}

    void CopyFrom(const CouchbaseResponse& from) {
        MemcacheResponse::CopyFrom(from);
    }

    bool GetStatus(Status* status);

private:
    void MergeFrom(const CouchbaseResponse& from);

    int pipelined_count();
};

} // namespace brpc


#endif  // BRPC_COUCHBASE_H
