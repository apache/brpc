// Copyright (c) 2014 Baidu, Inc.G
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

// Authors: Yaofu Zhang (zhangyaofu@qiyi.com)

#ifndef  BRPC_POLICY_CONSUL_NAMING_SERVICE
#define  BRPC_POLICY_CONSUL_NAMING_SERVICE

#include "brpc/naming_service.h"
#include "brpc/channel.h"


namespace brpc {
class Channel;
namespace policy {

class ConsulNamingService : public NamingService {
private:
    int RunNamingService(const char* service_name,
                         NamingServiceActions* actions);

    int GetServers(const char* service_name,
                   std::vector<ServerNode>* servers);

    void Describe(std::ostream& os, const DescribeOptions&) const;

    NamingService* New() const;

    int DegradeToOtherServiceIfNeeded(const char* service_name,
                                      std::vector<ServerNode>* servers);

    void Destroy();

private:
    Channel _channel;
    std::string _consul_index;
    std::string _consul_url;
    bool _backup_file_loaded = false;
    bool _consul_connected = false;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_CONSUL_NAMING_SERVICE
