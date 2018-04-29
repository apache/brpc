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

#ifdef BRPC_RDMA

#ifndef BRPC_RDMA_ACCEPTOR_H
#define BRPC_RDMA_ACCEPTOR_H

#include <rdma/rdma_cma.h>
#include <sys/socket.h>
#include <butil/endpoint.h>
#include <butil/atomicops.h>                    // butil::atomic
#include <butil/macros.h>
#include <bthread/bthread.h>

namespace brpc {
namespace rdma {

class RdmaAcceptor {
friend class RdmaEndpoint;
public:
    explicit RdmaAcceptor();
    ~RdmaAcceptor();

    int StartAccept(butil::EndPoint);

    void StopAccept(int);
    
    void Join();

    static int StartAcceptEvent(RdmaAcceptor* acceptor);

private:
    enum Status {
        UNINITIALIZED,
        RUNNING,
        STOPPING
    };

    struct ConnectData {
        uint64_t sid;  // Socket Id
        uint64_t rbuf_addr;
        uint64_t rbuf_size;
        uint32_t rbuf_mr_key;
    };

    void CleanUp();

    static void* GetRequest(void* arg);

    rdma_cm_id* _cm_id;
    Status _status;
    bool _stop;
    butil::atomic<int> _nevents;
    bthread_t _tid;

    DISALLOW_COPY_AND_ASSIGN(RdmaAcceptor);
};

}  // namespace rdma
}  // namespace brpc

#endif // BRPC_RDMA_ACCEPTOR_H

#endif

