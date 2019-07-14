// Copyright (c) 2015 baidu-rpc authors.
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

// Authors: Li Zhaogeng (lizhaogeng01@baidu.com)

#ifndef BRPC_RDMA_FALLBACK_CHANNEL_H
#define BRPC_RDMA_FALLBACK_CHANNEL_H

#include "butil/macros.h"
#include "brpc/channel.h"
#include "brpc/rdma_health.pb.h"

namespace brpc {
namespace rdma {

// Since many users hope RPC should fall back to TCP smoothly when RDMA
// cannot be used, RdmaFallbackChannel is provided. It is a combo
// channel including two sub-channels: RDMA channel and TCP channel.
// RDMA channel is always first tried. If RDMA channel cannot get RPC
// response succesfully, TCP channel will be used. In such cases, a
// health detect built-in RPC is sent by RDMA channel periodically.
// Once it detects that RDMA channel is available, all subsequent RPC
// request should use RDMA channel rather than TCP channel.
//
// IMPORTANT:
// In order to avoid performance degradation as far as possible, we do
// not create new Controller for the sub-channel call. This results in
// unfixed call_id() for Controller provided by users. The call_id() of
// the Controller after the rpc call and before the rpc call done is
// undefined! If you want to use call_id() by yourself, please get it
// before the rpc call.
class RdmaFallbackChannel : public ChannelBase {
public:
    RdmaFallbackChannel();
    ~RdmaFallbackChannel();

    static void GlobalStart();
    static void GlobalStop();
    static void RdmaUnavailable(butil::EndPoint remote_side);

    // Provide same Init functions with 'Channel'
    int Init(butil::EndPoint server_addr_and_port, const ChannelOptions* options);
    int Init(const char* server_addr_and_port, const ChannelOptions* options);
    int Init(const char* server_addr, int port, const ChannelOptions* options);
    // TODO (lizhaogeng01@baidu.com) 
    // Try to find a way to support initialization with load balance

    // Send request by one of two sub-channels
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done);

    // @override
    void Describe(std::ostream& os, const DescribeOptions& options) const;

private:
    DISALLOW_COPY_AND_ASSIGN(RdmaFallbackChannel);

    static void* RdmaHealthCheckThread(void* arg);
    static int CheckHealth(Channel* chan);

    // @override
    int CheckHealth();

    int InitInternal(const ChannelOptions* options, int type,
                     butil::EndPoint* server_ep,
                     const char* server_addr_and_port,
                     const char* server_addr, int port);

    Channel _rdma_chan;
    Channel _tcp_chan;
    ChannelOptions _options;
    bool _initialized;
};

class RdmaHealthServiceImpl : public RdmaHealthService {
public:
    RdmaHealthServiceImpl() { }
    ~RdmaHealthServiceImpl() { }

    void Check(google::protobuf::RpcController* cntl_base,
               const RdmaHealthRequest* request,
               RdmaHealthResponse* response,
               google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
    }
private:
    DISALLOW_COPY_AND_ASSIGN(RdmaHealthServiceImpl);
};

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_RDMA_FALLBACK_CHANNEL_H
