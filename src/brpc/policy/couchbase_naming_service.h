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

class CouchbaseChannel;
class NamingServiceActions;

namespace policy {

class CouchbaseServerListener;

class CouchbaseNamingService : public NamingService {
friend class brpc::CouchbaseChannel;
friend class CouchbaseServerListener;
public:
    CouchbaseNamingService();
    ~CouchbaseNamingService();

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

    static std::string BuildNsUrl(
        const char* servers_addr, const std::string& streaming_url, 
        const std::string& init_url, const std::string& auth, 
        const std::string& unique_id);
	
    bool ParseNsUrl(const butil::StringPiece service_full_name, 
                    butil::StringPiece& server_list, 
                    butil::StringPiece& streaming_url, 
                    butil::StringPiece& init_url, butil::StringPiece& auth);

    void SetVBucketNumber(const size_t vb_num);

    static size_t GetVBucketNumber(const std::string& service_name);

    static std::map<std::string, CouchbaseNamingService*> _vbucket_num_map;

    butil::atomic<size_t> _vbucket_num;

    // Naming service of this CouchbaseNamingService.
    std::string _service_name;
	
    NamingServiceActions* _actions;
    // Listener monitor and update vbucket map.
    std::unique_ptr<CouchbaseServerListener> _listener;
};

}  // namespace policy
} // namespace brpc


#endif  //BRPC_POLICY_COUCHBASE_NAMING_SERVICE
