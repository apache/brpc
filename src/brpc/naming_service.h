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

#ifndef BRPC_NAMING_SERVICE_H
#define BRPC_NAMING_SERVICE_H

#include <vector>                                   // std::vector
#include <string>                                   // std::string
#include <ostream>                                  // std::ostream
#include "butil/endpoint.h"                          // butil::EndPoint
#include "butil/macros.h"                            // BAIDU_CONCAT
#include "brpc/describable.h"
#include "brpc/destroyable.h"
#include "brpc/extension.h"                    // Extension<T>


namespace brpc {

const std::string kCallerCreatedNamingService = "CreatedByCaller";

// Representing a server inside a NamingService.
struct ServerNode {
    ServerNode() {}
    ServerNode(butil::ip_t ip, int port, const std::string& tag2)
        : addr(ip, port), tag(tag2) {}
    ServerNode(const butil::EndPoint& pt, const std::string& tag2)
        : addr(pt), tag(tag2) {}
    ServerNode(butil::ip_t ip, int port) : addr(ip, port) {}
    explicit ServerNode(const butil::EndPoint& pt) : addr(pt) {}

    butil::EndPoint addr;
    std::string tag;
};

// Continuing actions to added/removed servers.
// NOTE: You don't have to implement this class.
class NamingServiceActions {
public:
    virtual ~NamingServiceActions() {}
    virtual void AddServers(const std::vector<ServerNode>& servers) = 0;
    virtual void RemoveServers(const std::vector<ServerNode>& servers) = 0;
    virtual void ResetServers(const std::vector<ServerNode>& servers) = 0;
};

// Mapping a name to ServerNodes.
class NamingService : public Describable, public Destroyable {
public:    
    // Implement this method to get servers associated with `service_name'
    // in periodic or event-driven manner, call methods of `actions' to
    // tell RPC system about server changes. This method will be run in
    // a dedicated bthread without access from other threads, thus the
    // implementation does NOT need to be thread-safe.
    // Return 0 on success, error code otherwise.
    virtual int RunNamingService(const char* service_name,
                                 NamingServiceActions* actions) = 0;

    // If this method returns true, RunNamingService will be called without
    // a dedicated bthread. As the name implies, this is suitable for static
    // and simple impl, saving the cost of creating a bthread. However most
    // impl of RunNamingService never quit, thread is a must to prevent the
    // method from blocking the caller.
    virtual bool RunNamingServiceReturnsQuickly() { return false; }

    // Create/destroy an instance.
    // Caller is responsible for Destroy() the instance after usage.
    virtual NamingService* New() const = 0;

    // If return true, NamingServiceThread will print logs about added/removed 
    // server when servers changed. If a naming service change servers too frequently
    // during a short time, make this method return false to avoid to print log too heavy.
    virtual bool PrintServerChangeLogsEveryTimes() { return true; }

    virtual ~NamingService() {}
};

inline Extension<const NamingService>* NamingServiceExtension() {
    return Extension<const NamingService>::instance();
}

inline bool operator<(const ServerNode& n1, const ServerNode& n2)
{ return n1.addr != n2.addr ? (n1.addr < n2.addr) : (n1.tag < n2.tag); }
inline bool operator==(const ServerNode& n1, const ServerNode& n2)
{ return n1.addr == n2.addr && n1.tag == n2.tag; }
inline bool operator!=(const ServerNode& n1, const ServerNode& n2)
{ return !(n1 == n2); }
inline std::ostream& operator<<(std::ostream& os, const ServerNode& n) {
    os << n.addr;
    if (!n.tag.empty()) {
        os << "(tag=" << n.tag << ')';
    }
    return os;
}

} // namespace brpc


#endif  // BRPC_NAMING_SERVICE_H
