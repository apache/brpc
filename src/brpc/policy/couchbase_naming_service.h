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

#include <unordered_map>
#include "brpc/periodic_naming_service.h"

namespace brpc {

class CouchbaseServerListener;

}

namespace brpc {
namespace policy {

// It is only used for couchbase channel. It updates servers for listen channel 
// of CouchbaseServerListener. The naming service format is like
// "couchbase_list://addr1:port,addr:port_****" where "_****" is a unique id for
// each couchbase channel since we can not share naming service and "addr*:port"
// are avalible servers for initializing.
// After initialization, it get the latest server list periodically from 
// 'servers_map' by service name as key. 
class CouchbaseNamingService : public PeriodicNamingService {
friend brpc::CouchbaseServerListener;
private:
    static butil::Mutex _mutex; 
    // Store the lastest server list for each couchbase channel.
    // Key is service name of each couchbase channel and value is the latest 
    // server list. It is like following:
    // key: addr1:port,addr2:port_****
    // value: addr1:port,addr2:port,addr3:port 
    static std::unordered_map<std::string, std::string> servers_map;

    int GetServers(const char *service_name,
                   std::vector<ServerNode>* servers);
    
    static bool ParseNamingServiceUrl(butil::StringPiece ns_url, 
                                      std::string* listen_port);

    static bool ParseListenUrl(
        const butil::StringPiece listen_url, std::string* server_address, 
        std::string* streaming_uri, std::string* init_uri);
    
    // Clear naming server data when couchbase channel destroyed.
    static void ClearNamingServiceData(const std::string& service_name) {
        BAIDU_SCOPED_LOCK(_mutex);
        servers_map.erase(service_name);
    }

    // Called by couchbase listener when vbucekt map changing. 
    // It set new server list for key 'service_name' in servers_map.
    static void ResetCouchbaseListenerServers(const std::string& service_name, 
                                              std::string& new_servers);

    // For couchbase listeners, we should not share this name service object.
    // So we append couchbase listener address to make name_url unique.
    // Input: couchbase_list://address1:port1,address2:port2
    // Output: couchbase_list://address1:port1,address2:port2_****
    static std::string AddUniqueSuffix(const char* name_url, 
                                       const char* unique_id);

    // Reserve handling to AddPrefixBeforeAddress.
    void RemoveUniqueSuffix(std::string& name_service);

    void Describe(std::ostream& os, const DescribeOptions& options) const;

    NamingService* New() const;

    void Destroy();
};

}  // namespace policy
} // namespace brpc

#endif  //BRPC_POLICY_COUCHBASE_NAMING_SERVICE
