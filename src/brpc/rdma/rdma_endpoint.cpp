// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#if BRPC_WITH_RDMA

#include <gflags/gflags.h>
#include "butil/fd_utility.h"
#include "butil/logging.h"                   // CHECK, LOG
#include "butil/sys_byteorder.h"             // HostToNet,NetToHost
#include "bthread/bthread.h"
#include "brpc/errno.pb.h"
#include "brpc/event_dispatcher.h"
#include "brpc/input_messenger.h"
#include "brpc/socket.h"
#include "brpc/reloadable_flags.h"
#include "brpc/rdma/block_pool.h"
#include "brpc/rdma/rdma_helper.h"
#include "brpc/rdma/rdma_endpoint.h"


namespace brpc {
namespace rdma {

extern ibv_cq* (*IbvCreateCq)(ibv_context*, int, void*, ibv_comp_channel*, int);
extern int (*IbvDestroyCq)(ibv_cq*);
extern ibv_comp_channel* (*IbvCreateCompChannel)(ibv_context*);
extern int (*IbvDestroyCompChannel)(ibv_comp_channel*);
extern int (*IbvGetCqEvent)(ibv_comp_channel*, ibv_cq**, void**);
extern void (*IbvAckCqEvents)(ibv_cq*, unsigned int);
extern ibv_qp* (*IbvCreateQp)(ibv_pd*, ibv_qp_init_attr*);
extern int (*IbvModifyQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask);
extern int (*IbvQueryQp)(ibv_qp*, ibv_qp_attr*, ibv_qp_attr_mask, ibv_qp_init_attr*);
extern int (*IbvDestroyQp)(ibv_qp*);
extern bool g_skip_rdma_init;

DEFINE_int32(rdma_sq_size, 128, "SQ size for RDMA");
DEFINE_int32(rdma_rq_size, 128, "RQ size for RDMA");
DEFINE_bool(rdma_recv_zerocopy, true, "Enable zerocopy for receive side");
DEFINE_int32(rdma_zerocopy_min_size, 512, "The minimal size for receive zerocopy");
DEFINE_string(rdma_recv_block_type, "default", "Default size type for recv WR: "
              "default(8KB - 32B)/large(64KB - 32B)/huge(2MB - 32B)");
DEFINE_int32(rdma_cqe_poll_once, 32, "The maximum of cqe number polled once.");
DEFINE_int32(rdma_prepared_qp_size, 128, "SQ and RQ size for prepared QP.");
DEFINE_int32(rdma_prepared_qp_cnt, 1024, "Initial count of prepared QP.");
DEFINE_bool(rdma_trace_verbose, false, "Print log message verbosely");
BRPC_VALIDATE_GFLAG(rdma_trace_verbose, brpc::PassValidate);

static const size_t IOBUF_BLOCK_HEADER_LEN = 32; // implementation-dependent

// DO NOT change this value unless you know the safe value!!!
// This is the number of reserved WRs in SQ/RQ for pure ACK.
static const size_t RESERVED_WR_NUM = 3;

// magic string RDMA (4B)
// message length (2B)
// hello version (2B)
// impl version (2B): 0 means should use tcp
// block size (4B)
// sq size (2B)
// rq size (2B)
// GID (16B)
// QP number (4B)
static const char* MAGIC_STR = "RDMA";
static const size_t MAGIC_STR_LEN = 4;
static const size_t HELLO_MSG_LEN_MIN = 40;
// static const size_t HELLO_MSG_LEN_MAX = 4096;
static const size_t ACK_MSG_LEN = 4;
static uint16_t g_rdma_hello_msg_len = 40;  // In Byte
static uint16_t g_rdma_hello_version = 2;
static uint16_t g_rdma_impl_version = 1;
static uint32_t g_rdma_recv_block_size = 0;

// static const uint32_t MAX_INLINE_DATA = 64;
static const uint8_t MAX_HOP_LIMIT = 16;
static const uint8_t TIMEOUT = 14;
static const uint8_t RETRY_CNT = 7;
static const uint16_t MIN_QP_SIZE = 16;
static const uint16_t MAX_QP_SIZE = 4096;
static const uint16_t MIN_BLOCK_SIZE = 1024;
static const uint32_t ACK_MSG_RDMA_OK = 0x1;

static butil::Mutex* g_rdma_resource_mutex = NULL;
static RdmaResource* g_rdma_resource_list = NULL;

struct HelloMessage {
    void Serialize(void* data) const;
    void Deserialize(void* data);

