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

#ifndef BRPC_RDMA_TRAFFIC_CONTROL_H
#define BRPC_RDMA_TRAFFIC_CONTROL_H

#include "butil/macros.h"
#include "brpc/closure_guard.h"
#include "brpc/rdma_traffic_control.pb.h"
#include "brpc/socket_id.h"

namespace brpc {
namespace rdma {

class RdmaTrafficControlServiceImpl : public RdmaTrafficControlService {
public:
    RdmaTrafficControlServiceImpl() { }
    ~RdmaTrafficControlServiceImpl() { }

    void TurnOn(google::protobuf::RpcController* cntl_base,
            const RdmaTrafficControlRequest* request,
            RdmaTrafficControlResponse* response,
            google::protobuf::Closure* done);

    void TurnOff(google::protobuf::RpcController* cntl_base,
            const RdmaTrafficControlRequest* request,
            RdmaTrafficControlResponse* response,
            google::protobuf::Closure* done);

private:
    DISALLOW_COPY_AND_ASSIGN(RdmaTrafficControlServiceImpl);
};

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_RDMA_TRAFFIC_CONTROL_H
