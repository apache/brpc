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

#include "brpc/couchbase_channel.h"
#include "bthread/bthread.h"

namespace brpc {

class CouchbaseServerListener {
public:
    CouchbaseServerListener(const char* server_addr, 
                            const CouchbaseChannel* channel) 
        : _server_addr(server_addr), _channel(channel) {
        //TODO: Init vbucket map for first time.
        CHECK(bthread_start_background(
              &_bthread_id, nullptr, ListenThread, this) == 0)
            << "Failed to start ListenThread."; 
    }

    ~CouchbaseServerListener() {
        bthread_stop(_bthread_id);
        bthread_join(_bthread_id, nullptr);
    }; 

private:
    CouchbaseServerListener(const CouchbaseServerListener&) = delete;
    CouchbaseServerListener& operator=(const CouchbaseServerListener&) = delete;
 
    static void* ListenThread(void* arg);

    bthread_t _bthread_id;
    const std::string _server_addr;
    const CouchbaseChannel* _channel;
    std::vector<std::string> _vbucket_servers;
};

//TODO: Get current vbucket map of couchbase server
void* CouchbaseServerListener::ListenThread(void* arg) {
    return nullptr;
}

CouchbaseChannel::~CouchbaseChannel() {
    _listener.reset(nullptr);
}

int CouchbaseChannel::Init(const char* server_addr, 
                           const ChannelOptions* options) {
    if (options != nullptr) {
        if (options->protocol != PROTOCOL_UNKNOWN && 
            options->protocol != PROTOCOL_MEMCACHE) {
            LOG(FATAL) << "Failed to init channel due to invalid protoc " 
                <<  options->protocol.name() << '.';
            return -1;
        }
        _common_options = *options;
        _common_options.protocol = PROTOCOL_MEMCACHE;
    } else {
        // TODO: use a default options.
    }
    auto ptr = new CouchbaseServerListener(server_addr, this);
    if (ptr == nullptr) {
        LOG(FATAL) << "Failed to init CouchbaseChannel to " << server_addr << '.';
        return -1;
    }
    _listener.reset(ptr);
    return 0;
} 

void CouchbaseChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                                  google::protobuf::RpcController* controller,
                                  const google::protobuf::Message* request,
                                  google::protobuf::Message* response,
                                  google::protobuf::Closure* done) {
    bool success = false;
    butil::StringPiece key;
    if (GetKeyFromRequest(request, &key)) {
        butil::DoublyBufferedData<VBucketServerMap>::ScopedPtr vbucket_map;
        if(_vbucket_map.Read(&vbucket_map) == 0) {
            Channel* mapped_channel = SelectChannel(key, vbucket_map.get());
            if (mapped_channel != nullptr) {
                mapped_channel->CallMethod(nullptr, 
                                           controller, 
                                           request, 
                                           response,
                                           done);
                success = true;
            }
        } else {
            LOG(ERROR) << "Failed to read vbucket map.";
        }
    } else {
        LOG(ERROR) << "Failed to get key from request.";
    } 

    if (!success) {
        controller->SetFailed("Failed to send request");
    }
}

bool CouchbaseChannel::GetKeyFromRequest(const google::protobuf::Message* request, 
                                         butil::StringPiece* key) {
    return true;
}

Channel* CouchbaseChannel::SelectChannel(
    const butil::StringPiece& key, const VBucketServerMap* vbucket_map) {
    size_t index = Hash(vbucket_map->_hash_algorithm, 
                        key, vbucket_map->_vbucket_servers.size());
    auto iter = vbucket_map->_channel_map.find(
        vbucket_map->_vbucket_servers[index]); 
    if (iter != vbucket_map->_channel_map.end()) {
        return iter->second.get();
    } else {
        LOG(ERROR) << "Failed to find mapped channel.";
    }
    return nullptr;
}

//TODO: Get different hash algorithm if needed.
size_t CouchbaseChannel::Hash(const std::string& type,
                              const butil::StringPiece& key, 
                              const size_t size) {
    return 0;  
}

bool CouchbaseChannel::UpdateVBucketServerMap(
    const std::string* hash_algo,
    std::vector<std::string>* vbucket_servers,
    const std::vector<std::string>* added_vbuckets,
    const std::vector<std::string>* removed_vbuckets) {
    auto fn = std::bind(Update, 
                        std::placeholders::_1, 
                        &_common_options,
                        hash_algo,
                        vbucket_servers,
                        added_vbuckets,
                        removed_vbuckets);
    return _vbucket_map.Modify(fn);
}

bool CouchbaseChannel::Update(VBucketServerMap& vbucket_map, 
                       const ChannelOptions* options,
                       const std::string* hash_algo,
                       std::vector<std::string>* vbucket_servers,
                       const std::vector<std::string>* added_vbuckets,
                       const std::vector<std::string>* removed_vbuckets) {
    bool ret = true;
    if (hash_algo != nullptr) {
        vbucket_map._hash_algorithm = *hash_algo;
    }
    if (vbucket_servers != nullptr) {
        vbucket_map._vbucket_servers.swap(*vbucket_servers);
    }
    if (added_vbuckets != nullptr) {
        for (const auto& servers: *added_vbuckets) {
            std::unique_ptr<Channel> p(new Channel());
            if (p == nullptr) {
                LOG(FATAL) << "Failed to init channel.";
                return false;
            }
            if (p->Init(servers.c_str(), "rr", options) != 0) {
                LOG(FATAL) << "Failed to init channel.";
                return false;
            }
            auto pair = vbucket_map._channel_map.emplace(servers, std::move(p));
            if (!pair.second) {
                LOG(ERROR) << "Failed to add new channel to server: " << servers;
                ret = false;
            }
        }
    }
    if (removed_vbuckets != nullptr) {
        for (const auto& servers: *removed_vbuckets) {
            auto n = vbucket_map._channel_map.erase(servers);
            if (n == 0) {
                LOG(ERROR) << "Failed to remove channel to server: " << servers;
                ret = false;
            }            
        }
    }

    return ret;
}

} // namespace brpc
