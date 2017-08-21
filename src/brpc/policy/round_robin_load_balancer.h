// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Sep 29 13:33:36 CST 2014

#ifndef BRPC_POLICY_ROUND_ROBIN_LOAD_BALANCER_H
#define BRPC_POLICY_ROUND_ROBIN_LOAD_BALANCER_H

#include <vector>                                      // std::vector
#include <map>                                         // std::map
#include "base/containers/doubly_buffered_data.h"
#include "brpc/load_balancer.h"


namespace brpc {
namespace policy {

// This LoadBalancer selects server evenly. Selected numbers of servers(added
// at the same time) are very close.
class RoundRobinLoadBalancer : public LoadBalancer {
public:
    bool AddServer(const ServerId& id);
    bool RemoveServer(const ServerId& id);
    size_t AddServersInBatch(const std::vector<ServerId>& servers);
    size_t RemoveServersInBatch(const std::vector<ServerId>& servers);
    int SelectServer(const SelectIn& in, SelectOut* out);
    RoundRobinLoadBalancer* New() const;
    void Destroy();
    void Describe(std::ostream&, const DescribeOptions& options);

private:
    struct Servers {
        std::vector<ServerId> server_list;
        std::map<ServerId, size_t> server_map;
    };
    struct TLS {
        TLS() : stride(0), offset(0) { }
        uint32_t stride;
        uint32_t offset;
    };
    static bool Add(Servers& bg, const ServerId& id);
    static bool Remove(Servers& bg, const ServerId& id);
    static size_t BatchAdd(Servers& bg, const std::vector<ServerId>& servers);
    static size_t BatchRemove(Servers& bg, const std::vector<ServerId>& servers);

    base::DoublyBufferedData<Servers, TLS> _db_servers;
};

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_ROUND_ROBIN_LOAD_BALANCER_H
