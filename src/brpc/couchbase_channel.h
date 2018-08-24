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

#ifndef BRPC_COUCHBASE_CHANNEL_H
#define BRPC_COUCHBASE_CHANNEL_H

#include <vector>
#include <unordered_map>

#include "brpc/channel.h"
#include "brpc/couchbase.h"
#include "butil/containers/doubly_buffered_data.h"

namespace brpc {

using CouchbaseChannelMap = 
    std::unordered_map<std::string, std::unique_ptr<Channel>>; 

class CouchbaseServerListener;

// A couchbase channel maps different key to sub memcache channel according to 
// current vbuckets mapping. It retrieves current vbuckets mapping by maintain
// an connection for streaming updates from ther couchbase server.
//
// CAUTION:
// ========
// For async rpc, Should not delete this channel until rpc done.
class CouchbaseChannel : public ChannelBase/*non-copyable*/ {
friend class CouchbaseServerListener;
public:
    CouchbaseChannel();
    ~CouchbaseChannel();

    // You MUST initialize a couchbasechannel before using it. 
    // 'Server_addr': address list of couchbase servers. On these addresses, we 
    //                can get vbucket map. 
    // 'options': is used for each memcache channel of vbucket. The protocol 
    //            should be PROTOCOL_MEMCACHE. If 'options' is null, 
    //            use default options. 
    int Init(const char* server_addr, const ChannelOptions* options);

    // TODO: Do not support pipeline mode now.
    // Send request to the mapped channel according to the key of request.
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done);

    void Describe(std::ostream& os, const DescribeOptions& options);

    // Couchbase has two type of distribution used to map keys to servers.
    // One is vbucket distribution and other is ketama distribution.
    // This struct describes vbucket distribution of couchbase.
    // 'num_replicas': the number of copies that will be stored on servers of one
    //                 vbucket. Each vbucket must have this number of servers 
    //                 indexes plus one.
    // '_vbucket': A zero-based indexed by vBucketId. The entries in the _vbucket
    //            are arrays of integers, where each integer is a zero-based 
    //            index into the '_servers'. 
    // '_fvbucket': It is fast forward map with same struct as _vbucket. It is 
    //              used to provide the final vBubcket-to-server map during the
    //              statrt of the rebalance. 
    // '_servers': all servers of a bucket.                    
    // '_channel_map': the memcache channel for each server.
    // TODO: support ketama vbucket distribution
    struct VBucketServerMap {
        uint64_t _version = 0;
        int _num_replicas = 0;    
        std::vector<std::vector<int>> _vbucket;
        std::vector<std::vector<int>> _fvbucket;
        std::vector<std::string> _servers;
        CouchbaseChannelMap _channel_map;
    };

private:
    int CheckHealth();

    Channel* SelectMasterChannel(const VBucketServerMap* vb_map, 
                                 const size_t vb_index);

    Channel* GetMappedChannel(const std::string* server,
                              const VBucketServerMap* vb_map);

    const CouchbaseChannelMap& GetChannelMap();

    const std::string* GetMaster(const VBucketServerMap* vb_map, 
                                 const size_t vb_index, int* index = nullptr);

    size_t Hash(const butil::StringPiece& key, const size_t vbuckets_num);

    bool UpdateVBucketServerMap(
        const int num_replicas,
        std::vector<std::vector<int>>& vbucket,
        std::vector<std::vector<int>>& fvbucket,
        std::vector<std::string>& servers,
        const std::vector<std::string>& added_servers,
        const std::vector<std::string>& removed_serverss);

    static bool Update(VBucketServerMap& vbucket_map, 
                       const ChannelOptions* options,
                       const int num_replicas,
                       std::vector<std::vector<int>>& vbucket,
                       std::vector<std::vector<int>>& fvbucket,
                       std::vector<std::string>& servers,
                       const std::vector<std::string>& added_servers,
                       const std::vector<std::string>& removed_servers);

    std::string GetAuthentication() const;

    // Options for each memcache channel of vbucket.
    ChannelOptions _common_options; 
    // Listener monitor and update vbucket map information.
    std::unique_ptr<CouchbaseServerListener> _listener;
    butil::DoublyBufferedData<VBucketServerMap> _vbucket_map;
};

} // namespace brpc

#endif  // BRPC_COUCHBASE_CHANNEL_H
