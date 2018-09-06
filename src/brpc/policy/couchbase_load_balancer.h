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

// Authors: Cai,Daojin (caidaojin@qiyi.com)

#ifndef BRPC_POLICY_COUCHBASE_LOAD_BALANCER_H
#define BRPC_POLICY_COUCHBASE_LOAD_BALANCER_H

#include <vector>                                      // std::vector
#include <map>                                         // std::map
#include "butil/containers/doubly_buffered_data.h"
#include "brpc/couchbase_helper.h"
#include "brpc/load_balancer.h"
#include "butil/third_party/libvbucket/vbucket.h"

namespace brpc {

class CouchbaseRetryPolicy;

namespace policy {

using SocketId = brpc::SocketId;
	
class DetectedMaster {
public:
    DetectedMaster() : _verified(false), _id(0) {}
    butil::atomic<bool> _verified;
    butil::atomic<SocketId> _id;

private:
    DetectedMaster(const DetectedMaster&) = delete;
    DetectedMaster& operator=(const DetectedMaster&) = delete;
};

using DetectedVBucketMap = std::vector<DetectedMaster>;

// Only used by CouchbaseChannel. It selects server according to request key
// which is brought in request_code.
class CouchbaseLoadBalancer : public brpc::LoadBalancer {
friend class brpc::CouchbaseHelper;
public:
    bool AddServer(const ServerId& id);
    bool RemoveServer(const ServerId& id);
    size_t AddServersInBatch(const std::vector<ServerId>& servers);
    size_t RemoveServersInBatch(const std::vector<ServerId>& servers);
    int SelectServer(const SelectIn& in, SelectOut* out);
    CouchbaseLoadBalancer* New() const;
    void Destroy();
    void Describe(std::ostream&, const DescribeOptions& options);

private:	
    const VBucketServerMap* vbucket_map();

	  bool IsInRebalancing(const VBucketServerMap* vb_map) const {
        return !vb_map->fvbucket.empty();
    }

    bool GetVBucketMapInfo(size_t* server_num, size_t* vb_num, 
                           bool* rebalance) {
        butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vb_map;
        if(_vbucket_map.Read(&vb_map) == 0) {
            if (server_num != nullptr) {
                *server_num = vb_map->server_list.size();
            }
            if (vb_num != nullptr) {
                *vb_num = vb_map->vbucket.size();
            }
            if (rebalance != nullptr) {
                *rebalance = IsInRebalancing(vb_map.get());
            }
            return true;
        }
        return false;
    }
		
    bool UpdateVBucketMap(butil::VBUCKET_CONFIG_HANDLE vb_conf);

    static bool Update(VBucketServerMap& vbucket_map, 
                       const size_t num_replicas,
                       std::vector<std::vector<int>>& vbucket,
                       std::vector<std::vector<int>>& fvbucket,
                       std::vector<brpc::SocketId>& server_list);
	
    SocketId GetDetectedMaster(const VBucketServerMap* vb_map, 
                               const size_t vb_id);

    SocketId GetNextProbeId(const VBucketServerMap* vb_map, 
                            const size_t vb_id, const SocketId id);

    void UpdateDetectedMasterIfNeeded(
        const bool is_reading_replicas, const uint32_t vb_id, 
        const uint32_t reason, const std::string& curr_server);
	
    // We need detect new vbucket map due to current vbucket map is invalid 
    // during rebalance.
    std::unique_ptr<DetectedVBucketMap> _detected_vbucket_map;

    butil::DoublyBufferedData<VBucketServerMap> _vbucket_map;
};

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_COUCHBASE_LOAD_BALANCER_H
