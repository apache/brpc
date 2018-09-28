// Copyright (c) 2018 Iqiyi  , Inc.
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

// Authors: Cai,Daojin (caidaojin@qiyi.com)

#include <gflags/gflags.h>
#include "brpc/socket.h"
#include "brpc/socket_map.h"
#include "brpc/policy/couchbase_load_balancer.h"
#include "butil/fast_rand.h"
#include "butil/macros.h"
#include "butil/third_party/libvbucket/vbucket.h"


namespace brpc {
namespace policy {


bool CouchbaseLoadBalancer::UpdateVBucketMap(
    const std::string& vbucket_map_str, 
    const std::map<std::string, SocketId>* server_id_map) {
    butil::VBUCKET_CONFIG_HANDLE vb_conf = 
        butil::vbucket_config_parse_string(vbucket_map_str.c_str());
    if (vb_conf == nullptr) {
        LOG(ERROR) << "Null VBUCKET_CONFIG_HANDLE.";
        return false;
    }

    butil::VBUCKET_DISTRIBUTION_TYPE distribution = 
        butil::vbucket_config_get_distribution_type(vb_conf);
    if (distribution != butil::VBUCKET_DISTRIBUTION_VBUCKET) {
        LOG(FATAL) << "Only support this type of vbucket distribution: " << distribution;
        butil::vbucket_config_destroy(vb_conf);
        return false;
    }
    const size_t vb_num = butil::vbucket_config_get_num_vbuckets(vb_conf);
    const size_t replicas_num = butil::vbucket_config_get_num_replicas(vb_conf);
    const size_t server_num = butil::vbucket_config_get_num_servers(vb_conf);
    std::vector<std::vector<int>> vbuckets(vb_num);
    std::vector<std::vector<int>> fvbuckets;
    std::vector<SocketId> servers(server_num, 0);  
    bool has_forward_vb = butil::vbucket_config_has_forward_vbuckets(vb_conf);
    // Init detected vbucket map.
    if (distribution == butil::VBUCKET_DISTRIBUTION_VBUCKET && 
        !_detected_vbucket_map) {
        _detected_vbucket_map.reset(new DetectedVBucketMap(vb_num));    
    }
    if (has_forward_vb) {
        fvbuckets.resize(vb_num);
    }
    for (size_t i = 0; i != vb_num; ++i) {
        vbuckets[i].resize(replicas_num + 1, -1);
        vbuckets[i][0] = butil::vbucket_get_master(vb_conf, i);
        if (has_forward_vb) {
            fvbuckets[i].resize(replicas_num + 1, -1);
            fvbuckets[i][0] = butil::fvbucket_get_master(vb_conf, i);
        } 
        for (size_t j = 0; j < replicas_num; ++j) {
            vbuckets[i][j+1] = butil::vbucket_get_replica(vb_conf, i, j);
            if (has_forward_vb) {
                fvbuckets[i][j+1] = butil::fvbucket_get_replica(vb_conf, i, j);
            } 
        }
    }
    const std::map<std::string, SocketId>* curr_server_id_map = nullptr;
    const VBucketServerMap* vb_map = vbucket_map();
    if (vb_map) {
        curr_server_id_map = &vb_map->server_id_map;
    }
    std::map<std::string, SocketId>::const_iterator iter;
    for (size_t i = 0; i != server_num; ++i) {
        const char* addr = butil::vbucket_config_get_server(vb_conf, i);
        if ((server_id_map && 
            ((iter = server_id_map->find(addr)) != server_id_map->end())) 
            || (curr_server_id_map && 
            (iter = curr_server_id_map->find(addr)) != curr_server_id_map->end())) {
            servers[i] = iter->second;
        } else {
            CHECK(false) << "Failed to find socket id of address=\'" << addr << '\'';
        }
    }
    uint64_t version = 0;
    bool last_rebalance = false;
    if (vb_map) {
        last_rebalance = IsInRebalancing(vb_map);
        version = vb_map->version;
    }
    bool curr_rebalance = !fvbuckets.empty();
	
    auto fn = std::bind(Update, std::placeholders::_1, replicas_num, 
                        vbuckets, fvbuckets, servers, server_id_map);
    bool ret = _vbucket_map.Modify(fn);

    if (!last_rebalance && curr_rebalance) {
        LOG(ERROR) << "Couchbase(bucket=" << butil::vbucket_config_get_user(vb_conf)
            << ") enter rebalance status from version " << ++version;
    }
    if (last_rebalance && !curr_rebalance) {
        DetectedVBucketMap& detect_map = *_detected_vbucket_map;
        for (size_t vb_id = 0; vb_id != vb_num; ++vb_id) {
            detect_map[vb_id]._verified.store(false, butil::memory_order_relaxed);
            detect_map[vb_id]._id.store(-1, butil::memory_order_relaxed); 
        }
        LOG(ERROR) << "Couchbase(bucket=" << butil::vbucket_config_get_user(vb_conf) 
            << ") quit rebalance status from version " << ++version;
    }
		butil::vbucket_config_destroy(vb_conf);
    return ret;
}

bool CouchbaseLoadBalancer::AddServer(const ServerId& id) {
    if (!id.tag.empty()) {
        return UpdateVBucketMap(id.tag, nullptr);
    }
    return false;
}

bool CouchbaseLoadBalancer::RemoveServer(const ServerId& id) {
    return true;
}

size_t CouchbaseLoadBalancer::AddServersInBatch(
    const std::vector<ServerId>& servers) {
    size_t n = 0;
    const ServerId* server_with_vbucket_map = nullptr;
    std::map<std::string, SocketId> server_id_map;
    for (const auto& server : servers) {
        if (!server.tag.empty()) {
            server_with_vbucket_map = &server;
            continue;
        }
        SocketUniquePtr ptr;
        if (Socket::AddressFailedAsWell(server.id, &ptr) == -1) {
            continue;
        }
        server_id_map.emplace(endpoint2str(ptr->remote_side()).c_str(), 
                              server.id);
        ++n;
    }
    if (server_with_vbucket_map != nullptr) {
        n += !!UpdateVBucketMap(server_with_vbucket_map->tag, &server_id_map);
    }
    return n;
}

size_t CouchbaseLoadBalancer::RemoveServersInBatch(
    const std::vector<ServerId>& servers) {
    return 0;
}

int CouchbaseLoadBalancer::SelectServer(const SelectIn& in, SelectOut* out) {
    if (!in.has_request_code) {
        return ENODATA;
    }
    uint32_t vb_id, reason;
    CouchbaseHelper::ParseRequestCode(in.request_code, &vb_id, &reason);
    butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vb_map;
    if(_vbucket_map.Read(&vb_map) != 0) {
        return ENOMEM;
    }
    if (vb_map->version == 0) {
        return ENODATA;
    }

    // if vb_id is 0xffffffff, it is called by CheckHealth(). 'Reason' is index of
    // server_list.
    if (vb_id == 0xffffffff) {
        if (reason >= vb_map->server_list.size() || 
            (Socket::Address(vb_map->server_list[reason], out->ptr) == 0 
                && !(*out->ptr)->IsLogOff())) {
            return 0;
        }
        return EHOSTDOWN;
    }
    SocketId selected_id = 0;
    switch (reason) {
    case FIRST_REQUEST: // first request
        if (!IsInRebalancing(vb_map.get()) || 
            brpc::FLAGS_couchbase_disable_retry_during_rebalance) {
            selected_id = CouchbaseHelper::GetMaster(vb_map.get(), vb_id);
        } else {
            selected_id = GetDetectedMaster(vb_map.get(), vb_id);
            if (selected_id == 0) {
                selected_id = CouchbaseHelper::GetMaster(vb_map.get(), vb_id);
            }
        }
        break;
    case SERVER_DOWN_RETRY_REPLICAS: // Retry to read replicas
        for(size_t i = 1; i <= vb_map->num_replicas; ++i) {
            selected_id = CouchbaseHelper::GetReplicaId(vb_map.get(), vb_id, i);
            if (selected_id == 0) {
                return ENODATA;
            }
            if (ExcludedServers::IsExcluded(in.excluded, selected_id)) {
                selected_id = 0;
                continue;
            }
        }
        break;
    default: // other retry case
        SocketId master_id = CouchbaseHelper::GetMaster(vb_map.get(), vb_id);
		    SocketId curr_id = in.excluded->GetLastId();
        SocketId dummy_id = 0;
        if (IsInRebalancing(vb_map.get())) {
            selected_id = GetDetectedMaster(vb_map.get(), vb_id);
            if (selected_id == 0) {
                if (curr_id != master_id) {
                    selected_id = master_id;
                    break;
                }
                selected_id = CouchbaseHelper::GetForwardMaster(vb_map.get(), 
                                                                vb_id);
                if (selected_id == 0) {
                    selected_id = GetNextProbeId(vb_map.get(), vb_id, curr_id);
                    if (selected_id == 0) {
                        return ENODATA;
                    }  
                }
                (*_detected_vbucket_map)[vb_id]._id.compare_exchange_strong(
                    dummy_id, selected_id, butil::memory_order_release);	
            }  
        } else if(reason == RPC_SUCCESS_BUT_WRONG_SERVER) {              
            selected_id = GetNextProbeId(vb_map.get(), vb_id, curr_id);
        } else {
            selected_id = master_id;
        }
        break;
    }

    if (selected_id == 0) {
        return ENODATA;
    }
    if (Socket::Address(selected_id, out->ptr) == 0 && !(*out->ptr)->IsLogOff()) {
        return 0;
    }
    return EHOSTDOWN;     
}

CouchbaseLoadBalancer* CouchbaseLoadBalancer::New() const {
    return new (std::nothrow) CouchbaseLoadBalancer;
}

void CouchbaseLoadBalancer::Destroy() {
    delete this;
}

void CouchbaseLoadBalancer::Describe(
    std::ostream &os, const DescribeOptions& options) {
    if (!options.verbose) {
        os << "cb_lb";
        return;
    }
    os << "CouchbaseLoadBalancer{";
    butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vb_map;
    if(_vbucket_map.Read(&vb_map) != 0) {
        os << "fail to read vbucket map";
    } else {
        os << "n=" << vb_map->server_list.size() << ':';
        auto iter = vb_map->server_map.begin();
        for (; iter != vb_map->server_map.end(); ++iter) {
            os << ' ' << iter->first;
        }
    }
    os << '}';
}

bool CouchbaseLoadBalancer::Update(VBucketServerMap& vbucket_map, 
                                   const size_t num_replicas,
                                   std::vector<std::vector<int>>& vbucket,
                                   std::vector<std::vector<int>>& fvbucket,
                                   std::vector<SocketId>& servers,
                                   const std::map<std::string, SocketId>* server_id_map) {
    ++(vbucket_map.version);
    vbucket_map.num_replicas = num_replicas;
    vbucket_map.vbucket.swap(vbucket);
    vbucket_map.fvbucket.swap(fvbucket);
    vbucket_map.server_list.swap(servers);
    vbucket_map.server_map.clear();
    for (size_t i = 0; i != vbucket_map.server_list.size(); ++i) {
        vbucket_map.server_map.emplace(vbucket_map.server_list[i], i);
    }
    // Override old 'server_id_map' by new one.
    if (server_id_map != nullptr) {
        for (auto iter = server_id_map->begin(); iter != server_id_map->end(); ++iter) {
            auto curr_iter = vbucket_map.server_id_map.emplace(*iter);
            if (!curr_iter.second) {
                curr_iter.first->second = iter->second;
            }
        }
    }
    return true;
}

const VBucketServerMap* CouchbaseLoadBalancer::vbucket_map() {
    butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vbucket_map;
    if(_vbucket_map.Read(&vbucket_map) != 0) {
        LOG(ERROR) << "Failed to read vbucket map.";
        return nullptr;
    }
    return vbucket_map.get();
}

SocketId CouchbaseLoadBalancer::GetDetectedMaster(const VBucketServerMap* vb_map, 
                                                  const size_t vb_id) {
    butil::atomic<SocketId>& detected_id = (*_detected_vbucket_map)[vb_id]._id;
    SocketId id = detected_id.load(butil::memory_order_acquire);
    if (vb_map->server_map.find(id) != vb_map->server_map.end()) {
        return id;
    } else {
        detected_id.compare_exchange_strong(id, 0, butil::memory_order_relaxed);
    }
    return 0;
}						  

SocketId CouchbaseLoadBalancer::GetNextProbeId(const VBucketServerMap* vb_map, 
                                               const size_t vb_id, const SocketId id) {
    const auto iter = vb_map->server_map.find(id);
	  if (iter == vb_map->server_map.end()) {
        return 0;
    }
    const size_t curr_index = iter->second;
    return vb_map->server_list[(curr_index + 1) % vb_map->server_list.size()];
}

void CouchbaseLoadBalancer::UpdateDetectedMasterIfNeeded(
    const bool is_reading_replicas, const uint32_t vb_id, 
    const uint32_t reason, const std::string& curr_server) {
    if (is_reading_replicas || reason == DEFAULT_DUMMY 
		    || reason == RPC_FAILED) {
        return;
    }
    butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vb_map;
    if(_vbucket_map.Read(&vb_map) != 0) {
        LOG(ERROR) << "Failed to read vbucket map.";
        return;
    }
    if (!IsInRebalancing(vb_map.get())) {
        return;
    }
    SocketId id = 0;
    const auto iter = vb_map->server_id_map.find(curr_server); 
    if (iter != vb_map->server_id_map.end()) {
        id = iter->second;
    } else {
        return;
    }
    DetectedMaster& detect_master = (*_detected_vbucket_map)[vb_id];
    butil::atomic<bool>& is_verified = detect_master._verified;
    butil::atomic<SocketId>& detected_id = detect_master._id;
    // reason == RPC_SUCCESS_BUT_RESPONSE_FAULT || reason == RESPONSE_OK
    if (reason != SERVER_DOWN && reason != RPC_SUCCESS_BUT_WRONG_SERVER) {
        // We detected the right new master for vbucket no matter the
        // response status is success or not. Record for following request
        // during rebalancing.
        if (id == CouchbaseHelper::GetMaster(vb_map.get(), vb_id)) {
            return;
        }
        if (detected_id.load(butil::memory_order_acquire) != id) {
            detected_id.store(id, butil::memory_order_relaxed);
        }
        bool not_verified = false;
        is_verified.compare_exchange_strong(not_verified, true,
                                            butil::memory_order_release, 
                                            butil::memory_order_relaxed);
    } else { 
        // Server is down or it is a wrong server of the vbucket. Go on probing 
        // other servers.
        SocketId next_id = 0;
        SocketId dummy_id = 0;
        SocketId next_probe_id = GetNextProbeId(vb_map.get(), vb_id, id);
        if (next_probe_id == 0) {
            return;
        }
        // Detect forward master as the first if having forwad master, 
        // otherwise, probe other servers. 
        if (0 == detected_id.load(butil::memory_order_acquire)) {
            const SocketId f_id = CouchbaseHelper::GetForwardMaster(vb_map.get(), vb_id);
            next_id = f_id != 0 ? f_id : next_probe_id;
            detected_id.compare_exchange_strong(dummy_id, next_id,
                                                butil::memory_order_release, 
                                                butil::memory_order_relaxed);
            if (is_verified.load(butil::memory_order_acquire)) {
                is_verified.store(false, butil::memory_order_relaxed); 
            }
        } else {
            if (is_verified.load(butil::memory_order_acquire)) {
                // Verified master server is invalid. Reset to detect again.
                if (detected_id.compare_exchange_strong(id, 0, butil::memory_order_relaxed, 
                                                        butil::memory_order_relaxed)) {
                   is_verified.store(false, butil::memory_order_relaxed); 
                }
            } else {
                // Probe next servers.
                detected_id.compare_exchange_strong(id, next_probe_id, 
                                                    butil::memory_order_release, 
                                                    butil::memory_order_relaxed);
            } 
        }
    }
}

}  // namespace policy
} // namespace brpc
