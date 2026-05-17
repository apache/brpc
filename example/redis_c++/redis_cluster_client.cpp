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

// A basic client for native Redis Cluster using brpc::RedisClusterChannel.

#include <gflags/gflags.h>
#include <bthread/countdown_event.h>
#include <butil/logging.h>
#include <brpc/controller.h>
#include <brpc/redis.h>
#include <brpc/redis_cluster.h>

DEFINE_string(seeds, "127.0.0.1:7000,127.0.0.1:7001",
              "Comma-separated redis cluster seed endpoints");
DEFINE_string(key_prefix, "brpc_cluster_demo", "Prefix for demo keys");
DEFINE_int32(timeout_ms, 1000, "RPC timeout in milliseconds");
DEFINE_int32(rpc_max_retry, 1, "Max retries for a single sub RPC");
DEFINE_int32(max_redirect, 5, "Max MOVED/ASK redirect retries");
DEFINE_int32(refresh_interval_s, 30, "Periodic topology refresh interval");
DEFINE_int32(topology_refresh_timeout_ms, 1000,
             "Timeout of CLUSTER SLOTS/NODES request");
DEFINE_bool(disable_periodic_refresh, false, "Disable periodic topology refresh");

namespace {

class Done : public google::protobuf::Closure {
public:
    explicit Done(bthread::CountdownEvent* event) : _event(event) {}
    void Run() override { _event->signal(); }

private:
    bthread::CountdownEvent* _event;
};

int PrintResponse(const brpc::RedisResponse& response) {
    for (int i = 0; i < response.reply_size(); ++i) {
        const brpc::RedisReply& reply = response.reply(i);
        if (reply.is_error()) {
            LOG(ERROR) << "reply[" << i << "] error=" << reply.error_message();
            return -1;
        }
        LOG(INFO) << "reply[" << i << "] " << reply;
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    brpc::RedisClusterChannelOptions options;
    options.max_redirect = FLAGS_max_redirect;
    options.refresh_interval_s = FLAGS_refresh_interval_s;
    options.enable_periodic_refresh = !FLAGS_disable_periodic_refresh;
    options.topology_refresh_timeout_ms = FLAGS_topology_refresh_timeout_ms;
    options.channel_options.timeout_ms = FLAGS_timeout_ms;
    options.channel_options.max_retry = FLAGS_rpc_max_retry;

    brpc::RedisClusterChannel channel;
    if (channel.Init(FLAGS_seeds, &options) != 0) {
        LOG(ERROR) << "Fail to init redis cluster channel, seeds=" << FLAGS_seeds;
        return -1;
    }

    const std::string key1 = FLAGS_key_prefix + "_1";
    const std::string key2 = FLAGS_key_prefix + "_2";

    // Sync pipeline.
    brpc::RedisRequest request;
    brpc::RedisResponse response;
    brpc::Controller cntl;
    CHECK(request.AddCommand("set %s v1", key1.c_str()));
    CHECK(request.AddCommand("set %s v2", key2.c_str()));
    CHECK(request.AddCommand("mget %s %s", key1.c_str(), key2.c_str()));
    channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Sync call failed: " << cntl.ErrorText();
        return -1;
    }
    if (PrintResponse(response) != 0) {
        return -1;
    }

    // Async single request.
    brpc::RedisRequest async_request;
    brpc::RedisResponse async_response;
    brpc::Controller async_cntl;
    CHECK(async_request.AddCommand("get %s", key1.c_str()));

    bthread::CountdownEvent event(1);
    Done done(&event);
    channel.CallMethod(NULL, &async_cntl, &async_request, &async_response, &done);
    event.wait();
    if (async_cntl.Failed()) {
        LOG(ERROR) << "Async call failed: " << async_cntl.ErrorText();
        return -1;
    }
    if (PrintResponse(async_response) != 0) {
        return -1;
    }

    LOG(INFO) << "Redis cluster demo finished";
    return 0;
}
