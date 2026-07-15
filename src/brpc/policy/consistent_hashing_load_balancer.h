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


#ifndef  BRPC_CONSISTENT_HASHING_LOAD_BALANCER_H
#define  BRPC_CONSISTENT_HASHING_LOAD_BALANCER_H

#include <stdint.h>                                     // uint32_t
#include <functional>
#include <memory>                                       // std::shared_ptr
#include <vector>                                       // std::vector
#include "butil/endpoint.h"                              // butil::EndPoint
#include "butil/containers/doubly_buffered_data.h"
#include "butil/containers/flat_map.h"                   // FlatMap
#include "brpc/load_balancer.h"


namespace brpc {
namespace policy {

class ReplicaPolicy;

enum ConsistentHashingLoadBalancerType {
    CONS_HASH_LB_MURMUR3 = 0,
    CONS_HASH_LB_MD5 = 1,
    CONS_HASH_LB_KETAMA = 2,

    // Identify the last one.
    CONS_HASH_LB_LAST = 3
};

class ConsistentHashingLoadBalancer : public LoadBalancer {
public:
    struct Node {
        uint32_t hash;
        ServerId server_sock;
        butil::EndPoint server_addr;  // To make sorting stable among all clients
        bool operator<(const Node &rhs) const {
            if (hash < rhs.hash) { return true; }
            if (hash > rhs.hash) { return false; }
            if (server_addr < rhs.server_addr) { return true; }
            if (server_addr > rhs.server_addr) { return false; }
            // compare by tag if has the same ip-port
            return server_sock.tag < rhs.server_sock.tag;
        }
        bool operator<(const uint32_t code) const {
            return hash < code;
        }
    };
    explicit ConsistentHashingLoadBalancer(ConsistentHashingLoadBalancerType type);
    bool AddServer(const ServerId& server);
    bool RemoveServer(const ServerId& server);
    size_t AddServersInBatch(const std::vector<ServerId> &servers);
    size_t RemoveServersInBatch(const std::vector<ServerId> &servers);
    LoadBalancer *New(const butil::StringPiece& params) const;
    void Destroy();
    int SelectServer(const SelectIn &in, SelectOut *out);
    void Describe(std::ostream &os, const DescribeOptions& options);

protected:
    bool SetParameters(const butil::StringPiece& params);
    virtual bool SetParameter(const butil::StringPiece& key,
                              const butil::StringPiece& value);

    size_t _num_replicas;
    ConsistentHashingLoadBalancerType _type;
    butil::DoublyBufferedData<std::vector<Node> > _db_hash_ring;

private:
    void GetLoads(std::map<butil::EndPoint, double> *load_map);
    static size_t AddBatch(std::vector<Node> &bg, const std::vector<Node> &fg,
                           const std::vector<Node> &servers, bool *executed);
    static size_t RemoveBatch(std::vector<Node> &bg, const std::vector<Node> &fg,
                              const std::vector<ServerId> &servers, bool *executed);
    static size_t Remove(std::vector<Node> &bg, const std::vector<Node> &fg,
                         const ServerId& server, bool *executed);
};

// "Consistent Hashing with Bounded Loads" (Mirrokni et al., CACM 2017) on
// top of the parent's hash ring: a server whose in-flight count reached
// ceil(load_factor * average_inflight) overflows to the next server
// clockwise on the ring with spare capacity, so a hot key spills to its
// ring successors instead of saturating a single server.
class ConsistentHashingBoundedLoadBalancer : public ConsistentHashingLoadBalancer {
public:
    explicit ConsistentHashingBoundedLoadBalancer(
        ConsistentHashingLoadBalancerType type);
    bool AddServer(const ServerId& server) override;
    bool RemoveServer(const ServerId& server) override;
    size_t AddServersInBatch(const std::vector<ServerId>& servers) override;
    size_t RemoveServersInBatch(const std::vector<ServerId>& servers) override;
    LoadBalancer* New(const butil::StringPiece& params) const override;
    int SelectServer(const SelectIn& in, SelectOut* out) override;
    void Feedback(const CallInfo& info) override;
    void Describe(std::ostream& os, const DescribeOptions& options) override;

private:
    struct ServerLoad {
        ServerLoad() : inflight(0) {}
        butil::atomic<int32_t> inflight;
    };
    // Counters are shared by both buffers like p2c's NodeStat.
    typedef butil::FlatMap<SocketId, std::shared_ptr<ServerLoad> > LoadMap;

    bool SetParameter(const butil::StringPiece& key,
                      const butil::StringPiece& value) override;
    // Rebuild the load map from the sockets currently on the ring, keeping
    // the counters of servers that stay.
    void SyncLoadMap();
    static size_t ResetLoads(LoadMap& bg, const LoadMap& fg,
                             const std::vector<SocketId>& ids);

    double _load_factor;
    butil::atomic<int64_t> _total_inflight;
    butil::DoublyBufferedData<LoadMap> _db_load_map;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_CONSISTENT_HASHING_LOAD_BALANCER_H
