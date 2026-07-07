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


#ifndef BRPC_POLICY_P2C_EWMA_LOAD_BALANCER_H
#define BRPC_POLICY_P2C_EWMA_LOAD_BALANCER_H

#include <memory>                                      // std::shared_ptr
#include <vector>                                      // std::vector
#include "butil/containers/flat_map.h"                 // FlatMap
#include "butil/containers/doubly_buffered_data.h"     // DoublyBufferedData
#include "brpc/load_balancer.h"

namespace brpc {
namespace policy {

// Power-of-Two-Choices with Peak-EWMA latency scoring ("p2c").
// Each selection samples `choices'(default 2) distinct servers and routes
// to the one with the lower load score:
//   score = peak_ewma_latency_us * (inflight + 1) / weight
// The latency EWMA is peak-sensitive: an upward spike replaces the average
// immediately while recovery decays with time constant `tau_ms'(default 10s),
// so a degraded server is shed within one observation. Selection is O(1)
// regardless of fleet size. Weight is got from tag of ServerId(default 1).
class P2CEwmaLoadBalancer : public LoadBalancer {
public:
    P2CEwmaLoadBalancer();
    bool AddServer(const ServerId& id) override;
    bool RemoveServer(const ServerId& id) override;
    size_t AddServersInBatch(const std::vector<ServerId>& servers) override;
    size_t RemoveServersInBatch(const std::vector<ServerId>& servers) override;
    int SelectServer(const SelectIn& in, SelectOut* out) override;
    void Feedback(const CallInfo& info) override;
    P2CEwmaLoadBalancer* New(const butil::StringPiece& params) const override;
    void Destroy() override;
    void Describe(std::ostream& os, const DescribeOptions&) override;

private:
    // Mutable per-server load state. Allocated once when the server is first
    // added and shared by both buffers of _db_servers, so a stable pointer
    // can be used from SelectServer()/Feedback() without copying.
    struct NodeStat {
        NodeStat() : inflight(0), ewma_us(0), stamp_us(0) {}
        butil::atomic<int32_t> inflight;
        // Peak-sensitive EWMA of latency in us. 0 means no observation yet.
        butil::atomic<int64_t> ewma_us;
        // Time of the last EWMA update.
        butil::atomic<int64_t> stamp_us;
        butil::Mutex update_mutex;
    };
    struct ServerInfo {
        SocketId id;
        uint32_t weight;
        std::shared_ptr<NodeStat> stat;
    };
    struct Servers {
        std::vector<ServerInfo> server_list;
        // Maps SocketId to index in server_list.
        butil::FlatMap<SocketId, size_t> server_map;
    };

    static bool Add(Servers& bg, const Servers& fg, const ServerId& id);
    static bool Remove(Servers& bg, const ServerId& id);
    static size_t BatchAdd(Servers& bg, const Servers& fg,
                           const std::vector<ServerId>& servers);
    static size_t BatchRemove(Servers& bg, const std::vector<ServerId>& servers);

    bool SetParameters(const butil::StringPiece& params);
    // Load score of the server at `now_us'. Smaller is better.
    double Score(const ServerInfo& info, int64_t now_us) const;

    butil::DoublyBufferedData<Servers> _db_servers;
    uint32_t _choices;
    int64_t _tau_us;
};

}  // namespace policy
} // namespace brpc

#endif  // BRPC_POLICY_P2C_EWMA_LOAD_BALANCER_H
