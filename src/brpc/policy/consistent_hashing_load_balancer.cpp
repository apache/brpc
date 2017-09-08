// Copyright (c) 2015 Baidu, Inc.
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

// Authors: Zhangyi Chen (chenzhangyi01@baidu.com)

#include <algorithm>                                           // std::set_union
#include <gflags/gflags.h>
#include "butil/containers/flat_map.h"
#include "butil/errno.h"
#include "brpc/socket.h"
#include "brpc/policy/consistent_hashing_load_balancer.h"


namespace brpc {
namespace policy {

// TODO: or 160?
DEFINE_int32(chash_num_replicas, 100, 
             "default number of replicas per server in chash");

ConsistentHashingLoadBalancer::ConsistentHashingLoadBalancer(HashFunc hash) 
    : _hash(hash)
    , _num_replicas(FLAGS_chash_num_replicas) {
}

ConsistentHashingLoadBalancer::ConsistentHashingLoadBalancer(
        HashFunc hash,
        size_t num_replicas) 
    : _hash(hash)
    , _num_replicas(num_replicas) {
}

size_t ConsistentHashingLoadBalancer::AddBatch(
        std::vector<Node> &bg, const std::vector<Node> &fg, 
        const std::vector<Node> &servers, bool *executed) {
    if (*executed) {
        // Hack DBD
        return fg.size() - bg.size();
    }
    *executed = true;
    bg.resize(fg.size() + servers.size());
    bg.resize(std::set_union(fg.begin(), fg.end(), 
                             servers.begin(), servers.end(), bg.begin())
              - bg.begin());
    return bg.size() - fg.size();
}

size_t ConsistentHashingLoadBalancer::RemoveBatch(
        std::vector<Node> &bg, const std::vector<Node> &fg,
        const std::vector<ServerId> &servers, bool *executed) {
    if (*executed) {
        return bg.size() - fg.size();
    }
    *executed = true;
    if (servers.empty()) {
        bg = fg;
        return 0;
    }
    butil::FlatSet<ServerId> id_set;
    bool use_set = true;
    if (id_set.init(servers.size() * 2) == 0) {
        for (size_t i = 0; i < servers.size(); ++i) {
            if (id_set.insert(servers[i]) == NULL) {
                use_set = false;
                break;
            }
        }
    } else {
        use_set = false;
    }
    CHECK(use_set) << "Fail to construct id_set, " << berror();
    bg.clear();
    for (size_t i = 0; i < fg.size(); ++i) {
        const bool removed = 
            use_set ? (id_set.seek(fg[i].server_sock) != NULL)
                    : (std::find(servers.begin(), servers.end(), 
                                fg[i].server_sock) != servers.end());
        if (!removed) {
            bg.push_back(fg[i]);
        }
    }
    return fg.size() - bg.size();
}

size_t ConsistentHashingLoadBalancer::Remove(
        std::vector<Node> &bg, const std::vector<Node> &fg,
        const ServerId& server, bool *executed) {
    if (*executed) {
        return bg.size() - fg.size();
    }
    *executed = true;
    bg.clear();
    for (size_t i = 0; i < fg.size(); ++i) {
        if (fg[i].server_sock != server) {
            bg.push_back(fg[i]);
        }
    }
    return fg.size() - bg.size();
}

bool ConsistentHashingLoadBalancer::AddServer(const ServerId& server) {
    std::vector<Node> add_nodes;
    add_nodes.reserve(_num_replicas);
    SocketUniquePtr ptr;
    if (Socket::AddressFailedAsWell(server.id, &ptr) == -1) {
        return false;
    }
    for (size_t i = 0; i < _num_replicas; ++i) {
        char host[32];
        int len = snprintf(host, sizeof(host), "%s-%lu", 
                 endpoint2str(ptr->remote_side()).c_str(), i);
        Node node;
        node.hash = _hash(host, len);
        node.server_sock = server;
        node.server_addr = ptr->remote_side();
        add_nodes.push_back(node);
    }
    std::sort(add_nodes.begin(), add_nodes.end());
    bool executed = false;
    const size_t ret = _db_hash_ring.ModifyWithForeground(
                        AddBatch, add_nodes, &executed);
    CHECK(ret == 0 || ret == _num_replicas) << ret;
    return ret != 0;
}

size_t ConsistentHashingLoadBalancer::AddServersInBatch(
    const std::vector<ServerId> &servers) {
    std::vector<Node> add_nodes;
    add_nodes.reserve(servers.size() * _num_replicas);
    for (size_t i = 0; i < servers.size(); ++i) {
        SocketUniquePtr ptr;
        if (Socket::AddressFailedAsWell(servers[i].id, &ptr) == -1) {
            continue;
        }
        for (size_t rep = 0; rep < _num_replicas; ++rep) {
            char host[32];
            // To be compatible with libmemcached, we formulate the key of
            // a virtual node as `|address|-|replica_index|', see
            // http://fe.baidu.com/-1bszwnf at line 297.
            int len = snprintf(host, sizeof(host), "%s-%lu",
                              endpoint2str(ptr->remote_side()).c_str(), rep);
            Node node;
            node.hash = _hash(host, len);
            node.server_sock = servers[i];
            node.server_addr = ptr->remote_side();
            add_nodes.push_back(node);
        }
    }
    std::sort(add_nodes.begin(), add_nodes.end());
    bool executed = false;
    const size_t ret = _db_hash_ring.ModifyWithForeground(AddBatch, add_nodes, &executed);
    CHECK(ret % _num_replicas == 0);
    const size_t n = ret / _num_replicas;
    LOG_IF(ERROR, n != servers.size())
        << "Fail to AddServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

bool ConsistentHashingLoadBalancer::RemoveServer(const ServerId& server) {
    bool executed = false;
    const size_t ret = _db_hash_ring.ModifyWithForeground(Remove, server, &executed);
    CHECK(ret == 0 || ret == _num_replicas);
    return ret != 0;
}

size_t ConsistentHashingLoadBalancer::RemoveServersInBatch(
    const std::vector<ServerId> &servers) {
    bool executed = false;
    const size_t ret = _db_hash_ring.ModifyWithForeground(RemoveBatch, servers, &executed);
    CHECK(ret % _num_replicas == 0);
    const size_t n = ret / _num_replicas;
    LOG_IF(ERROR, n != servers.size())
        << "Fail to RemoveServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

LoadBalancer *ConsistentHashingLoadBalancer::New() const {
    return new (std::nothrow) ConsistentHashingLoadBalancer(_hash);
}

void ConsistentHashingLoadBalancer::Destroy() {
    delete this;
}

int ConsistentHashingLoadBalancer::SelectServer(
    const SelectIn &in, SelectOut *out) {
    if (!in.has_request_code) {
        LOG(ERROR) << "Controller.set_request_code() is required";
        return EINVAL;
    }
    if (in.request_code > UINT_MAX) {
        LOG(ERROR) << "request_code must be 32-bit currently";
        return EINVAL;
    }
    butil::DoublyBufferedData<std::vector<Node> >::ScopedPtr s;
    if (_db_hash_ring.Read(&s) != 0) {
        return ENOMEM;
    }
    if (s->empty()) {
        return ENODATA;
    }
    std::vector<Node>::const_iterator choice =
        std::lower_bound(s->begin(), s->end(), (uint32_t)in.request_code);
    if (choice == s->end()) {
        choice = s->begin();
    }
    for (size_t i = 0; i < s->size(); ++i) {
        if (((i + 1) == s->size() // always take last chance
             || !ExcludedServers::IsExcluded(in.excluded, choice->server_sock.id))
            && Socket::Address(choice->server_sock.id, out->ptr) == 0 
            && !(*out->ptr)->IsLogOff()) {
            return 0;
        } else {
            if (++choice == s->end()) {
                choice = s->begin();
            }
        }
    }
    return EHOSTDOWN;
}

extern const char *GetHashName(uint32_t (*hasher)(const void* key, size_t len));

void ConsistentHashingLoadBalancer::Describe(
    std::ostream &os, const DescribeOptions& options) {
    if (!options.verbose) {
        os << "c_hash";
        return;
    }
    os << "ConsistentHashingLoadBalancer {\n"
       << "  hash function: " << GetHashName(_hash) << '\n'
       << "  replica per host: " << _num_replicas << '\n';
    std::map<butil::EndPoint, double> load_map;
    GetLoads(&load_map);
    os << "  number of hosts: " << load_map.size() << '\n';
    os << "  load of hosts: {\n";
    double expected_load_per_server = 1.0 / load_map.size();
    double load_sum = 0;
    double load_sqr_sum = 0;
    for (std::map<butil::EndPoint, double>::iterator 
            it = load_map.begin(); it!= load_map.end(); ++it) {
        os << "    " << it->first << ": " << it->second << '\n';
        double normalized_load = it->second / expected_load_per_server;
        load_sum += normalized_load;
        load_sqr_sum += normalized_load * normalized_load;
    }
    os << "  }\n";
    os << "deviation: "  
       << sqrt(load_sqr_sum * load_map.size() - load_sum * load_sum) 
          / load_map.size();
    os << "}\n";
}

void ConsistentHashingLoadBalancer::GetLoads(
    std::map<butil::EndPoint, double> *load_map) {
    load_map->clear();
    std::map<butil::EndPoint, uint32_t> count_map;
    do {
        butil::DoublyBufferedData<std::vector<Node> >::ScopedPtr s;
        if (_db_hash_ring.Read(&s) != 0) {
            break;
        }
        if (s->empty()) {
            break;
        }
        count_map[s->begin()->server_addr] += 
                s->begin()->hash + (UINT_MAX - (s->end() - 1)->hash);
        for (size_t i = 1; i < s->size(); ++i) {
            count_map[(*s.get())[i].server_addr] +=
                    (*s.get())[i].hash - (*s.get())[i - 1].hash;
        }
    } while (0);
    for (std::map<butil::EndPoint, uint32_t>::iterator 
            it = count_map.begin(); it!= count_map.end(); ++it) {
        (*load_map)[it->first] = (double)it->second / UINT_MAX;
    }
}

}  // namespace policy
} // namespace brpc
