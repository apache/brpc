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

#ifndef BRPC_REDIS_CLUSTER_H
#define BRPC_REDIS_CLUSTER_H

#include <stdint.h>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "bthread/bthread.h"
#include "butil/synchronization/lock.h"
#include "brpc/channel.h"
#include "brpc/channel_base.h"
#include "brpc/redis.h"

namespace brpc {

struct RedisClusterChannelOptions {
    RedisClusterChannelOptions();

    ChannelOptions channel_options;
    int max_redirect;
    int refresh_interval_s;
    bool enable_periodic_refresh;
    int topology_refresh_timeout_ms;
};

// Channel implementation for Redis Cluster.
class RedisClusterChannel : public ChannelBase {
public:
    RedisClusterChannel();
    ~RedisClusterChannel() override;

    DISALLOW_COPY_AND_ASSIGN(RedisClusterChannel);

    int Init(const std::string& seed_nodes,
             const RedisClusterChannelOptions* options = NULL);

    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done) override;

    int CheckHealth() override;
    int Weight() override;

private:
    struct ParsedCommand {
        std::vector<std::string> args;
    };

    struct RedirectInfo {
        bool valid;
        bool asking;
        int slot;
        std::string endpoint;
    };

    struct SingleCommandResult {
        bool ok;
        butil::IOBuf encoded_reply;
        bool is_status_ok;
        int64_t integer_value;
        bool is_error;
        std::string error_text;
        SingleCommandResult();
    };

    struct AsyncCall;

    static void* RunPeriodicRefresh(void* arg);
    static void* RunAsyncCall(void* arg);

    bool CallMethodImpl(Controller* cntl,
                        const RedisRequest& request,
                        RedisResponse* response);

    bool ParseRequest(const RedisRequest& request,
                      std::vector<ParsedCommand>* commands,
                      Controller* cntl) const;

    bool ExecuteCommand(const ParsedCommand& cmd,
                        butil::IOBuf* encoded_reply,
                        Controller* cntl);

    bool ExecuteSingleCommand(const std::vector<std::string>& args,
                              const std::string* forced_endpoint,
                              SingleCommandResult* result,
                              Controller* cntl);

    bool ExecuteMGet(const ParsedCommand& cmd,
                     butil::IOBuf* encoded_reply,
                     Controller* cntl);
    bool ExecuteMSet(const ParsedCommand& cmd,
                     butil::IOBuf* encoded_reply,
                     Controller* cntl);
    bool ExecuteIntegerAggregate(const ParsedCommand& cmd,
                                 butil::IOBuf* encoded_reply,
                                 Controller* cntl);
    bool ExecuteEvalLike(const ParsedCommand& cmd,
                         butil::IOBuf* encoded_reply,
                         Controller* cntl);

    bool PickEndpointForKey(const std::string& key, std::string* endpoint, int* slot) const;
    bool PickAnyEndpoint(std::string* endpoint) const;

    bool SendToEndpoint(const std::string& endpoint,
                        const std::vector<std::string>& args,
                        bool asking,
                        SingleCommandResult* result,
                        RedirectInfo* redirect,
                        Controller* cntl);

    bool RefreshTopology();
    bool RefreshTopologyFromEndpoint(const std::string& endpoint);
    bool FetchAndParseClusterSlots(Channel* channel,
                                   const std::string& endpoint,
                                   std::vector<std::string>* slot_to_endpoint,
                                   std::vector<std::string>* discovered_endpoints);
    bool FetchAndParseClusterNodes(Channel* channel,
                                   std::vector<std::string>* slot_to_endpoint,
                                   std::vector<std::string>* discovered_endpoints);

    void ApplyTopology(const std::vector<std::string>& slot_to_endpoint,
                       const std::vector<std::string>& discovered_endpoints);

    Channel* GetOrCreateChannel(const std::string& endpoint);

    static uint16_t HashSlot(const std::string& key);
    static std::string ExtractHashtag(const std::string& key);
    static bool ParseRedirectReply(const SingleCommandResult& result,
                                   RedirectInfo* redirect);

    static bool BuildRedisRequest(const std::vector<std::string>& args,
                                  RedisRequest* request);

    static void AppendIntegerReply(butil::IOBuf* buf, int64_t value);
    static void AppendStatusReply(butil::IOBuf* buf, const std::string& value);
    static void AppendErrorReply(butil::IOBuf* buf, const std::string& value);
    static void AppendArrayHeader(butil::IOBuf* buf, size_t size);

    static bool ParseRedisNodeAddress(const std::string& token,
                                      std::string* endpoint);
    static bool ParseInt(const std::string& s, int64_t* out);

    static bool IsNoKeyCommand(const std::string& cmd);

private:
    RedisClusterChannelOptions _options;

    mutable butil::Mutex _mutex;
    std::vector<std::string> _slot_to_endpoint;
    std::vector<std::string> _seed_endpoints;
    std::unordered_map<std::string, std::unique_ptr<Channel> > _channels;

    std::atomic<bool> _stop_refresh;
    bool _refresh_started;
    bthread_t _refresh_tid;
};

}  // namespace brpc

#endif  // BRPC_REDIS_CLUSTER_H
