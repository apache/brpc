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


#ifndef BRPC_POLICY_DISCOVERY_NAMING_SERVICE_H
#define BRPC_POLICY_DISCOVERY_NAMING_SERVICE_H

#include "brpc/periodic_naming_service.h"
#include "brpc/channel.h"
#include "butil/synchronization/lock.h"

namespace brpc {
namespace policy {

struct DiscoveryRegisterParam {
    std::string appid;
    std::string hostname;
    std::string env;
    std::string zone;
    std::string region;
    std::string addrs;          // splitted by ','
    int status;
    std::string version;
    std::string metadata;

    bool IsValid() const;
};

// ONE DiscoveryClient corresponds to ONE service instance.
// If your program has multiple service instances to register,
// you need multiple DiscoveryClient.
// Note: Cancel to the server is automatically called in dtor.
class DiscoveryClient {
public:
    DiscoveryClient();
    ~DiscoveryClient();

    // Initialize this client.
    // Returns 0 on success.
    // NOTE: Calling more than once does nothing and returns 0.
    int Register(const DiscoveryRegisterParam& req);

private:
    static void* PeriodicRenew(void* arg);
    int DoCancel() const;
    int DoRegister();
    int DoRenew() const;

private:
    bthread_t _th;
    butil::atomic<bool> _registered;
    DiscoveryRegisterParam _params;
    butil::EndPoint _current_discovery_server;
};

class DiscoveryNamingService : public PeriodicNamingService {
private:
    int GetServers(const char* service_name,
                   std::vector<ServerNode>* servers) override;

    void Describe(std::ostream& os, const DescribeOptions&) const override;

    NamingService* New() const override;

    void Destroy() override;

private:
    DiscoveryClient _client;
};


} // namespace policy
} // namespace brpc

#endif // BRPC_POLICY_DISCOVERY_NAMING_SERVICE_H
