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
