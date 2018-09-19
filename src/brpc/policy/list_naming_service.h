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

#ifndef  BRPC_POLICY_LIST_NAMING_SERVICE
#define  BRPC_POLICY_LIST_NAMING_SERVICE

#include "brpc/naming_service.h"


namespace brpc {
namespace policy {

class ListNamingService : public NamingService {
public:
    ListNamingService() {}
    ListNamingService(const char* servers) 
        : _return_quickly(false), _allow_update(true), _server_list(servers) {}

    void UpdateServerList(std::string* server_list);
	
private:
    int RunNamingService(const char* service_name,
                         NamingServiceActions* actions);

    // We don't need a dedicated bthread to run this static NS.
    bool RunNamingServiceReturnsQuickly() { return _return_quickly; }
    
    int GetServers(const char *service_name,
                   std::vector<ServerNode>* servers);

    std::string GetServerList();

    void Describe(std::ostream& os, const DescribeOptions& options) const;

    NamingService* New() const;
    
    void Destroy();
	
    bool allow_update() const { return _allow_update; }

    bool _return_quickly = true;
	
    bool _allow_update = false;
	
    butil::Mutex _mutex; 
	
    std::string _server_list;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_LIST_NAMING_SERVICE
