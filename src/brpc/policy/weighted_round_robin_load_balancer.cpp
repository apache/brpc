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
#include "brpc/policy/weighted_round_robin_load_balancer.h"
#include "butil/strings/string_number_conversions.h"

namespace {

const std::vector<uint64_t> prime_stride = {
2,3,5,11,17,29,47,71,107,137,163,251,307,379,569,683,857,1289,1543,1949,2617,
2927,3407,4391,6599,9901,14867,22303,33457,50207,75323,112997,169501,254257,
381389,572087,849083,1273637,1910471,2865727,4298629,6447943,9671923,14507903,
21761863,32642861,48964297,73446469,110169743,165254623,247881989,371822987,
557734537,836601847,1254902827,1882354259,2823531397,4235297173,6352945771,
9529418671};

bool IsCoprime(uint64_t num1, uint64_t num2) {
    uint64_t temp;
    if (num1 < num2) {
        temp = num1;
        num1 = num2;
        num2 = temp;
    }
    while (true) {
        temp = num1 % num2;
        if (temp == 0) {
            break;
        } else {
            num1 = num2;
            num2 = temp;
        }
    }
    return num2 == 1;
}

// Get a reasonable stride according to weights configured of servers.
uint64_t GetStride(const uint64_t weight_sum, const size_t num) {
    if (weight_sum == 1) {
      return 1;
    }
    uint32_t average_weight = weight_sum / num;
    auto iter = std::lower_bound(prime_stride.begin(), prime_stride.end(),
                                 average_weight);
    while (iter != prime_stride.end()
           && !IsCoprime(weight_sum, *iter)) {
        ++iter;
    }
    CHECK(iter != prime_stride.end()) << "Failed to get stride";
    return *iter > weight_sum ? *iter % weight_sum : *iter;
}

}  // namespace

namespace brpc {
namespace policy {

bool WeightedRoundRobinLoadBalancer::Add(Servers& bg, const ServerId& id) {
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
        bg.server_list.emplace_back(id.id, weight);
        bg.weight_sum += weight;
        return true;
    }
    return false;
}

bool WeightedRoundRobinLoadBalancer::Remove(Servers& bg, const ServerId& id) {
    auto iter = bg.server_map.find(id.id);
    if (iter != bg.server_map.end()) {
        const size_t index = iter->second;
        bg.weight_sum -= bg.server_list[index].weight;
        bg.server_list[index] = bg.server_list.back();
        bg.server_map[bg.server_list[index].id] = index;
        bg.server_list.pop_back();
        bg.server_map.erase(iter);
        return true;
    }
    return false;
}

size_t WeightedRoundRobinLoadBalancer::BatchAdd(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Add(bg, servers[i]);
    }
    return count;
}

size_t WeightedRoundRobinLoadBalancer::BatchRemove(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Remove(bg, servers[i]);
    }
    return count;
}

bool WeightedRoundRobinLoadBalancer::AddServer(const ServerId& id) {
    return _db_servers.Modify(Add, id);
}

bool WeightedRoundRobinLoadBalancer::RemoveServer(const ServerId& id) {
    return _db_servers.Modify(Remove, id);
}

size_t WeightedRoundRobinLoadBalancer::AddServersInBatch(
    const std::vector<ServerId>& servers) {
    const size_t n = _db_servers.Modify(BatchAdd, servers);
    LOG_IF(ERROR, n != servers.size())
        << "Fail to AddServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

size_t WeightedRoundRobinLoadBalancer::RemoveServersInBatch(
    const std::vector<ServerId>& servers) {
    const size_t n = _db_servers.Modify(BatchRemove, servers);
    LOG_IF(ERROR, n != servers.size())
        << "Fail to RemoveServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

int WeightedRoundRobinLoadBalancer::SelectServer(const SelectIn& in, SelectOut* out) {
    butil::DoublyBufferedData<Servers, TLS>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        return ENOMEM;
    }
    if (s->server_list.empty()) {
        return ENODATA;
    }
    TLS& tls = s.tls();
    if (tls.IsNeededCalculateNewStride(s->weight_sum, s->server_list.size())) {
      if (tls.stride == 0) {
          tls.position = butil::fast_rand_less_than(s->server_list.size());
      }
      tls.stride = GetStride(s->weight_sum, s->server_list.size());
    }
    // If server list changed, the position may be out of range.
    tls.position %= s->server_list.size();
    // Check whether remain server was removed from server list.
    if (tls.remain_server.weight > 0 &&
        tls.remain_server.id != s->server_list[tls.position].id) {
        tls.remain_server.weight = 0;
    }
    // The servers that can not be chosen.
    std::unordered_set<SocketId> filter;
    TLS tls_temp = tls;
    uint64_t remain_weight = s->weight_sum;
    size_t remain_servers = s->server_list.size();
    while (remain_servers > 0) {
        SocketId server_id = GetServerInNextStride(s->server_list, filter, tls_temp);
        if (!ExcludedServers::IsExcluded(in.excluded, server_id)
            && Socket::Address(server_id, out->ptr) == 0
            && (*out->ptr)->IsAvailable()) {
            // update tls.
            tls.remain_server = tls_temp.remain_server;
            tls.position = tls_temp.position;
            return 0;
        } else {
            // Skip this invalid server. We need calculate a new stride for server selection.
            if (--remain_servers == 0) {
                break;
            }
            filter.emplace(server_id);
            remain_weight -= (s->server_list[s->server_map.at(server_id)]).weight;
            // Select from begining status.
            tls_temp.stride = GetStride(remain_weight, remain_servers);
            tls_temp.position = tls.position;
            tls_temp.remain_server = tls.remain_server;
        }
    }
    return EHOSTDOWN;
}

SocketId WeightedRoundRobinLoadBalancer::GetServerInNextStride(
        const std::vector<Server>& server_list,
        const std::unordered_set<SocketId>& filter,
        TLS& tls) {
    SocketId final_server = INVALID_SOCKET_ID;
    uint64_t stride = tls.stride;
    Server& remain = tls.remain_server;
    if (remain.weight > 0) {
        if (filter.count(remain.id) == 0) {
            final_server = remain.id;
            if (remain.weight > stride) {
                remain.weight -= stride;
                return final_server;
            } else {
                stride -= remain.weight;
            }
        }
        remain.weight = 0;
        ++tls.position;
        tls.position %= server_list.size();
    }
    while (stride > 0) {
        final_server = server_list[tls.position].id;
        if (filter.count(final_server) == 0) {
            uint32_t configured_weight = server_list[tls.position].weight;
            if (configured_weight > stride) {
                remain.id = final_server;
                remain.weight = configured_weight - stride;
                return final_server;
            }
            stride -= configured_weight;
        }
        ++tls.position;
        tls.position %= server_list.size();
    }
    return final_server;
}

LoadBalancer* WeightedRoundRobinLoadBalancer::New(
    const butil::StringPiece&) const {
    return new (std::nothrow) WeightedRoundRobinLoadBalancer;
}

void WeightedRoundRobinLoadBalancer::Destroy() {
    delete this;
}

void WeightedRoundRobinLoadBalancer::Describe(
    std::ostream &os, const DescribeOptions& options) {
    if (!options.verbose) {
        os << "wrr";
        return;
    }
    os << "WeightedRoundRobin{";
    butil::DoublyBufferedData<Servers, TLS>::ScopedPtr s;
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
