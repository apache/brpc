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


#include <algorithm>

#include "butil/fast_rand.h"
#include "brpc/socket.h"
#include "brpc/policy/weighted_randomized_load_balancer.h"
#include "butil/strings/string_number_conversions.h"

namespace brpc {
namespace policy {

static bool server_compare(const WeightedRandomizedLoadBalancer::Server& lhs, const WeightedRandomizedLoadBalancer::Server& rhs) {
        return (lhs.current_weight_sum < rhs.current_weight_sum);
}

bool WeightedRandomizedLoadBalancer::Add(Servers& bg, const ServerId& id) {
    if (bg.server_list.capacity() < 128) {
        bg.server_list.reserve(128);
    }
    uint32_t weight = 0;
    if (!butil::StringToUint(id.tag, &weight) || weight <= 0) {
        if (FLAGS_default_weight_of_wlb > 0) {
            LOG(WARNING) << "Invalid weight is set: " << id.tag
                         << ". Now, 'weight' has been set to 'FLAGS_default_weight_of_wlb' by default.";
            weight = FLAGS_default_weight_of_wlb;
        } else {
            LOG(ERROR) << "Invalid weight is set: " << id.tag;
            return false;
        }
    }
    bool insert_server =
             bg.server_map.emplace(id.id, bg.server_list.size()).second;
    if (insert_server) {
        uint64_t current_weight_sum = bg.weight_sum + weight;
        bg.server_list.emplace_back(id.id, weight, current_weight_sum);
        bg.weight_sum = current_weight_sum;
        return true;
    }
    return false;
}

bool WeightedRandomizedLoadBalancer::Remove(Servers& bg, const ServerId& id) {
    typedef std::map<SocketId, size_t>::iterator MapIter_t;
    MapIter_t iter = bg.server_map.find(id.id);
    if (iter != bg.server_map.end()) {
        size_t index = iter->second;
        Server remove_server = bg.server_list[index];
        int32_t weight_diff = bg.server_list.back().weight - remove_server.weight;
        bg.weight_sum -= remove_server.weight;
        bg.server_list[index] = bg.server_list.back();
        bg.server_list[index].current_weight_sum = remove_server.current_weight_sum + weight_diff;
        bg.server_map[bg.server_list[index].id] = index;
        bg.server_list.pop_back();
        bg.server_map.erase(iter);
        size_t n = bg.server_list.size();
        for (index++; index < n; index++) {
            bg.server_list[index].current_weight_sum += weight_diff;
        }
        return true;
    }
    return false;
}

size_t WeightedRandomizedLoadBalancer::BatchAdd(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Add(bg, servers[i]);
    }
    return count;
}

size_t WeightedRandomizedLoadBalancer::BatchRemove(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Remove(bg, servers[i]);
    }
    return count;
}

bool WeightedRandomizedLoadBalancer::AddServer(const ServerId& id) {
    return _db_servers.Modify(Add, id);
}

bool WeightedRandomizedLoadBalancer::RemoveServer(const ServerId& id) {
    return _db_servers.Modify(Remove, id);
}

size_t WeightedRandomizedLoadBalancer::AddServersInBatch(
    const std::vector<ServerId>& servers) {
    return _db_servers.Modify(BatchAdd, servers);
}

size_t WeightedRandomizedLoadBalancer::RemoveServersInBatch(
    const std::vector<ServerId>& servers) {
    return _db_servers.Modify(BatchRemove, servers);
}

int WeightedRandomizedLoadBalancer::SelectServer(const SelectIn& in, SelectOut* out) {
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        return ENOMEM;
    }
    size_t n = s->server_list.size();
    if (n == 0) {
        return ENODATA;
    }
    uint64_t weight_sum = s->weight_sum;
    for (size_t i = 0; i < n; ++i) {
        uint64_t random_weight = butil::fast_rand_less_than(weight_sum);
        const Server random_server(0, 0, random_weight);
        const auto& server = std::lower_bound(s->server_list.begin(), s->server_list.end(), random_server, server_compare);
        const SocketId id = server->id;
        if (((i + 1) == n  // always take last chance
             || !ExcludedServers::IsExcluded(in.excluded, id))
            && Socket::Address(id, out->ptr) == 0
            && (*out->ptr)->IsAvailable()) {
            // We found an available server
            return 0;
        }
    }
    // After we traversed the whole server list, there is still no
    // available server
    return EHOSTDOWN;
}

LoadBalancer* WeightedRandomizedLoadBalancer::New(
    const butil::StringPiece&) const {
    return new (std::nothrow) WeightedRandomizedLoadBalancer;
}

void WeightedRandomizedLoadBalancer::Destroy() {
    delete this;
}

void WeightedRandomizedLoadBalancer::Describe(
    std::ostream &os, const DescribeOptions& options) {
    if (!options.verbose) {
        os << "wr";
        return;
    }
    os << "WeightedRandomized{";
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        os << "fail to read _db_servers";
    } else {
        os << "n=" << s->server_list.size() << ':';
        for (const auto& server : s->server_list) {
            os << ' ' << server.id << '(' << server.weight << ')';
        }
    }
    os << '}';
}

}  // namespace policy
} // namespace brpc
