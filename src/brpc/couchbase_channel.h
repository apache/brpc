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

#include "brpc/channel.h"
#include "brpc/policy/couchbase_naming_service.h"


namespace brpc {

// A couchbase channel maps different key to sub memcache channel according to 
// current vbuckets mapping. It retrieves current vbuckets mapping by maintain
// an connection for streaming updates from the couchbase server.
// CouchbaseChannel is a fully functional Channel:
//   * synchronous and asynchronous RPC.
//   * deletable immediately after an asynchronous call.
//   * cancelable call_id.
//   * timeout.
class CouchbaseChannel : public ChannelBase/*non-copyable*/ {
public:
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

    // Send request to the mapped channel according to the key of request.
    // Do not support pipeline mode now.
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done);

    void Describe(std::ostream& os, const DescribeOptions& options);

private:
    int InitMemcacheChannel(const char* servers, const std::string& streaming_url,
                            const std::string& init_url, const ChannelOptions* options);
							 
    int CheckHealth();

    static bool ParseListenUri(const char* listen_uri, std::string* server, 
                               std::string* streaming_uri, std::string* init_uri);

    policy::CouchbaseNamingService* _ns = nullptr;

    Channel _channel;
};

} // namespace brpc

#endif  // BRPC_COUCHBASE_CHANNEL_H
