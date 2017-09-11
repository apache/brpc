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

#ifndef  BRPC_POLICY_HTTP_FILE_NAMING_SERVICE
#define  BRPC_POLICY_HTTP_FILE_NAMING_SERVICE

#include "brpc/periodic_naming_service.h"
#include "brpc/channel.h"
#include "butil/unique_ptr.h"


namespace brpc {
class Channel;
namespace policy {

class RemoteFileNamingService : public PeriodicNamingService {
private:
    int GetServers(const char* service_name,
                   std::vector<ServerNode>* servers);

    void Describe(std::ostream& os, const DescribeOptions&) const;

    NamingService* New() const;
    
    void Destroy();
    
private:
    std::unique_ptr<Channel> _channel;
    std::string _server_addr;
    std::string _path;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_HTTP_FILE_NAMING_SERVICE
