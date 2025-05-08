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


#ifndef BRPC_POLICY_DYNPART_LOAD_BALANCER_H
#define BRPC_POLICY_DYNPART_LOAD_BALANCER_H

#include <vector>                                      // std::vector
#include <map>                                         // std::map
#include "butil/containers/doubly_buffered_data.h"
#include "brpc/load_balancer.h"


namespace brpc {
namespace policy {

// CAUTION: This is just a quick/hacking impl. for loading balancing between
// partchans in a DynamicPartitionChannel. Any details are subject to change.

class DynPartLoadBalancer : public LoadBalancer {
public:
    bool AddServer(const ServerId& id) override;
    bool RemoveServer(const ServerId& id) override;
    size_t AddServersInBatch(const std::vector<ServerId>& servers) override;
    size_t RemoveServersInBatch(const std::vector<ServerId>& servers) override;
    int SelectServer(const SelectIn& in, SelectOut* out) override;
    DynPartLoadBalancer* New(const butil::StringPiece&) const override;
    void Destroy() override;
    void Describe(std::ostream&, const DescribeOptions& options) override;

private:
    struct Servers {
        std::vector<ServerId> server_list;
        std::map<ServerId, size_t> server_map;
    };
    static bool Add(Servers& bg, const ServerId& id);
    static bool Remove(Servers& bg, const ServerId& id);
    static size_t BatchAdd(Servers& bg, const std::vector<ServerId>& servers);
    static size_t BatchRemove(Servers& bg, const std::vector<ServerId>& servers);

    butil::DoublyBufferedData<Servers> _db_servers;
};

}  // namespace policy
} // namespace brpc


#endif  // BRPC_POLICY_DYNPART_LOAD_BALANCER_H
