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

#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <butil/logging.h>                  // CHECK, LOG
#include <butil/object_pool.h>              // get_object
#include <butil/raw_pack.h>
#include <butil/strings/stringprintf.h>
#include <bthread/unstable.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/descriptor.h>
#include "brpc/authenticator.h"
#include "brpc/controller.h"
#include "brpc/input_messenger.h"
#include "brpc/socket.h"
#include "brpc/policy/rdma_message.h"
#include "brpc/policy/rdma_meta.pb.h"
#include "brpc/policy/rdma_protocol.h"
#include "brpc/rdma/rdma_acceptor.h"
#include "brpc/rdma/rdma_event_dispatcher.h"
#include "brpc/rdma/rdma_global.h"
#include "brpc/rdma/rdma_iobuf.h"
#include "brpc/rdma/rdma_endpoint.h"

namespace brpc {

DECLARE_bool(usercode_in_pthread);

namespace policy {
// defined in rdma_protocol.cpp
bool SerializeRpcHeaderAndMeta(butil::IOBuf *out, const RdmaRpcMeta& meta,
                               int payload_size);
}

namespace rdma {

static const uint32_t MAX_CQ_EVENTS = 1024;
static const int MAX_CQE_ONCE = 32;
static const int FLOW_CONTROL = 1;
static const int RETRY_COUNT = 1;
static const int RNR_RETRY_COUNT = 1;
static const int RESPONDER_RESOURCES = 1;
static const int INITIATOR_DEPTH = 1;
static const int TIMEOUT = 1;
static const int MAX_INLINE_SIZE = 64;
static const int IOBUF_POOL_CAP = 128;
static void* WR_ERROR = (void*)(intptr_t)-2;

// Defferent devices may has different following limits.
// Please check if your device support the value.
static const int MAX_SGE = 6;  // Recommandation: 6-16
static const int QP_SIZE = 1024;
static const int CQ_SIZE = QP_SIZE << 1;

// Note:
// The following two const should be changed together.
// SEQ_CELL_SHIFT = RBUF_SIZE >> 16;
// The only reason we set SEQ_CELL_SHIFT directly here is to avoid
// compiler complaining about the right shift.
static const size_t RBUF_SIZE = 8388608;  // 8MB
static const size_t SEQ_CELL_SHIFT = 7;

DEFINE_string(rdma_cluster, "0.0.0.0/0", "Destinations which use rdma");

// create IMM carried in RDMA_WRITE_WITH_IMM: seq (16-bit) & ack (16-bit)
static inline uint32_t MakeImm(uint32_t seq, uint32_t ack) {
    seq >>= SEQ_CELL_SHIFT;
    seq &= 0xffff;
    ack >>= SEQ_CELL_SHIFT;
    ack &= 0xffff;
    return htonl((seq << 16) + ack);
}

RdmaEndpoint::RdmaEndpoint(Socket* s)
    : _socket(s)
    , _iobuf_pool(IOBUF_POOL_CAP)
    , _write_butex(NULL)
{
}

RdmaEndpoint::~RdmaEndpoint() {
    CleanUp();
}

void RdmaEndpoint::Init() {
    _iobuf_index = 0;
    _iobuf_idle_count.store(IOBUF_POOL_CAP, butil::memory_order_relaxed);

    _cm_id = NULL;
    _status = UNINITIALIZED;

    _write_head.store(NULL, butil::memory_order_relaxed);
    _write_butex = bthread::butex_create_checked<butil::atomic<int> >();

    _seq = 0;
    _received_ack.store(RBUF_SIZE - 1, butil::memory_order_relaxed);
    _reply_ack.store(RBUF_SIZE - 1, butil::memory_order_relaxed);
    _new_local_rq_wr.store(0, butil::memory_order_relaxed);
    _remote_rq_wr.store(QP_SIZE, butil::memory_order_relaxed);
    _new_local_rq_wr_tmp = 0;
    _remote_rq_wr_tmp = 0;
    _unack_len = 0;

    _cq_events = 0;
    _npolls.store(0, butil::memory_order_relaxed);

    _local_rbuf = NULL;
    _local_rbuf_size = RBUF_SIZE;
    _local_rbuf_mr = NULL;
    _remote_rbuf = NULL;
    _remote_rbuf_size = 0;
    _remote_rbuf_rkey = 0;

    _rdma_error.store(false, butil::memory_order_relaxed);
}

void RdmaEndpoint::CleanUp() {
    BAIDU_SCOPED_LOCK(_cm_lock);
    _status = UNINITIALIZED;
    _iobuf_pool.clear();
    if (_write_butex) {
        bthread::butex_destroy(_write_butex);
        _write_butex = NULL;
    }
    DeallocateResources();

    _read_buf.clear();
}

// send rpc request for the remote sid by tcp channel
bool RdmaEndpoint::SendRdmaRequest(Socket *s, butil::IOBuf *packet,
                                   RdmaConnRequest *request,
                                   bthread_id_t cid) {
    butil::IOBuf request_buf;
    butil::IOBufAsZeroCopyOutputStream wrapper_request(&request_buf);
    if (!request->SerializeToZeroCopyStream(&wrapper_request)) {
        LOG(ERROR) << "Fail to serialize RdmaConnect request";
        return false;
    }

    brpc::policy::RdmaRpcMeta meta;
    brpc::Authenticator* auth =
            (brpc::Authenticator*)_auth_for_connect;
    if (auth && auth->GenerateCredential(
                meta.mutable_authentication_data()) != 0) {
        LOG(ERROR) << "Fail to generate auth data";
        return false;
    }

    meta.set_correlation_id(cid.value);
    brpc::policy::RdmaRpcRequestMeta *request_meta = meta.mutable_request();
    request_meta->set_service_name("brpc.rdma.RdmaConnService");
    request_meta->set_method_name("RdmaConnect");

    const int request_size = request_buf.length();
    if (!brpc::policy::SerializeRpcHeaderAndMeta(
                packet, meta, request_size)) {
        LOG(ERROR) << "Fail to serialize RdmaConnect request";
        return false;
    }
    packet->append(request_buf);

    Socket::WriteOptions opt;
    opt.id_wait = cid;
    s->Write(packet, &opt);
    CHECK_EQ(0, bthread_id_unlock(cid));
    return true;
}

struct RdmaCluster {
    uint32_t ip;
    uint32_t mask;
};

// parse the cluster specified in FLAGS_rdma_cluster
static struct RdmaCluster ParseRdmaClusterAddr(const std::string& str) {
    bool has_error = false;
    struct RdmaCluster rdma_cluster;
    rdma_cluster.mask = 0xffffffff;
    rdma_cluster.ip = 0;

