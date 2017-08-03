// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Wed Mar  4 10:49:35 2015

#include "brpc/details/load_balancer_with_naming.h"


namespace brpc {

LoadBalancerWithNaming::~LoadBalancerWithNaming() {
    if (_nsthread_ptr.get()) {
        _nsthread_ptr->RemoveWatcher(this);
    }
}

int LoadBalancerWithNaming::Init(const char* ns_url, const char* lb_name,
                                 const NamingServiceFilter* filter,
                                 const GetNamingServiceThreadOptions* options) {
    if (SharedLoadBalancer::Init(lb_name) != 0) {
        return -1;
    }
    if (GetNamingServiceThread(&_nsthread_ptr, ns_url, options) != 0) {
        LOG(FATAL) << "Fail to get NamingServiceThread";
        return -1;
    }
    if (_nsthread_ptr->AddWatcher(this, filter) != 0) {
        LOG(FATAL) << "Fail to add watcher into _server_list";
        return -1;
    }
    return 0;
}

void LoadBalancerWithNaming::OnAddedServers(
    const std::vector<ServerId>& servers) {
    AddServersInBatch(servers);
}

void LoadBalancerWithNaming::OnRemovedServers(
    const std::vector<ServerId>& servers) {
    RemoveServersInBatch(servers);
}

void LoadBalancerWithNaming::Describe(std::ostream& os,
                                      const DescribeOptions& options) {
    if (_nsthread_ptr) {
        _nsthread_ptr->Describe(os, options);
    } else {
        os << "NULL";
    }
    os << " lb=";
    SharedLoadBalancer::Describe(os, options);
}

} // namespace brpc

