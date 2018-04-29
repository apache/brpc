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

#include <butil/logging.h>
#include "brpc/closure_guard.h"
#include "brpc/controller.h"
#include "brpc/socket.h"
#include "brpc/rdma/rdma_conn_service_impl.h"

namespace brpc {
namespace rdma {

void RdmaConnServiceImpl::RdmaConnect(google::protobuf::RpcController *cntl_base,
                                      const RdmaConnRequest *request,
                                      RdmaConnResponse *response,
                                      google::protobuf::Closure *done) {
    brpc::ClosureGuard done_guard(done);
    brpc::Controller *cntl = static_cast<brpc::Controller*>(cntl_base);
    SocketUniquePtr sock;
    if (Socket::Address(cntl->_current_call.peer_id, &sock) < 0) {
        PLOG(ERROR) << "SocketId=" << cntl->_current_call.peer_id
                    << " was SetFailed before set up rdma connection.";
        return;
    }
    response->set_socketid(cntl->_current_call.peer_id);
}

}  // namespace rdma
}  // namespace brpc

#endif

