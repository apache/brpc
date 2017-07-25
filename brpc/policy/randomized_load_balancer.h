// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Sep 29 13:33:36 CST 2014

#ifndef BRPC_POLICY_RANDOMIZED_LOAD_BALANCER_H
#define BRPC_POLICY_RANDOMIZED_LOAD_BALANCER_H

#include <vector>                                      // std::vector
#include <map>                                         // std::map
#include "base/containers/doubly_buffered_data.h"
#include "brpc/load_balancer.h"


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
    RandomizedLoadBalancer* New() const;
    void Destroy();
    void Describe(std::ostream& os, const DescribeOptions&);
    
private:
    struct Servers {
        std::vector<ServerId> server_list;
        std::map<ServerId, size_t> server_map;
    };
    static bool Add(Servers& bg, const ServerId& id);
    static bool Remove(Servers& bg, const ServerId& id);
    static size_t BatchAdd(Servers& bg, const std::vector<ServerId>& servers);
    static size_t BatchRemove(Servers& bg, const std::vector<ServerId>& servers);

    base::DoublyBufferedData<Servers> _db_servers;
};

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_RANDOMIZED_LOAD_BALANCER_H
