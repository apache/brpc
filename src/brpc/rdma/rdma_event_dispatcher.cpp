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

#include <sys/epoll.h>                               // epoll_create
#include <butil/logging.h>
#include <bthread/bthread.h>
#include <gflags/gflags.h>                           // DEFINE_bool
#include "brpc/rdma/rdma_acceptor.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_event_dispatcher.h"

namespace brpc {

DECLARE_bool(usercode_in_pthread);

namespace rdma {

struct EventTuple {
    uint64_t data;
    RdmaEventDispatcher::EventType type;
    EventTuple(uint64_t d, RdmaEventDispatcher::EventType t) 
        : data(d), type(t) { }
};

RdmaEventDispatcher::RdmaEventDispatcher()
    : EventDispatcher()
{
}

RdmaEventDispatcher::~RdmaEventDispatcher() {
}

int RdmaEventDispatcher::AddConsumer(uint64_t id, EventType type, int fd) {
    if (_epfd < 0) {
        errno = EINVAL;
        return -1;
    }
    epoll_event evt;
    evt.events = EPOLLIN | EPOLLET;
    EventTuple* t = new (std::nothrow) EventTuple(id, type);
    if (!t) {
        return -1;
    }
    evt.data.ptr = (void*)t;
    return epoll_ctl(_epfd, EPOLL_CTL_ADD, fd, &evt);
}

int RdmaEventDispatcher::Start(const bthread_attr_t* consumer_thread_attr) {
    if (_epfd < 0) {
        LOG(FATAL) << "epoll was not created";
        return -1;
    }

    if (_tid != 0) {
        LOG(FATAL) << "Already started this dispatcher(" << this 
                   << ") in bthread=" << _tid;
        return -1;
    }

    _consumer_thread_attr = consumer_thread_attr ?
                            *consumer_thread_attr : BTHREAD_ATTR_NORMAL;
    int rc = bthread_start_background(
        &_tid, &_consumer_thread_attr, RdmaEventDispatcher::RunThis, this);
    if (rc) {
        LOG(FATAL) << "Fail to create epoll thread: " << berror(rc);
        return -1;
    }
    return 0;
}

void RdmaEventDispatcher::Run() {
    epoll_event e[32];
    while (!_stop) {
#ifdef BAIDU_RPC_ADDITIONAL_EPOLL
        // Performance downgrades in examples.
        int n = epoll_wait(_epfd, e, ARRAY_SIZE(e), 0);
        if (n == 0) {
            n = epoll_wait(_epfd, e, ARRAY_SIZE(e), -1);
        }
#else
        const int n = epoll_wait(_epfd, e, ARRAY_SIZE(e), -1);
#endif
        if (_stop) {
            break;
        }
        if (n < 0) {
            if (EINTR == errno) {
                continue;
            }
            PLOG(FATAL) << "Fail to epoll_wait epfd=" << _epfd;
            break;
        }
        for (int i = 0; i < n; ++i) {
            if (e[i].events & (EPOLLIN)) {
                EventTuple* t = (EventTuple*)e[i].data.ptr;
                if (t->type == RDMA_VERBS) {
                    RdmaEndpoint::StartVerbsEvent(t->data);
                } else if (t->type == RDMA_ACCEPTOR) {
                    RdmaAcceptor::StartAcceptEvent((RdmaAcceptor*)t->data);
                } else {
                    RdmaEndpoint::StartCmEvent(t->data);
                }
            }
        }
    }
}

static RdmaEventDispatcher* g_edisp = NULL;
static pthread_once_t g_edisp_once = PTHREAD_ONCE_INIT;

static void StopAndJoinGlobalDispatchers() {
    g_edisp->Stop();
    g_edisp->Join();
}

void InitializeGlobalRdmaDispatchers() {
    g_edisp = new (std::nothrow) RdmaEventDispatcher;
    CHECK(g_edisp != NULL);
    bthread_attr_t attr = FLAGS_usercode_in_pthread ?
            BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL;
    CHECK_EQ(0, g_edisp->Start(&attr));
    CHECK_EQ(0, atexit(StopAndJoinGlobalDispatchers));
}

RdmaEventDispatcher* GetGlobalRdmaEventDispatcher() {
    pthread_once(&g_edisp_once, InitializeGlobalRdmaDispatchers);
    return g_edisp;
}

}  // namespace rdma
}  // namespace brpc

#endif

