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


#ifndef BRPC_PERIODIC_NAMING_SERVICE_H
#define BRPC_PERIODIC_NAMING_SERVICE_H

#include "brpc/naming_service.h"


namespace brpc {

class PeriodicNamingService : public NamingService {
protected:
    virtual int GetServers(const char *service_name,
                           std::vector<ServerNode>* servers) = 0;
    
    virtual int GetNamingServiceAccessIntervalMs() const;

    int RunNamingService(const char* service_name,
                         NamingServiceActions* actions) override;
};

} // namespace brpc


#endif  // BRPC_PERIODIC_NAMING_SERVICE_H
