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
// It is used to detect the new master server of vbuckets when the lastest 
// vbucket mapping has not been received during rebalance.
class DetectedMaster {
public:
    DetectedMaster() : _verified(false), _index(-1) {}
    butil::atomic<bool> _verified;
    butil::atomic<int> _index;

private:
    DetectedMaster(const DetectedMaster&) = delete;
    DetectedMaster& operator=(const DetectedMaster&) = delete;
};

using CouchbaseChannelMap = 
    std::unordered_map<std::string, std::unique_ptr<Channel>>; 
using DetectedVBucketMap = std::vector<DetectedMaster>;

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
    size_t _num_replicas = 0;    
    std::vector<std::vector<int>> _vbucket;
    std::vector<std::vector<int>> _fvbucket;
    std::vector<std::string> _servers;
    CouchbaseChannelMap _channel_map;
};

enum VBucketStatus {
    FORWARD_CREATE = 0x00,
    FORWARD_FINISH = 0x01,
    FORWARD_KEEPING = 0x02,
    FORWARD_CHANGE = 0x03,
    MASTER_CHANGE_WITHOUT_F = 0x04,
    MASTER_KEEPING_WITHOUT_F = 0x05,
    NO_CHANGE = 0x06,
};

class CouchbaseServerListener;
class VBucketContext;

// A couchbase channel maps different key to sub memcache channel according to 
// current vbuckets mapping. It retrieves current vbuckets mapping by maintain
// an connection for streaming updates from ther couchbase server.
//
// CAUTION:
// ========
// For async rpc, Should not delete this channel until rpc done.
class CouchbaseChannel : public ChannelBase/*non-copyable*/ {
friend class CouchbaseServerListener;
friend class VBucketContext;
friend class CouchbaseDone;
public:
    CouchbaseChannel();
    ~CouchbaseChannel();

    // You MUST initialize a couchbasechannel before using it. 
    // 'Server_addr': address list of couchbase servers. On these addresses, we 
    //                can get vbucket map. Like following: "addr1:port1,addr2:port2" 
    // 'bucket_name': the bucket name of couchbase server to access.
    // 'options': is used for each memcache channel of vbucket. The protocol 
    //            should be PROTOCOL_MEMCACHE. If 'options' is null, 
    //            use default options. 
    int Init(const char* server_addr, const char* bucket_name, 
             const ChannelOptions* options);

    // 'listen_url': from this url, we can get vbucket map. Usually, it is 
    // somthing like following:
    // "http://host:port/pools/default/bucketsStreaming/bucket_name" or
    // "http://host:port/pools/default/buckets/bucket_name"
    int Init(const char* listen_url, const ChannelOptions* options);

    // TODO: Do not support pipeline mode now.
    // Send request to the mapped channel according to the key of request.
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done);

    void Describe(std::ostream& os, const DescribeOptions& options);

private:
    int CheckHealth();

    Channel* SelectBackupChannel(const VBucketServerMap* vb_map, 
                                 const size_t vb_index, const int reason, 
                                 VBucketContext* context);

    Channel* GetMappedChannel(const std::string* server,
                              const VBucketServerMap* vb_map);

    const VBucketServerMap* vbucket_map();

    bool IsNeedRetry(const Controller* cntl, const VBucketContext& context,
                     CouchbaseResponse* response, int* reason, 
                     std::string* error_text);

    bool DoRetry(const int reason, Controller* cntl, 
                 CouchbaseResponse* response, VBucketContext* vb_ct);

    int GetDetectedMaster(const VBucketServerMap* vb_map, const size_t vb_index);

    void UpdateDetectedMasterIfNeeded(const int reason, 
                                      const VBucketContext& context);

    bool IsInRebalancing(const VBucketServerMap* vb_map) {
        return !vb_map->_fvbucket.empty();
    }
 
    const std::string* GetNextRetryServer(
        const VBucketStatus change, const int reason, 
        const VBucketServerMap* vb_map, const size_t vb_index, 
        VBucketContext* context);

    bool UpdateVBucketServerMap(
        const size_t num_replicas,
        std::vector<std::vector<int>>& vbucket,
        std::vector<std::vector<int>>& fvbucket,
        std::vector<std::string>& servers,
        const std::vector<std::string>& added_servers,
        const std::vector<std::string>& removed_serverss);

    static bool Update(VBucketServerMap& vbucket_map, 
                       const ChannelOptions* options,
                       const size_t num_replicas,
                       std::vector<std::vector<int>>& vbucket,
                       std::vector<std::vector<int>>& fvbucket,
                       std::vector<std::string>& servers,
                       const std::vector<std::string>& added_servers,
                       const std::vector<std::string>& removed_servers);

    std::string GetAuthentication() const;

    // Options for each memcache channel of real servers.
    ChannelOptions _common_options; 
    // Listener monitor and update vbucket map.
    std::unique_ptr<CouchbaseServerListener> _listener;
    // We need detect new vbucket map due to current vbucket map is invalid 
    // during rebalance.
    std::unique_ptr<DetectedVBucketMap> _detected_vbucket_map;
    butil::DoublyBufferedData<VBucketServerMap> _vbucket_map;
};

} // namespace brpc

#endif  // BRPC_COUCHBASE_CHANNEL_H
