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

#include "butil/fast_rand.h"
#include "brpc/socket.h"
#include "brpc/policy/weighted_round_robin_load_balancer.h"
#include "butil/strings/string_number_conversions.h"

namespace brpc {
namespace policy {

bool IsCoprime(uint32_t num1, uint32_t num2) {
    uint32_t temp;
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
            bg.weight_sum += weight;
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
        bg.weight_sum -= bg.server_list[index].second;
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
    if (tls.IsNeededCaculateNewStride(s->weight_sum, s->server_list.size())) {
      tls.stride = GetStride(s->weight_sum, s->server_list.size()); 
      tls.offset = butil::fast_rand_less_than(tls.stride);
    }
    // If server list changed, the position may be out of range.
    tls.position %= s->server_list.size();
    // Check whether remain server was removed from server list.
    if (tls.HasRemainServer() && 
        s->server_map.find(tls.remain_server.first) == s->server_map.end()) {
        tls.ResetRemainServer();
    }
    for ( uint32_t i = 0; i != tls.stride; ++i) {
        int64_t best = GetBestServer(s->server_list, tls, tls.stride);
        if (!ExcludedServers::IsExcluded(in.excluded, best)
            && Socket::Address(best, out->ptr) == 0
            && !(*out->ptr)->IsLogOff()) {
            return 0;
        }
    }
    return EHOSTDOWN;
}

int64_t WeightedRoundRobinLoadBalancer::GetBestServer(
            const std::vector<std::pair<SocketId, int>>& server_list,
            TLS& tls, uint32_t stride) {
    uint32_t comp_weight = 0;
    int64_t final_server = -1;
    while (stride > 0) {
      if (tls.HasRemainServer()) {
          uint32_t remain_weight = tls.remain_server.second;
          if (remain_weight < stride) {
              TryToGetFinalServer(tls, tls.remain_server, 
                                  comp_weight, &final_server);
              tls.ResetRemainServer();
              stride -= remain_weight;
          } else if (remain_weight == stride) {
              TryToGetFinalServer(tls, tls.remain_server,
                                  comp_weight, &final_server);
              tls.ResetRemainServer();
              break;
          } else {
              TryToGetFinalServer(tls,  
                  std::pair<SocketId, int>(tls.remain_server.first, stride),
                  comp_weight, &final_server);
              tls.remain_server.second -= stride;
              break;
          }
      } else {
          uint32_t weight = server_list[tls.position].second;
          if (weight < stride) {
              TryToGetFinalServer(tls, server_list[tls.position], 
                                  comp_weight, &final_server);
              stride -= weight;
              tls.UpdatePosition(server_list.size()); 
          } else if (weight == stride) {
              TryToGetFinalServer(tls, server_list[tls.position], 
                                  comp_weight, &final_server);
              tls.UpdatePosition(server_list.size()); 
              break; 
          } else {
              TryToGetFinalServer(tls, 
                  std::pair<SocketId, int>(
                      server_list[tls.position].first, stride), 
                  comp_weight, &final_server);
              tls.SetRemainServer(server_list[tls.position].first, 
                                  weight - stride);
              tls.UpdatePosition(server_list.size()); 
              break;
          }
      }
    }
    return final_server;
}

uint32_t WeightedRoundRobinLoadBalancer::GetStride(
    const uint32_t weight_sum, const uint32_t num) {
    uint32_t average_weight = weight_sum / num;
    // The stride is the first number which is greater than or equal to 
    // average weight and coprime to weight_sum.
    while (!IsCoprime(weight_sum, average_weight)) {
       ++average_weight;
    }
    return average_weight;  
}

void WeightedRoundRobinLoadBalancer::TryToGetFinalServer(
    const TLS& tls, const std::pair<SocketId, int> server, 
    uint32_t& comp_weight, int64_t* final_server) {
    if (*final_server == -1) {
        comp_weight += server.second;
        if (comp_weight >= tls.offset) {
            *final_server = server.first;
        }
    }
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
