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

#if BRPC_WITH_UBRING

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
#include "brpc/ubring/ub_helper.h"
#include "brpc/ubring/ub_endpoint.h"
#include "brpc/ubring/shm/shm_def.h"
#include "brpc/ubring/common/common.h"
#include "brpc/ub_transport.h"
#include "brpc/ubring/ubr_trx.h"

DECLARE_int32(task_group_ntags);

namespace brpc {
DECLARE_bool(log_connection_close);
namespace ubring {

extern bool g_skip_ub_init;
DEFINE_int32(data_queue_size, 4, "data queue size for UB");
DEFINE_bool(ub_trace_verbose, false, "Print log message verbosely");
BRPC_VALIDATE_GFLAG(ub_trace_verbose, brpc::PassValidate);
DEFINE_int32(ub_poller_num, 1, "Poller number in ub polling mode.");
DEFINE_bool(ub_poller_yield, false, "Yield thread in RDMA polling mode.");
DEFINE_bool(ub_edisp_unsched, false, "Disable event dispatcher schedule");
DEFINE_bool(ub_disable_bthread, false, "Disable bthread in RDMA");

static const size_t MIN_ONCE_READ = 4096;
static const size_t MAX_ONCE_READ = 524288;
static const size_t IOBUF_IOV_MAX = 256;

static const char* MAGIC_STR = "UB";
static const size_t MAGIC_STR_LEN = 2;
static const size_t HELLO_MSG_LEN_MIN = 64;
static const size_t ACK_MSG_LEN = 4;
static uint16_t g_ub_hello_msg_len = 64;
static uint16_t g_ub_hello_version = 2;
static uint16_t g_ub_impl_version = 1;

static const uint32_t ACK_MSG_UB_OK = 0x1;

static butil::Mutex* g_ubring_resource_mutex = NULL;

struct HelloMessage {
    void Serialize(void* data) const;
    void Deserialize(void* data);

