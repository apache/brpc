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


#include <stdlib.h>                                   // strtol
#include <string>                                     // std::string
#include <set>                                        // std::set
#include "butil/string_splitter.h"                     // StringSplitter
#include "brpc/log.h"
#include "brpc/policy/list_naming_service.h"


namespace brpc {
namespace policy {

// Defined in file_naming_service.cpp
bool SplitIntoServerAndTag(const butil::StringPiece& line,
                           butil::StringPiece* server_addr,
                           butil::StringPiece* tag);

int ListNamingService::GetServers(const char *service_name,
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
    for (butil::StringSplitter sp(service_name, ','); sp != NULL; ++sp) {
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

int ListNamingService::RunNamingService(const char* service_name,
                                        NamingServiceActions* actions) {
    std::vector<ServerNode> servers;
    const int rc = GetServers(service_name, &servers);
    if (rc != 0) {
        servers.clear();
    }
    actions->ResetServers(servers);
    return 0;
}

void ListNamingService::Describe(
    std::ostream& os, const DescribeOptions&) const {
    os << "list";
    return;
}

NamingService* ListNamingService::New() const {
    return new ListNamingService;
}

void ListNamingService::Destroy() {
    delete this;
}

}  // namespace policy
} // namespace brpc
