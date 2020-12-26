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


#ifndef BRPC_POLICY_WEIGHTED_RANDOMIZED_LOAD_BALANCER_H
#define BRPC_POLICY_WEIGHTED_RANDOMIZED_LOAD_BALANCER_H

#include <map>
#include <vector>
#include "butil/containers/doubly_buffered_data.h"
#include "brpc/load_balancer.h"

namespace brpc {
namespace policy {

// This LoadBalancer selects server as the assigned weight.
// Weight is got from tag of ServerId.
class WeightedRandomizedLoadBalancer : public LoadBalancer {
public:
    bool AddServer(const ServerId& id);
    bool RemoveServer(const ServerId& id);
    size_t AddServersInBatch(const std::vector<ServerId>& servers);
    size_t RemoveServersInBatch(const std::vector<ServerId>& servers);
    int SelectServer(const SelectIn& in, SelectOut* out);
    LoadBalancer* New(const butil::StringPiece&) const;
    void Destroy();
    void Describe(std::ostream& os, const DescribeOptions&);

    struct Server {
        Server(SocketId s_id = 0, uint32_t s_w = 0, uint64_t s_c_w_s = 0): id(s_id), weight(s_w), current_weight_sum(s_c_w_s) {}
        SocketId id;
        uint32_t weight;
        uint64_t current_weight_sum;
    };
    struct Servers {
        // The value is configured weight and weight_sum for each server.
        std::vector<Server> server_list;
        // The value is the index of the server in "server_list".
        std::map<SocketId, size_t> server_map;
        uint64_t weight_sum;
        Servers() : weight_sum(0) {}
    };

private:
    static bool Add(Servers& bg, const ServerId& id);
    static bool Remove(Servers& bg, const ServerId& id);
    static size_t BatchAdd(Servers& bg, const std::vector<ServerId>& servers);
    static size_t BatchRemove(Servers& bg, const std::vector<ServerId>& servers);

    butil::DoublyBufferedData<Servers> _db_servers;
};

}  // namespace policy
} // namespace brpc

#endif  // BRPC_POLICY_WEIGHTED_RANDOMIZED_LOAD_BALANCER_H
