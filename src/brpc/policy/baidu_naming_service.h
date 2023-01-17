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

#ifdef BAIDU_INTERNAL

#ifndef  BRPC_POLICY_BAIDU_NAMING_SERVICE_H
#define  BRPC_POLICY_BAIDU_NAMING_SERVICE_H

#include "brpc/periodic_naming_service.h"

namespace brpc {
namespace policy {

// Acquire server list from Baidu-Naming-Service, aka BNS
class BaiduNamingService : public PeriodicNamingService {
public:
    // You can specify port by appending port selector:
    // e.g.: bns://DPOP-inner-API-inner-API.jpaas.hosts:main
    //                                                 ^^^^^
    int GetServers(const char *service_name,
                   std::vector<ServerNode>* servers);
    
    void Describe(std::ostream& os, const DescribeOptions&) const;
    
    NamingService* New() const;
    
    void Destroy();
};

}  // namespace policy
} // namespace brpc

#endif //BRPC_POLICY_BAIDU_NAMING_SERVICE_H
#endif // BAIDU_INTERNAL
