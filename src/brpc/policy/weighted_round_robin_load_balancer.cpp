// Copyright (c) 2018 Iqiyi, Inc.
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

// Authors: Daojin Cai (caidaojin@qiyi.com)

#include "brpc/socket.h"
#include "brpc/policy/weighted_round_robin_load_balancer.h"
#include "butil/strings/string_number_conversions.h"

namespace brpc {
namespace policy {

static const int EraseBatchSize = 100;

bool WeightedRoundRobinLoadBalancer::Add(Servers& bg, const ServerId& id) {
    if (bg.server_list.capacity() < 128) {
        bg.server_list.reserve(128);
    }
    int weight = 0;
    if (butil::StringToInt(id.tag, &weight) && weight > 0) {
        bool insert_server = 
                 bg.server_map.emplace(id.id, bg.server_list.size()).second;
        if (insert_server) {
            bg.server_list.emplace_back(id.id, weight);
            return true;
        }
    } else {
        LOG(ERROR) << "Invalid weight is set: " << id.tag;
    }
    return false;
}

bool WeightedRoundRobinLoadBalancer::Remove(Servers& bg, const ServerId& id) {
    auto iter = bg.server_map.find(id.id);
    if (iter != bg.server_map.end()) {
        const size_t index = iter->second;
        bg.server_list[index] = bg.server_list.back();
        bg.server_map[bg.server_list[index].first] = index;
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
    int64_t best = -1;
    int total_weight = 0;
    // TODO: each thread requsts service as the same sequence.
    // We can set a random beginning position for each thread.
    for (const auto& server : s->server_list) {
        // A new server is added or the wrr fisrt run.
        // Add the servers into TLS.
        const SocketId server_id = server.first;
        auto iter = tls.emplace(server_id, 0).first;
        if (ExcludedServers::IsExcluded(in.excluded, server_id)
            || Socket::Address(server_id, out->ptr) != 0
            || (*out->ptr)->IsLogOff()) {
            continue;
        }
        iter->second += server.second;
        total_weight += server.second;
        if (best == -1 || tls[server_id] > tls[best]) {
            best = server_id;
        }
    }
    // If too many servers were removed from _db_servers(name service),
    // remove these servers from TLS.
    if (s->server_list.size() + EraseBatchSize < tls.size()) {
        auto iter = tls.begin(); 
        while (iter != tls.end()) {
            if (s->server_map.find(iter->first) == s->server_map.end()) {
                iter = tls.erase(iter);
            } else {
                ++iter;
            }
        }
    }
    if (best != -1) {
        tls[best] -= total_weight;
        if (!ExcludedServers::IsExcluded(in.excluded, best)
            && Socket::Address(best, out->ptr) == 0
            && !(*out->ptr)->IsLogOff()) {
            return 0;
        }
    }  
    return EHOSTDOWN;
}

LoadBalancer* WeightedRoundRobinLoadBalancer::New() const {
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
            os << ' ' << server.first << '(' << server.second << ')';
        }
    }
    os << '}';
}

}  // namespace policy
} // namespace brpc
