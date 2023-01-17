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


#ifndef  BRPC_POLICY_DOMAIN_NAMING_SERVICE_H
#define  BRPC_POLICY_DOMAIN_NAMING_SERVICE_H

#include "brpc/periodic_naming_service.h"
#include "butil/unique_ptr.h"


namespace brpc {
namespace policy {

class DomainNamingService : public PeriodicNamingService {
public:
    DomainNamingService(int default_port);
    DomainNamingService() : DomainNamingService(80) {}

private:
    int GetServers(const char *service_name,
                   std::vector<ServerNode>* servers) override;

    void Describe(std::ostream& os, const DescribeOptions&) const override;

    NamingService* New() const override;
    
    void Destroy() override;

private:
    std::unique_ptr<char[]> _aux_buf;
    size_t _aux_buf_len;
    int _default_port;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_DOMAIN_NAMING_SERVICE_H
