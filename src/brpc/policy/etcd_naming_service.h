// Copyright (c) 2014 Baidu, Inc.
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

// Authors: Kevin.XU (xuhuahai@sogou-inc.com)

#ifndef  BRPC_POLICY_ETCD_NAMING_SERVICE_H
#define  BRPC_POLICY_ETCD_NAMING_SERVICE_H

#include <vector>

#include "brpc/periodic_naming_service.h"
#include "butil/third_party/etcdc/cetcd.h"

namespace brpc {
namespace policy {


// Acquire server list from Etcd, aka ETCDNS
class EtcdNamingService : public PeriodicNamingService {
public:
    // You can specify port by appending port selector:
    // e.g.: bns://DPOP-inner-API-inner-API.jpaas.hosts:main
    //                                                 ^^^^^
    int GetServers(const char *service_name,
                   std::vector<ServerNode>* servers);
    
    void CollectProviders(cetcd_response_node *node, std::vector<std::string>& result);

    void Describe(std::ostream& os, const DescribeOptions&) const;
    
    NamingService* New() const;
    
    void Destroy();
};

}  // namespace policy
} // namespace brpc

#endif //BRPC_POLICY_ETCD_NAMING_SERVICE_H

