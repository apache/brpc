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

#include "brpc/policy/redis_cluster_naming_service.h"
#include <brpc/policy/redis_authenticator.h>
#include "brpc/log.h"
#include "bthread/bthread.h"
#include <gflags/gflags.h>
#include <brpc/channel.h>
#include <brpc/redis.h>
#include <netdb.h>  // gethostbyname_r
#include <stdlib.h> // strtol
#include <string>   // std::string

namespace brpc {
namespace policy {

RedisClusterNamingService::RedisClusterNamingService() = default;

std::pair<std::string, std::string> GetServiceNameAndToken (const std::string& input) {
    std::stringstream ss(input);
    std::string service_name, token;
    std::getline(ss, service_name, '|');
    std::getline(ss, token);
    return {service_name, token};
}


int RedisClusterNamingService::GetServers(const char *service_name_and_token, std::vector<ServerNode> *servers) {
    servers->clear();
    const auto service_name_token_pair = GetServiceNameAndToken (service_name_and_token);
    const auto& service_name = service_name_token_pair.first;
    const auto& token = service_name_token_pair.second;

    brpc::Channel channel;
    
    // Initialize the channel, NULL means using default options.
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_REDIS;
    options.timeout_ms = 1000;
    options.max_retry = 3;
    if (!token.empty()) {
        brpc::policy::RedisAuthenticator* auth = new brpc::policy::RedisAuthenticator(token);
        options.auth = auth;
    }
    if (channel.Init(service_name.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }

    brpc::RedisRequest request;
    if (!request.AddCommand("CLUSTER SLOTS")) {
        LOG(ERROR) << "Fail to add command";
        return -1;
    }

    brpc::RedisResponse response;
    brpc::Controller cntl;
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access redis, " << cntl.ErrorText();
        return -1;
    }
    const auto& reply = response.reply(0);
    for (int i = 0; i < reply.size(); i++) {
        const auto& slot_start = reply[i][0];
        const auto& slot_end = reply[i][1];
        const std::string tag = std::to_string(slot_start.integer()) + "-" + std::to_string(slot_end.integer());

        const auto& ip = reply[i][2][0];
        const auto& port = reply[i][2][1];
        butil::EndPoint point;
        if (butil::str2endpoint(ip.c_str(), port.integer(), &point) != 0 &&
            butil::hostname2endpoint(ip.c_str(), port.integer(), &point) != 0) {
            LOG(ERROR) << "Invalid address=`" << ip.c_str() << ":" << port.integer() << '\'';
            continue;
        }
        servers->emplace_back(point, tag);
    }
    return 0;
}

void RedisClusterNamingService::Describe(std::ostream &os, const DescribeOptions &) const {
    os << "redis_cluster";
    return;
}

NamingService *RedisClusterNamingService::New() const { return new RedisClusterNamingService; }

void RedisClusterNamingService::Destroy() { delete this; }

} // namespace policy
} // namespace brpc
