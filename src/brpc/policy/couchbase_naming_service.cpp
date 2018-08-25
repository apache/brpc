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
#include "brpc/policy/couchbase_naming_service.h"

namespace brpc {
namespace policy {

// Defined in file_naming_service.cpp
bool SplitIntoServerAndTag(const butil::StringPiece& line,
                           butil::StringPiece* server_addr,
                           butil::StringPiece* tag);

butil::Mutex CouchbaseNamingService::_mutex; 
std::unordered_map<std::string, std::string> CouchbaseNamingService::servers_map;

bool CouchbaseNamingService::ParseListenUrl(
    const butil::StringPiece listen_url, std::string* server, 
    std::string* streaming_uri, std::string* init_uri) {
    do {
        const size_t pos = listen_url.find("//");
        if (pos == listen_url.npos) {
            break;
        }
        const size_t host_pos = listen_url.find('/', pos + 2);        
        if (host_pos == listen_url.npos) {
            break;
        }
        butil::StringPiece sub_str = listen_url.substr(pos + 2, host_pos - pos - 2);
        server->clear();
        server->append(sub_str.data(), sub_str.length());
        butil::EndPoint point;
        if (butil::str2endpoint(server->c_str(), &point) != 0) {
            LOG(FATAL) << "Failed to get address and port \'" 
                       << server << "\'.";
            break; 
        }
        butil::StringPiece uri_sub = listen_url;
        uri_sub.remove_prefix(host_pos);
        size_t uri_pos = uri_sub.find("/bucketsStreaming/");
        if (uri_pos != uri_sub.npos) {
            streaming_uri->clear();
            streaming_uri->append(uri_sub.data(), uri_sub.length()); 
            init_uri->clear();
            init_uri->append(uri_sub.data(), uri_pos);
            init_uri->append("/buckets/");
            butil::StringPiece bucket_name = uri_sub;
            bucket_name.remove_prefix(uri_pos + std::strlen("/bucketsStreaming/"));
            init_uri->append(bucket_name.data(), bucket_name.length());
            return true;
        }
        uri_pos = uri_sub.find("/buckets/");
        if (uri_pos != uri_sub.npos) {
            init_uri->clear();
            init_uri->append(uri_sub.data(), uri_sub.length()); 
            streaming_uri->clear();
            streaming_uri->append(uri_sub.data(), uri_pos);
            streaming_uri->append("/bucketsStreaming/");
            butil::StringPiece bucket_name = uri_sub; 
            bucket_name.remove_prefix(uri_pos + std::strlen("/buckets/"));
            streaming_uri->append(bucket_name.data(), bucket_name.length());
            return true;
        }
    } while (false);
    LOG(FATAL) << "Failed to parse listen url \'" <<  listen_url << "\'.";
    return false;
}

bool CouchbaseNamingService::ParseNamingServiceUrl(const butil::StringPiece ns_url, 
                                                   std::string* listen_port) {
    butil::StringPiece protocol;
    std::string server_list;
    const size_t pos = ns_url.find("//");
    if (pos != ns_url.npos) {
        protocol = ns_url.substr(0, pos);
        butil::StringPiece sub = ns_url.substr(pos+2);
        server_list.append(sub.data(), sub.length());
    }
    if (protocol != "couchbase_list:" && server_list.empty()) {
        LOG(FATAL) << "Invalid couchbase naming service  " << ns_url;
        return false;
    }
    std::vector<std::string> server_array;
    butil::SplitString(server_list, ',', &server_array);
    listen_port->clear();
    for (const std::string& addr_port : server_array) {
        butil::EndPoint point;
        if (butil::str2endpoint(addr_port.c_str(), &point) != 0) {
            LOG(FATAL) << "Failed to get endpoint from \'" << addr_port 
                       << "\' of the naming server url \'" << ns_url << "\'.";
            return false;
        }
        if (listen_port->empty()) {
            *listen_port = butil::IntToString(point.port);
        }
    }    
    return true;
}

int CouchbaseNamingService::GetServers(const char *service_name,
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
    std::string new_servers(service_name);
    {
        BAIDU_SCOPED_LOCK(_mutex);
        const auto& iter = servers_map.find(new_servers);
        if (iter != servers_map.end()) {
            new_servers = iter->second;
        }
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

void CouchbaseNamingService::Describe(
    std::ostream& os, const DescribeOptions&) const {
    os << "Couchbase_list";
    return;
}

NamingService* CouchbaseNamingService::New() const {
    return new CouchbaseNamingService;
}

void CouchbaseNamingService::Destroy() {
    delete this;
}

void CouchbaseNamingService::ResetCouchbaseListenerServers(
    const std::string& service_name, std::string& new_servers) {
    BAIDU_SCOPED_LOCK(_mutex);
    auto iter = servers_map.find(service_name);
    if (iter != servers_map.end()) {
        iter->second.swap(new_servers);
    } else {
        servers_map.emplace(service_name, new_servers);
    }
}

std::string CouchbaseNamingService::AddUniqueSuffix(
    const char* name_url, const char* unique_id) {
    std::string couchbase_name_url;
    couchbase_name_url.append(name_url);
    couchbase_name_url.append(1, '_');
    couchbase_name_url.append(unique_id);
    return std::move(couchbase_name_url);
}

void CouchbaseNamingService::RemoveUniqueSuffix(std::string& name_service) {
    const size_t pos = name_service.find('_');
    if (pos != std::string::npos) {
        name_service.resize(pos);
    }
}

}  // namespace policy
} // namespace brpc
