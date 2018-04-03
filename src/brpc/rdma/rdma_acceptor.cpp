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

#include <fcntl.h>
#include <netinet/in.h>
#include <butil/logging.h>
#include <butil/strings/stringprintf.h>
#include <bthread/bthread.h>
#include "brpc/socket.h"
#include "brpc/rdma/rdma_event_dispatcher.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_global.h"
#include "brpc/rdma/rdma_acceptor.h"

namespace brpc {
namespace rdma {

static const int BACKLOG = 1024;
static const int RESPONDER_RESOURCES = 1;
static const int INITIATOR_DEPTH = 1;
static const int FLOW_CONTROL = 1;
static const int RETRY_COUNT = 1;
static const int RNR_RETRY_COUNT = 1;

RdmaAcceptor::RdmaAcceptor()
    : _cm_id(NULL)
    , _status(UNINITIALIZED)
    , _stop(false)
    , _nevents(0)
    , _tid(0)
{
}

RdmaAcceptor::~RdmaAcceptor() {
    CleanUp();
}

void RdmaAcceptor::CleanUp() {
    if (_cm_id) {
        if (rdma_destroy_id(_cm_id)) {
            PLOG(WARNING) << "Fail to rdma_destroy_id";
        }
        _cm_id = NULL;
    }
    _status = UNINITIALIZED;
}

int RdmaAcceptor::StartAccept(butil::EndPoint listen_ep) {
    if (_cm_id != NULL || _status != UNINITIALIZED) {
        LOG(WARNING) << "Another rdma accept thread is running";
        return -1;
    }

    // create cm_id
    if (rdma_create_id(NULL, &_cm_id, this, RDMA_PS_TCP)) {
        PLOG(WARNING) << "Fail to rdma_create_id";
        return -1;
    }

    // set to nonblocking
    int val = fcntl(_cm_id->channel->fd, F_GETFL);
    if (val == -1) {
        PLOG(WARNING) << "Fail to get fcntl";
        CleanUp();
        return -1;
    }
    val |= O_NONBLOCK;
    if (fcntl(_cm_id->channel->fd, F_SETFL, val) == -1) {
        PLOG(WARNING) << "Fail to set fcntl";
        CleanUp();
        return -1;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_ep.port);
    addr.sin_addr = listen_ep.ip;
    if (rdma_bind_addr(_cm_id, (sockaddr*)&addr)) {
        PLOG(WARNING) << "Fail to rdma_bind_addr";
        CleanUp();
        return -1;
    }

    // do listen
    if (rdma_listen(_cm_id, BACKLOG)) {
        PLOG(WARNING) << "Fail to rdma_listen";
        CleanUp();
        return -1;
    }

    if (GetGlobalRdmaEventDispatcher()->AddConsumer((uint64_t)this,
                RdmaEventDispatcher::RDMA_ACCEPTOR,
                _cm_id->channel->fd)) {
        PLOG(WARNING) << "Fail to listen on rdma acceptor";
        CleanUp();
        return -1;
    }

    _status = RUNNING;
    return 0;
}

void RdmaAcceptor::StopAccept(int) {
    _stop = true;
    _status = STOPPING;
    Join();
    CleanUp();
}

void* RdmaAcceptor::GetRequest (void* arg) {
    int progress = Socket::PROGRESS_INIT;
    RdmaAcceptor* acceptor = (RdmaAcceptor*)arg;
    while (!acceptor->_stop) {
        rdma_cm_id* conn_cm_id;
        if (BAIDU_UNLIKELY(rdma_get_request(acceptor->_cm_id, &conn_cm_id))) {
            if (errno != EAGAIN) {
                PLOG(WARNING) << "Fail to rdma_get_request";
            }
            if (!acceptor->_nevents.compare_exchange_strong(progress, 0,
                        butil::memory_order_release,
                        butil::memory_order_acquire)) {
                continue;
            }
            break;
        }

        ConnectData* data = (ConnectData*)
                            conn_cm_id->event->param.conn.private_data;
        SocketId sid = data->sid;
        SocketUniquePtr s;
        if (Socket::Address(sid, &s)) {
            LOG(WARNING) << "Invalid socket id for rdma connect sid=" << sid;
            rdma_destroy_id(conn_cm_id);
            continue;
        }

        if (s->_rdma_ep->_cm_id) {
            LOG(WARNING) << "Rdma connection already exists";
            rdma_destroy_id(conn_cm_id);
            continue;
        }

        // set to nonblocking
        int val = fcntl(conn_cm_id->channel->fd, F_GETFL);
        if (BAIDU_UNLIKELY(val < 0)) {
            PLOG(WARNING) << "Fail to get fcntl";
            rdma_destroy_id(conn_cm_id);
            continue;
        }
        val |= O_NONBLOCK;
        if (BAIDU_UNLIKELY(fcntl(conn_cm_id->channel->fd, F_SETFL, val) < 0)) {
            PLOG(WARNING) << "Fail to set fcntl";
            rdma_destroy_id(conn_cm_id);
            continue;
        }

        // add to epoll
        if (BAIDU_UNLIKELY(GetGlobalRdmaEventDispatcher()
                    ->AddConsumer(s->_this_id,
                                  RdmaEventDispatcher::RDMA_CM,
                                  conn_cm_id->channel->fd))) {
            PLOG(WARNING) << "Fail to listen on rdmacm fd";
            rdma_destroy_id(conn_cm_id);
            continue;
        }

        // allocate resources
        s->_rdma_ep->_cm_id = conn_cm_id;
        s->_rdma_ep->_status = RdmaEndpoint::ACCEPTING;
        if (s->_rdma_ep->AllocateResources()) {
            s->_rdma_ep->CleanUp();
            continue;
        }

        s->_rdma_ep->_remote_rbuf = (uint8_t*)data->rbuf_addr;
        s->_rdma_ep->_remote_rbuf_size = data->rbuf_size;
        s->_rdma_ep->_remote_rbuf_rkey = data->rbuf_mr_key;

        ConnectData ret_data = { 0, (uint64_t)s->_rdma_ep->_local_rbuf,
                                 s->_rdma_ep->_local_rbuf_size,
                                 s->_rdma_ep->_local_rbuf_mr->rkey };
        rdma_conn_param param;
        memset(&param, 0, sizeof param);
        param.responder_resources = RESPONDER_RESOURCES;
        param.private_data = (void *)&ret_data;
        param.private_data_len = sizeof ret_data;
        param.flow_control = FLOW_CONTROL;
        param.retry_count = RETRY_COUNT;
        param.rnr_retry_count = RNR_RETRY_COUNT;
        param.initiator_depth = INITIATOR_DEPTH;
        if (rdma_accept(s->_rdma_ep->_cm_id, &param)) {
            if (BAIDU_UNLIKELY(errno != EAGAIN)) {
                PLOG(WARNING) << "Fail to rdma_accept";
                s->_rdma_ep->CleanUp();
            }
            continue;
        }
        s->_rdma_ep->_status = RdmaEndpoint::ESTABLISHED;
    }
    acceptor->_tid = 0;
    return NULL;
}

int RdmaAcceptor::StartAcceptEvent(RdmaAcceptor* acceptor) {
    if (acceptor->_nevents.fetch_add(1, butil::memory_order_acq_rel) == 0) {
        if (bthread_start_urgent(&acceptor->_tid, &BTHREAD_ATTR_NORMAL,
                                 GetRequest, acceptor)) {
            LOG(ERROR) << "Fail to start GetRequest";
        }
    }
    return 0;
}

void RdmaAcceptor::Join() {
    bthread_join(_tid, NULL);
}

}  // namespace rdma
}  // namespace brpc

#endif

