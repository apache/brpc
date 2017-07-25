// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2015/06/09 13:48:22

#ifndef  BRPC_CONSISTENT_HASHING_LOAD_BALANCER_H
#define  BRPC_CONSISTENT_HASHING_LOAD_BALANCER_H

#include <stdint.h>                                     // uint32_t
#include <vector>                                       // std::vector
#include "base/endpoint.h"                              // base::EndPoint
#include "base/containers/doubly_buffered_data.h"
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
    void GetLoads(std::map<base::EndPoint, double> *load_map);
    struct Node {
        uint32_t hash;
        ServerId server_sock;
        base::EndPoint server_addr;  // To make sorting stable among all clients
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
    base::DoublyBufferedData<std::vector<Node> > _db_hash_ring;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_CONSISTENT_HASHING_LOAD_BALANCER_H
