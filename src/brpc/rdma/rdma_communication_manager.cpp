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

#ifdef BRPC_RDMA
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#endif
#include <arpa/inet.h>
#include <gflags/gflags.h>
#include "butil/fd_utility.h"                     // make_non_blocking
#include "butil/logging.h"
#include "butil/unique_ptr.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/rdma_communication_manager.h"

namespace brpc {
namespace rdma {

#ifdef BRPC_RDMA
extern int (*RdmaCreateId)(rdma_event_channel*, rdma_cm_id**, void*, rdma_port_space);
extern int (*RdmaDestroyId)(rdma_cm_id*);
extern int (*RdmaResolveAddr)(rdma_cm_id*, sockaddr*, sockaddr*, int);
extern int (*RdmaBindAddr)(rdma_cm_id*, sockaddr*);
extern int (*RdmaResolveRoute)(rdma_cm_id*, int);
extern int (*RdmaListen)(rdma_cm_id*, int);
extern int (*RdmaConnect)(rdma_cm_id*, rdma_conn_param*);
extern int (*RdmaGetRequest)(rdma_cm_id*, rdma_cm_id**);
extern int (*RdmaAccept)(rdma_cm_id*, rdma_conn_param*);
extern int (*RdmaGetCmEvent)(rdma_event_channel*, rdma_cm_event**);
extern int (*RdmaAckCmEvent)(rdma_cm_event*);
extern int (*RdmaCreateQp)(rdma_cm_id*, ibv_pd*, ibv_qp_init_attr*);
extern int (*IbvDestroyQp)(ibv_qp*);

DEFINE_int32(rdma_backlog, 1024, "The backlog for rdma connection.");
DEFINE_int32(rdma_conn_timeout_ms, 30000,
            "The timeout (ms) for RDMA connection establishment.");
#endif

static const int FLOW_CONTROL = 1;          // for creating QP
static const int RETRY_COUNT = 1;           // for creating QP
static const int RNR_RETRY_COUNT = 0;       // for creating QP

RdmaCommunicationManager::RdmaCommunicationManager(void* cm_id)
    : _cm_id(cm_id)
{
}

RdmaCommunicationManager::~RdmaCommunicationManager() {
#ifdef BRPC_RDMA
    ReleaseQP();
    if (_cm_id) {
        RdmaDestroyId((rdma_cm_id*)_cm_id);
    }
#endif
}

RdmaCommunicationManager* RdmaCommunicationManager::Create() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    RdmaCommunicationManager* rcm = new (std::nothrow) RdmaCommunicationManager;
    if (!rcm) {
        return NULL;
    }
    if (RdmaCreateId(NULL, (rdma_cm_id**)(&rcm->_cm_id),
                       NULL, RDMA_PS_TCP) < 0) {
        PLOG(WARNING) << "Fail to rdma_create_id";
        delete rcm;
        return NULL;
    }
    rdma_cm_id* cm_id = (rdma_cm_id*)rcm->_cm_id;
    butil::make_close_on_exec(cm_id->channel->fd);
    if (butil::make_non_blocking(cm_id->channel->fd) < 0) {
        PLOG(WARNING) << "Fail to set rdmacm fd to nonblocking";
        delete rcm;
        return NULL;
    }
    return rcm;
#endif
}

RdmaCommunicationManager* RdmaCommunicationManager::Listen(
        butil::EndPoint& listen_ep) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    std::unique_ptr<RdmaCommunicationManager> rcm(Create());
    if (rcm == NULL) {
        return NULL;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_ep.port);
    addr.sin_addr = listen_ep.ip;

    rdma_cm_id* cm_id = (rdma_cm_id*)rcm->_cm_id;
    if (RdmaBindAddr(cm_id, (sockaddr*)&addr) < 0) {
        PLOG(WARNING) << "Fail to rdma_bind_addr";
        return NULL;
    }

    if (RdmaListen(cm_id, FLAGS_rdma_backlog) < 0) {
        PLOG(WARNING) << "Fail to rdma_listen";
        return NULL;
    }

