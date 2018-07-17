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
#include "butil/containers/doubly_buffered_data.h"

namespace brpc {

class CouchbaseServerListener;

class CouchbaseChannel : public ChannelBase/*non-copyable*/ {
public:
    CouchbaseChannel() = default;
    ~CouchbaseChannel();

    // You MUST initialize a couchbasechannel before using it. 'Server_addr' 
    // is address of couchbase server. 'options' is used for each channel to 
    // real servers of bucket. The protocol should be PROTOCOL_MEMCACHE. 
    // If 'options' is null, use default options. 
    int Init(const char* server_addr, const ChannelOptions* options);

    // TODO: Do not support pipeline mode now.
    // Send request to the mapped channel according to the key of request.
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done);

    void Describe(std::ostream& os, const DescribeOptions& options) const;

private:
    // TODO: This struct describes map between vbucket and real memcache server.
    // '_hash_algorithm': The hash algorithm couchbase used.
    // '_vbucket_servers': server list of vbuckets, like "list://addr1:port1,
    //                     addr2:port2...".
    // '_channel_map': the channel for each vbucket.
    struct VBucketServerMap {
        std::string _hash_algorithm;
        std::vector<std::string> _vbucket_servers;
        std::unordered_map<std::string, std::unique_ptr<Channel>> _channel_map;
    };

    int CheckHealth();

    bool GetKeyFromRequest(const google::protobuf::Message* request, 
                           butil::StringPiece* key);

    Channel* SelectChannel(const butil::StringPiece& key, 
                           const VBucketServerMap* vbucket_map);

    //TODO: Get different hash algorithm if needed.
    size_t Hash(const std::string& type,
                const butil::StringPiece& key, 
                const size_t size);

    bool UpdateVBucketServerMap(
        const std::string* hash_algo,
        std::vector<std::string>* vbucket_servers,
        const std::vector<std::string>* added_vbuckets,
        const std::vector<std::string>* removed_vbuckets);

    static bool Update(VBucketServerMap& vbucket_map, 
                       const ChannelOptions* options,
                       const std::string* hash_algo,
                       std::vector<std::string>* vbucket_servers,
                       const std::vector<std::string>* added_vbuckets,
                       const std::vector<std::string>* removed_vbuckets);

    // Options for each memcache channel of vbucket.
    ChannelOptions _common_options; 
    std::unique_ptr<CouchbaseServerListener> _listener;
    // Memcache channel of each vbucket of couchbase. The key is the server list
    // of this vbucket, like 'list://addr1:port1,addr2:port2...'.
    butil::DoublyBufferedData<VBucketServerMap> _vbucket_map;
};

} // namespace brpc

#endif  // BRPC_COUCHBASE_CHANNEL_H
