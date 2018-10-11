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

namespace brpc {
namespace policy {

class DiscoveryNamingService : public PeriodicNamingService {
private:
    int GetServers(const char* service_name,
                   std::vector<ServerNode>* servers) override;

    void Describe(std::ostream& os, const DescribeOptions&) const override;

    NamingService* New() const override;

    void Destroy() override;

private:
    int ParseNodesResult(const butil::IOBuf& buf, std::string* server_addr);
    int ParseFetchsResult(const butil::IOBuf& buf, const char* service_name,
                            std::vector<ServerNode>* servers);

    Channel _channel;
    bool _is_initialized = false;
};

} // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_DISCOVERY_NAMING_SERVICE_H
