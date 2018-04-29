// Copyright (c) 2014 baidu-rpc authors.
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

// Author: Li,Zhaogeng (lizhaogeng01@baidu.com)
//         Ding,Jie (dingjie01@baidu.com)

#ifdef BRPC_RDMA

#ifndef BRPC_RDMA_RDMA_CONNECT_SERVICE_H
#define BRPC_RDMA_RDMA_CONNECT_SERVICE_H

#include "brpc/rdma/rdma.pb.h"

namespace brpc {
namespace rdma {

class RdmaConnServiceImpl : public RdmaConnService {
public:
    RdmaConnServiceImpl() {}
    ~RdmaConnServiceImpl() {}
    void RdmaConnect(google::protobuf::RpcController *cntl_base,
                     const RdmaConnRequest *request,
                     RdmaConnResponse *response,
                     google::protobuf::Closure *done);
};

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_RDMA_RDMA_CONNECT_SERVICE_H

#endif