    uint16_t msg_len;
    uint16_t hello_ver;
    uint16_t impl_ver;
    uint64_t len;
    char shm_name[SHM_MAX_NAME_BUFF_LEN];
};

void HelloMessage::Serialize(void* data) const {
    uint16_t* current_pos = (uint16_t*)data;
    *(current_pos++) = butil::HostToNet16(msg_len);
    *(current_pos++) = butil::HostToNet16(hello_ver);
    *(current_pos++) = butil::HostToNet16(impl_ver);
    uint64_t* len_pos = (uint64_t*)current_pos;
    *len_pos = butil::HostToNet64(len);
    current_pos += 4;
    memcpy(current_pos, shm_name, SHM_MAX_NAME_BUFF_LEN);
}

void HelloMessage::Deserialize(void* data) {
    uint16_t* current_pos = (uint16_t*)data;
    msg_len = butil::NetToHost16(*current_pos++);
    hello_ver = butil::NetToHost16(*current_pos++);
    impl_ver = butil::NetToHost16(*current_pos++);
    len = butil::NetToHost64(*(uint64_t*)current_pos);
    current_pos += 4; // move forward 4 Bytes
    memcpy(shm_name, current_pos, SHM_MAX_NAME_BUFF_LEN);
}

UBShmEndpoint::UBShmEndpoint(Socket* s)
    : _socket(s)
    , _ub_ring(nullptr)
    , _cq_sid(INVALID_SOCKET_ID)
{
    _read_butex = bthread::butex_create_checked<butil::atomic<int>>();
}

UBShmEndpoint::~UBShmEndpoint() {
    Reset();
    bthread::butex_destroy(_read_butex);
}

void UBShmEndpoint::Reset() {
    DeallocateResources();

    delete _ub_ring;
    _ub_ring = nullptr;
    _cq_sid = INVALID_SOCKET_ID;
}

void UBConnect::StartConnect(const Socket* socket,
                               void (*done)(int err, void* data),
                               void* data) {
    auto* ub_transport = static_cast<UBShmTransport*>(socket->_transport.get());
    CHECK(ub_transport->_ub_ep != NULL);
    SocketUniquePtr s;
    if (Socket::Address(socket->id(), &s) != 0) {
        return;
    }
    if (!IsUBAvailable()) {
        ub_transport->_ub_ep->_state = UBShmEndpoint::FALLBACK_TCP;
        ub_transport->_ub_state = UBShmTransport::UB_OFF;
        done(0, data);
        return;
    }
    _done = done;
    _data = data;
    bthread_t tid;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    bthread_attr_set_name(&attr, "UBProcessHandshakeAtClient");
    if (bthread_start_background(&tid, &attr,
                UBShmEndpoint::ProcessHandshakeAtClient, ub_transport->_ub_ep) < 0) {
        LOG(FATAL) << "Fail to start handshake bthread";
        Run();
    } else {
        s.release();
    }
}

void UBConnect::StopConnect(Socket* socket) { }

void UBConnect::Run() {
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

void UBShmEndpoint::OnNewDataFromTcp(Socket* m) {
    auto* ub_transport = static_cast<UBShmTransport*>(m->_transport.get());
    UBShmEndpoint* ep = ub_transport->GetUBShmEp();
    CHECK(ep != NULL);

    int progress = Socket::PROGRESS_INIT;
    while (true) {
        if (ep->_state == UNINIT) {
            if (!m->CreatedByConnect()) {
                if (!IsUBAvailable()) {
                    ep->_state = FALLBACK_TCP;
                    ub_transport->_ub_state = UBShmTransport::UB_OFF;
                    continue;
                }
                bthread_t tid;
                ep->_state = S_HELLO_WAIT;
                SocketUniquePtr s;
                m->ReAddress(&s);
                bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
                bthread_attr_set_name(&attr, "UBProcessHandshakeAtServer");
                if (bthread_start_background(&tid, &attr,
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
    if (msg.hello_ver == g_ub_hello_version &&
        msg.impl_ver == g_ub_impl_version) {
        // This can be modified for future compatibility
        return true;
    }
    return false;
}

static const int WAIT_TIMEOUT_MS = 50;

int UBShmEndpoint::ReadFromFd(void* data, size_t len) {
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
        } else if (nr == 0) {
            errno = EEOF;
            return -1;
        } else {
            received += nr;
        }
    } while (received < len);
    return 0;
}

int UBShmEndpoint::WriteToFd(void* data, size_t len) {
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

inline void UBShmEndpoint::TryReadOnTcp() {
    if (_socket->_nevent.fetch_add(1, butil::memory_order_acq_rel) == 0) {
        if (_state == FALLBACK_TCP) {
            InputMessenger::OnNewMessages(_socket);
        } else if (_state == ESTABLISHED) {
            TryReadOnTcpDuringRdmaEst(_socket);
        }
    }
}

void* UBShmEndpoint::ProcessHandshakeAtClient(void* arg) {
    UBShmEndpoint* ep = static_cast<UBShmEndpoint*>(arg);
    SocketUniquePtr s(ep->_socket);
    UBConnect::RunGuard rg((UBConnect*)s->_app_connect.get());

    LOG_IF(INFO, FLAGS_ub_trace_verbose) 
        << "Start handshake on " << s->_local_side;

    uint8_t data[g_ub_hello_msg_len];

    ep->_state = C_ALLOC_SHM;
    auto* ub_transport = static_cast<UBShmTransport*>(s->_transport.get());
    size_t local_shm_len = (size_t)(FLAGS_data_queue_size) * MB_TO_BYTE;
    SHM local_trx_shm = {NULL, local_shm_len, 0, {0}, (uint32_t)s->fd()};
    const char* shm_name = butil::endpoint2str(s->local_side()).c_str();
    if (ep->AllocateClientResources(&local_trx_shm, shm_name) < 0) {
        LOG(WARNING) << "Fallback to tcp:" << s->description();
        ub_transport->_ub_state = UBShmTransport::UB_OFF;
        ep->_state = FALLBACK_TCP;
        return NULL;
    }

    ep->_state = C_HELLO_SEND;
    HelloMessage local_msg;
    local_msg.msg_len = g_ub_hello_msg_len;
    local_msg.hello_ver = g_ub_hello_version;
    local_msg.impl_ver = g_ub_impl_version;
    local_msg.len = local_shm_len;
    memcpy(local_msg.shm_name, local_trx_shm.name, SHM_MAX_NAME_BUFF_LEN);
    memcpy(data, MAGIC_STR, MAGIC_STR_LEN);
    local_msg.Serialize((char*)data + MAGIC_STR_LEN);
    if (ep->WriteToFd(data, g_ub_hello_msg_len) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to send hello message to server:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete ubring handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    ep->_state = C_HELLO_WAIT;
    if (ep->ReadFromFd(data, MAGIC_STR_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to get hello message from server:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete ubring handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }
    if (memcmp(data, MAGIC_STR, MAGIC_STR_LEN) != 0) {
        LOG(WARNING) << "Read unexpected data during handshake:" << s->description();
        s->SetFailed(EPROTO, "Fail to complete ubring handshake from %s: %s",
                s->description().c_str(), berror(EPROTO));
        ep->_state = FAILED;
        return NULL;
    }

    if (ep->ReadFromFd(data, HELLO_MSG_LEN_MIN - MAGIC_STR_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to get Hello Message from server:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete ubring handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }
    HelloMessage remote_msg;
    remote_msg.Deserialize(data);
    if (remote_msg.msg_len < HELLO_MSG_LEN_MIN) {
        LOG(WARNING) << "Fail to parse Hello Message length from server:"
                     << s->description();
        s->SetFailed(EPROTO, "Fail to complete ubring handshake from %s: %s",
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
        ub_transport->_ub_state = UBShmTransport::UB_OFF;
    } else {
        ep->_state = C_MAP_REMOTE_SHM;
        if (ep->_ub_ring->UbrMapRemoteShm(&local_trx_shm, shm_name) < 0) {
            LOG(WARNING) << "Fail to map the remote shm, fallback to tcp:" << s->description();
            ub_transport->_ub_state = UBShmTransport::UB_OFF;
        } else {
            ub_transport->_ub_state = UBShmTransport::UB_ON;
        }
    }

    ep->_state = C_ACK_SEND;
    uint32_t flags = 0;
    if (ub_transport->_ub_state != UBShmTransport::UB_OFF) {
        flags |= ACK_MSG_UB_OK;
    }
    uint32_t* tmp = (uint32_t*)data;
    *tmp = butil::HostToNet32(flags);
    if (ep->WriteToFd(data, ACK_MSG_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to send Ack Message to server:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete ubring handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    if (ub_transport->_ub_state == UBShmTransport::UB_ON) {
        ep->_state = ESTABLISHED;
        LOG_IF(INFO, FLAGS_ub_trace_verbose) 
            << "Client handshake ends (use ubring) on " << s->description();
    } else {
        ep->_state = FALLBACK_TCP;
        LOG_IF(INFO, FLAGS_ub_trace_verbose) 
            << "Client handshake ends (use tcp) on " << s->description();
    }

    errno = 0;

    return NULL;
}

void* UBShmEndpoint::ProcessHandshakeAtServer(void* arg) {
    UBShmEndpoint* ep = static_cast<UBShmEndpoint*>(arg);
    SocketUniquePtr s(ep->_socket);

    LOG_IF(INFO, FLAGS_ub_trace_verbose)
        << "Start handshake on " << s->description();

    uint8_t data[g_ub_hello_msg_len];

    ep->_state = S_HELLO_WAIT;
    if (ep->ReadFromFd(data, MAGIC_STR_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to read Hello Message from client:" << s->description() << " " << s->_remote_side;
        s->SetFailed(saved_errno, "Fail to complete ubring handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }
    auto* ub_transport = static_cast<UBShmTransport*>(s->_transport.get());
    if (memcmp(data, MAGIC_STR, MAGIC_STR_LEN) != 0) {
        LOG_IF(INFO, FLAGS_ub_trace_verbose) << "It seems that the "
            << "client does not use RDMA, fallback to TCP:"
            << s->description();
        s->_read_buf.append(data, MAGIC_STR_LEN);
        ep->_state = FALLBACK_TCP;
        ub_transport->_ub_state = UBShmTransport::UB_OFF;
        ep->TryReadOnTcp();
        return NULL;
    }

    if (ep->ReadFromFd(data, g_ub_hello_msg_len - MAGIC_STR_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to read Hello Message from client:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete ubring handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    HelloMessage remote_msg;
    remote_msg.Deserialize(data);
    if (remote_msg.msg_len < HELLO_MSG_LEN_MIN) {
        LOG(WARNING) << "Fail to parse Hello Message length from client:"
                     << s->description();
        s->SetFailed(EPROTO, "Fail to complete ubring handshake from %s: %s",
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
        ub_transport->_ub_state = UBShmTransport::UB_OFF;
    } else {
        ep->_state = S_ALLOC_SHM;
        ubring::SHM remote_trx_shm = {NULL, remote_msg.len, 0, {0}, (uint8_t)ep->_socket->fd()};
        strncpy(remote_trx_shm.name, remote_msg.shm_name, SHM_MAX_NAME_BUFF_LEN);

        size_t local_shm_len = (size_t)(FLAGS_data_queue_size) * MB_TO_BYTE;
        // server端共享内存名称
        ubring::SHM local_trx_shm = {NULL, local_shm_len, 0, {0}, (uint8_t)ep->_socket->fd()};
        char clientName[SHM_MAX_NAME_BUFF_LEN];
        strncpy(clientName, remote_msg.shm_name, SHM_MAX_NAME_BUFF_LEN);

        char *clientIpPort = strrchr(clientName, '_');
        if (clientIpPort != NULL) {
            *clientIpPort = '\0';
        }
        int result = snprintf(local_trx_shm.name, SHM_MAX_NAME_BUFF_LEN, "%s_%s",
            clientName, SERVER_SHM_NAME_SUFFIX);
        if (UNLIKELY(result < 0)) {
            LOG(WARNING) << "Copy client shared memory name failed, ret=" << result;
            ub_transport->_ub_state = UBShmTransport::UB_OFF;
        }
        if (result >= 0 && ep->AllocateServerResources(&remote_trx_shm, &local_trx_shm) < 0) {
            LOG(WARNING) << "Fail to allocate ub resources, fallback to tcp:"
                         << s->description();
            ub_transport->_ub_state = UBShmTransport::UB_OFF;
        }
    }

    ep->_state = S_HELLO_SEND;
    HelloMessage local_msg;
    local_msg.msg_len = g_ub_hello_msg_len;
    if (ub_transport->_ub_state == UBShmTransport::UB_OFF) {
        local_msg.impl_ver = 0;
        local_msg.hello_ver = 0;
    } else {
        local_msg.hello_ver = g_ub_hello_version;
        local_msg.impl_ver = g_ub_impl_version;
        local_msg.len = (FLAGS_data_queue_size) * MB_TO_BYTE;
        memcpy(local_msg.shm_name, remote_msg.shm_name, SHM_MAX_NAME_BUFF_LEN);
    }
    memcpy(data, MAGIC_STR, MAGIC_STR_LEN);
    local_msg.Serialize((char*)data + MAGIC_STR_LEN);
    if (ep->WriteToFd(data, g_ub_hello_msg_len) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to send Hello Message to client:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete ub handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    ep->_state = S_ACK_WAIT;
    if (ep->ReadFromFd(data, ACK_MSG_LEN) < 0) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to read ack message from client:" << s->description();
        s->SetFailed(saved_errno, "Fail to complete ubring handshake from %s: %s",
                s->description().c_str(), berror(saved_errno));
        ep->_state = FAILED;
        return NULL;
    }

    uint32_t* tmp = (uint32_t*)data;
    uint32_t flags = butil::NetToHost32(*tmp);
    if (flags & ACK_MSG_UB_OK) {
        if (ub_transport->_ub_state == UBShmTransport::UB_OFF) {
            LOG(WARNING) << "Fail to parse Hello Message length from client:"
                         << s->description();
            s->SetFailed(EPROTO, "Fail to complete ub handshake from %s: %s",
                    s->description().c_str(), berror(EPROTO));
            ep->_state = FAILED;
            return NULL;
        } else {
            ub_transport->_ub_state = UBShmTransport::UB_ON;
            ep->_state = ESTABLISHED;
            LOG_IF(INFO, FLAGS_ub_trace_verbose) 
                << "Server handshake ends (use ubring) on " << s->description();
        }
    } else {
        ub_transport->_ub_state = UBShmTransport::UB_OFF;
        ep->_state = FALLBACK_TCP;
        LOG_IF(INFO, FLAGS_ub_trace_verbose) 
            << "Server handshake ends (use tcp) on " << s->description();
    }
    ep->TryReadOnTcp();

    return NULL;
}

bool UBShmEndpoint::IsWritable() const {
    if (BAIDU_UNLIKELY(g_skip_ub_init)) {
        // Just for UT
        return false;
    }
    auto ret = _ub_ring->IsUbrTrxWriteable(EPOLLET);
    if (ret == 0) {
        return true;
    }
    return false;
}

ssize_t UBShmEndpoint::CutFromIOBufList(butil::IOBuf** from, size_t ndata) {
    if (BAIDU_UNLIKELY(g_skip_ub_init)) {
        // Just for UT
        errno = EAGAIN;
        return -1;
    }
    if (BAIDU_UNLIKELY(ndata == 0)) {
        return 0;
    }
    struct iovec vec[IOBUF_IOV_MAX];
    size_t nvec = 0;
    for (size_t i = 0; i < ndata; ++i) {
        const butil::IOBuf* p = from[i];
        const size_t nref = p->backing_block_num();
        for (size_t j = 0; j < nref && nvec < IOBUF_IOV_MAX; ++j, ++nvec) {
            butil::StringPiece sp = p->backing_block(j);
            vec[nvec].iov_base = const_cast<char*>(sp.data());
            vec[nvec].iov_len = sp.size();
        }
    }

    ssize_t nw = 0;
    nw = _ub_ring->UbrTrxWritev(vec, nvec);
    if (UNLIKELY(nw == -1)) {
        LOG(ERROR) << "Non-blocking send msg in failed, connection has been closed.";
        errno = EPIPE;
    } else if (UNLIKELY(nw == UBRING_RETRY)) {
        errno = EAGAIN;
        nw = -1;
    }
    if (nw <= 0) {
        return nw;
    }
    size_t npop_all = nw;
    for (size_t i = 0; i < ndata; ++i) {
        npop_all -= from[i]->pop_front(npop_all);
        if (npop_all == 0) {
            break;
        }
    }
    return nw;
}

int UBShmEndpoint::AllocateClientResources(ubring::SHM* local_trx_shm, const char* shm_name) {
    if (BAIDU_UNLIKELY(g_skip_ub_init)) {
        // For UT
        return 0;
    }

    CHECK(_ub_ring == NULL);
    // TODO: Pooling management
    _ub_ring = new UBRing();

    SocketOptions options;
    options.user = this;
    options.keytable_pool = _socket->_keytable_pool;
    if (Socket::Create(options, &_cq_sid) < 0) {
        PLOG(WARNING) << "Fail to create socket for cq";
        return -1;
    }
    int ret = _ub_ring->UbrAllocateLocalShm(local_trx_shm, shm_name);
    if (ret != 0) {
        return ret;
    }
    PollerRegisterEvent(CqSidOp::ADD, EPOLLIN);
    return 0;
}

int UBShmEndpoint::AllocateServerResources(ubring::SHM* remote_trx_shm, ubring::SHM* local_trx_shm) {
    if (BAIDU_UNLIKELY(g_skip_ub_init)) {
        // For UT
        return 0;
    }

    CHECK(_ub_ring == NULL);
    // TODO: Pooling management
    _ub_ring = new UBRing();

    SocketOptions options;
    options.user = this;
    options.keytable_pool = _socket->_keytable_pool;
    if (Socket::Create(options, &_cq_sid) < 0) {
        PLOG(WARNING) << "Fail to create socket for cq";
        return -1;
    }
    int ret = _ub_ring->UbrAllocateServerShm(remote_trx_shm, local_trx_shm);
    if (ret != 0) {
        return ret;
    }
    // TODO mwj 是否应该在连接之后再进行轮询?
    PollerRegisterEvent(CqSidOp::ADD, EPOLLIN);
    return ret;
}

void UBShmEndpoint::DeallocateResources() {
    if (!_ub_ring) {
        return;
    }
    PollerRegisterEvent(CqSidOp::REMOVE);
    _ub_ring->UbrTrxClose();
    if (INVALID_SOCKET_ID != _cq_sid) {
        SocketUniquePtr s;
        if (Socket::Address(_cq_sid, &s) == 0) {
            s->_user = NULL;
            s->_fd = -1;
            s->SetFailed();
        }
    }
}

void UBShmEndpoint::PollIn(UBShmEndpoint* ep, uint32_t epEvent) {
    SocketUniquePtr s;
    if (Socket::Address(ep->_socket->id(), &s) < 0) {
        return;
    }
    auto* ub_transport = static_cast<UBShmTransport*>(s->_transport.get());
    CHECK(ep == ub_transport->_ub_ep);

    InputMessageClosure last_msg;
    while (true) {
        int ret = ep->_ub_ring->IsUbrTrxReadable(epEvent);
        if (ret < 0) {
            return;
        }

        bool read_eof = false;
        while (!read_eof) {
            const int64_t received_us = butil::cpuwide_time_us();
            const int64_t base_realtime = butil::gettimeofday_us() - received_us;

            size_t once_read = s->_avg_msg_size * 16;
            if (once_read < MIN_ONCE_READ) {
                once_read = MIN_ONCE_READ;
            } else if (once_read > MAX_ONCE_READ) {
                once_read = MAX_ONCE_READ;
            }

            const ssize_t nr = s->_read_buf.pappend_from_ub_ring(ep->_ub_ring, once_read);
            if (nr <= 0) {
                if (0 == nr) {
                    // Set `read_eof' flag and proceed to feed EOF into `Protocol'
                    // (implied by m->_read_buf.empty), which may produce a new
                    // `InputMessageBase' under some protocols such as HTTP
                    LOG_IF(WARNING, FLAGS_log_connection_close) << *s << " was closed by remote side";
                    read_eof = true;
                } else if (errno != EAGAIN) {
                    if (errno == EINTR) {
                        continue;
                    }
                    const int saved_errno = errno;
                    PLOG(WARNING) << "Fail to read from " << *s;
                    s->SetFailed(saved_errno, "Fail to read from %s: %s",
                                 s->description().c_str(), berror(saved_errno));
                    return;
                } else {
                    return;
                }
            }

            InputMessenger* messenger = static_cast<InputMessenger*>(s->user());
            if (messenger->ProcessNewMessage(s.get(), nr, read_eof, received_us,
                                             base_realtime, last_msg) < 0) {
                return;
            } 
        }

        if (read_eof) {
            s->SetEOF();
        }
    }
}

void UBShmEndpoint::PollOut(UBShmEndpoint* ep, uint32_t epEvent) {
    SocketUniquePtr s;
    if (Socket::Address(ep->_socket->id(), &s) < 0) {
        return;
    }
    auto* ub_transport = static_cast<UBShmTransport*>(s->_transport.get());
    CHECK(ep == ub_transport->_ub_ep);
    if (ep->IsWritable()) {
        ep->_socket->WakeAsEpollOut();
    }
    
}

int UBShmEndpoint::GlobalInitialize() {
    g_ubring_resource_mutex = new butil::Mutex;
    _poller_groups = std::vector<PollerGroup>(FLAGS_task_group_ntags);
    return 0;
}

void UBShmEndpoint::GlobalRelease() {
    for (int i = 0; i < FLAGS_task_group_ntags; ++i) {
        PollingModeRelease(i);
    }
}

std::vector<UBShmEndpoint::PollerGroup> UBShmEndpoint::_poller_groups;

int UBShmEndpoint::PollingModeInitialize(bthread_tag_t tag,
                                        std::function<void()> callback,
                                        std::function<void()> init_fn,
                                        std::function<void()> release_fn) {
    auto& group = _poller_groups[tag];
    auto& pollers = group.pollers;
    auto& running = group.running;
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        return 0;
    }
    struct FnArgs {
        Poller* poller;
        std::atomic<bool>* running;
    };
    auto fn = [](void* p) -> void* {
        std::unique_ptr<FnArgs> args(static_cast<FnArgs*>(p));
        auto poller = args->poller;
        auto running = args->running;
        std::unordered_set<CqSidOp, CqSidOpHash, CqSidOpEqual> cq_sids;
        CqSidOp op;

        if (poller->init_fn) {
            poller->init_fn();
        }
        while (running->load(std::memory_order_relaxed)) {
            while (poller->op_queue.Dequeue(op)) {
                if (op.type == CqSidOp::ADD) {
                    cq_sids.emplace(op);
                } else if (op.type == CqSidOp::REMOVE) {
                    cq_sids.erase(op);
                    
                } else if (op.type == CqSidOp::MOD) {
                    cq_sids.erase(op);
                    cq_sids.emplace(op);
                }
            }
            for (auto cq : cq_sids) {
                SocketUniquePtr s;
                if (Socket::Address(cq.sid, &s) < 0) {
                    continue;
                }
                UBShmEndpoint* ep = static_cast<UBShmEndpoint*>(s->user());
                if (!ep) {
                    continue;
                }

                if (cq.event & EPOLLIN) {
                    PollIn(ep, cq.event);
                }

                if (cq.event & EPOLLOUT) {
                    PollOut(ep, cq.event);
                }
            }
            if (poller->callback) {
                poller->callback();
            }
            if (FLAGS_ub_poller_yield) {
                bthread_yield();
            }
        }

        if (poller->release_fn) {
            poller->release_fn();
        }

        return nullptr;
    };
    for (int i = 0; i < FLAGS_ub_poller_num; ++i) {
        auto args = new FnArgs{&pollers[i], &running};
        auto attr = FLAGS_ub_disable_bthread ? BTHREAD_ATTR_PTHREAD
                                               : BTHREAD_ATTR_NORMAL;
        attr.tag = tag;
        bthread_attr_set_name(&attr, "UBPolling");
        pollers[i].callback = callback;
        pollers[i].init_fn = init_fn; 
        pollers[i].release_fn = release_fn;
        auto rc = bthread_start_background(&pollers[i].tid, &attr, fn, args);
        if (rc != 0) {
            LOG(ERROR) << "Fail to start ubring polling bthread";
            return -1;
        }
    }
    return 0;
}

void UBShmEndpoint::PollingModeRelease(bthread_tag_t tag) {
    auto& group = _poller_groups[tag];
    auto& pollers = group.pollers;
    auto& running = group.running;
    running.store(false, std::memory_order_relaxed);
    for (int i = 0; i < FLAGS_ub_poller_num; ++i) {
        bthread_join(pollers[i].tid, NULL);
    }
}

void UBShmEndpoint::PollerRegisterEvent(CqSidOp::OpType op, uint32_t events) {
    auto index = butil::fmix32(_cq_sid) % FLAGS_ub_poller_num;
    auto& group = _poller_groups[bthread_self_tag()];
    auto& pollers = group.pollers;
    auto& poller = pollers[index];
    if (INVALID_SOCKET_ID != _cq_sid) {
        poller.op_queue.Enqueue(CqSidOp{_cq_sid, events, op});
    }
}

}  // namespace ubring
}  // namespace brpc

#endif  // if BRPC_WITH_UBRING
