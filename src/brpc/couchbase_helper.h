// Copyright (c) 2018 Iqiyi, Inc.
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

#ifndef BRPC_COUCHBASE_HELPER_H
#define BRPC_COUCHBASE_HELPER_H

#include <map>
#include <vector>
#include <gflags/gflags.h>
#include "butil/intrusive_ptr.hpp"
#include "butil/strings/string_piece.h"
#include "butil/third_party/libvbucket/hash.h"
#include "brpc/socket_id.h"


namespace brpc {

DECLARE_bool(couchbase_disable_retry_during_rebalance); 
DECLARE_bool(couchbase_disable_retry_during_active);

// The maximum number of vbuckets that couchbase support 
const size_t kCouchbaseMaxVbucketNum = 65536;

class SharedLoadBalancer;

// Define error_code about retry during rebalance.
enum RetryReason { 
    // First request. 
    FIRST_REQUEST = 0,
    // No need retry, dummy value.
    DEFAULT_DUMMY = 1,
    // No need retry, Rpc failed except cases include in SERVER_DOWN.
    RPC_FAILED = 2,
    // Server is down, need retry other servers during rebalance.
    SERVER_DOWN = 3,
    // Server down and suggest to read replicas.
    SERVER_DOWN_RETRY_REPLICAS = 4,
    // Server is not mapped to the bucket, need retry other servers.
    RPC_SUCCESS_BUT_WRONG_SERVER = 5,
    // Server is mapped to the bucket right, retry the same server during rebalance.
    RPC_SUCCESS_BUT_RESPONSE_FAULT = 6, 
    // No need retry, response is ok.
    RESPONSE_OK = 7,
};

// Will set in controller::_request_code
union CouchbaseRequestCode {
    uint64_t cntl_request_code;
    struct {
        // vbucket id indicate which vbucket this request send to.
        uint32_t vb_id;
        // retry reason set by CouchbaseRetryPolicy. Defined in RetryReason. 
        uint32_t retry_reason;
    } request_code;
};

struct continuum_item_st {
    uint32_t index;     /* server index */
    uint32_t point;     /* point on the ketama continuum */
};

// Couchbase has two type of distribution used to map keys to servers.
// One is vbucket distribution and other is ketama distribution.
// This struct describes vbucket distribution of couchbase.
// 'num_replicas': the number of copies that will be stored on servers of one
//                 vbucket. Each vbucket must have this number of servers 
//                 indexes plus one.
// 'vbucket': A zero-based indexed by vBucketId. The entries in the _vbucket
//            are arrays of integers, where each integer is a zero-based 
//            index into the '_servers'. 
// 'fvbucket': It is fast forward map with same struct as _vbucket. It is 
//              used to provide the final vBubcket-to-server map during the
//              statrt of the rebalance. 
// 'server_list': all servers of a bucket.                    
// 'server_map': key is server in 'server_list', value is the index of 'server_list'.
struct VBucketServerMap {
    uint64_t version = 0;
    // vbucket distribution
    size_t num_replicas = 0;	
    std::vector<std::vector<int>> vbucket;
    std::vector<std::vector<int>> fvbucket;
    // ketama distribution
    std::vector<continuum_item_st> continuums;
    std::vector<SocketId> server_list;
    std::map<SocketId, size_t> server_map;
};

class CouchbaseContext {
public:
    CouchbaseContext(bool replicas, const std::string& get_key) 
        : can_read_replicas(replicas), key(get_key) {}
    bool can_read_replicas;
    std::string key;
};

class CouchbaseHelper {
public:
    static uint32_t GetVBucketId(const butil::StringPiece& key, 
                                 const uint32_t vbuckets_num) {
        uint32_t digest = butil::hash_crc32(key.data(), key.size());
        return digest & (vbuckets_num - 1);
    }    

    static inline uint64_t InitRequestCode(const uint32_t vb_id) {
        union CouchbaseRequestCode code;
        code.request_code.vb_id = vb_id;
        code.request_code.retry_reason = FIRST_REQUEST;
        return code.cntl_request_code;
    }  
							   
    static inline void ParseRequestCode(
        const uint64_t request_code, uint32_t* vb_id, uint32_t* retry_reason) {
        union CouchbaseRequestCode code;
        code.cntl_request_code = request_code;
        *vb_id = code.request_code.vb_id;
        *retry_reason = code.request_code.retry_reason;
    }  
	
    static inline uint64_t AddReasonToRequestCode(const uint64_t request_code, 
                                                  const uint32_t reason) {
        union CouchbaseRequestCode code;
        code.cntl_request_code = request_code;
        code.request_code.retry_reason = reason;
        return code.cntl_request_code;
    }

    static inline bool IsReadingReplicas(const uint64_t request_code) {
        uint32_t vb_id, reason;
        ParseRequestCode(request_code, &vb_id, &reason);
        return reason == SERVER_DOWN_RETRY_REPLICAS;
    }

    static SocketId GetMaster(const VBucketServerMap* vb_map, const size_t vb_id);

    static SocketId GetForwardMaster(const VBucketServerMap* vb_map, 
                                     const size_t vb_id);

    static SocketId GetReplicaId(const VBucketServerMap* vb_map, 
                                 const size_t vb_id, const size_t i);

    static bool GetVBucketMapInfo(
        butil::intrusive_ptr<SharedLoadBalancer> lb, 
        size_t* server_num, size_t* vb_num, bool* rebalance);

    static void UpdateDetectedMasterIfNeeded(
        butil::intrusive_ptr<SharedLoadBalancer> lb, 
        const bool is_reading_replicas, const uint32_t vb_id, 
        const uint32_t reason, const std::string& curr_server);
};

} // namespace brpc

#endif  // BRPC_COUCHBASE_HELPER_H
