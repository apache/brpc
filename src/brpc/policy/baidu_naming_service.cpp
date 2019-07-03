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

#ifdef BAIDU_INTERNAL

#include <webfoot_naming.h>                             //webfoot::*
#include <naming.pb.h>                                  //BnsInput BnsOutput
#include "butil/logging.h"                               // CHECK
#include "brpc/policy/baidu_naming_service.h"

namespace brpc {
namespace policy {

int BaiduNamingService::GetServers(const char *service_name,
                                   std::vector<ServerNode>* servers) {
    servers->clear();
    BnsInput input;
    input.set_service_name(service_name);
    BnsOutput output;
    const int rc = webfoot::get_instance_by_service(input, &output);
    if (rc != webfoot::WEBFOOT_RET_SUCCESS) {
        if (rc != webfoot::WEBFOOT_SERVICE_BEYOND_THRSHOLD) {
            LOG(WARNING) << "Fail to get servers of `" << service_name
                         << "', " << webfoot::error_to_string(rc);
            return -1;
        } else {
            // NOTE: output is valid for this error, just print a warning.
            LOG(WARNING) << webfoot::error_to_string(rc);
        }
    } 
    const int instance_number = output.instance_size(); 
    if (instance_number == 0) {
        LOG(WARNING) << "No server attached to `" << service_name << "'";
        return 0;
    }
    for (int i = 0; i < instance_number; i++) {
        const BnsInstance& instance = output.instance(i);
        if (instance.status() == 0) {
            butil::ip_t ip;
            if (butil::str2ip(instance.host_ip().c_str(), &ip) != 0) {
                LOG(WARNING) << "Invalid ip=" << instance.host_ip();
                continue;
            }
            servers->push_back(ServerNode(ip, instance.port(), instance.tag()));
        }
    }
    return 0;
}

void BaiduNamingService::Describe(
    std::ostream& os, const DescribeOptions&) const {
    os << "bns";
    return;
}

NamingService* BaiduNamingService::New() const {
    return new BaiduNamingService;
}

void BaiduNamingService::Destroy() {
    delete this;
}

}  // namespace policy
} // namespace brpc
#endif  // BAIDU_INTERNAL
