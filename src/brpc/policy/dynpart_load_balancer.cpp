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
#include "brpc/policy/dynpart_load_balancer.h"


namespace brpc {

namespace schan {
// defined in brpc/selective_channel.cpp
int GetSubChannelWeight(SocketUser* u);
}

namespace policy {

bool DynPartLoadBalancer::Add(Servers& bg, const ServerId& id) {
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

bool DynPartLoadBalancer::Remove(Servers& bg, const ServerId& id) {
    std::map<ServerId, size_t>::iterator it = bg.server_map.find(id);
    if (it != bg.server_map.end()) {
        size_t index = it->second;
        bg.server_list[index] = bg.server_list.back();
        bg.server_map[bg.server_list[index]] = index;
        bg.server_list.pop_back();
        bg.server_map.erase(it);
        return true;
    }
    return false;
}

size_t DynPartLoadBalancer::BatchAdd(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Add(bg, servers[i]);
    }
    return count;
}

size_t DynPartLoadBalancer::BatchRemove(
    Servers& bg, const std::vector<ServerId>& servers) {
    size_t count = 0;
    for (size_t i = 0; i < servers.size(); ++i) {
        count += !!Remove(bg, servers[i]);
    }
    return count;
}

bool DynPartLoadBalancer::AddServer(const ServerId& id) {
    return _db_servers.Modify(Add, id);
}

bool DynPartLoadBalancer::RemoveServer(const ServerId& id) {
    return _db_servers.Modify(Remove, id);
}

size_t DynPartLoadBalancer::AddServersInBatch(
    const std::vector<ServerId>& servers) {
    const size_t n = _db_servers.Modify(BatchAdd, servers);
    LOG_IF(ERROR, n != servers.size())
        << "Fail to AddServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

size_t DynPartLoadBalancer::RemoveServersInBatch(
    const std::vector<ServerId>& servers) {
    const size_t n = _db_servers.Modify(BatchRemove, servers);
    LOG_IF(ERROR, n != servers.size())
        << "Fail to RemoveServersInBatch, expected " << servers.size()
        << " actually " << n;
    return n;
}

int DynPartLoadBalancer::SelectServer(const SelectIn& in, SelectOut* out) {
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
    if (_db_servers.Read(&s) != 0) {
        return ENOMEM;
    }
    const size_t n = s->server_list.size();
    if (n == 0) {
        return ENODATA;
    }
    if (n == 1) {
        if (Socket::Address(s->server_list[0].id, out->ptr) == 0) {
            return 0;
        }
        return EHOSTDOWN;
    }
    int64_t total_weight = 0;
    std::pair<SocketUniquePtr, int64_t> ptrs[8];
    int nptr = 0;
    bool exclusion = true;
    do {
        for (size_t i = 0; i < n; ++i) {
            const SocketId id = s->server_list[i].id;
            if ((!exclusion || !ExcludedServers::IsExcluded(in.excluded, id))
                && Socket::Address(id, &ptrs[nptr].first) == 0) {
                int w = schan::GetSubChannelWeight(ptrs[nptr].first->user());
                total_weight += w;
                if (nptr < 8) {
                    ptrs[nptr].second = total_weight;
                    ++nptr;
                } else {
                    CHECK(false) << "Not supported yet";
                    abort();
                }
            }
        }
        if (nptr != 0) {
            break;
        }
        if (!exclusion) {
            return EHOSTDOWN;
        }
        exclusion = false;
        CHECK_EQ(0, total_weight);
        total_weight = 0;
    } while (1);
    
    if (nptr == 1) {
        out->ptr->reset(ptrs[0].first.release());
        return 0;
    }
    uint32_t r = butil::fast_rand_less_than(total_weight);
    for (int i = 0; i < nptr; ++i) {
        if (ptrs[i].second > r) {
            out->ptr->reset(ptrs[i].first.release());
            return 0;
        }
    }
    return EHOSTDOWN;
}

DynPartLoadBalancer* DynPartLoadBalancer::New(const butil::StringPiece&) const {
    return new (std::nothrow) DynPartLoadBalancer;
}

void DynPartLoadBalancer::Destroy() {
    delete this;
}

void DynPartLoadBalancer::Describe(
    std::ostream &os, const DescribeOptions& options) {
    if (!options.verbose) {
        os << "dynpart";
        return;
    }
    os << "DynPart{";
    butil::DoublyBufferedData<Servers>::ScopedPtr s;
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

}  // namespace policy
} // namespace brpc
