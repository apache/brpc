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

#ifndef BRPC_POLICY_NACOS_NAMING_SERVICE_H
#define BRPC_POLICY_NACOS_NAMING_SERVICE_H

#include <time.h>

#include <string>
#include <vector>

#include "brpc/channel.h"
#include "brpc/periodic_naming_service.h"
#include "brpc/server_node.h"

namespace brpc {
namespace policy {

// Acquire server list from nacos
class NacosNamingService : public PeriodicNamingService {
public:
    NacosNamingService();

    int GetServers(const char* service_name,
                   std::vector<ServerNode>* servers) override;

    int GetNamingServiceAccessIntervalMs() const override;

    void Describe(std::ostream& os, const DescribeOptions&) const override;

    NamingService* New() const override;

    void Destroy() override;

private:
    int Connect();
    int RefreshAccessToken(const char* service_name);
    int GetServerNodes(const char* service_name, bool token_changed,
                       std::vector<ServerNode>* nodes);

private:
    brpc::Channel _channel;
    std::string _nacos_url;
    std::string _access_token;
    bool _nacos_connected;
    long _cache_ms;
    time_t _token_expire_time;
};

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_NACOS_NAMING_SERVICE_H
