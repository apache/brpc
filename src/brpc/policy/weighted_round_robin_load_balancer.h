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
        uint32_t weight_sum = 0;
    };
    struct TLS {
        TLS(): remain_server(0, 0) { }
        uint32_t position = 0;
        uint32_t stride = 0;
        uint32_t offset = 0;
        std::pair<SocketId, int> remain_server;
        bool HasRemainServer() const {
            return remain_server.second != 0;
        }
        void SetRemainServer(const SocketId id, const int weight) {
            remain_server.first = id;
            remain_server.second = weight;
        }
        void ResetRemainServer() {
            remain_server.second = 0;
        }
        void UpdatePosition(const uint32_t size) {
            ++position;
            position %= size;
        }
        // If server list changed, we need caculate a new stride.
        bool IsNeededCaculateNewStride(const uint32_t curr_weight_sum, 
                                       const uint32_t curr_servers_num) {
            if (curr_weight_sum != weight_sum 
                || curr_servers_num != servers_num) {
                weight_sum = curr_weight_sum;
                servers_num = curr_servers_num;
                return true;
            }
            return false;
        }
    private:
        uint32_t weight_sum = 0;
        uint32_t servers_num = 0;
    };
    static bool Add(Servers& bg, const ServerId& id);
    static bool Remove(Servers& bg, const ServerId& id);
    static size_t BatchAdd(Servers& bg, const std::vector<ServerId>& servers);
    static size_t BatchRemove(Servers& bg, const std::vector<ServerId>& servers);
    static int64_t GetBestServer(
                       const std::vector<std::pair<SocketId, int>>& server_list,
                       TLS& tls, uint32_t stride);
    // Get a reasonable stride according to weights configured of servers. 
    static uint32_t GetStride(const uint32_t weight_sum, const uint32_t num);
    static void TryToGetFinalServer(const TLS& tls, 
                             const std::pair<SocketId, int> server, 
                             uint32_t& comp_weight, int64_t* final_server);

    butil::DoublyBufferedData<Servers, TLS> _db_servers;
};

}  // namespace policy
} // namespace brpc

#endif  // BRPC_POLICY_WEIGHTED_ROUND_ROBIN_LOAD_BALANCER_H