    return rcm.release();
#endif
}

RdmaCommunicationManager* RdmaCommunicationManager::GetRequest(
        char** data, size_t* len) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    CHECK(_cm_id != NULL);

    rdma_cm_id* cm_id = NULL;
    if (RdmaGetRequest((rdma_cm_id*)_cm_id, &cm_id) < 0 || cm_id == NULL) {
        if (errno != EAGAIN) {
            PLOG(WARNING) << "Fail to rdma_get_request";
        }
        return NULL;
    }

    std::unique_ptr<RdmaCommunicationManager> rcm(
            new (std::nothrow) RdmaCommunicationManager(cm_id));
    if (rcm == NULL) {
        PLOG(WARNING) << "Fail to create RdmaCommunicationManager";
        RdmaDestroyId(cm_id);
        return NULL;
    }

    butil::make_close_on_exec(cm_id->channel->fd);
    if (butil::make_non_blocking(cm_id->channel->fd) < 0) {
        PLOG(WARNING) << "Fail to set rdmacm fd to nonblocking";
        return NULL;
    }

    CHECK(cm_id->event != NULL);
    *data = (char*)cm_id->event->param.conn.private_data;
    *len = (size_t)cm_id->event->param.conn.private_data_len;

    return rcm.release();
#endif
}

// Used by UT, do not set it static
#ifdef BRPC_RDMA
void InitRdmaConnParam(rdma_conn_param* p, const char* data, size_t len) {
    CHECK(p != NULL);

    memset(p, 0, sizeof(*p));
    if (data) {
        p->private_data = data;
        p->private_data_len = len;
    }
    p->flow_control = FLOW_CONTROL;
    p->retry_count = RETRY_COUNT;
    p->rnr_retry_count = RNR_RETRY_COUNT;
}
#endif

int RdmaCommunicationManager::Accept(char* data, size_t len) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    CHECK(_cm_id != NULL);

    rdma_conn_param param;
    InitRdmaConnParam(&param, data, len);
    return RdmaAccept((rdma_cm_id*)_cm_id, &param);
#endif
}

int RdmaCommunicationManager::Connect(char* data, size_t len) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    CHECK(_cm_id != NULL);

    rdma_conn_param param;
    InitRdmaConnParam(&param, data, len);
    return RdmaConnect((rdma_cm_id*)_cm_id, &param);
#endif
}

int RdmaCommunicationManager::BindLocalAddress() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    sockaddr_in client_sockaddr;
    bzero(&client_sockaddr, sizeof(client_sockaddr));
    client_sockaddr.sin_family = AF_INET;
    client_sockaddr.sin_port = htons(INADDR_ANY);
    client_sockaddr.sin_addr = GetRdmaIP();
    int ret = RdmaBindAddr((rdma_cm_id*)_cm_id, (sockaddr *)&client_sockaddr);
    if (ret != 0) {
        PLOG(WARNING) << "Failed to bind local address";
        return -1;
    }
    return 0;
#endif
}

int RdmaCommunicationManager::ResolveAddr(butil::EndPoint& remote_ep) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    CHECK(_cm_id != NULL);
    rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;

    sockaddr_in* addr = &cm_id->route.addr.dst_sin;
    addr->sin_family = AF_INET;
    addr->sin_port = htons(remote_ep.port);
    if (IsLocalIP(remote_ep.ip)) {
        addr->sin_addr = GetRdmaIP();
    } else {
        addr->sin_addr = remote_ep.ip;
    }
    cm_id->route.addr.src_addr.sa_family = addr->sin_family;

    return RdmaResolveAddr(cm_id, NULL, (sockaddr*)addr,
                           FLAGS_rdma_conn_timeout_ms / 2);
#endif
}

int RdmaCommunicationManager::ResolveRoute() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    CHECK(_cm_id != NULL);

    return RdmaResolveRoute((rdma_cm_id*)_cm_id,
                            FLAGS_rdma_conn_timeout_ms / 2);
#endif
}

