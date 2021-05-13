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

// Author: Li Zhaogeng (lizhaogeng01@baidu.com)

#ifndef BRPC_RDMA_COMMUNICATION_MANAGER_H
#define BRPC_RDMA_COMMUNICATION_MANAGER_H

#include "butil/endpoint.h"
#include "butil/macros.h"

namespace brpc {
namespace rdma {

// Event type of rdmacm
enum RdmaCMEvent {
    RDMACM_EVENT_NONE,
    RDMACM_EVENT_ADDR_RESOLVED,     // for client side
    RDMACM_EVENT_ROUTE_RESOLVED,    // for client side
    RDMACM_EVENT_ESTABLISHED,
    RDMACM_EVENT_ACCEPT,            // for server side
    RDMACM_EVENT_DISCONNECT,
    RDMACM_EVENT_OTHER,
    RDMACM_EVENT_ERROR
};

// This class is a wrapper of rdmacm.
class RdmaCommunicationManager {
public:
    ~RdmaCommunicationManager();

    // Factory function to create a new rdmacm
    // Return:
    //     RdmaCommunicationManager created if success
    //     NULL if failed and errno set
    static RdmaCommunicationManager* Create();

    // Create a new rdmacm listening on the listen_ep
    // Arguments:
    //     listen_ep: the endpoint to listen on
    // Return:
    //     RdmaCommunicationManager created if success
    //     NULL if failed and errno set
    static RdmaCommunicationManager* Listen(butil::EndPoint& listen_ep);

    // Get a new request on the rdmacm
    // When using rdmacm, we must get the request before accepting it
    // Arguments(Output):
    //     data: The data included in the request
    //     len: The length of data
    // Return:
    //     RdmaCommunicationManager got if success
    //     NULL if failed and errno set
    RdmaCommunicationManager* GetRequest(char** data, size_t* len);

    // Accept a new request
    // Arguments:
    //     data: The data should be included in the response
    //     len: The length of data
    // Return:
    //     0:   success
    //     -1:  failed, errno set
    int Accept(char* data, size_t len);

    // Start RDMA connect
    // Arguments:
    //     data: The data should be included in the request
    //     len: The length of data
    // Return:
    //     0:   success
    //     -1:  failed, errno set
    int Connect(char* data, size_t len);

    // Bind rdma_cm_id to local address
    // the rdma_cm_id will be bound to a specified local RDMA device
    // For client side if we only have one RDMA device or we just want to use the first RDMA device.
    // We don't not need call this method.
    // But if we have multi RDMA device, and want to use special device. We need to call this method 
    // to bind the local address, this funciton will use the --rdma_device GFLAGS specified device ip
    // address to bind. So you don't need to specified ip address param.
    // Return:
    //     0:   success
    //     -1:  failed, errno set
    int BindLocalAddress();

    // Resolve address
    // Arguments:
    //     remote_ep: the remote endpoint to connect
    // Return:
    //     0:   success
    //     -1:  failed, errno set
    int ResolveAddr(butil::EndPoint& remote_ep);

    // Resolve route
    // Return:
    //     0:   success
    //     -1:  failed, errno set
    int ResolveRoute();

    // Get and ack rdmacm event
    // Return:
    //     the got rdmacm event
    RdmaCMEvent GetCMEvent();

    // Create QP
    // Arguments:
    //     sq_size: the capacity of Send Queue
    //     rq_size: the capacity of Recv Queue
    //     cq:      associated CQ
    //     id:      SocketId of associated Socket
    // Return:
    //     QP created:   success
    //     NULL:  failed, errno set
    void* CreateQP(uint32_t sq_size, uint32_t rq_size, void* cq, uint64_t id);

    // Get rdmacm fd
    int GetFD() const;

    // Get Connect Data
    char* GetConnData() const;

    // Get the length of Connect Data
    size_t GetConnDataLen() const;

private:
    DISALLOW_COPY_AND_ASSIGN(RdmaCommunicationManager);

    // Can only construct RdmaCommunicationManager by Create() from outside
    RdmaCommunicationManager(void* cm_id = NULL);

    // Destroy QP
    void ReleaseQP();

    // rdma_cm_id*, use void* to avoid including rdma_cma.h
    void* _cm_id;
};

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_RDMA_COMMUNICATION_MANAGER_H
