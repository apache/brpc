// Copyright (c) 2018 BiliBili, Inc.
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

// Authors: Jiashun Zhu(zhujiashun@bilibili.com)

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

struct DiscoveryFetchsParam {
    std::string appid;
    std::string env;
    std::string status;

    bool IsValid() const;
};

// ONE DiscoveryClient corresponds to ONE service instance.
// If your program has multiple service instances to register,
// you need multiple DiscoveryClient.
// Note: Unregister is automatically called in dtor.
class DiscoveryClient {
public:
    DiscoveryClient();
    ~DiscoveryClient();

    int Register(const DiscoveryRegisterParam& req);
    int Fetchs(const DiscoveryFetchsParam& req, std::vector<ServerNode>* servers) const;

private:
    static void* PeriodicRenew(void* arg);
    int DoCancel() const;
    int DoRegister() const;
    int DoRenew() const;

private:
    bthread_t _th;
    butil::atomic<bool> _registered;
    std::string _appid;
    std::string _hostname;
    std::string _addrs;
    std::string _env;
    std::string _region;
    std::string _zone;
    int _status;
    std::string _version;
    std::string _metadata;
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