RdmaCMEvent RdmaCommunicationManager::GetCMEvent() {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return RDMACM_EVENT_ERROR;
#else
    CHECK(_cm_id != NULL);
    rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;

    if (cm_id->event && RdmaAckCmEvent(cm_id->event) < 0) {
        PLOG(WARNING) << "Fail to rdma_ack_cm_event";
        return RDMACM_EVENT_ERROR;
    }
    cm_id->event = NULL;

    if (RdmaGetCmEvent(cm_id->channel, &cm_id->event) < 0) {
        if (errno != EAGAIN) {
            PLOG(WARNING) << "Fail to rdma_get_cm_event";
            return RDMACM_EVENT_ERROR;
        }
        return RDMACM_EVENT_NONE;
    }

    switch (cm_id->event->event) {
    case RDMA_CM_EVENT_ADDR_RESOLVED: {
        return RDMACM_EVENT_ADDR_RESOLVED;
    }
    case RDMA_CM_EVENT_ROUTE_RESOLVED: {
        return RDMACM_EVENT_ROUTE_RESOLVED;
    }
    case RDMA_CM_EVENT_ESTABLISHED: {
        return RDMACM_EVENT_ESTABLISHED;
    }
    case RDMA_CM_EVENT_DISCONNECTED: {
        return RDMACM_EVENT_DISCONNECT;
    }
    case RDMA_CM_EVENT_DEVICE_REMOVAL: {
        GlobalDisableRdma();
        break;
    }
    default:
        break;
    }

    return RDMACM_EVENT_OTHER;
#endif
}

void* RdmaCommunicationManager::CreateQP(
        uint32_t sq_size, uint32_t rq_size, void* cq, uint64_t id) {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    CHECK(_cm_id != NULL);
    rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;
    
    // Create QP
    ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.qp_context = (void*)id;
    qp_attr.send_cq = (ibv_cq*)cq;
    qp_attr.recv_cq = (ibv_cq*)cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.sq_sig_all = 0;
    qp_attr.cap.max_send_wr = sq_size;
    qp_attr.cap.max_recv_wr = rq_size;
    qp_attr.cap.max_send_sge = GetRdmaMaxSge();
    qp_attr.cap.max_recv_sge = 1;
    qp_attr.cap.max_inline_data = 64;
    if (RdmaCreateQp(cm_id, (ibv_pd*)GetRdmaProtectionDomain(), &qp_attr) < 0) {
        PLOG(WARNING) << "Fail to rdma_create_qp";
        return NULL;
    }

    return cm_id->qp;
#endif
}

void RdmaCommunicationManager::ReleaseQP() {
#ifdef BRPC_RDMA
    if (_cm_id) {
        rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;

        if (cm_id->qp) {
            // Do not use rdma_destroy_qp, which will release CQ as well
            if (IsRdmaAvailable()) {
                if (IbvDestroyQp(cm_id->qp) < 0) {
                    // Maybe the rdma_cm has been disconnected by the peer
                    PLOG(WARNING) << "Fail to destroy rdma qp";
                }
            }
            cm_id->qp = NULL;
        }
    }
#endif
}

int RdmaCommunicationManager::GetFD() const {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return -1;
#else
    if (!_cm_id) {
        return -1;
    }
    return ((rdma_cm_id*)_cm_id)->channel->fd;
#endif
}

char* RdmaCommunicationManager::GetConnData() const {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return NULL;
#else
    CHECK(_cm_id != NULL);

    rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;
    if (!cm_id->event) {
        return NULL;
    }
    return (char*)cm_id->event->param.conn.private_data;
#endif
}

size_t RdmaCommunicationManager::GetConnDataLen() const {
#ifndef BRPC_RDMA
    CHECK(false) << "This should not happen";
    return 0;
#else
    CHECK(_cm_id != NULL);

    rdma_cm_id* cm_id = (rdma_cm_id*)_cm_id;
    if (!cm_id->event) {
        return 0;
    }
    return cm_id->event->param.conn.private_data_len;
#endif
}

}  // namespace rdma
}  // namespace brpc
