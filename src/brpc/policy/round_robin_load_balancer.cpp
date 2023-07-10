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


#include "butil/macros.h"
#include "butil/fast_rand.h"
#include "brpc/socket.h"
#include "brpc/policy/round_robin_load_balancer.h"


namespace brpc {
namespace policy {

const uint32_t prime_offset[] = {
#include "bthread/offset_inl.list"
};

inline uint32_t GenRandomStride() {
    return prime_offset[butil::fast_rand_less_than(ARRAY_SIZE(prime_offset))];
}

bool RoundRobinLoadBalancer::Add(Servers& bg, const ServerId& id) {
    if (bg.server_list.capacity() < 128) {
        bg.server_list.reserve(128);
    }
    std::map<ServerId, size_t>::iterator it = bg.server_map.find(id);
    if (it != bg.server_map.end()) {
        return false;
    }
    bg.server_map[id] = bg.server_list.size();
    bg.server_list.push_back(id);
    return true;
}

bool RoundRobinLoadBalancer::Remove(Servers& bg, const ServerId& id) {
    std::map<ServerId, size_t>::iterator it = bg.server_map.find(id);
    if (it != bg.server_map.end()) {
        const size_t index = it->second;
        bg.server_list[index] = bg.server_list.back();
        bg.server_map[bg.server_list[index]] = index;
        bg.server_list.pop_back();
        bg.server_map.erase(it);
        return true;
    }
    return false;
}

size_t RoundRobinLoadBalancer::BatchAdd(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Add(bg, servers[i]);
    }
    return count;
}

size_t RoundRobinLoadBalancer::BatchRemove(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Remove(bg, servers[i]);
    }
    return count;
}

bool RoundRobinLoadBalancer::AddServer(const ServerId& id) {
    return _db_servers.Modify(Add, id);
}

bool RoundRobinLoadBalancer::RemoveServer(const ServerId& id) {
    return _db_servers.Modify(Remove, id);
}

size_t RoundRobinLoadBalancer::AddServersInBatch(
    const std::vector<ServerId>& servers) {
    const size_t n = _db_servers.Modify(BatchAdd, servers);
    LOG_IF(ERROR, n != servers.size())
        << "Fail to AddServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

size_t RoundRobinLoadBalancer::RemoveServersInBatch(
    const std::vector<ServerId>& servers) {
    const size_t n = _db_servers.Modify(BatchRemove, servers);
    LOG_IF(ERROR, n != servers.size())
        << "Fail to RemoveServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

int RoundRobinLoadBalancer::SelectServer(const SelectIn& in, SelectOut* out) {
    butil::DoublyBufferedData<Servers, TLS>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        return ENOMEM;
    }
    const size_t n = s->server_list.size();
    if (n == 0) {
        return ENODATA;
    }
    if (_cluster_recover_policy && _cluster_recover_policy->StopRecoverIfNecessary()) {
        if (_cluster_recover_policy->DoReject(s->server_list)) {
            return EREJECT;
        }
    }
    TLS tls = s.tls();
    if (tls.stride == 0) {
        tls.stride = GenRandomStride();
        // use random at first time, for the case of
        // use rr lb every time in new thread
        tls.offset = butil::fast_rand_less_than(n);
    }

    for (size_t i = 0; i < n; ++i) {
        tls.offset = (tls.offset + tls.stride) % n;
        const SocketId id = s->server_list[tls.offset].id;
        if (((i + 1) == n  // always take last chance
             || !ExcludedServers::IsExcluded(in.excluded, id))
            && Socket::Address(id, out->ptr) == 0
            && (*out->ptr)->IsAvailable()) {
            s.tls() = tls;
            return 0;
        }
    }
    if (_cluster_recover_policy) {
        _cluster_recover_policy->StartRecover();
    }
    s.tls() = tls;
    return EHOSTDOWN;
}

RoundRobinLoadBalancer* RoundRobinLoadBalancer::New(
    const butil::StringPiece& params) const {
    RoundRobinLoadBalancer* lb = new (std::nothrow) RoundRobinLoadBalancer;
    if (lb && !lb->SetParameters(params)) {
        delete lb;
        lb = NULL;
    }
    return lb;
}

void RoundRobinLoadBalancer::Destroy() {
    delete this;
}

void RoundRobinLoadBalancer::Describe(
    std::ostream &os, const DescribeOptions& options) {
    if (!options.verbose) {
        os << "rr";
        return;
    }
    os << "RoundRobin{";
    butil::DoublyBufferedData<Servers, TLS>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        os << "fail to read _db_servers";
    } else {
        os << "n=" << s->server_list.size() << ':';
        for (size_t i = 0; i < s->server_list.size(); ++i) {
            os << ' ' << s->server_list[i];
        }
    }
    os << '}';
}

bool RoundRobinLoadBalancer::SetParameters(const butil::StringPiece& params) {
    return GetRecoverPolicyByParams(params, &_cluster_recover_policy);
}

}  // namespace policy
} // namespace brpc
