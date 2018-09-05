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

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_LOAD_BALANCER_WITH_NAMING_H
#define BRPC_LOAD_BALANCER_WITH_NAMING_H

#include "butil/intrusive_ptr.hpp"
#include "brpc/details/shared_load_balancer.h"
#include "brpc/details/shared_naming_service.h"    // NamingServiceWatcher


namespace brpc {

class LoadBalancerWithNaming : public SharedLoadBalancer,
                               public NamingServiceWatcher {
public:
    LoadBalancerWithNaming() {}
    ~LoadBalancerWithNaming();

    int Init(const char* ns_url, const char* lb_name,
             const NamingServiceFilter* filter,
             const GetSharedNamingServiceOptions* options);

protected:
    void OnAddedServers(const std::vector<ServerId>& servers);
    void OnRemovedServers(const std::vector<ServerId>& servers);

    void Describe(std::ostream& os, const DescribeOptions& options);

private:
    butil::intrusive_ptr<SharedNamingService> _nsthread_ptr;
};

} // namespace brpc


#endif // BRPC_LOAD_BALANCER_WITH_NAMING_H