    uint16_t msg_len;
    uint16_t hello_ver;
    uint16_t impl_ver;
    uint32_t block_size;
    uint16_t sq_size;
    uint16_t rq_size;
    uint16_t lid;
    ibv_gid gid;
    uint32_t qp_num;
};

void HelloMessage::Serialize(void* data) const {
    uint16_t* current_pos = (uint16_t*)data;
    *(current_pos++) = butil::HostToNet16(msg_len);
    *(current_pos++) = butil::HostToNet16(hello_ver);
    *(current_pos++) = butil::HostToNet16(impl_ver);
    uint32_t* block_size_pos = (uint32_t*)current_pos;
    *block_size_pos = butil::HostToNet32(block_size);
    current_pos += 2; // move forward 4 Bytes
    *(current_pos++) = butil::HostToNet16(sq_size);
    *(current_pos++) = butil::HostToNet16(rq_size);
    *(current_pos++) = butil::HostToNet16(lid);
    memcpy(current_pos, gid.raw, 16);
    uint32_t* qp_num_pos = (uint32_t*)((char*)current_pos + 16);
    *qp_num_pos = butil::HostToNet32(qp_num);
}

void HelloMessage::Deserialize(void* data) {
    uint16_t* current_pos = (uint16_t*)data;
    msg_len = butil::NetToHost16(*current_pos++);
    hello_ver = butil::NetToHost16(*current_pos++);
    impl_ver = butil::NetToHost16(*current_pos++);
    block_size = butil::NetToHost32(*(uint32_t*)current_pos);
    current_pos += 2; // move forward 4 Bytes
    sq_size = butil::NetToHost16(*current_pos++);
    rq_size = butil::NetToHost16(*current_pos++);
    lid = butil::NetToHost16(*current_pos++);
    memcpy(gid.raw, current_pos, 16);
    qp_num = butil::NetToHost32(*(uint32_t*)((char*)current_pos + 16));
}

RdmaResource::RdmaResource() 
    : qp(NULL)
    , cq(NULL)
    , comp_channel(NULL)
    , next(NULL) { }

RdmaResource::~RdmaResource() {
    if (qp) {
        IbvDestroyQp(qp);
        qp = NULL;
    }
    if (cq) {
        IbvDestroyCq(cq);
        cq = NULL;
    }
    if (comp_channel) {
        IbvDestroyCompChannel(comp_channel);
        comp_channel = NULL;
    }
}

RdmaEndpoint::RdmaEndpoint(Socket* s)
    : _socket(s)
    , _state(UNINIT)
    , _resource(NULL)
    , _cq_events(0)
    , _cq_sid(INVALID_SOCKET_ID)
    , _sq_size(FLAGS_rdma_sq_size)
    , _rq_size(FLAGS_rdma_rq_size)
    , _sbuf()
    , _rbuf()
    , _rbuf_data()
    , _remote_recv_block_size(0)
    , _accumulated_ack(0)
    , _unsolicited(0)
    , _unsolicited_bytes(0)
    , _sq_current(0)
    , _sq_unsignaled(0)
    , _sq_sent(0)
    , _rq_received(0)
    , _local_window_capacity(0)
    , _remote_window_capacity(0)
    , _window_size(0)
    , _new_rq_wrs(0)
{
    if (_sq_size < MIN_QP_SIZE) {
        _sq_size = MIN_QP_SIZE;
    }
    if (_sq_size > MAX_QP_SIZE) {
        _sq_size = MAX_QP_SIZE;
    }
    if (_rq_size < MIN_QP_SIZE) {
        _rq_size = MIN_QP_SIZE;
    }
    if (_rq_size > MAX_QP_SIZE) {
        _rq_size = MAX_QP_SIZE;
    }
    _read_butex = bthread::butex_create_checked<butil::atomic<int> >();
}

RdmaEndpoint::~RdmaEndpoint() {
    Reset();
    bthread::butex_destroy(_read_butex);
}

void RdmaEndpoint::Reset() {
    DeallocateResources();

    _cq_events = 0;
    _cq_sid = INVALID_SOCKET_ID;
    _state = UNINIT;
    _sbuf.clear();
    _rbuf.clear();
    _rbuf_data.clear();
    _accumulated_ack = 0;
    _unsolicited = 0;
    _sq_current = 0;
    _sq_unsignaled = 0;
    _local_window_capacity = 0;
    _remote_window_capacity = 0;
    _window_size.store(0, butil::memory_order_relaxed);
    _new_rq_wrs = 0;
    _sq_sent = 0;
    _rq_received = 0;
}

void RdmaConnect::StartConnect(const Socket* socket,
                               void (*done)(int err, void* data),
                               void* data) {
    CHECK(socket->_rdma_ep != NULL);
    SocketUniquePtr s;
    if (Socket::Address(socket->id(), &s) != 0) {
        return;
    }
    if (!IsRdmaAvailable()) {
        socket->_rdma_ep->_state = RdmaEndpoint::FALLBACK_TCP;
        s->_rdma_state = Socket::RDMA_OFF;
        done(0, data);
        return;
    }
    _done = done;
    _data = data;
    bthread_t tid;
    if (bthread_start_background(&tid, &BTHREAD_ATTR_NORMAL,
                RdmaEndpoint::ProcessHandshakeAtClient, socket->_rdma_ep) < 0) {
        LOG(FATAL) << "Fail to start handshake bthread";
    } else {
        s.release();
    }
}

void RdmaConnect::StopConnect(Socket* socket) { }

void RdmaConnect::Run() {
    _done(errno, _data);
}

static void TryReadOnTcpDuringRdmaEst(Socket* s) {
    int progress = Socket::PROGRESS_INIT;
    while (true) {
        uint8_t tmp;
        ssize_t nr = read(s->fd(), &tmp, 1);
        if (nr < 0) {
            if (errno != EAGAIN) {
                const int saved_errno = errno;
                PLOG(WARNING) << "Fail to read from " << s;
                s->SetFailed(saved_errno, "Fail to read from %s: %s",
                        s->description().c_str(), berror(saved_errno));
                return;
            }
            if (!s->MoreReadEvents(&progress)) {
                break;
            }
        } else if (nr == 0) {
            s->SetEOF();
            return;
        } else {
            LOG(WARNING) << "Read unexpected data from " << s;
            s->SetFailed(EPROTO, "Read unexpected data from %s",
                    s->description().c_str());
            return;
        }
    }
}

void RdmaEndpoint::OnNewDataFromTcp(Socket* m) {
    RdmaEndpoint* ep = m->_rdma_ep;
    CHECK(ep != NULL);

    int progress = Socket::PROGRESS_INIT;
    while (true) {
        if (ep->_state == UNINIT) {
            if (!m->CreatedByConnect()) {
                if (!IsRdmaAvailable()) {
                    ep->_state = FALLBACK_TCP;
                    m->_rdma_state = Socket::RDMA_OFF;
                    continue;
                }
                bthread_t tid;
                ep->_state = S_HELLO_WAIT;
                SocketUniquePtr s;
                m->ReAddress(&s);
                if (bthread_start_background(&tid, &BTHREAD_ATTR_NORMAL,
                            ProcessHandshakeAtServer, ep) < 0) {
                    ep->_state = UNINIT;
                    LOG(FATAL) << "Fail to start handshake bthread";
                } else {
                    s.release();
                }
            } else {
                // The connection may be closed or reset before the client
                // starts handshake. This will be handled by client handshake.
                // Ignore the exception here.
            }
        } else if (ep->_state < ESTABLISHED) {  // during handshake
            ep->_read_butex->fetch_add(1, butil::memory_order_release);
            bthread::butex_wake(ep->_read_butex);
        } else if (ep->_state == FALLBACK_TCP){  // handshake finishes
            InputMessenger::OnNewMessages(m);
            return;
        } else if (ep->_state == ESTABLISHED) {
            TryReadOnTcpDuringRdmaEst(ep->_socket);
            return;
        }
        if (!m->MoreReadEvents(&progress)) {
            break;
        }
    }
}

bool HelloNegotiationValid(HelloMessage& msg) {
    if (msg.hello_ver == g_rdma_hello_version &&
        msg.impl_ver == g_rdma_impl_version &&
        msg.block_size >= MIN_BLOCK_SIZE &&
        msg.sq_size >= MIN_QP_SIZE &&
        msg.rq_size >= MIN_QP_SIZE) {
        // This can be modified for future compatibility
        return true;
    }
    return false;
}

static const int WAIT_TIMEOUT_MS = 50;

int RdmaEndpoint::ReadFromFd(void* data, size_t len) {
    CHECK(data != NULL);
    int nr = 0;
    size_t received = 0;
    do {
        const int expected_val = _read_butex->load(butil::memory_order_acquire);
        const timespec duetime = butil::milliseconds_from_now(WAIT_TIMEOUT_MS);
        nr = read(_socket->fd(), (uint8_t*)data + received, len - received);
        if (nr < 0) {
            if (errno == EAGAIN) {
                if (bthread::butex_wait(_read_butex, expected_val, &duetime) < 0) {
                    if (errno != EWOULDBLOCK && errno != ETIMEDOUT) {
                        return -1;
                    }
                }
            } else {
                return -1;
            }
        } else if (nr == 0) {  // Got EOF
            errno = EEOF;
            return -1;
        } else {
            received += nr;
        }
    } while (received < len);
    return 0;
}

int RdmaEndpoint::WriteToFd(void* data, size_t len) {
    CHECK(data != NULL);
    int nw = 0;
    size_t written = 0;
    do {
        const timespec duetime = butil::milliseconds_from_now(WAIT_TIMEOUT_MS);
        nw = write(_socket->fd(), (uint8_t*)data + written, len - written);
        if (nw < 0) {
            if (errno == EAGAIN) {
                if (_socket->WaitEpollOut(_socket->fd(), true, &duetime) < 0) {
                    if (errno != ETIMEDOUT) {
                        return -1;
                    }
                }
            } else {
                return -1;
            }
        } else {
            written += nw;
        }
    } while (written < len);
    return 0;
}

inline void RdmaEndpoint::TryReadOnTcp() {
    if (_socket->_nevent.fetch_add(1, butil::memory_order_acq_rel) == 0) {
        if (_state == FALLBACK_TCP) {
            InputMessenger::OnNewMessages(_socket);
        } else if (_state == ESTABLISHED) {
            TryReadOnTcpDuringRdmaEst(_socket);
        }
    }
}

void* RdmaEndpoint::ProcessHandshakeAtClient(void* arg) {
    RdmaEndpoint* ep = static_cast<RdmaEndpoint*>(arg);
    SocketUniquePtr s(ep->_socket);
    RdmaConnect::RunGuard rg((RdmaConnect*)s->_app_connect.get());

    LOG_IF(INFO, FLAGS_rdma_trace_verbose) 
        << "Start handshake on " << s->_local_side;

    uint8_t data[g_rdma_hello_msg_len];

    // First initialize CQ and QP resources
    ep->_state = C_ALLOC_QPCQ;
    if (ep->AllocateResources() < 0) {
        LOG(WARNING) << "Fallback to tcp:" << s->description();
        s->_rdma_state = Socket::RDMA_OFF;
        ep->_state = FALLBACK_TCP;
        return NULL;
    }

    // Send hello message to server
    ep->_state = C_HELLO_SEND;
    HelloMessage local_msg;
    local_msg.msg_len = g_rdma_hello_msg_len;
    local_msg.hello_ver = g_rdma_hello_version;
    local_msg.impl_ver = g_rdma_impl_version;
    local_msg.block_size = g_rdma_recv_block_size;
    local_msg.sq_size = ep->_sq_size;
    local_msg.rq_size = ep->_rq_size;
    local_msg.lid = GetRdmaLid();
    local_msg.gid = GetRdmaGid();
    if (BAIDU_LIKELY(ep->_resource)) {
        local_msg.qp_num = ep->_resource->qp->qp_num;
    } else {
        // Only happens in UT
        local_msg.qp_num = 0;
    }
    memcpy(data, MAGIC_STR, 4);
    local_msg.Serialize((char*)data + 4);
    if (ep->WriteToFd(data, g_rdma_hello_msg_len) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to send hello message to server:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    // Check magic str
    ep->_state = C_HELLO_WAIT;
    if (ep->ReadFromFd(data, MAGIC_STR_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to get hello message from server:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }
    if (memcmp(data, MAGIC_STR, MAGIC_STR_LEN) != 0) {
        LOG(WARNING) << "Read unexpected data during handshake:" << s->description();
        s->SetFailed(EPROTO, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(EPROTO));
        ep->_state = FAILED;
        return NULL;
    }

    // Read hello message from server
    if (ep->ReadFromFd(data, HELLO_MSG_LEN_MIN - MAGIC_STR_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to get Hello Message from server:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }
    HelloMessage remote_msg;
    remote_msg.Deserialize(data);
    if (remote_msg.msg_len < HELLO_MSG_LEN_MIN) {
        LOG(WARNING) << "Fail to parse Hello Message length from server:"
                     << s->description();
        s->SetFailed(EPROTO, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(EPROTO));
        ep->_state = FAILED;
        return NULL;
    }

    if (remote_msg.msg_len > HELLO_MSG_LEN_MIN) {
        // TODO: Read Hello Message customized data
        // Just for future use, should not happen now
    }

    if (!HelloNegotiationValid(remote_msg)) {
        LOG(WARNING) << "Fail to negotiate with server, fallback to tcp:"
                     << s->description();
        s->_rdma_state = Socket::RDMA_OFF;
    } else {
        ep->_remote_recv_block_size = remote_msg.block_size;
        ep->_local_window_capacity = 
            std::min(ep->_sq_size, remote_msg.rq_size) - RESERVED_WR_NUM;
        ep->_remote_window_capacity = 
            std::min(ep->_rq_size, remote_msg.sq_size) - RESERVED_WR_NUM,
        ep->_window_size.store(ep->_local_window_capacity, butil::memory_order_relaxed);

        ep->_state = C_BRINGUP_QP;
        if (ep->BringUpQp(remote_msg.lid, remote_msg.gid, remote_msg.qp_num) < 0) {
            LOG(WARNING) << "Fail to bringup QP, fallback to tcp:" << s->description();
            s->_rdma_state = Socket::RDMA_OFF;
        } else {
            s->_rdma_state = Socket::RDMA_ON;
        }
    }

    // Send ACK message to server
    ep->_state = C_ACK_SEND;
    uint32_t flags = 0;
    if (s->_rdma_state != Socket::RDMA_OFF) {
        flags |= ACK_MSG_RDMA_OK;
    }
    uint32_t* tmp = (uint32_t*)data;  // avoid GCC warning on strict-aliasing
    *tmp = butil::HostToNet32(flags);
    if (ep->WriteToFd(data, ACK_MSG_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to send Ack Message to server:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    if (s->_rdma_state == Socket::RDMA_ON) {
        ep->_state = ESTABLISHED;
        LOG_IF(INFO, FLAGS_rdma_trace_verbose) 
            << "Handshake ends (use rdma) on " << s->description();
    } else {
        ep->_state = FALLBACK_TCP;
        LOG_IF(INFO, FLAGS_rdma_trace_verbose) 
            << "Handshake ends (use tcp) on " << s->description();
    }

    errno = 0;

    return NULL;
}

void* RdmaEndpoint::ProcessHandshakeAtServer(void* arg) {
    RdmaEndpoint* ep = static_cast<RdmaEndpoint*>(arg);
    SocketUniquePtr s(ep->_socket);

    LOG_IF(INFO, FLAGS_rdma_trace_verbose) 
        << "Start handshake on " << s->description();

    uint8_t data[g_rdma_hello_msg_len];

    ep->_state = S_HELLO_WAIT;
    if (ep->ReadFromFd(data, MAGIC_STR_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to read Hello Message from client:" << s->description() << " " << s->_remote_side;
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    if (memcmp(data, MAGIC_STR, MAGIC_STR_LEN) != 0) {
        LOG_IF(INFO, FLAGS_rdma_trace_verbose) << "It seems that the "
            << "client does not use RDMA, fallback to TCP:"
            << s->description();
        // we need to copy data read back to _socket->_read_buf
        s->_read_buf.append(data, MAGIC_STR_LEN);
        ep->_state = FALLBACK_TCP;
        s->_rdma_state = Socket::RDMA_OFF;
        ep->TryReadOnTcp();
        return NULL;
    }

    if (ep->ReadFromFd(data, g_rdma_hello_msg_len - MAGIC_STR_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to read Hello Message from client:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    HelloMessage remote_msg;
    remote_msg.Deserialize(data);
    if (remote_msg.msg_len < HELLO_MSG_LEN_MIN) {
        LOG(WARNING) << "Fail to parse Hello Message length from client:"
                     << s->description();
        s->SetFailed(EPROTO, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(EPROTO));
        ep->_state = FAILED;
        return NULL;
    }
    if (remote_msg.msg_len > HELLO_MSG_LEN_MIN) {
        // TODO: Read Hello Message customized header
        // Just for future use, should not happen now
    }

    if (!HelloNegotiationValid(remote_msg)) {
        LOG(WARNING) << "Fail to negotiate with client, fallback to tcp:"
                     << s->description();
        s->_rdma_state = Socket::RDMA_OFF;
    } else {
        ep->_remote_recv_block_size = remote_msg.block_size;
        ep->_local_window_capacity = 
            std::min(ep->_sq_size, remote_msg.rq_size) - RESERVED_WR_NUM;
        ep->_remote_window_capacity = 
            std::min(ep->_rq_size, remote_msg.sq_size) - RESERVED_WR_NUM,
        ep->_window_size.store(ep->_local_window_capacity, butil::memory_order_relaxed);

        ep->_state = S_ALLOC_QPCQ;
        if (ep->AllocateResources() < 0) {
            LOG(WARNING) << "Fail to allocate rdma resources, fallback to tcp:"
                         << s->description();
            s->_rdma_state = Socket::RDMA_OFF;
        } else {
            ep->_state = S_BRINGUP_QP;
            if (ep->BringUpQp(remote_msg.lid, remote_msg.gid, remote_msg.qp_num) < 0) {
                LOG(WARNING) << "Fail to bringup QP, fallback to tcp:"
                             << s->description();
                s->_rdma_state = Socket::RDMA_OFF;
            }
        }
    }

    // Send hello message to client
    ep->_state = S_HELLO_SEND;
    HelloMessage local_msg;
    local_msg.msg_len = g_rdma_hello_msg_len;
    if (s->_rdma_state == Socket::RDMA_OFF) {
        local_msg.impl_ver = 0;
        local_msg.hello_ver = 0;
    } else {
        local_msg.lid = GetRdmaLid();
        local_msg.gid = GetRdmaGid();
        local_msg.block_size = g_rdma_recv_block_size;
        local_msg.sq_size = ep->_sq_size;
        local_msg.rq_size = ep->_rq_size;
        local_msg.hello_ver = g_rdma_hello_version;
        local_msg.impl_ver = g_rdma_impl_version;
        if (BAIDU_LIKELY(ep->_resource)) {
            local_msg.qp_num = ep->_resource->qp->qp_num;
        } else {
            // Only happens in UT
            local_msg.qp_num = 0;
        }
    }
    memcpy(data, MAGIC_STR, 4);
    local_msg.Serialize((char*)data + 4);
    if (ep->WriteToFd(data, g_rdma_hello_msg_len) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to send Hello Message to client:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    // Recv ACK Message
    ep->_state = S_ACK_WAIT;
    if (ep->ReadFromFd(data, ACK_MSG_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to read ack message from client:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    // Check RDMA enable flag
    uint32_t* tmp = (uint32_t*)data;  // avoid GCC warning on strict-aliasing
    uint32_t flags = butil::NetToHost32(*tmp);
    if (flags & ACK_MSG_RDMA_OK) {
        if (s->_rdma_state == Socket::RDMA_OFF) {
            LOG(WARNING) << "Fail to parse Hello Message length from client:"
                         << s->description();
            s->SetFailed(EPROTO, "Fail to complete rdma handshake from %s: %s",
                    s->description().c_str(), berror(EPROTO));
            ep->_state = FAILED;
            return NULL;
        } else {
            s->_rdma_state = Socket::RDMA_ON;
            ep->_state = ESTABLISHED;
            LOG_IF(INFO, FLAGS_rdma_trace_verbose) 
                << "Handshake ends (use rdma) on " << s->description();
        }
    } else {
        s->_rdma_state = Socket::RDMA_OFF;
        ep->_state = FALLBACK_TCP;
        LOG_IF(INFO, FLAGS_rdma_trace_verbose) 
            << "Handshake ends (use tcp) on " << s->description();
    }
 
    ep->TryReadOnTcp();

    return NULL;
}

bool RdmaEndpoint::IsWritable() const {
    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        // Just for UT
        return false;
    }

    return _window_size.load(butil::memory_order_relaxed) > 0;
}

// RdmaIOBuf inherits from IOBuf to provide a new function.
// The reason is that we need to use some protected member function of IOBuf.
class RdmaIOBuf : public butil::IOBuf {
friend class RdmaEndpoint;
private:
    // Cut the current IOBuf to ibv_sge list and `to' for at most first max_sge
    // blocks or first max_len bytes.
    // Return: the bytes included in the sglist, or -1 if failed
    ssize_t cut_into_sglist_and_iobuf(ibv_sge* sglist, size_t* sge_index,
            butil::IOBuf* to, size_t max_sge, size_t max_len) {
        size_t len = 0;
        while (*sge_index < max_sge) {
            if (len == max_len || _ref_num() == 0) {
                break;
            }
            butil::IOBuf::BlockRef const& r = _ref_at(0);
            CHECK(r.length > 0);
            const void* start = fetch1();
            uint32_t lkey = GetRegionId(start);
            if (lkey == 0) {  // get lkey for user registered memory
                uint64_t meta = get_first_data_meta();
                if (meta <= UINT_MAX) {
                    lkey = (uint32_t)meta;
                }
            }
            if (BAIDU_UNLIKELY(lkey == 0)) {  // only happens when meta is not specified
                lkey = GetLKey((char*)start - r.offset);
            }
            if (lkey == 0) {
                LOG(WARNING) << "Memory not registered for rdma. "
                             << "Is this iobuf allocated before calling "
                             << "GlobalRdmaInitializeOrDie? Or just forget to "
                             << "call RegisterMemoryForRdma for your own buffer?";
                errno = ERDMAMEM;
                return -1;
            }
            size_t i = *sge_index;
            if (len + r.length > max_len) {
                // Split the block to comply with size for receiving
                sglist[i].length = max_len - len;
                len = max_len;
            } else {
                sglist[i].length = r.length;
                len += r.length;
            }
            sglist[i].addr = (uint64_t)start;
            sglist[i].lkey = lkey;
            cutn(to, sglist[i].length);
            (*sge_index)++;
        }
        return len;
    }
};

// Note this function is coupled with the implementation of IOBuf
ssize_t RdmaEndpoint::CutFromIOBufList(butil::IOBuf** from, size_t ndata) {
    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        // Just for UT
        errno = EAGAIN;
        return -1;
    }

    CHECK(from != NULL);
    CHECK(ndata > 0);

    size_t total_len = 0;
    size_t current = 0;
    uint32_t window = 0;
    ibv_send_wr wr;
    int max_sge = GetRdmaMaxSge();
    ibv_sge sglist[max_sge];
    while (current < ndata) {
        window = _window_size.load(butil::memory_order_relaxed);
        if (window == 0) {
            if (total_len > 0) {
                break;
            } else {
                errno = EAGAIN;
                return -1;
            }
        }
        butil::IOBuf* to = &_sbuf[_sq_current];
        size_t this_len = 0;

        memset(&wr, 0, sizeof(wr));
        wr.sg_list = sglist;
        wr.opcode = IBV_WR_SEND_WITH_IMM;

        RdmaIOBuf* data = (RdmaIOBuf*)from[current];
        size_t sge_index = 0;
        while (sge_index < (uint32_t)max_sge &&
                this_len < _remote_recv_block_size) {
            if (data->size() == 0) {
                // The current IOBuf is empty, find next one
                ++current;
                if (current == ndata) {
                    break;
                }
                data = (RdmaIOBuf*)from[current];
                continue;
            }

            ssize_t len = data->cut_into_sglist_and_iobuf(
                    sglist, &sge_index, to, max_sge,
                    _remote_recv_block_size - this_len);
            if (len < 0) {
                return -1;
            }
            CHECK(len > 0);
            this_len += len;
            total_len += len;
        }
        if (this_len == 0) {
            continue;
        }

        wr.num_sge = sge_index;

        uint32_t imm = _new_rq_wrs.exchange(0, butil::memory_order_relaxed);
        wr.imm_data = butil::HostToNet32(imm);
        // Avoid too much recv completion event to reduce the cpu overhead
        bool solicited = false;
        if (window == 1 || current + 1 >= ndata) {
            // Only last message in the write queue or last message in the
            // current window will be flagged as solicited.
            solicited = true;
        } else {
            if (_unsolicited > _local_window_capacity / 4) {
                // Make sure the recv side can be signaled to return ack
                solicited = true;
            } else if (_accumulated_ack > _remote_window_capacity / 4) {
                // Make sure the recv side can be signaled to handle ack
                solicited = true;
            } else if (_unsolicited_bytes > 1048576) {
                // Make sure the recv side can be signaled when it receives enough data
                solicited = true;
            } else {
                ++_unsolicited;
                _unsolicited_bytes += this_len;
                _accumulated_ack += imm;
            }
        }
        if (solicited) {
            wr.send_flags |= IBV_SEND_SOLICITED;
            _unsolicited = 0;
            _unsolicited_bytes = 0;
            _accumulated_ack = 0;
        }

        // Avoid too much send completion event to reduce the CPU overhead
        ++_sq_unsignaled;
        if (_sq_unsignaled >= _local_window_capacity / 4) {
            // Refer to:
            // http::www.rdmamojo.com/2014/06/30/working-unsignaled-completions/
            wr.send_flags |= IBV_SEND_SIGNALED;
            _sq_unsignaled = 0;
        }

        ibv_send_wr* bad = NULL;
        if (ibv_post_send(_resource->qp, &wr, &bad) < 0) {
            // We use other way to guarantee the Send Queue is not full.
            // So we just consider this error as an unrecoverable error.
            PLOG(WARNING) << "Fail to ibv_post_send";
            return -1;
        }

        ++_sq_current;
        if (_sq_current == _sq_size - RESERVED_WR_NUM) {
            _sq_current = 0;
        }

        // Update _window_size. Note that _window_size will never be negative.
        // Because there is at most one thread can enter this function for each
        // Socket, and the other thread of HandleCompletion can only add this
        // counter.
        _window_size.fetch_sub(1, butil::memory_order_relaxed);
    }

    return total_len;
}

int RdmaEndpoint::SendAck(int num) {
    if (_new_rq_wrs.fetch_add(num, butil::memory_order_relaxed) > _remote_window_capacity / 2) {
        return SendImm(_new_rq_wrs.exchange(0, butil::memory_order_relaxed));
    }
    return 0;
}

int RdmaEndpoint::SendImm(uint32_t imm) {
    if (imm == 0) {
        return 0;
    }

    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.opcode = IBV_WR_SEND_WITH_IMM;
    wr.imm_data = butil::HostToNet32(imm);
    wr.send_flags |= IBV_SEND_SOLICITED;
    wr.send_flags |= IBV_SEND_SIGNALED;

    ibv_send_wr* bad = NULL;
    if (ibv_post_send(_resource->qp, &wr, &bad) < 0) {
        // We use other way to guarantee the Send Queue is not full.
        // So we just consider this error as an unrecoverable error.
        PLOG(WARNING) << "Fail to ibv_post_send";
        return -1;
    }
    return 0;
}

ssize_t RdmaEndpoint::HandleCompletion(ibv_wc& wc) {
    bool zerocopy = FLAGS_rdma_recv_zerocopy;
    switch (wc.opcode) {
    case IBV_WC_SEND: {  // send completion
        // Do nothing
        break;
    }
    case IBV_WC_RECV: {  // recv completion
        // Please note that only the first wc.byte_len bytes is valid
        if (wc.byte_len > 0) {
            if (wc.byte_len < (uint32_t)FLAGS_rdma_zerocopy_min_size) {
                zerocopy = false;
            }
            CHECK(_state != FALLBACK_TCP);
            if (zerocopy) {
                butil::IOBuf tmp;
                _rbuf[_rq_received].cutn(&tmp, wc.byte_len);
                _socket->_read_buf.append(tmp);
            } else {
                // Copy data when the receive data is really small
                _socket->_read_buf.append(_rbuf_data[_rq_received], wc.byte_len);
            }
        }
        if (wc.imm_data > 0) {
            // Clear sbuf here because we ignore event wakeup for send completions
            uint32_t acks = butil::NetToHost32(wc.imm_data);
            uint32_t num = acks;
            while (num > 0) {
                _sbuf[_sq_sent++].clear();
                if (_sq_sent == _sq_size - RESERVED_WR_NUM) {
                    _sq_sent = 0;
                }
                --num;
            }
            butil::subtle::MemoryBarrier();

            // Update window
            uint32_t wnd_thresh = _local_window_capacity / 8;
            if (_window_size.fetch_add(acks, butil::memory_order_relaxed) >= wnd_thresh
                    || acks >= wnd_thresh) {
                // Do not wake up writing thread right after _window_size > 0.
                // Otherwise the writing thread may switch to background too quickly.
                _socket->WakeAsEpollOut();
            }
        }
        // We must re-post recv WR
        if (PostRecv(1, zerocopy) < 0) {
            return -1;
        }
        if (wc.byte_len > 0) {
            SendAck(1);
        }
        return wc.byte_len;
    }
    default:
        // Some driver bugs may lead to unexpected completion opcode.
        // If this happens, please update your driver.
        CHECK(false) << "This should not happen. Got a completion with opcode="
                     << wc.opcode;
        return -1;
    }
    return 0;
}

int RdmaEndpoint::DoPostRecv(void* block, size_t block_size) {
    ibv_recv_wr wr;
    memset(&wr, 0, sizeof(wr));
    ibv_sge sge;
    sge.addr = (uint64_t)block;
    sge.length = block_size;
    sge.lkey = GetRegionId(block);
    wr.num_sge = 1;
    wr.sg_list = &sge;

    ibv_recv_wr* bad = NULL;
    if (ibv_post_recv(_resource->qp, &wr, &bad) < 0) {
        PLOG(WARNING) << "Fail to ibv_post_recv";
        return -1;
    }
    return 0;
}

int RdmaEndpoint::PostRecv(uint32_t num, bool zerocopy) {
    // We do the post repeatedly from the _rbuf[_rq_received].
    while (num > 0) {
        if (zerocopy) {
            _rbuf[_rq_received].clear();
            butil::IOBufAsZeroCopyOutputStream os(&_rbuf[_rq_received],
                    g_rdma_recv_block_size + IOBUF_BLOCK_HEADER_LEN);
            int size = 0;
            if (!os.Next(&_rbuf_data[_rq_received], &size)) {
                // Memory is not enough for preparing a block
                PLOG(WARNING) << "Fail to allocate rbuf";
                return -1;
            } else {
                CHECK(static_cast<uint32_t>(size) == g_rdma_recv_block_size) << size;
            }
        }
        if (DoPostRecv(_rbuf_data[_rq_received], g_rdma_recv_block_size) < 0) {
            _rbuf[_rq_received].clear();
            return -1;
        }
        --num;
        ++_rq_received;
        if (_rq_received == _rq_size) {
            _rq_received = 0;
        }
    };
    return 0;
}

static RdmaResource* AllocateQpCq(uint16_t sq_size, uint16_t rq_size) {
    RdmaResource* res = new (std::nothrow) RdmaResource;
    if (!res) {
        return NULL;
    }

    res->comp_channel = IbvCreateCompChannel(GetRdmaContext());
    if (!res->comp_channel) {
        PLOG(WARNING) << "Fail to create comp channel for CQ";
        delete res;
        return NULL;
    }

    butil::make_close_on_exec(res->comp_channel->fd);
    if (butil::make_non_blocking(res->comp_channel->fd) < 0) {
        PLOG(WARNING) << "Fail to set comp channel nonblocking";
        delete res;
        return NULL;
    }

    res->cq = IbvCreateCq(GetRdmaContext(), 2 * FLAGS_rdma_prepared_qp_size,
            NULL, res->comp_channel, GetRdmaCompVector());
    if (!res->cq) {
        PLOG(WARNING) << "Fail to create CQ";
        delete res;
        return NULL;
    }

    ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.send_cq = res->cq;
    attr.recv_cq = res->cq;
    // NOTE: Since we hope to reduce send completion events, we set signaled
    // send_wr every 1/4 of the total wnd. The wnd will increase when the ack
    // is received, which means the receive side has already received the data
    // in the corresponding send_wr. However, the ack does not mean the send_wr
    // has been removed from SQ if it is set unsignaled. The reason is that
    // the unsignaled send_wr is removed from SQ only after the CQE of next
    // signaled send_wr is polled. Thus in a rare case, a new send_wr cannot be
    // posted to SQ even in the wnd is not empty. In order to solve this
    // problem, we enlarge the size of SQ to contain redundant 1/4 of the wnd,
    // which is the maximum number of unsignaled send_wrs.
    attr.cap.max_send_wr = sq_size * 5 / 4; /*NOTE*/
    attr.cap.max_recv_wr = rq_size;
    attr.cap.max_send_sge = GetRdmaMaxSge();
    attr.cap.max_recv_sge = 1;
    attr.qp_type = IBV_QPT_RC;
    res->qp = IbvCreateQp(GetRdmaPd(), &attr);
    if (!res->qp) {
        PLOG(WARNING) << "Fail to create QP";
        delete res;
        return NULL;
    }

    return res;
}

int RdmaEndpoint::AllocateResources() {
    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        // For UT
        return 0;
    }

    CHECK(_resource == NULL);

    if (_sq_size <= FLAGS_rdma_prepared_qp_size &&
        _rq_size <= FLAGS_rdma_prepared_qp_size) {
        BAIDU_SCOPED_LOCK(*g_rdma_resource_mutex);
        if (g_rdma_resource_list) {
            _resource = g_rdma_resource_list;
            g_rdma_resource_list = g_rdma_resource_list->next;
        }
    }
    if (!_resource) {
        _resource = AllocateQpCq(_sq_size, _rq_size);
    } else {
        _resource->next = NULL;
    }
    if (!_resource) {
        return -1;
    }

    SocketOptions options;
    options.user = this;
    options.keytable_pool = _socket->_keytable_pool;
    options.fd = _resource->comp_channel->fd;
    options.on_edge_triggered_events = PollCq;
    if (Socket::Create(options, &_cq_sid) < 0) {
        PLOG(WARNING) << "Fail to create socket for cq";
        return -1;
    }

    if (ibv_req_notify_cq(_resource->cq, 1) < 0) {
        PLOG(WARNING) << "Fail to arm CQ comp channel";
        return -1;
    }

    _sbuf.resize(_sq_size - RESERVED_WR_NUM);
    if (_sbuf.size() != _sq_size - RESERVED_WR_NUM) {
        return -1;
    }
    _rbuf.resize(_rq_size);
    if (_rbuf.size() != _rq_size) {
        return -1;
    }
    _rbuf_data.resize(_rq_size, NULL);
    if (_rbuf_data.size() != _rq_size) {
        return -1;
    }

    return 0;
}

int RdmaEndpoint::BringUpQp(uint16_t lid, ibv_gid gid, uint32_t qp_num) {
    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        // For UT
        return 0;
    }

    ibv_qp_attr attr;

    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;  // TODO: support more pkey use in future
    attr.port_num = GetRdmaPortNum();
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
    if (IbvModifyQp(_resource->qp, &attr, (ibv_qp_attr_mask)(
                IBV_QP_STATE | 
                IBV_QP_PKEY_INDEX |
                IBV_QP_PORT |
                IBV_QP_ACCESS_FLAGS)) < 0) {
        PLOG(WARNING) << "Fail to modify QP from RESET to INIT";
        return -1;
    }

    if (PostRecv(_rq_size, true) < 0) {
        PLOG(WARNING) << "Fail to post recv wr";
        return -1;
    }

    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_1024;  // TODO: support more mtu in future
    attr.ah_attr.grh.dgid = gid;
    attr.ah_attr.grh.flow_label = 0;
    attr.ah_attr.grh.sgid_index = GetRdmaGidIndex();
    attr.ah_attr.grh.hop_limit = MAX_HOP_LIMIT;
    attr.ah_attr.grh.traffic_class = 0;
    attr.ah_attr.dlid = lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.static_rate = 0;
    attr.ah_attr.is_global = 1;
    attr.ah_attr.port_num = GetRdmaPortNum();
    attr.dest_qp_num = qp_num;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 0;
    attr.min_rnr_timer = 0;  // We do not allow rnr error
    if (IbvModifyQp(_resource->qp, &attr, (ibv_qp_attr_mask)(
                IBV_QP_STATE |
                IBV_QP_PATH_MTU |
                IBV_QP_MIN_RNR_TIMER |
                IBV_QP_AV |
                IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN)) < 0) {
        PLOG(WARNING) << "Fail to modify QP from INIT to RTR";
        return -1;
    }

    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = TIMEOUT;
    attr.retry_cnt = RETRY_CNT;
    attr.rnr_retry = 0;  // We do not allow rnr error
    attr.sq_psn = 0;
    attr.max_rd_atomic = 0;
    if (IbvModifyQp(_resource->qp, &attr, (ibv_qp_attr_mask)(
                IBV_QP_STATE |
                IBV_QP_RNR_RETRY |
                IBV_QP_RETRY_CNT |
                IBV_QP_TIMEOUT |
                IBV_QP_SQ_PSN |
                IBV_QP_MAX_QP_RD_ATOMIC)) < 0) {
        PLOG(WARNING) << "Fail to modify QP from RTR to RTS";
        return -1;
    }

    return 0;
}

void RdmaEndpoint::DeallocateResources() {
    if (!_resource) {
        return;
    }
    bool move_to_rdma_resource_list = false;
    if (_sq_size <= FLAGS_rdma_prepared_qp_size &&
        _rq_size <= FLAGS_rdma_prepared_qp_size &&
        FLAGS_rdma_prepared_qp_cnt > 0) {
        ibv_qp_attr attr;
        attr.qp_state = IBV_QPS_RESET;
        if (IbvModifyQp(_resource->qp, &attr, IBV_QP_STATE) == 0) {
            move_to_rdma_resource_list = true;
        }
    }
    int fd = _resource->comp_channel->fd;
    if (!move_to_rdma_resource_list) {
        if (_resource->qp) {
            if (IbvDestroyQp(_resource->qp) < 0) {
                PLOG(WARNING) << "Fail to destroy QP";
            }
            _resource->qp = NULL;
        }
        if (_resource->cq) {
            IbvAckCqEvents(_resource->cq, _cq_events);
            if (IbvDestroyCq(_resource->cq) < 0) {
                PLOG(WARNING) << "Fail to destroy CQ";
            }
            _resource->cq = NULL;
        }
        if (_resource->comp_channel) {
            // destroy comp_channel will destroy this fd
            // so that we should remove it from epoll fd first
            _socket->_io_event.RemoveConsumer(fd);
            fd = -1;
            if (IbvDestroyCompChannel(_resource->comp_channel) < 0) {
                PLOG(WARNING) << "Fail to destroy CQ channel";
            }
            _resource->comp_channel = NULL;
        }
        delete _resource;
        _resource = NULL;
    }

    SocketUniquePtr s;
    if (_cq_sid != INVALID_SOCKET_ID) {
        if (Socket::Address(_cq_sid, &s) == 0) {
            s->_user = NULL;  // do not release user (this RdmaEndpoint)
            if (fd >= 0) {
                _socket->_io_event.RemoveConsumer(fd);
            }
            s->_fd = -1;  // already remove fd from epoll fd
            s->SetFailed();
        }
        _cq_sid = INVALID_SOCKET_ID;
    }

    if (move_to_rdma_resource_list) {
        if (_resource->cq) {
            IbvAckCqEvents(_resource->cq, _cq_events);
        }
        BAIDU_SCOPED_LOCK(*g_rdma_resource_mutex);
        _resource->next = g_rdma_resource_list;
        g_rdma_resource_list = _resource;
    }

    _resource = NULL;
}

static const int MAX_CQ_EVENTS = 128;

int RdmaEndpoint::GetAndAckEvents() {
    int events = 0; void* context = NULL;
    while (1) {
        if (IbvGetCqEvent(_resource->comp_channel, &_resource->cq, &context) < 0) {
            if (errno != EAGAIN) {
                return -1;
            }
            break;
        }
        ++events;
    }
    if (events == 0) {
        return 0;
    }
    _cq_events += events;
    if (_cq_events >= MAX_CQ_EVENTS) {
        IbvAckCqEvents(_resource->cq, _cq_events);
        _cq_events = 0;
    }
    return 0;
}

void RdmaEndpoint::PollCq(Socket* m) {
    RdmaEndpoint* ep = static_cast<RdmaEndpoint*>(m->user());
    if (!ep) {
        return;
    }

    SocketUniquePtr s;
    if (Socket::Address(ep->_socket->id(), &s) < 0) {
        return;
    }
    CHECK(ep == s->_rdma_ep);

    if (ep->GetAndAckEvents() < 0) {
        const int saved_errno = errno;
        PLOG(ERROR) << "Fail to get cq event: " << s->description();
        s->SetFailed(saved_errno, "Fail to get cq event from %s: %s",
                s->description().c_str(), berror(saved_errno));
        return;
    }

    int progress = Socket::PROGRESS_INIT;
    bool notified = false;
    InputMessenger::InputMessageClosure last_msg;
    ibv_wc wc[FLAGS_rdma_cqe_poll_once];
    while (true) {
        int cnt = ibv_poll_cq(ep->_resource->cq, FLAGS_rdma_cqe_poll_once, wc);
        if (cnt < 0) {
            const int saved_errno = errno;
            PLOG(WARNING) << "Fail to poll cq: " << s->description();
            s->SetFailed(saved_errno, "Fail to poll cq from %s: %s",
                    s->description().c_str(), berror(saved_errno));
            return;
        }
        if (cnt == 0) {
            if (!notified) {
                // Since RDMA only provides one shot event, we have to call the
                // notify function every time. Because there is a possibility
                // that the event arrives after the poll but before the notify,
                // we should re-poll the CQ once after the notify to check if
                // there is an available CQE.
                if (ibv_req_notify_cq(ep->_resource->cq, 1) < 0) {
                    const int saved_errno = errno;
                    PLOG(WARNING) << "Fail to arm CQ comp channel: " << s->description();
                    s->SetFailed(saved_errno, "Fail to arm cq channel from %s: %s",
                            s->description().c_str(), berror(saved_errno));
                    return;
                }
                notified = true;
                continue;
            }
            if (!m->MoreReadEvents(&progress)) {
                break;
            }
            if (ep->GetAndAckEvents() < 0) {
                s->SetFailed(errno, "Fail to ack CQ event on %s",
                        s->description().c_str());
                return;
            }
            notified = false;
            continue;
        }
        notified = false;

        ssize_t bytes = 0;
        for (int i = 0; i < cnt; ++i) {
            if (s->Failed()) {
                continue;
            }

            if (wc[i].status != IBV_WC_SUCCESS) {
                PLOG(WARNING) << "Fail to handle RDMA completion, error status("
                              << wc[i].status << "): " << s->description();
                s->SetFailed(ERDMA, "RDMA completion error(%d) from %s: %s",
                             wc[i].status, s->description().c_str(), berror(ERDMA));
                continue;
            }

            ssize_t nr = ep->HandleCompletion(wc[i]);
            if (nr < 0) {
                const int saved_errno = errno;
                PLOG(WARNING) << "Fail to handle RDMA completion: " << s->description();
                s->SetFailed(saved_errno, "Fail to handle rdma completion from %s: %s",
                        s->description().c_str(), berror(saved_errno));
            } else if (nr > 0) {
                bytes += nr;
            }
        }

        // Just call PrcessNewMessage once for all of these CQEs.
        // Otherwise it may call too many bthread_flush to affect performance.
        const int64_t received_us = butil::cpuwide_time_us();
        const int64_t base_realtime = butil::gettimeofday_us() - received_us;
        InputMessenger* messenger = static_cast<InputMessenger*>(s->user());
        if (messenger->ProcessNewMessage(
                    s.get(), bytes, false, received_us, base_realtime, last_msg) < 0) {
            return;
        }
    }
}

std::string RdmaEndpoint::GetStateStr() const {
    switch (_state) {
    case UNINIT: return "UNINIT";
    case C_ALLOC_QPCQ: return "C_ALLOC_QPCQ";
    case C_HELLO_SEND: return "C_HELLO_SEND";
    case C_HELLO_WAIT: return "C_HELLO_WAIT";
    case C_BRINGUP_QP: return "C_BRINGUP_QP";
    case C_ACK_SEND: return "C_ACK_SEND";
    case S_HELLO_WAIT: return "S_HELLO_WAIT";
    case S_ALLOC_QPCQ: return "S_ALLOC_QPCQ";
    case S_BRINGUP_QP: return "S_BRINGUP_QP";
    case S_HELLO_SEND: return "S_HELLO_SEND";
    case S_ACK_WAIT: return "S_ACK_WAIT";
    case ESTABLISHED: return "ESTABLISHED";
    case FALLBACK_TCP: return "FALLBACK_TCP";
    case FAILED: return "FAILED";
    default: return "UNKNOWN";
    }
}

void RdmaEndpoint::DebugInfo(std::ostream& os) const {
    os << "\nrdma_state=ON"
       << "\nhandshake_state=" << GetStateStr()
       << "\nrdma_window_size=" << _window_size.load(butil::memory_order_relaxed)
       << "\nrdma_local_window_capacity=" << _local_window_capacity
       << "\nrdma_remote_window_capacity=" << _remote_window_capacity
       << "\nrdma_sbuf_head=" << _sq_current
       << "\nrdma_sbuf_tail=" << _sq_sent
       << "\nrdma_rbuf_head=" << _rq_received
       << "\nrdma_unacked_rq_wr=" << _new_rq_wrs
       << "\nrdma_received_ack=" << _accumulated_ack
       << "\nrdma_unsolicited_sent=" << _unsolicited
       << "\nrdma_unsignaled_sq_wr=" << _sq_unsignaled
       << "\n";
}

int RdmaEndpoint::GlobalInitialize() {
    if (FLAGS_rdma_recv_block_type == "default") {
        g_rdma_recv_block_size = GetBlockSize(0) - IOBUF_BLOCK_HEADER_LEN;
    } else if (FLAGS_rdma_recv_block_type == "large") {
        g_rdma_recv_block_size = GetBlockSize(1) - IOBUF_BLOCK_HEADER_LEN;
    } else if (FLAGS_rdma_recv_block_type == "huge") {
        g_rdma_recv_block_size = GetBlockSize(2) - IOBUF_BLOCK_HEADER_LEN;
    } else {
        errno = EINVAL;
        return -1;
    }

    g_rdma_resource_mutex = new butil::Mutex;
    for (int i = 0; i < FLAGS_rdma_prepared_qp_cnt; ++i) {
        RdmaResource* res = AllocateQpCq(FLAGS_rdma_prepared_qp_size,
                                         FLAGS_rdma_prepared_qp_size);
        if (!res) {
            return -1;
        }
        res->next = g_rdma_resource_list;
        g_rdma_resource_list = res;
    }

    return 0;
}

void RdmaEndpoint::GlobalRelease() {
    if (g_rdma_resource_mutex) {
        BAIDU_SCOPED_LOCK(*g_rdma_resource_mutex);
        while (g_rdma_resource_list) {
            RdmaResource* res = g_rdma_resource_list;
            g_rdma_resource_list = g_rdma_resource_list->next;
            delete res;
        }
    }
}

}  // namespace rdma
}  // namespace brpc

#endif  // if BRPC_WITH_RDMA
