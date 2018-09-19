// Copyright (c) 2018 Iqiyi, Inc.
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

// Authors: Cai,Daojin (caidaojin@qiyi.com)

#ifndef  BRPC_POLICY_COUCHBASE_NAMING_SERVICE
#define  BRPC_POLICY_COUCHBASE_NAMING_SERVICE

#include <map>
#include "brpc/naming_service.h"
#include "butil/macros.h"

namespace brpc {

class NamingServiceActions;

namespace policy {

class CouchbaseServerListener;

class CouchbaseNamingService : public NamingService {
friend class CouchbaseServerListener;
public:
    CouchbaseNamingService(
        const char* servers, const std::string& init_url,
        const std::string& streaming_url, const std::string& auth);
    CouchbaseNamingService();
    ~CouchbaseNamingService();

    virtual bool PrintServerChangeLogsEveryTime() { return false; }

private:
    DISALLOW_COPY_AND_ASSIGN(CouchbaseNamingService);

    int RunNamingService(const char* service_name,
                         NamingServiceActions* actions);

    // We need a dedicated bthread to run this static NS.
    bool RunNamingServiceReturnsQuickly() { return false; }
    
    int ResetServers(const std::vector<std::string>& servers, std::string& vb_map);

    void Describe(std::ostream& os, const DescribeOptions& options) const;

    NamingService* New() const;
    
    void Destroy();

    std::string _initial_servers;

    std::string _init_url;

    std::string _streaming_url;

    std::string _auth;
	
    NamingServiceActions* _actions;
    // Listener monitor and update vbucket map.
    std::unique_ptr<CouchbaseServerListener> _listener;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_COUCHBASE_NAMING_SERVICE
