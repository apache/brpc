// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Thu Jan  1 18:55:20 CST 2015

#include <stdlib.h>                                   // strtol
#include <string>                                     // std::string
#include <set>                                        // std::set
#include "base/string_splitter.h"                     // StringSplitter
#include "brpc/log.h"
#include "brpc/policy/list_naming_service.h"


namespace brpc {
namespace policy {

// Defined in file_naming_service.cpp
bool SplitIntoServerAndTag(const base::StringPiece& line,
                           base::StringPiece* server_addr,
                           base::StringPiece* tag);

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
    for (base::StringSplitter sp(service_name, ','); sp != NULL; ++sp) {
        line.assign(sp.field(), sp.length());
        base::StringPiece addr;
        base::StringPiece tag;
        if (!SplitIntoServerAndTag(line, &addr, &tag)) {
            continue;
        }
        const_cast<char*>(addr.data())[addr.size()] = '\0'; // safe
        base::EndPoint point;
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

