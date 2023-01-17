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


#ifndef BRPC_POLICY_RANDOMIZED_LOAD_BALANCER_H
#define BRPC_POLICY_RANDOMIZED_LOAD_BALANCER_H

#include <vector>                                      // std::vector
#include <map>                                         // std::map
#include "butil/containers/doubly_buffered_data.h"
#include "brpc/load_balancer.h"
#include "brpc/cluster_recover_policy.h"

namespace brpc {
namespace policy {

// This LoadBalancer selects servers randomly using a thread-specific random
// number. Selected numbers of servers(added at the same time) are less close
// than RoundRobinLoadBalancer.
class RandomizedLoadBalancer : public LoadBalancer {
public:
    bool AddServer(const ServerId& id);
    bool RemoveServer(const ServerId& id);
    size_t AddServersInBatch(const std::vector<ServerId>& servers);
    size_t RemoveServersInBatch(const std::vector<ServerId>& servers);
    int SelectServer(const SelectIn& in, SelectOut* out);
    RandomizedLoadBalancer* New(const butil::StringPiece&) const;
    void Destroy();
    void Describe(std::ostream& os, const DescribeOptions&);
    
private:
    struct Servers {
        std::vector<ServerId> server_list;
        std::map<ServerId, size_t> server_map;
    };
    bool SetParameters(const butil::StringPiece& params);
    static bool Add(Servers& bg, const ServerId& id);
    static bool Remove(Servers& bg, const ServerId& id);
    static size_t BatchAdd(Servers& bg, const std::vector<ServerId>& servers);
    static size_t BatchRemove(Servers& bg, const std::vector<ServerId>& servers);

    butil::DoublyBufferedData<Servers> _db_servers;
    std::shared_ptr<ClusterRecoverPolicy> _cluster_recover_policy;
};

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_RANDOMIZED_LOAD_BALANCER_H
