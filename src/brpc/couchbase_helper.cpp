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

#include "brpc/couchbase_helper.h"
#include "brpc/policy/couchbase_load_balancer.h"

namespace brpc {

DEFINE_bool(couchbase_disable_retry_during_rebalance, false, 
            "A swith indicating whether to open retry during rebalance status");

DEFINE_bool(couchbase_disable_retry_during_active, false,
            "A swith indicating whether to open retry during active status");

SocketId CouchbaseHelper::GetMaster(const VBucketServerMap* vb_map, 
						            const size_t vb_id) {
    if (vb_id < vb_map->vbucket.size()) {
        const int i = vb_map->vbucket[vb_id][0];
        if (i >= 0 && i < static_cast<int>(vb_map->server_list.size())) {
            return vb_map->server_list[i];
        }
    }
    return 0;
}

SocketId CouchbaseHelper::GetForwardMaster(const VBucketServerMap* vb_map, 
                                           const size_t vb_id) {
    if (vb_id < vb_map->fvbucket.size()) {
        const int i = vb_map->fvbucket[vb_id][0];
        if (i >= 0 && i < static_cast<int>(vb_map->server_list.size())) {
            return vb_map->server_list[i];
        }
    } 
    return 0;
}

SocketId CouchbaseHelper::GetReplicaId(const VBucketServerMap* vb_map, 
                                       const size_t vb_id, const size_t i) {
    if (vb_id < vb_map->vbucket.size() && i <= vb_map->num_replicas) {
        const int index = vb_map->vbucket[vb_id][i];
        if (index >= 0 && index < static_cast<int>(vb_map->server_list.size())) {
            return vb_map->server_list[index];
        }
    }
    return 0;
}

bool CouchbaseHelper::GetVBucketMapInfo(
    butil::intrusive_ptr<SharedLoadBalancer> shared_lb, 
    size_t* server_num, size_t* vb_num, bool* rebalance) {
    policy::CouchbaseLoadBalancer* lb = 
        dynamic_cast<policy::CouchbaseLoadBalancer*>(shared_lb->lb()); 
    CHECK(lb != nullptr) << "Failed to get couchbase load balancer.";
    return lb->GetVBucketMapInfo(server_num, nullptr, rebalance);
}

void CouchbaseHelper::UpdateDetectedMasterIfNeeded(
    butil::intrusive_ptr<SharedLoadBalancer> shared_lb, const bool is_reading_replicas, 
    const uint32_t vb_id, const uint32_t reason, const std::string& curr_server) {
    policy::CouchbaseLoadBalancer* lb = 
        dynamic_cast<policy::CouchbaseLoadBalancer*>(shared_lb->lb()); 
    CHECK(lb != nullptr) << "Failed to get couchbase load balancer.";
    lb->UpdateDetectedMasterIfNeeded(is_reading_replicas, vb_id, 
                                     reason, curr_server);
}
			
} // namespace brpc
