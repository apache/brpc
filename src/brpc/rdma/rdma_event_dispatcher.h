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

#ifndef BRPC_RDMA_EVENT_DISPATCHER_H
#define BRPC_RDMA_EVENT_DISPATCHER_H

#include <butil/macros.h>                // DISALLOW_AND_ASSIGN
#include <bthread/bthread.h>
#include "brpc/event_dispatcher.h"

namespace brpc {
namespace rdma {

class RdmaEventDispatcher : public brpc::EventDispatcher {
public:
    RdmaEventDispatcher();
    ~RdmaEventDispatcher();

    enum EventType {
        RDMA_CM,
        RDMA_VERBS,
        RDMA_ACCEPTOR
    };

    // add the fd into the event dispatcher
    // different types have different callbacks
    int AddConsumer(uint64_t data, EventType type, int fd);

    // override
    virtual int Start(const bthread_attr_t* consumer_thread_attr);

private:
    DISALLOW_COPY_AND_ASSIGN(RdmaEventDispatcher);

    virtual void Run();
};

// get global rdma event dispatcher
RdmaEventDispatcher* GetGlobalRdmaEventDispatcher();

}  // namespace rdma
}  // namespace brpc

#endif  // BRPC_RDMA_EVENT_DISPATCHER_H

#endif

