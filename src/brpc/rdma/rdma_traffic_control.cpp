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

#include "butil/logging.h"
#include "brpc/rdma/rdma_traffic_control.h"

namespace brpc {
namespace rdma {

extern bool g_rdma_traffic_enabled;

void RdmaTrafficControlServiceImpl::TurnOn(
        google::protobuf::RpcController* cntl_base,
        const RdmaTrafficControlRequest* request,
        RdmaTrafficControlResponse* response,
        google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    g_rdma_traffic_enabled = true;
    LOG(INFO) << "RDMA traffic enabled";
}

void RdmaTrafficControlServiceImpl::TurnOff(
        google::protobuf::RpcController* cntl_base,
        const RdmaTrafficControlRequest* request,
        RdmaTrafficControlResponse* response,
        google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    g_rdma_traffic_enabled = false;
    LOG(INFO) << "RDMA traffic disabled";
}

}  // namespace rdma
}  // namespace brpc
