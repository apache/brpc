// Copyright (c) 2015 Baidu, Inc.
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

// Authors: Zhangyi Chen (chenzhangyi01@baidu.com)

#ifndef  BRPC_CONSISTENT_HASHING_LOAD_BALANCER_H
#define  BRPC_CONSISTENT_HASHING_LOAD_BALANCER_H

#include <stdint.h>                                     // uint32_t
#include <vector>                                       // std::vector
#include "butil/endpoint.h"                              // butil::EndPoint
#include "butil/containers/doubly_buffered_data.h"
#include "brpc/load_balancer.h"


namespace brpc {
namespace policy {

class ConsistentHashingLoadBalancer : public LoadBalancer {
public:
    typedef uint32_t (*HashFunc)(const void* key, size_t len);
    explicit ConsistentHashingLoadBalancer(HashFunc hash);
    ConsistentHashingLoadBalancer(HashFunc hash, size_t num_replicas);
    bool AddServer(const ServerId& server);
    bool RemoveServer(const ServerId& server);
    size_t AddServersInBatch(const std::vector<ServerId> &servers);
    size_t RemoveServersInBatch(const std::vector<ServerId> &servers);
    LoadBalancer *New() const;
    void Destroy();
    int SelectServer(const SelectIn &in, SelectOut *out);
    void Describe(std::ostream &os, const DescribeOptions& options);

private:
    void GetLoads(std::map<butil::EndPoint, double> *load_map);
    struct Node {
        uint32_t hash;
        ServerId server_sock;
        butil::EndPoint server_addr;  // To make sorting stable among all clients
        bool operator<(const Node &rhs) const {
            if (hash < rhs.hash) { return true; }
            if (hash > rhs.hash) { return false; }
            return server_addr < rhs.server_addr;
        }
        bool operator<(const uint32_t code) const {
            return hash < code;
        }
    };
    static size_t AddBatch(std::vector<Node> &bg, const std::vector<Node> &fg,
                           const std::vector<Node> &servers, bool *executed);
    static size_t RemoveBatch(std::vector<Node> &bg, const std::vector<Node> &fg,
                              const std::vector<ServerId> &servers, bool *executed);
    static size_t Remove(std::vector<Node> &bg, const std::vector<Node> &fg,
                         const ServerId& server, bool *executed);
    HashFunc _hash;
    size_t _num_replicas;
    butil::DoublyBufferedData<std::vector<Node> > _db_hash_ring;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_CONSISTENT_HASHING_LOAD_BALANCER_H
