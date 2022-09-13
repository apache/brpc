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


#include <gflags/gflags.h>
#include "butil/string_printf.h"
#include "butil/strings/string_split.h"
#include "butil/fast_rand.h"
#include "bthread/bthread.h"
#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/policy/mongo_naming_service.h"
#include "brpc/mongo.h"

namespace brpc {
namespace policy {

// ========== DiscoveryNamingService =============

int MongoNamingService::GetServers(const char* service_name,
                                       std::vector<ServerNode>* servers) {
    // 127.0.0.1:27017,127.0.0.1:27027,127.0.0.1:27037
    if (service_name == NULL || *service_name == '\0') {
        LOG_ONCE(ERROR) << "Invalid parameters";
        return -1;
    }
    std::string mongo_service_url(service_name);
    
    for (butil::StringSplitter sp(mongo_service_url.c_str(), ','); sp; ++sp) {
        std::string addr(sp.field(), sp.length());
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_MONGO;
        options.connection_type = "pooled";
        options.timeout_ms = 500;
        options.connect_timeout_ms = 300;
        brpc::Channel channel;
        if (channel.Init(addr.c_str(), &options) != 0) {
            LOG(ERROR) << "Fail to init channel";
            continue;
        }
        brpc::MongoGetReplSetStatusRequest request;
        brpc::MongoGetReplSetStatusResponse response;
        brpc::Controller cntl;
        channel.CallMethod(nullptr, &cntl, &request, &response, nullptr);
        if (cntl.Failed()) {
            LOG(INFO) << "get repl set status from:" << addr << " failed, error=" << cntl.ErrorText();
            continue;
        } else {
            // find primary member
            for (size_t i = 0; i < response.members().size(); ++i) {
                if (response.members()[i].state_str == "PRIMARY") {
                    butil::EndPoint endpoint;
                    int ret = hostname2endpoint(response.members()[i].addr.c_str(), &endpoint);
                    if (ret == 0) {
                        servers->push_back(ServerNode(endpoint));
                        LOG(INFO) << "get primary server:" << response.members()[i].addr;
                        return 0;
                    }
                }
            }
        }
    }
    return 0;
}

void MongoNamingService::Describe(std::ostream& os,
                                      const DescribeOptions&) const {
    os << "discovery";
    return;
}

NamingService* MongoNamingService::New() const {
    return new MongoNamingService;
}

void MongoNamingService::Destroy() {
    delete this;
}


} // namespace policy
} // namespace brpc
