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

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef  BRPC_POLICY_FILE_NAMING_SERVICE
#define  BRPC_POLICY_FILE_NAMING_SERVICE

#include "brpc/naming_service.h"


namespace brpc {
namespace policy {

class FileNamingService : public NamingService {
friend class ConsulNamingService;
private:
    int RunNamingService(const char* service_name,
                         NamingServiceActions* actions);

    int GetServers(const char *service_name,
                   std::vector<ServerNode>* servers);

    void Describe(std::ostream& os, const DescribeOptions&) const;

    NamingService* New() const;

    void Destroy();
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_FILE_NAMING_SERVICE