    butil::StringPiece ip_str(str);
    size_t pos = str.find('/');
    int len = 32;
    uint32_t ip_addr = 0;
    if (pos != std::string::npos) {
        // check rdma cluster mask
        butil::StringPiece mask_str(str.c_str() + pos + 1);
        if (mask_str.length() < 1 || mask_str.length() > 2) {
            has_error = true;
        } else {
            char *end;
            len = strtol(mask_str.data(), &end, 10);
            if (*end || errno == ERANGE || len > 32 || len < 0) {
                has_error = true;
                len = 32;
            }
        }
        ip_str.remove_suffix(mask_str.length() + 1);
    } else {
        has_error = true;
    }

    if (len == 0) {
        rdma_cluster.mask = 0;
    } else {
        rdma_cluster.mask <<= 32 - len;
    }

    if (inet_pton(AF_INET, ip_str.as_string().c_str(), &ip_addr) == 0) {
        has_error = true;
    } else {
        ip_addr = ntohl(ip_addr);
    }
    rdma_cluster.ip = ip_addr & rdma_cluster.mask;
    if (has_error) {
        LOG(WARNING) << "rdma cluster error (" << str
                     << "), the correct configuration should be: ip/mask (0<mask<=32)";
    }
    return rdma_cluster;
}

static inline bool DestinationInRdmaCluster(in_addr_t addr) {
    // Currently we only support ipv4 address
    static struct RdmaCluster s_rdma_cluster = ParseRdmaClusterAddr(FLAGS_rdma_cluster);

    if ((addr & s_rdma_cluster.mask) == s_rdma_cluster.ip) {
        return true;
    }
    return false;
}

// handle the timeout of rpc request for remote sid
static void HandleTimeout(void *arg) {
    bthread_id_t cid = { (uint64_t)arg };
    bthread_id_error(cid, ERPCTIMEDOUT);
}

bool RdmaEndpoint::StartConnect() {
    Socket* s = _socket;
    CHECK(_cm_id == NULL);

    if (!DestinationInRdmaCluster(ntohl(butil::ip2int(s->remote_side().ip)))) {
        LOG(WARNING) << "Destination is not in current rdma cluster";
        errno = ERDMA;
        return false;
    }

    if (!GetGlobalRdmaBuffer()) {
        LOG(WARNING) << "Fail to initialize RDMA environment";
        errno = ERDMA;
        return false;
    }

    // find the peer sid by a specific rpc call
    RdmaConnRequest request;
    RdmaConnResponse response;
    brpc::Controller cntl;
    bthread_id_t cid = cntl.call_id();
    cntl.set_max_retry(0);
    cntl.set_timeout_ms(100);
    cntl._response = &response;
    cntl.set_used_by_rpc();
    if (bthread_id_lock_and_reset_range(cid, NULL, 2)) {
        PLOG(ERROR) << "Fail to call RdmaConnect";
        return false;
    }
    cntl._abstime_us = cntl.timeout_ms() * 1000L + butil::gettimeofday_us();
    if (bthread_timer_add(&cntl._timeout_id,
                          butil::microseconds_to_timespec(cntl._abstime_us),
                          HandleTimeout, (void*)(cid.value))) {
        PLOG(ERROR) << "Fail to add timer for timeout";
        return false;
    }

    butil::IOBuf packet;
    if (!SendRdmaRequest(s, &packet, &request, cntl.current_id())) {
        LOG(WARNING) << "RdmaConnect request failed";
        errno = ERDMA;
        return false;
    }

    // wait for rpc response and parse it
    bthread_id_join(cid);
    if (cntl.ErrorCode() != ERPCTIMEDOUT && cntl._timeout_id != 0) {
        bthread_timer_del(cntl._timeout_id);
        cntl._timeout_id = 0;
    }
    if (cntl.Failed()) {
        LOG(WARNING) << "RdmaConnect failed: " << cntl.ErrorText();
        errno = ERDMA;
        return false;
    }
    _remote_sid = response.socketid();

    // create cm_id
    if (BAIDU_UNLIKELY(rdma_create_id(NULL, &_cm_id, this, RDMA_PS_TCP) < 0)) {
        PLOG(WARNING) << "Fail to rdma_create_id";
        return false;
    }

    // set to nonblocking
    int val = fcntl(_cm_id->channel->fd, F_GETFL);
    if (BAIDU_UNLIKELY(val < 0)) {
        PLOG(WARNING) << "Fail to get fcntl";
        return false;
    }
    val |= O_NONBLOCK;
    if (BAIDU_UNLIKELY(fcntl(_cm_id->channel->fd, F_SETFL, val) < 0)) {
        PLOG(WARNING) << "Fail to set fcntl";
        return false;
    }

    // add fd to rdma event dispatch
    if (GetGlobalRdmaEventDispatcher()->AddConsumer(_socket->_this_id,
                RdmaEventDispatcher::RDMA_CM, _cm_id->channel->fd)) {
        PLOG(WARNING) << "Fail to add rdmacm channel fd "
                      << "to rdma event dispatcher";
        errno = ERDMA;
        return false;
    }

    bool no_ip = false;
    butil::ip_t local_ip;
    butil::str2ip("127.0.0.1", &local_ip);
    if (butil::ip2int(s->_remote_side.ip) == butil::ip2int(local_ip) ||
            butil::ip2int(s->_remote_side.ip) == 0) {
        // user use 127.0.0.1 or 0.0.0.0, we must find real device addr
        no_ip = true;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(s->_remote_side.port);
    if (no_ip) {
        addr.sin_addr = GetGlobalIpAddress();
    } else {
        addr.sin_addr = s->_remote_side.ip;
    }
    _cm_id->route.addr.src_addr.sa_family = addr.sin_family;
    memcpy(&_cm_id->route.addr.dst_addr, &addr, sizeof(addr));

    _status = ADDR_RESOLVING;
    if (rdma_resolve_addr(_cm_id, NULL, &_cm_id->route.addr.dst_addr,
                          TIMEOUT) < 0) {
        if (BAIDU_LIKELY(errno != EAGAIN)) {
            PLOG(WARNING) << "Fail to rdma_resolve_addr";
        }
        return false;
    }
    if (DoConnect(0)) {
        return false;
    }

    return true;
}

void RdmaEndpoint::SetFailed() {
    if (!_rdma_error.exchange(true, butil::memory_order_relaxed)) {
        BAIDU_SCOPED_LOCK(_cm_lock);
        if (_write_butex) {
            _write_butex->fetch_add(1, butil::memory_order_acquire);
            bthread::butex_wake_except(_write_butex, 0);
        }
    }
}

int RdmaEndpoint::DoConnect(int err) {
    BAIDU_SCOPED_LOCK(_cm_lock);

    switch (_status) {
    case ADDR_RESOLVING: {
        if (BAIDU_UNLIKELY(err != 0)) {
            errno = err;
            PLOG(WARNING) << "Fail to rdma_resolve_addr";
            return -1;
        }
        _status = ROUTE_RESOLVING;
        if (BAIDU_UNLIKELY(rdma_resolve_route(_cm_id, TIMEOUT) < 0)) {
            if (BAIDU_LIKELY(errno != EAGAIN)) {
                PLOG(WARNING) << "Fail to rdma_resolve_route";
            }
            return -1;
        }
    }
    case ROUTE_RESOLVING: {
        if (BAIDU_UNLIKELY(err != 0)) {
            errno = err;
            PLOG(WARNING) << "Fail to rdma_resolve_route";
            return -1;
        }
        _status = CONNECTING;

        if (BAIDU_UNLIKELY(AllocateResources())) {
            LOG(WARNING) << "Fail to allocate resources for rdma connect";
            return -1;
        }

        RdmaAcceptor::ConnectData data = { _remote_sid,
                                           (uint64_t)_local_rbuf,
                                           _local_rbuf_size,
                                           _local_rbuf_mr->rkey };
        rdma_conn_param param;
        memset(&param, 0, sizeof param);
        param.responder_resources = RESPONDER_RESOURCES;
        param.private_data = (void *)&data;
        param.private_data_len = sizeof data;
        param.flow_control = FLOW_CONTROL;
        param.retry_count = RETRY_COUNT;
        param.rnr_retry_count = RNR_RETRY_COUNT;
        param.initiator_depth = INITIATOR_DEPTH;
        if (BAIDU_UNLIKELY(rdma_connect(_cm_id, &param) < 0)) {
            if (errno != EAGAIN) {
                PLOG(WARNING) << "Fail to rdma_connect";
            }
            return -1;
        }
    }
    case CONNECTING: {
        if (BAIDU_UNLIKELY(err != 0)) {
            errno = err;
            PLOG(WARNING) << "Fail to rdma_connect";
            return -1;
        }
        RdmaAcceptor::ConnectData* get_data = (RdmaAcceptor::ConnectData*)
                _cm_id->event->param.conn.private_data;
        _remote_rbuf = (uint8_t*)get_data->rbuf_addr;
        _remote_rbuf_size = get_data->rbuf_size;
        _remote_rbuf_rkey = get_data->rbuf_mr_key;
        _status = ESTABLISHED;
        _write_butex->fetch_add(1, butil::memory_order_relaxed);
        bthread::butex_wake_except(_write_butex, 0);
        return 0;
    }
    default:
        LOG(ERROR) << "Invalid status for rdma connect" << _status;
        errno = EINVAL;
    }

    return -1;
}

int RdmaEndpoint::DoAccept(int err) {
    CHECK(_cm_id != NULL);

    if (_status != ACCEPTING) {
        errno = EINVAL;
        return -1;
    }

    if (err != 0) {
        errno = err;
        return -1;
    }

    _status = ESTABLISHED;
    return 0;
}

int RdmaEndpoint::HandleCmEvent() {
    rdma_cm_event_type event_type;
    int event_status = 0;
    {
        // rdma_ack_cm_event may have a contention with ibv_create_cq in
        // AllocateResources, so we use a mutex here.
        BAIDU_SCOPED_LOCK(_cm_lock);

        if (_cm_id == NULL) {
            errno = ENOTCONN;
            return -1;
        }

        if (_cm_id->event && rdma_ack_cm_event(_cm_id->event)) {
            PLOG(WARNING) << "Fail to rdma_ack_cm_event";
            return -1;
        }
        _cm_id->event = NULL;

        if (rdma_get_cm_event(_cm_id->channel, &_cm_id->event)) {
            if (errno != EAGAIN) {
                PLOG(WARNING) << "Fail to rdma_get_cm_event";
            }
            return -1;
        }
        event_type = _cm_id->event->event;
        event_status = _cm_id->event->status;
    }

    switch (event_type) {
    case RDMA_CM_EVENT_ADDR_RESOLVED:
    case RDMA_CM_EVENT_ADDR_ERROR:
    case RDMA_CM_EVENT_ROUTE_RESOLVED:
    case RDMA_CM_EVENT_ROUTE_ERROR:
    case RDMA_CM_EVENT_UNREACHABLE:
    case RDMA_CM_EVENT_CONNECT_RESPONSE: {
        return DoConnect(event_status);
    }
    case RDMA_CM_EVENT_CONNECT_REQUEST: {
        return DoAccept(event_status);
    }
    case RDMA_CM_EVENT_CONNECT_ERROR:
    case RDMA_CM_EVENT_REJECTED:  // Note: server may also receive this event
    case RDMA_CM_EVENT_ESTABLISHED: {
        if (_status == ACCEPTING) {
            return DoAccept(event_status);
        } else if (_status == CONNECTING){
            return DoConnect(event_status);
        }
    }
    case RDMA_CM_EVENT_TIMEWAIT_EXIT:
    case RDMA_CM_EVENT_MULTICAST_JOIN:
    case RDMA_CM_EVENT_MULTICAST_ERROR:
    case RDMA_CM_EVENT_DEVICE_REMOVAL:
    case RDMA_CM_EVENT_ADDR_CHANGE:
    case RDMA_CM_EVENT_DISCONNECTED: {
        errno = EINVAL;
        return -1;
    }
    default:
        LOG(ERROR) << "Not supported rdma_cm event!";
        errno = EPROTO;
        return -1;
    }
}

void RdmaEndpoint::StartCmEvent(uint64_t id)  {
    SocketUniquePtr s;
    if (Socket::Address(id, &s) < 0) {
        return;
    }
    if (s->_rdma_ep->HandleCmEvent() < 0) {
        if (errno != EAGAIN) {
            s->_rdma_ep->SetFailed();
        }
    }
}

bool RdmaEndpoint::PostRecv() {
    ibv_recv_wr wr;
    memset(&wr, 0, sizeof(wr));

    ibv_recv_wr* bad = NULL;
    if (BAIDU_UNLIKELY(ibv_post_recv(_cm_id->qp, &wr, &bad) < 0)) {
        PLOG(ERROR) << "Fail to ibv_post_recv";
        return false;
    }
    return true;
}

void RdmaEndpoint::StartVerbsEvent(uint64_t id) {
    SocketUniquePtr s;
    if (Socket::Address(id, &s) < 0) {
        return;
    }
    if (s->_rdma_ep->_npolls.fetch_add(1, butil::memory_order_relaxed) == 0) {
        bthread_t tid;
        bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
        Socket* m = s.release();
        if (BAIDU_UNLIKELY(bthread_start_urgent(&tid, &attr, PollCQ, m))) {
            LOG(FATAL) << "Fail to start PollCQ";
            m->_rdma_ep->SetFailed();
        }
    }
}

void RdmaEndpoint::GetAndAckCQEvents() {
    void* context = NULL;
    if (BAIDU_UNLIKELY(ibv_get_cq_event(_cm_id->recv_cq_channel,
                                        &_cm_id->recv_cq, &context) < 0)) {
        LOG(ERROR) << "Fail to get CQ event";
        SetFailed();
        return;
    }
    _cq_events++;
    if (_cq_events == MAX_CQ_EVENTS) {
        ibv_ack_cq_events(_cm_id->recv_cq, _cq_events);
        _cq_events = 0;
    }
}

void* RdmaEndpoint::PollCQ(void* arg) {
    SocketUniquePtr s((Socket*)arg);
    RdmaEndpoint* ep = s->_rdma_ep;
    int progress = Socket::PROGRESS_INIT;

    ep->GetAndAckCQEvents();

    bool notified = false;
    ibv_wc wc[MAX_CQE_ONCE];
    brpc::InputMessageBase* last_msg = NULL;
    while (true) {
        // poll cq to get cqe
        int cnt = ibv_poll_cq(ep->_cm_id->recv_cq, MAX_CQE_ONCE, wc);
        if (BAIDU_UNLIKELY(cnt < 0)) {
            LOG(ERROR) << "Fail to poll CQ";
            ep->SetFailed();
            break;
        }
        if (cnt == 0) {
            if (!notified) {
                // Since rdma only provides one shot event, we have to call the
                // notify function every time. Because there is a possibility
                // that the event arrives after the poll but before the notify,
                // we should re-poll the cq once after the notify to check if
                // there is an available cqe.
                if (BAIDU_UNLIKELY(ibv_req_notify_cq(ep->_cm_id->recv_cq, 0))) {
                    LOG(ERROR) << "Fail to notify CQ";
                    ep->SetFailed();
                }
                notified = true;
                continue;
            } else {
                if (!ep->_npolls.compare_exchange_strong(progress, 0,
                            butil::memory_order_release,
                            butil::memory_order_acquire)) {
                    ep->GetAndAckCQEvents();
                    // events come, cannot exit loop now
                    notified = false;
                    continue;
                }
                break;
            }
        }
        if (last_msg) {
            ep->StartProcess(last_msg);
            last_msg = NULL;
        }

        // handle cqe one by one
        for (int i = 0; i < cnt; i++) {
            if (wc[i].status == IBV_WC_WR_FLUSH_ERR) {
                ep->SetFailed();
                break;
            }
            switch (wc[i].opcode) {
            case IBV_WC_RDMA_WRITE: {
                if (wc[i].status != IBV_WC_SUCCESS) {
                    LOG(WARNING) << "Fail to rdma write err=" << wc[i].status;
                    continue;
                }
                if (wc[i].wr_id >= IOBUF_POOL_CAP) {
                    continue;
                }
                // clear the corresponding element of _iobuf_pool
                ep->_iobuf_pool[wc[i].wr_id].clear();
                if (ep->_iobuf_idle_count.fetch_add(1,
                        butil::memory_order_relaxed) == 0) {
                    ep->_write_butex->fetch_add(1, butil::memory_order_relaxed);
                    bthread::butex_wake_except(ep->_write_butex, 0);
                }
                break;
            }
            case IBV_WC_RECV_RDMA_WITH_IMM: {
                if (wc[i].status != IBV_WC_SUCCESS) {
                    LOG(WARNING) << "Fail rdma recv status=" << wc[i].status;
                    continue;
                }
                ep->PostRecv();
                ep->_new_local_rq_wr_tmp++;
                if (ep->_new_local_rq_wr_tmp > (QP_SIZE >> 2)) {
                    ep->_new_local_rq_wr.fetch_add(ep->_new_local_rq_wr_tmp,
                            butil::memory_order_relaxed);
                    ep->_new_local_rq_wr_tmp = 0;
                }

                if ((wc[i].wc_flags & IBV_WC_WITH_IMM) == 0) {
                    LOG(ERROR) << "imm_data invalid";
                    continue;
                }

                uint32_t imm = ntohl(wc[i].imm_data);
                uint32_t ack = (imm & 0xffff) << SEQ_CELL_SHIFT;
                uint32_t len = wc[i].byte_len;
                if (len == 0) {  // only ack
                    ep->_remote_rq_wr_tmp += imm >> 16;
                    if (ep->_remote_rq_wr_tmp > (QP_SIZE >> 2)) {
                        ep->_remote_rq_wr.fetch_add(ep->_remote_rq_wr_tmp,
                                butil::memory_order_relaxed);
                        ep->_remote_rq_wr_tmp = 0;
                    }
                    ep->_received_ack.store(ack, butil::memory_order_relaxed);
                    ep->_write_butex->fetch_add(1, butil::memory_order_relaxed);
                    bthread::butex_wake_except(ep->_write_butex, 0);
                    continue;
                }
                uint32_t seq = (imm >> 16) << SEQ_CELL_SHIFT;
                ep->_unack_len += len;
                if (ep->_unack_len > (ep->_local_rbuf_size >> 1)) {
                    ep->SendAck();
                    ep->_unack_len = 0;
                }

                // copy the data out to _read_buf
                void* buf = ep->_local_rbuf + (seq % ep->_local_rbuf_size);
                if (ep->_read_buf.append(buf, len) < 0) {
                    LOG(WARNING) << "Fail to copy received message";
                }

                // parse
                bool parse_ok = false;
                ParseResult result = brpc::policy::ParseRdmaRpcMessage(
                        &ep->_read_buf, NULL, false, NULL);
                if (!result.is_ok()) {
                    if (result.error() != PARSE_ERROR_NOT_ENOUGH_DATA) {
                        LOG(WARNING) << "Fail to parse rpc message";
                        ep->_read_buf.clear();
                    }
                } else if (result.message() == NULL) {
                    LOG(WARNING) << "Fail to parse rpc message";
                    ep->_read_buf.clear();
                } else {
                    parse_ok = true;
                }

                // update seq and ack
                ep->_received_ack.store(ack, butil::memory_order_relaxed);
                ep->_reply_ack.store(seq + len - 1, butil::memory_order_relaxed);
                ep->_write_butex->fetch_add(1, butil::memory_order_relaxed);
                bthread::butex_wake_except(ep->_write_butex, 0);
                s->AddInputBytes(len);

                if (!parse_ok) {
                    continue;
                }

                s->AddInputMessages(1);

                if (i + 1 < cnt) {
                    ep->StartProcess(result.message());
                } else {
                    last_msg = result.message();
                }
                break;
            }
            default:
                LOG(ERROR) << "Invalid opcode of work completion";
            }
        }

        notified = false;
    }

    // The use of last_msg is to allow the DoProcess handled in the
    // current thread to improve performance.
    if (last_msg) {
        s->ReAddress(&last_msg->_socket);
        DoProcess(last_msg);
    }
    return NULL;
}

void RdmaEndpoint::StartProcess(brpc::InputMessageBase* msg) {
    _socket->ReAddress(&msg->_socket);
    bthread_t tid;
    bthread_attr_t attr = brpc::FLAGS_usercode_in_pthread ?
                          BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL;
    attr.keytable_pool = _socket->_keytable_pool;
    if (BAIDU_UNLIKELY(bthread_start_background(&tid, &attr,
                    DoProcess, msg))) {
        LOG(FATAL) << "Fail to start DoProcess";
        SetFailed();
    }
}

void* RdmaEndpoint::DoProcess(void* arg) {
    DestroyingPtr<InputMessageBase> msg((InputMessageBase*)arg);
    CHECK(msg->_socket != NULL);
    SocketUniquePtr s;
    msg->_socket->ReAddress(&s);

    // update time
    const int64_t received_us = butil::cpuwide_time_us();
    msg->_received_us = received_us;
    const int64_t base_realtime = butil::gettimeofday_us() - received_us;
    msg->_base_real_us = base_realtime;
    s->_last_readtime_us.store(received_us, butil::memory_order_relaxed);

    // call rpc method
    s->PostponeEOF();  // important
    InputMessenger* messenger = static_cast<InputMessenger*>(s->user());
    msg->_process = messenger->_handlers[PROTOCOL_RDMA].process;
    msg->_arg = messenger->_handlers[PROTOCOL_RDMA].arg;
    msg->_process(msg.release());

    return NULL;
}

int RdmaEndpoint::AllocateResources() {
    // create recv_cq
    _cm_id->recv_cq_channel = ibv_create_comp_channel(_cm_id->verbs);
    if (_cm_id->recv_cq_channel == NULL) {
        PLOG(WARNING) << "Fail to ibv_create_comp_channel";
        return -1;
    }
    _cm_id->recv_cq = ibv_create_cq(_cm_id->verbs, CQ_SIZE, _cm_id,
            _cm_id->recv_cq_channel,
            GetGlobalCpuAffinity());
    if (_cm_id->recv_cq == NULL) {
        PLOG(WARNING) << "Fail to ibv_create_cq";
        return -1;
    }
    if (ibv_req_notify_cq(_cm_id->recv_cq, 0)) {
        PLOG(WARNING) << "Fail to ibv_req_notify_cq";
        return -1;
    }
    if (GetGlobalRdmaEventDispatcher()->AddConsumer(_socket->_this_id,
                RdmaEventDispatcher::RDMA_VERBS,
                _cm_id->recv_cq_channel->fd)) {
        LOG(WARNING) << "Fail to add ibverbs fd to epoll";
        return -1;
    }

    // set send_cq to recv_cq
    _cm_id->send_cq_channel = _cm_id->recv_cq_channel;
    _cm_id->send_cq = _cm_id->recv_cq;

    // create qp
    ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof qp_attr);
    qp_attr.qp_context = this; 
    qp_attr.send_cq = _cm_id->send_cq;
    qp_attr.recv_cq = _cm_id->recv_cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.sq_sig_all = 0;
    qp_attr.cap.max_send_wr = QP_SIZE;
    qp_attr.cap.max_recv_wr = QP_SIZE;
    qp_attr.cap.max_send_sge = MAX_SGE;
    qp_attr.cap.max_recv_sge = MAX_SGE;
    qp_attr.cap.max_inline_data = MAX_INLINE_SIZE;
    if (rdma_create_qp(_cm_id, GetGlobalRdmaProtectionDomain(), &qp_attr)) {
        PLOG(WARNING) << "Fail to rdma_create_qp";
        return -1;
    }

    // create buffer
    _seq = 0;
    _received_ack.store(RBUF_SIZE - 1, butil::memory_order_relaxed);
    _reply_ack.store(RBUF_SIZE - 1, butil::memory_order_relaxed);

    _local_rbuf = (uint8_t*)GlobalRdmaAllocate(_local_rbuf_size);
    if (!_local_rbuf) {
        LOG(WARNING) << "Fail to allocate rdma rbuf";
        errno = ERDMA;
        return -1;
    }

    _local_rbuf_mr = GetGlobalRdmaBuffer();
    if (!_local_rbuf_mr) {
        LOG(WARNING) << "Fail to allocate rdma mr";
        errno = ERDMA;
        return -1;
    }

    for (int i = 0; i < QP_SIZE; i++) {
        if (!PostRecv()) {
            return -1;
        }
    }

    return 0;
}

void RdmaEndpoint::DeallocateResources() {
    // deallocate buffer
    if (_local_rbuf) {
        GlobalRdmaDeallocate(_local_rbuf);
        _local_rbuf = NULL;
    }

    if (_cm_id) {
        // deallocate qp
        if (_cm_id->qp && ibv_destroy_qp(_cm_id->qp)) {
            _cm_id->qp = NULL;
        }

        // deallocate cq
        if (_cm_id->recv_cq) {
            ibv_ack_cq_events(_cm_id->recv_cq, _cq_events);
            _cq_events = 0;
            if (ibv_destroy_cq(_cm_id->recv_cq)) {
                _cm_id->recv_cq = NULL;
                _cm_id->send_cq = NULL;
            }
        }
        if (_cm_id->recv_cq_channel &&
                ibv_destroy_comp_channel(_cm_id->recv_cq_channel)) {
            _cm_id->recv_cq_channel = NULL;
            _cm_id->send_cq_channel = NULL;
        }
        rdma_destroy_id(_cm_id);
        _cm_id = NULL;
    }
}

void RdmaEndpoint::SendAck() {
    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.wr_id = IOBUF_POOL_CAP;
    // the first 16 bits are new wr num in local rq, rather than seq
    int new_wr = _new_local_rq_wr.exchange(0, butil::memory_order_relaxed);
    wr.imm_data = MakeImm(new_wr << SEQ_CELL_SHIFT,
        _reply_ack.load(butil::memory_order_relaxed) % _remote_rbuf_size);

    _remote_rq_wr.fetch_sub(1, butil::memory_order_relaxed);
    ibv_send_wr* bad = NULL;
    if (ibv_post_send(_cm_id->qp, &wr, &bad)) {
        _remote_rq_wr.fetch_add(1, butil::memory_order_relaxed);
        PLOG(WARNING) << "Fail to ibv_post_send";
        return;
    }
}

int RdmaEndpoint::DoWrite(void* arg) {
    Socket::WriteRequest* req = (Socket::WriteRequest*)arg;

    if (_iobuf_idle_count.load(butil::memory_order_relaxed) == 0) {
        // all the iobufs in iobuf_pool is being used
        errno = EAGAIN;
        return -1;
    }

    // find the size to send this time
    size_t block_count = req->data.backing_block_num();
    if (block_count > MAX_SGE) {
        block_count = MAX_SGE;
    }

    ibv_sge sglist[block_count];
    RdmaIOBuf& data = (RdmaIOBuf&)req->data;
    int req_size = data.get_block_sglist(sglist, _local_rbuf_mr->lkey);

    // Description of the window algorithm:
    //
    // The send side writes data to the rbuf of the receive side directly.
    // The send side maintains a sliding window similar to tcp. When the
    // send side write some data, the head of the window moves forward.
    // When the send side receives an ack, the tail of the window moves
    // forward.
    //
    // However, it is more complex when we use Cell as the smallest unit
    // instead of byte in tcp. A Cell is (1 << SEQ_CELL_SHIFT) bytes.
    // Every message will consume integer number times Cells, which means
    // there may be some unused bytes between two messages.
    //
    // And we can only use continuous memory in the rbuf of the remote side.
    // That means if the size between the head offset and end of the rbuf
    // is smaller than the message size, we cannot put part of it in this
    // area.

    size_t req_size_in_cell = req_size >> SEQ_CELL_SHIFT;
    if (req_size % (1 << SEQ_CELL_SHIFT) > 0) {
        req_size_in_cell++;
    }

    int32_t offset = -1;
    size_t head_offset = _seq % _remote_rbuf_size;
    size_t tail_offset = _received_ack.load(butil::memory_order_relaxed)
                                % _remote_rbuf_size;

    CHECK(head_offset != tail_offset);
    if (head_offset < tail_offset) {
        // head is before tail, meaning that the head already back from end
        // [start ...+++... head_offset ......... tail_offset ...+++... end]
        if (tail_offset - head_offset > req_size_in_cell << SEQ_CELL_SHIFT) {
            offset = head_offset;
        }
    } else {
        // head is after tail, meaning that the head not yet back from end
        // [start ......... tail_offset ...+++... head_offset ......... end]
        if (_remote_rbuf_size - head_offset > 
                (req_size_in_cell << SEQ_CELL_SHIFT)) {
            offset = head_offset;
        } else if (tail_offset > req_size_in_cell << SEQ_CELL_SHIFT) {
            offset = 0;
        }
    }
    if (offset < 0) {
        SendAck();
        errno = EAGAIN;
        return -1;
    }

    if (_new_local_rq_wr.load(butil::memory_order_relaxed) > QP_SIZE >> 2) {
        SendAck();
    }

    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = _iobuf_index;
    wr.sg_list = sglist;
    data.cutn(&_iobuf_pool[_iobuf_index], req_size);

    wr.num_sge = block_count;
    wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr.wr.rdma.remote_addr = (uint64_t)_remote_rbuf + offset;
    wr.wr.rdma.rkey = _remote_rbuf_rkey;
    wr.send_flags = IBV_SEND_SIGNALED;
    if (req_size < MAX_INLINE_SIZE) {
        wr.send_flags |= IBV_SEND_INLINE;
    }
    if ((uint32_t)offset != head_offset) {
        _seq += _remote_rbuf_size - head_offset;
        if (_seq >= RBUF_SIZE) {
            _seq -= RBUF_SIZE;
        }
    }
    wr.imm_data = MakeImm(_seq % _remote_rbuf_size, 
            _reply_ack.load(butil::memory_order_relaxed) % _remote_rbuf_size);
    _seq += req_size_in_cell << SEQ_CELL_SHIFT;
    if (_seq >= RBUF_SIZE) {
        _seq -= RBUF_SIZE;
    }

    _remote_rq_wr.fetch_sub(1, butil::memory_order_relaxed);
    ibv_send_wr* bad = NULL;
    if (ibv_post_send(_cm_id->qp, &wr, &bad)) {
        PLOG(WARNING) << "Fail to ibv_post_send";
        _iobuf_pool[_iobuf_index].swap(data);
        data.append(_iobuf_pool[_iobuf_index]);
        _iobuf_pool[_iobuf_index].clear();
        errno = ERDMA;
        _remote_rq_wr.fetch_add(1, butil::memory_order_relaxed);
        return -1;
    }

    _iobuf_index++;
    _iobuf_idle_count.fetch_sub(1, butil::memory_order_relaxed);
    if (_iobuf_index >= IOBUF_POOL_CAP) {
        _iobuf_index = 0;
    }
    return req_size;
}

inline void RdmaEndpoint::StartKeepWrite(void* arg) {
    Socket::WriteRequest* req = (Socket::WriteRequest*)arg;
    SocketUniquePtr s;
    req->socket->ReAddress(&s);
    bthread_t tid;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    attr.keytable_pool = s->_keytable_pool;
    s.release();  // equivalent to req->socket carries a refcnt
    if (bthread_start_background(&tid, &attr, KeepWrite, req)) {
        LOG(FATAL) << "Fail to start KeepWrite";
        KeepWrite(req);
    }
}

int RdmaEndpoint::Write(void* arg, void* rdma_arg) {
    if (_rdma_error.load(butil::memory_order_relaxed)) {
        return -1;
    }
    CHECK(arg != NULL);
    Socket::WriteRequest* req = (Socket::WriteRequest*)arg;
    Socket* s = req->socket;
    Socket::WriteRequest* const prev_head = (Socket::WriteRequest*)
        _write_head.exchange(req, butil::memory_order_acquire);
    if (_rdma_error.load(butil::memory_order_relaxed)) {
        return -1;
    }
    if (prev_head != NULL) {
        if (prev_head == WR_ERROR) {
            return -1;
        }
        req->next = prev_head;
        return 0;
    }

    req->next = NULL;

    // if connection is still uninitialized, establish it in another thread
    if (_status == UNINITIALIZED) {
        _auth_for_connect = rdma_arg;
        StartKeepWrite(arg);
        return 0;
    }

    req->Setup(_socket);

    int nw = DoWrite(req);
    if (nw < 0) {
        if (errno == EAGAIN) {
            StartKeepWrite(arg);
            return 0;
        } else {
            LOG(WARNING) << "Fail to write request";
            s->_rdma_ep->ReleaseAllFailedWriteRequests(req);
            return 0;
        }
    } else {
        s->AddOutputBytes(nw);
    }
    if (IsWriteComplete(req, true, NULL)) {
        s->ReturnSuccessfulWriteRequest(req);
        return 0;
    }

    StartKeepWrite(arg);
    return 0;
}

// similar to Socket::IsWriteComplete
bool RdmaEndpoint::IsWriteComplete(void* old_head_void, bool singular_node,
                                   void** new_tail_void) {
    Socket::WriteRequest* old_head = (Socket::WriteRequest*)old_head_void;
    Socket::WriteRequest** new_tail = (Socket::WriteRequest**)new_tail_void;
    CHECK(NULL == old_head->next);
    // Try to set _write_head to NULL to mark that the write is done.
    void* new_head = old_head_void;
    Socket::WriteRequest* desired = NULL;
    bool return_when_no_more = true;
    if (!old_head->data.empty() || !singular_node) {
        desired = old_head;
        // Write is obviously not complete if old_head is not fully written.
        return_when_no_more = false;
    }
    if (_write_head.compare_exchange_strong(
            new_head, (void*)desired, butil::memory_order_acquire)) {
        // No one added new requests.
        if (new_tail) {
            *new_tail = old_head;
        }
        return return_when_no_more;
    }
    CHECK_NE(new_head, (void*)old_head);
    // Above acquire fence pairs release fence of exchange in Write() to make
    // sure that we see all fields of requests set.

    // Someone added new requests.
    // Reverse the list until old_head.
    Socket::WriteRequest* tail = NULL;
    Socket::WriteRequest* p = (Socket::WriteRequest*)new_head;
    do {
        while (p->next == Socket::WriteRequest::UNCONNECTED) {
            sched_yield();  // cannot remove
        }
        Socket::WriteRequest* const saved_next = p->next;
        p->next = tail;
        tail = p;
        p = saved_next;
        CHECK(p != NULL);
    } while (p != old_head);

    // Link old list with new list.
    old_head->next = tail;
    for (Socket::WriteRequest* q = tail; q; q = q->next) {
        q->Setup(_socket);
    }
    if (new_tail) {
        *new_tail = (Socket::WriteRequest*)new_head;
    }
    return false;
}

void* RdmaEndpoint::KeepWrite(void* arg) {
    Socket::WriteRequest* req = static_cast<Socket::WriteRequest*>(arg);
    SocketUniquePtr s(req->socket);

    if (s->_rdma_ep->_status == UNINITIALIZED) {
        req->Setup(s.get());
        const int expected_val = s->_rdma_ep->_write_butex
                                    ->load(butil::memory_order_relaxed);
        if (!s->_rdma_ep->StartConnect()) {
            if (errno == EAGAIN) {
                do {
                    bthread::butex_wait(s->_rdma_ep->_write_butex,
                                        expected_val, NULL);
                    if (s->_rdma_ep->_rdma_error.load(
                                butil::memory_order_relaxed)) {
                        s->_rdma_ep->ReleaseAllFailedWriteRequests(req);
                        return NULL;
                    }
                } while (s->_rdma_ep->_status != ESTABLISHED &&
                         s->_rdma_ep->_status != UNINITIALIZED);
                if (s->_rdma_ep->_status != ESTABLISHED) {
                    LOG(WARNING) << "Fail to establish rdma connection";
                    s->_rdma_ep->ReleaseAllFailedWriteRequests(req);
                    return NULL;
                }
            } else {
                LOG(WARNING) << "Fail to establish rdma connection";
                s->_rdma_ep->ReleaseAllFailedWriteRequests(req);
                return NULL;
            }
        }
    }

    // When error occurs, spin until there's no more requests instead of
    // returning directly otherwise _write_head is permantly non-NULL which
    // makes later Write() abnormal.
    Socket::WriteRequest* cur_tail = NULL;
    do {
        // req was written, skip it.
        if (req->next != NULL && req->data.empty()) {
            Socket::WriteRequest* saved_req = req;
            req = req->next;
            s->ReturnSuccessfulWriteRequest(saved_req);
        }
        const int expected_val = s->_rdma_ep->_write_butex
                                    ->load(butil::memory_order_relaxed);
        int nw = s->_rdma_ep->DoWrite(req);
        if (nw < 0) {
            if (errno == EAGAIN) {
                // wait for completion events
                bthread::butex_wait(s->_rdma_ep->_write_butex,
                                    expected_val, NULL);
                if (s->_rdma_ep->_rdma_error.load(
                            butil::memory_order_relaxed)) {
                    LOG(DEBUG) << "rdma has been set failed";
                    break;
                }
            } else {
                LOG(DEBUG) << "Fail to write request";
                break;
            }
        } else {
            s->AddOutputBytes(nw);
        }
        // Release WriteRequest until non-empty data or last request.
        while (req->next != NULL && req->data.empty()) {
            Socket::WriteRequest* const saved_req = req;
            req = req->next;
            s->ReturnSuccessfulWriteRequest(saved_req);
        }
        if (NULL == cur_tail) {
            for (cur_tail = req;
                 cur_tail->next != NULL;
                 cur_tail = cur_tail->next);
        }
        // Return when there's no more WriteRequests and req is completely
        // written.
        if (s->_rdma_ep->IsWriteComplete(cur_tail, (req == cur_tail),
                                         (void**)&cur_tail)) {
            CHECK_EQ(cur_tail, req);
            s->ReturnSuccessfulWriteRequest(req);
            return NULL;
        }
    } while (1);

    s->_rdma_ep->ReleaseAllFailedWriteRequests(req);
    return NULL;
}

void RdmaEndpoint::ReleaseAllFailedWriteRequests(void* arg) {
    SetFailed();
    Socket::WriteRequest* req = (Socket::WriteRequest*)arg;
    Socket* s = _socket;
    void* wr_error = WR_ERROR;
    Socket::WriteRequest* head = (Socket::WriteRequest*)_write_head
            .exchange(wr_error, butil::memory_order_release);
    if (head == NULL || head == WR_ERROR) {
        return;
    }

    while (head->next == Socket::WriteRequest::UNCONNECTED) {
        sched_yield();
    }

    Socket::WriteRequest* q = req;
    while (q->next != NULL) {
        q = q->next;
    }

    // reverse from head to q
    Socket::WriteRequest* p = head;
    Socket::WriteRequest* tail = NULL;
    while (p != q) {
        Socket::WriteRequest* const saved_next = p->next;
        p->next = tail;
        tail = p;
        p = saved_next;
        CHECK(p != NULL);
    }

    Socket::WriteOptions opt;
    while (req != NULL) {
        Socket::WriteRequest* const saved_next = req->next;
        req->next = Socket::WriteRequest::UNCONNECTED;
        if (!req->reset_pipelined_count_and_user_message()) {
            s->CancelUnwrittenBytes(req->data.size());
        }
        req->data.clear();
        butil::return_object<Socket::WriteRequest>(req);
        s->StartWrite(req, opt);
        req = saved_next;
    }
}

}  // namespace rdma
}  // namespace brpc

#endif

