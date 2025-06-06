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


#ifndef BRPC_POLICY_WEIGHTED_ROUND_ROBIN_LOAD_BALANCER_H
#define BRPC_POLICY_WEIGHTED_ROUND_ROBIN_LOAD_BALANCER_H

#include <map>                              
#include <vector>
#include <unordered_set>
#include "butil/containers/doubly_buffered_data.h"
#include "brpc/load_balancer.h"

namespace brpc {
namespace policy {

// This LoadBalancer selects server as the assigned weight.
// Weight is got from tag of ServerId.
class WeightedRoundRobinLoadBalancer : public LoadBalancer {
public:
    bool AddServer(const ServerId& id) override;
    bool RemoveServer(const ServerId& id) override;
    size_t AddServersInBatch(const std::vector<ServerId>& servers) override;
    size_t RemoveServersInBatch(const std::vector<ServerId>& servers) override;
    int SelectServer(const SelectIn& in, SelectOut* out) override;
    LoadBalancer* New(const butil::StringPiece&) const override;
    void Destroy() override;
    void Describe(std::ostream&, const DescribeOptions& options) override;

private:
    struct Server {
        Server(SocketId s_id = 0, uint32_t s_w = 0): id(s_id), weight(s_w) {}
        SocketId id;
        uint32_t weight;
    };
    struct Servers {
        // The value is configured weight for each server.
        std::vector<Server> server_list;
        // The value is the index of the server in "server_list".
        std::map<SocketId, size_t> server_map;
        uint64_t weight_sum = 0;
    };
    struct TLS {
        size_t position = 0;
        uint64_t stride = 0;
        Server remain_server;
        // If server list changed, we need calculate a new stride.
        bool IsNeededCalculateNewStride(const uint64_t curr_weight_sum,
                                        const size_t curr_servers_num) {
            if (curr_weight_sum != weight_sum 
                || curr_servers_num != servers_num) {
                weight_sum = curr_weight_sum;
                servers_num = curr_servers_num;
                return true;
            }
            return false;
        }
    private:
        uint64_t weight_sum = 0;
        size_t servers_num = 0;
    };
    static bool Add(Servers& bg, const ServerId& id);
    static bool Remove(Servers& bg, const ServerId& id);
    static size_t BatchAdd(Servers& bg, const std::vector<ServerId>& servers);
    static size_t BatchRemove(Servers& bg, const std::vector<ServerId>& servers);
    static SocketId GetServerInNextStride(const std::vector<Server>& server_list,
                                          const std::unordered_set<SocketId>& filter,
                                          TLS& tls);

    butil::DoublyBufferedData<Servers, TLS> _db_servers;
};

}  // namespace policy
} // namespace brpc

#endif  // BRPC_POLICY_WEIGHTED_ROUND_ROBIN_LOAD_BALANCER_H
