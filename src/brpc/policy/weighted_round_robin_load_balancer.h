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

#ifndef BRPC_POLICY_WEIGHTED_ROUND_ROBIN_LOAD_BALANCER_H
#define BRPC_POLICY_WEIGHTED_ROUND_ROBIN_LOAD_BALANCER_H

#include <map>                              
#include <vector>
#include "butil/containers/doubly_buffered_data.h"
#include "brpc/load_balancer.h"

namespace brpc {
namespace policy {

// This LoadBalancer selects server as the assigned weight.
// Weight is got from tag of ServerId.
class WeightedRoundRobinLoadBalancer : public LoadBalancer {
public:
    bool AddServer(const ServerId& id);
    bool RemoveServer(const ServerId& id);
    size_t AddServersInBatch(const std::vector<ServerId>& servers);
    size_t RemoveServersInBatch(const std::vector<ServerId>& servers);
    int SelectServer(const SelectIn& in, SelectOut* out);
    LoadBalancer* New() const;
    void Destroy();
    void Describe(std::ostream&, const DescribeOptions& options);

private:
    struct Servers {
        // The value is configured weight for each server.
        std::vector<std::pair<SocketId, int>> server_list;
        // The value is the index of the server in "server_list".
        std::map<SocketId, size_t> server_map;
    };
    // The value is current weight for a server. 
    // It will be changed in the selection of servers.
    using TLS = std::map<SocketId, int>;
    static bool Add(Servers& bg, const ServerId& id);
    static bool Remove(Servers& bg, const ServerId& id);
    static size_t BatchAdd(Servers& bg, const std::vector<ServerId>& servers);
    static size_t BatchRemove(Servers& bg, const std::vector<ServerId>& servers);

    butil::DoublyBufferedData<Servers, TLS> _db_servers;
};

}  // namespace policy
} // namespace brpc

#endif  // BRPC_POLICY_WEIGHTED_ROUND_ROBIN_LOAD_BALANCER_H
