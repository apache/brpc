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
#include "brpc/server_node.h"


namespace brpc {

// Continuing actions to added/removed servers.
// NOTE: You don't have to implement this class.
class NamingServiceActions {
public:
    NamingServiceActions()
        :_cleaned_up(false) {}
    virtual ~NamingServiceActions() {}

    // Call this method when servers are successfully refreshed.
    virtual void ResetServers(const std::vector<ServerNode>& servers) = 0;

    // Clean up resources
    void CleanUp() { CleanUpImp(); }
    bool IsCleanedUp() { return _cleaned_up; }

protected:
    virtual void CleanUpImp() { _cleaned_up = true; }

private:
    bool _cleaned_up;
};

// Mapping a name to ServerNodes.
class NamingService : public Describable, public Destroyable {
public:    
    // Implement this method to get servers associated with `service_name'
    // periodically or by event-driven, call methods of `actions' to tell
    // RPC system about server changes.
    // `actions' is owned and deleted by this naming service.
    virtual void RunNamingService(const char* service_name,
                                  NamingServiceActions* actions) = 0;

    // Create an instance which will be destroyed by Destroy() (inherited from Destroyable)
    virtual NamingService* New() const = 0;

protected:
    virtual ~NamingService() {}
};

inline Extension<const NamingService>* NamingServiceExtension() {
    return Extension<const NamingService>::instance();
}

} // namespace brpc


#endif  // BRPC_NAMING_SERVICE_H
