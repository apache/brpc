// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Mar  3 16:30:03 2015

#ifndef BRPC_LOAD_BALANCER_WITH_NAMING_H
#define BRPC_LOAD_BALANCER_WITH_NAMING_H

#include "base/intrusive_ptr.hpp"
#include "brpc/load_balancer.h"
#include "brpc/details/naming_service_thread.h"         // NamingServiceWatcher


namespace brpc {

class LoadBalancerWithNaming : public SharedLoadBalancer,
                               public NamingServiceWatcher {
public:
    LoadBalancerWithNaming() {}
    ~LoadBalancerWithNaming();

    int Init(const char* ns_url, const char* lb_name,
             const NamingServiceFilter* filter,
             const GetNamingServiceThreadOptions* options);
    
    void OnAddedServers(const std::vector<ServerId>& servers);
    void OnRemovedServers(const std::vector<ServerId>& servers);

    void Describe(std::ostream& os, const DescribeOptions& options);

private:
    base::intrusive_ptr<NamingServiceThread> _nsthread_ptr;
};

} // namespace brpc


#endif // BRPC_LOAD_BALANCER_WITH_NAMING_H

