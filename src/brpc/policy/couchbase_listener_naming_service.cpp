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

// Authors: Cai,Daojin (Caidaojin@qiyi.com)

#include <stdlib.h>                                   // strtol
#include <string>                                     // std::string
#include <set>                                        // std::set
#include "butil/string_splitter.h"                     // StringSplitter
#include "butil/strings/string_piece.h"
#include "butil/strings/string_split.h"
#include "butil/strings/string_number_conversions.h"
#include "brpc/log.h"
#include "brpc/policy/couchbase_listener_naming_service.h"

namespace brpc {
namespace policy {

// Defined in file_naming_service.cpp
bool SplitIntoServerAndTag(const butil::StringPiece& line,
                           butil::StringPiece* server_addr,
                           butil::StringPiece* tag);

butil::Mutex CouchbaseListenerNamingService::_mutex; 
std::unordered_map<std::string, std::string> CouchbaseListenerNamingService::servers_map;


int CouchbaseListenerNamingService::GetServers(const char* service_name,
                                               std::vector<ServerNode>* servers) {
    servers->clear();
    // Sort/unique the inserted vector is faster, but may have a different order
    // of addresses from the file. To make assertions in tests easier, we use
    // set to de-duplicate and keep the order.
    std::set<ServerNode> presence;
    std::string line;

    if (!service_name) {
        LOG(FATAL) << "Param[service_name] is NULL";
        return -1;
    }
    // Just handle at first time.
    if (_service_name.empty()) {
        _service_name = service_name;
        {
            BAIDU_SCOPED_LOCK(_mutex);
            servers_map.emplace(_service_name, _service_name);
        }
    }
    std::string new_servers;
    {
        BAIDU_SCOPED_LOCK(_mutex);
        new_servers = servers_map.at(_service_name);
    }
    RemoveUniqueSuffix(new_servers);
    for (butil::StringSplitter sp(new_servers.c_str(), ','); sp != NULL; ++sp) {
        line.assign(sp.field(), sp.length());
        butil::StringPiece addr;
        butil::StringPiece tag;
        if (!SplitIntoServerAndTag(line, &addr, &tag)) {
            continue;
        }
        const_cast<char*>(addr.data())[addr.size()] = '\0'; // safe
        butil::EndPoint point;
        if (str2endpoint(addr.data(), &point) != 0 &&
            hostname2endpoint(addr.data(), &point) != 0) {
            LOG(ERROR) << "Invalid address=`" << addr << '\'';
            continue;
        }
        ServerNode node;
        node.addr = point;
        tag.CopyToString(&node.tag);
        if (presence.insert(node).second) {
            servers->push_back(node);
        } else {
            RPC_VLOG << "Duplicated server=" << node;
        }
    }
    RPC_VLOG << "Got " << servers->size()
             << (servers->size() > 1 ? " servers" : " server");
    return 0;
}

void CouchbaseListenerNamingService::Describe(
    std::ostream& os, const DescribeOptions&) const {
    os << "couchbase_listener";
    return;
}

NamingService* CouchbaseListenerNamingService::New() const {
    return new CouchbaseListenerNamingService;
}

void CouchbaseListenerNamingService::Destroy() {
    {
        // Clear naming server data when couchbase channel destroyed.
        BAIDU_SCOPED_LOCK(_mutex);
        servers_map.erase(_service_name);
    }
    delete this;
}

void CouchbaseListenerNamingService::ResetCouchbaseListenerServers(
    const std::string& service_name, std::string& new_servers) {
    BAIDU_SCOPED_LOCK(_mutex);
    servers_map.at(service_name).swap(new_servers);
}

std::string CouchbaseListenerNamingService::AddUniqueSuffix(
    const char* name_url, const char* unique_id) {
    std::string couchbase_name_url;
    couchbase_name_url.append(name_url);
    couchbase_name_url.append(1, '_');
    couchbase_name_url.append(unique_id);
    return std::move(couchbase_name_url);
}

void CouchbaseListenerNamingService::RemoveUniqueSuffix(std::string& name_service) {
    const size_t pos = name_service.find('_');
    if (pos != std::string::npos) {
        name_service.resize(pos);
    }
}

}  // namespace policy
} // namespace brpc
