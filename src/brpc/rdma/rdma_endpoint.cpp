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
#include "brpc/rdma_transport.h"
#include "brpc/rdma/rdma_handshake.h"

DECLARE_int32(task_group_ntags);

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
extern int (*IbvQueryEce)(ibv_qp*, ibv_ece*);
extern int (*IbvSetEce)(ibv_qp*, ibv_ece*);
extern bool g_skip_rdma_init;

DEFINE_int32(rdma_sq_size, 128, "SQ size for RDMA");
DEFINE_int32(rdma_rq_size, 128, "RQ size for RDMA");
DEFINE_bool(rdma_recv_zerocopy, true, "Enable zerocopy for receive side");
DEFINE_int32(rdma_zerocopy_min_size, 512, "The minimal size for receive zerocopy");
DEFINE_int32(rdma_cqe_poll_once, 32, "The maximum of cqe number polled once.");
DEFINE_int32(rdma_prepared_qp_size, 128, "SQ and RQ size for prepared QP.");
DEFINE_int32(rdma_prepared_qp_cnt, 1024, "Initial count of prepared QP.");
DEFINE_bool(rdma_trace_verbose, false, "Print log message verbosely");
BRPC_VALIDATE_GFLAG(rdma_trace_verbose, brpc::PassValidate);
DEFINE_bool(rdma_use_polling, false, "Use polling mode for RDMA.");
DEFINE_int32(rdma_poller_num, 1, "Poller number in RDMA polling mode.");
DEFINE_bool(rdma_poller_yield, false, "Yield thread in RDMA polling mode.");
DEFINE_bool(rdma_disable_bthread, false, "Disable bthread in RDMA");
DEFINE_bool(rdma_ece, false, "Open ece in RDMA, should use this feature when rdma nics are from the same merchant.");

static const size_t IOBUF_BLOCK_HEADER_LEN = 32; // implementation-dependent

// DO NOT change this value unless you know the safe value!!!
// This is the number of reserved WRs in SQ/RQ for pure ACK.
extern const size_t RESERVED_WR_NUM = 3;

// The local recv block size, set during GlobalInitialize.
uint32_t g_rdma_recv_block_size = 0;

// static const uint32_t MAX_INLINE_DATA = 64;
static const uint8_t MAX_HOP_LIMIT = 16;
static const uint8_t TIMEOUT = 14;
static const uint8_t RETRY_CNT = 7;
extern const uint16_t MIN_QP_SIZE = 16;
static const uint16_t MAX_QP_SIZE = 4096;
extern const uint16_t MIN_BLOCK_SIZE = 1024;

// ACK message wire format (shared by all protocol versions): a single
// 4B big-endian flags word; bit 0 (HELLO_ACK_RDMA_OK) indicates the
// sender wants to use RDMA. The state machines in
// ProcessHandshakeAt{Client,Server} inline the corresponding 4B
// send/recv directly using ReadFromFd / WriteToFd.
static const size_t HELLO_ACK_LEN = 4;
static const uint32_t HELLO_ACK_RDMA_OK = 0x1;

static butil::Mutex* g_rdma_resource_mutex = NULL;
static RdmaResource* g_rdma_resource_list = NULL;

RdmaResource::~RdmaResource() {
    if (NULL != qp) {
        IbvDestroyQp(qp);
    }
    if (NULL != polling_cq) {
        IbvDestroyCq(polling_cq);
    }
    if (NULL != send_cq) {
        IbvDestroyCq(send_cq);
    }
    if (NULL != recv_cq) {
        IbvDestroyCq(recv_cq);
    }
    if (NULL != comp_channel) {
        IbvDestroyCompChannel(comp_channel);
    }
}

RdmaEndpoint::RdmaEndpoint(Socket* s)
    : _socket(s)
    , _state(UNINIT)
    , _handshake_version(0)
    , _resource(NULL)
    , _send_cq_events(0)
    , _recv_cq_events(0)
    , _cq_sid(INVALID_SOCKET_ID)
    , _sq_size(FLAGS_rdma_sq_size)
    , _rq_size(FLAGS_rdma_rq_size)
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
    , _sq_imm_window_size(0)
    , _remote_rq_window_size(0)
    , _sq_window_size(0)
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

    _state.store(UNINIT, butil::memory_order_relaxed);
    _resource = NULL;
    _send_cq_events = 0;
    _recv_cq_events = 0;
    _cq_sid = INVALID_SOCKET_ID;
    _sbuf.clear();
    _rbuf.clear();
    _rbuf_data.clear();
    _remote_recv_block_size = 0;
    _accumulated_ack = 0;
    _unsolicited = 0;
    _unsolicited_bytes = 0;
    _sq_current = 0;
    _sq_unsignaled = 0;
    _sq_sent = 0;
    _rq_received = 0;
    _local_window_capacity = 0;
    _remote_window_capacity = 0;
    _sq_imm_window_size = 0;
    _remote_rq_window_size.store(0, butil::memory_order_relaxed);
    _sq_window_size.store(0, butil::memory_order_relaxed);
    _new_rq_wrs.store(0, butil::memory_order_relaxed);
}

void RdmaConnect::StartConnect(const Socket* socket,
                               void (*done)(int err, void* data),
                               void* data) {
    auto* rdma_transport = static_cast<RdmaTransport*>(socket->_transport.get());
    CHECK(rdma_transport->_rdma_ep != NULL);
    SocketUniquePtr s;
    if (Socket::Address(socket->id(), &s) != 0) {
        return;
    }
    if (!IsRdmaAvailable()) {
        rdma_transport->_rdma_ep->_state.store(RdmaEndpoint::FALLBACK_TCP,
                                               butil::memory_order_relaxed);
        rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
        done(0, data);
        return;
    }
    _done = done;
    _data = data;
    bthread_t tid;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    bthread_attr_set_name(&attr, "RdmaProcessHandshakeAtClient");
    if (bthread_start_background(&tid, &attr,
                                 RdmaEndpoint::ProcessHandshakeAtClient,
                                 rdma_transport->_rdma_ep) < 0) {
        LOG(FATAL) << "Fail to start handshake bthread";
        Run();
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
    auto* rdma_transport = static_cast<RdmaTransport*>(m->_transport.get());
    RdmaEndpoint* ep = rdma_transport->GetRdmaEp();
    CHECK(ep != NULL);

    int progress = Socket::PROGRESS_INIT;
    while (true) {
        State state = ep->_state.load(butil::memory_order_acquire);
        if (state == UNINIT) {
            if (!m->CreatedByConnect()) {
                if (!IsRdmaAvailable()) {
                    rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
                    ep->_state.store(FALLBACK_TCP, butil::memory_order_relaxed);
                    continue;
                }
                bthread_t tid;
                ep->_state.store(S_HELLO_WAIT, butil::memory_order_relaxed);
                SocketUniquePtr s;
                m->ReAddress(&s);
                bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
                bthread_attr_set_name(&attr, "RdmaProcessHandshakeAtServer");
                if (bthread_start_background(&tid, &attr, ProcessHandshakeAtServer, ep) < 0) {
                    ep->_state.store(UNINIT, butil::memory_order_relaxed);
                    LOG(FATAL) << "Fail to start handshake bthread";
                } else {
                    s.release();
                }
            } else {
                // The connection may be closed or reset before the client
                // starts handshake. This will be handled by client handshake.
                // Ignore the exception here.
            }
        } else if (state < ESTABLISHED) {  // during handshake
            ep->_read_butex->fetch_add(1, butil::memory_order_release);
            bthread::butex_wake(ep->_read_butex);
        } else if (state == FALLBACK_TCP){  // handshake finishes
            InputMessenger::OnNewMessages(m);
            return;
        } else if (state == ESTABLISHED) {
            TryReadOnTcpDuringRdmaEst(ep->_socket);
            return;
        }
        if (!m->MoreReadEvents(&progress)) {
            break;
        }
    }
}

static const int WAIT_TIMEOUT_MS = 50;

// Drive an EAGAIN-aware read loop to completion (exactly `len` bytes).
// `read_once(offset, remaining)` performs ONE underlying read attempt:
//   - returns > 0  : number of bytes consumed (added to running total);
//   - returns = 0  : end-of-stream (the loop fails with EEOF);
//   - returns < 0  : errno set; EAGAIN is handled here via butex_wait,
//                   any other errno bubbles up.
// `offset` is bytes already received in THIS call (initially 0); the
// callable uses it to choose the next write target (e.g. `(char*)buf
// + offset`). Callables that don't need offset (e.g. IOPortal append)
// can ignore it.
//
// Centralizes the EAGAIN/butex/EOF loop so the two ReadFromFd
// overloads below stay one-liners; any future read source (memory-
// mapped, scatter-vector, etc.) can plug in by passing its own
// `read_once`.
template <class ReadOnce>
static int ReadFromFdLoop(butil::atomic<int>* read_butex,
                          size_t len, ReadOnce&& read_once) {
    size_t received = 0;
    while (received < len) {
        const int expected_val = read_butex->load(butil::memory_order_acquire);
        const timespec duetime = butil::milliseconds_from_now(WAIT_TIMEOUT_MS);
        ssize_t nr = read_once(received, len - received);
        if (nr < 0) {
            if (errno == EAGAIN) {
                if (bthread::butex_wait(read_butex, expected_val, &duetime) < 0) {
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
    }
    return 0;
}

int RdmaEndpoint::ReadFromFd(void* data, size_t len) {
    CHECK(data != NULL);
    const int fd = _socket->fd();
    return ReadFromFdLoop(_read_butex, len,
        [data, fd](size_t offset, size_t remaining) {
            return read(fd, (uint8_t*)data + offset, remaining);
        });
}

int RdmaEndpoint::ReadFromFd(butil::IOPortal* data, size_t len) {
    CHECK(data != NULL);
    const int fd = _socket->fd();
    return ReadFromFdLoop(_read_butex, len,
        [data, fd](size_t /*offset*/, size_t remaining) {
            return data->append_from_file_descriptor(fd, remaining);
        });
}

// Drive an EAGAIN-aware write loop to completion (exactly `len` bytes).
//
// `write_once(offset, remaining)` performs ONE underlying write attempt:
//   - returns >= 0 : number of bytes consumed (added to running total);
//   - returns < 0  : errno set; EAGAIN triggers `wait_writable(duetime)`,
//                   any other errno bubbles up.
// `offset` is bytes already written in THIS call (initially 0); the
// callable uses it to choose the next read source (e.g. `(char*)buf
// + offset`). Callables that drain a self-tracking sink (e.g.
// IOBuf::cut_into_file_descriptor) can ignore both args.
//
// `wait_writable(duetime)` is invoked on EAGAIN to park until the fd
// becomes writable again. It returns 0 on wake-up (or ETIMEDOUT),
// non-zero on hard failure.
template <class WriteOnce, class WaitWritable>
static int WriteToFdLoop(size_t len, WriteOnce&& write_once, WaitWritable&& wait_writable) {
    size_t written = 0;
    while (written < len) {
        const timespec duetime = butil::milliseconds_from_now(WAIT_TIMEOUT_MS);
        ssize_t nw = write_once(written, len - written);
        if (nw >= 0) {
            written += nw;
            continue;
        }

        if (errno != EAGAIN) {
            return -1;
        }
        if (!wait_writable(&duetime)) {
            return -1;
        }
    }
    return 0;
}

int RdmaEndpoint::WriteToFd(void* data, size_t len) {
    CHECK(data != NULL);
    Socket* s = _socket;
    const int fd = s->fd();
    return WriteToFdLoop(len,
        [data, fd](size_t offset, size_t remaining) {
            return write(fd, (uint8_t*)data + offset, remaining);
        },
        [s, fd](const timespec* duetime) {
            return s->WaitEpollOut(fd, true, duetime) == 0 || errno == ETIMEDOUT;
        });
}

int RdmaEndpoint::WriteToFd(butil::IOBuf* data) {
    CHECK(data != NULL);
    Socket* s = _socket;
    const int fd = s->fd();
    return WriteToFdLoop(data->size(),
        [data, fd](size_t /*offset*/, size_t /*remaining*/) {
            return data->cut_into_file_descriptor(fd);
        },
        [s, fd](const timespec* duetime) {
            return s->WaitEpollOut(fd, true, duetime) == 0 || errno == ETIMEDOUT;
        });
}

inline void RdmaEndpoint::TryReadOnTcp() {
    if (_socket->_nevent.fetch_add(1, butil::memory_order_acq_rel) == 0) {
        State state = _state.load(butil::memory_order_acquire);
        if (state == FALLBACK_TCP) {
            InputMessenger::OnNewMessages(_socket);
        } else if (state == ESTABLISHED) {
            TryReadOnTcpDuringRdmaEst(_socket);
        }
    }
}

void RdmaEndpoint::ApplyRemoteHello(const ParsedHello& remote) {
    _remote_recv_block_size = remote.block_size;
    _local_window_capacity =
        std::min(_sq_size, remote.rq_size) - RESERVED_WR_NUM;
    _remote_window_capacity =
        std::min(_rq_size, remote.sq_size) - RESERVED_WR_NUM;
    _sq_imm_window_size = RESERVED_WR_NUM;
    _remote_rq_window_size.store(
        _local_window_capacity, butil::memory_order_relaxed);
    _sq_window_size.store(
        _local_window_capacity, butil::memory_order_relaxed);
}

// Client-side handshake entry: the state machine.
//
//   C_ALLOC_QPCQ
//     |
//     v
//   C_HELLO_SEND  (hs->SendLocalHello)
//     |
//     v
//   C_HELLO_WAIT  (hs->ReceiveAndParseRemoteHello)
//     |
//     v
//   [negotiation: ApplyRemoteHello + C_BRINGUP_QP]
//     |
//     v
//   C_ACK_SEND
//     |
//     v
//   ESTABLISHED / FALLBACK_TCP
void* RdmaEndpoint::ProcessHandshakeAtClient(void* arg) {
    auto ep = static_cast<RdmaEndpoint*>(arg);
    SocketUniquePtr s(ep->_socket);
    RdmaConnect::RunGuard rg((RdmaConnect*)s->_app_connect.get());
    auto rdma_transport = static_cast<RdmaTransport*>(s->_transport.get());

    LOG_IF(INFO, FLAGS_rdma_trace_verbose)
        << "Start handshake on " << s->description();

    std::unique_ptr<RdmaHandshake> handshake = CreateClientHandshake(ep);
    CHECK(handshake != NULL);
    ep->_handshake_version = handshake->ProtocolVersion();

    // First initialize CQ and QP resources.
    ep->_state.store(C_ALLOC_QPCQ, butil::memory_order_relaxed);
    if (ep->AllocateResources() < 0) {
        LOG(WARNING) << "Fallback to tcp:" << s->description();
        rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
        ep->_state.store(FALLBACK_TCP, butil::memory_order_release);
        return NULL;
    }

    // Send hello message to server
    ep->_state.store(C_HELLO_SEND, butil::memory_order_relaxed);
    if (handshake->SendLocalHello() < 0) {
        int saved_errno = errno;
        PLOG(WARNING) << "Fail to send hello message to server:"
                      << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                     s->description().c_str(), berror(saved_errno));
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        return NULL;
    }

    // Receive and parse remote hello.
    ep->_state.store(C_HELLO_WAIT, butil::memory_order_relaxed);
    ParsedHello remote{};
    bool negotiated = false;
    if (handshake->ReceiveAndParseRemoteHello(&remote, &negotiated) < 0) {
        int saved_errno = errno;
        PLOG(WARNING) << "Fail to receive hello from server:"
                      << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                     s->description().c_str(), berror(saved_errno));
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        return NULL;
    }

    if (!negotiated) {
        LOG(WARNING) << "Fail to negotiate with server, fallback to tcp:"
                     << s->description();
        rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
    } else {
        ep->ApplyRemoteHello(remote);
        ep->_state.store(C_BRINGUP_QP, butil::memory_order_relaxed);
        if (ep->BringUpQp(remote.lid, remote.gid, remote.qp_num) < 0) {
            LOG(WARNING) << "Fail to bringup QP, fallback to tcp:"
                         << s->description();
            rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
        } else {
            rdma_transport->_rdma_state = RdmaTransport::RDMA_ON;
        }
    }

    // Send ACK message to server
    ep->_state.store(C_ACK_SEND, butil::memory_order_relaxed);
    bool rdma_on = rdma_transport->_rdma_state == RdmaTransport::RDMA_ON;
    uint32_t flags = rdma_on ? HELLO_ACK_RDMA_OK : 0;
    uint32_t flags_be = butil::HostToNet32(flags);
    if (ep->WriteToFd(&flags_be, HELLO_ACK_LEN) < 0) {
        int saved_errno = errno;
        PLOG(WARNING) << "Fail to send Ack Message to server:"
                      << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                     s->description().c_str(), berror(saved_errno));
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        return NULL;
    }

    if (rdma_transport->_rdma_state == RdmaTransport::RDMA_ON) {
        ep->_state.store(ESTABLISHED, butil::memory_order_release);
        LOG_IF(INFO, FLAGS_rdma_trace_verbose)
            << "Client handshake ends (use rdma v" << ep->_handshake_version
            << ") on " << s->description();
    } else {
        ep->_state.store(FALLBACK_TCP, butil::memory_order_release);
        LOG_IF(INFO, FLAGS_rdma_trace_verbose)
            << "Client handshake ends (use tcp) on " << s->description();
    }

    errno = 0;

    return NULL;
}

// Server-side handshake entry: the state machine.
//
//   S_HELLO_WAIT  (read magic + dispatch + hs->ReceiveAndParseRemoteHello)
//     |
//     v
//   [negotiation: ApplyRemoteHello + S_ALLOC_QPCQ + S_BRINGUP_QP]
//     |
//     v
//   S_HELLO_SEND  (hs->SendLocalHello)
//     |
//     v
//   S_ACK_WAIT
//     |
//     v
//   ESTABLISHED / FALLBACK_TCP
void* RdmaEndpoint::ProcessHandshakeAtServer(void* arg) {
    auto ep = static_cast<RdmaEndpoint*>(arg);
    SocketUniquePtr s(ep->_socket);
    auto rdma_transport = static_cast<RdmaTransport*>(s->_transport.get());

    LOG_IF(INFO, FLAGS_rdma_trace_verbose)
        << "Start handshake on " << s->description();

    ep->_state.store(S_HELLO_WAIT, butil::memory_order_relaxed);
    uint8_t magic[MAGIC_STR_LEN];
    if (ep->ReadFromFd(magic, MAGIC_STR_LEN) < 0) {
        int saved_errno = errno;
        PLOG(WARNING) << "Fail to read Hello Message from client:"
                      << s->description() << " " << s->_remote_side;
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                     s->description().c_str(), berror(saved_errno));
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        return NULL;
    }

    // Dispatch on magic, or fall back to TCP
    std::unique_ptr<RdmaHandshake> handshake = CreateServerHandshakeByMagic(ep, magic);
    if (!handshake) {
        LOG_IF(INFO, FLAGS_rdma_trace_verbose)
            << "It seems that the client does not use RDMA, fallback to TCP:"
            << s->description();
        // We need to copy data read back to _socket->_read_buf.
        s->_read_buf.append(magic, MAGIC_STR_LEN);
        rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
        // Use release memory order to publish the magic bytes appended
        // above to whoever reads `_state == FALLBACK_TCP` (the event
        // thread in OnNewDataFromTcp).
        ep->_state.store(FALLBACK_TCP, butil::memory_order_release);
        ep->TryReadOnTcp();
        return NULL;
    }
    ep->_handshake_version = handshake->ProtocolVersion();

    // Magic was already consumed above; the subclass MUST NOT re-read it.
    ParsedHello remote{};
    bool negotiated = false;
    if (handshake->ReceiveAndParseRemoteHello(&remote, &negotiated) < 0) {
        int saved_errno = errno;
        PLOG(WARNING) << "Fail to receive hello from client:"
                      << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                     s->description().c_str(), berror(saved_errno));
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        return NULL;
    }

    if (!negotiated) {
        LOG(WARNING) << "Fail to negotiate with client, fallback to tcp:"
                     << s->description();
        rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
    } else {
        ep->ApplyRemoteHello(remote);
        ep->_state.store(S_ALLOC_QPCQ, butil::memory_order_relaxed);
        if (ep->AllocateResources() < 0) {
            LOG(WARNING) << "Fail to allocate rdma resources, fallback to tcp:"
                         << s->description();
            rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
        } else {
            ep->_state.store(S_BRINGUP_QP, butil::memory_order_relaxed);
            if (ep->BringUpQp(remote.lid, remote.gid, remote.qp_num) < 0) {
                LOG(WARNING) << "Fail to bringup QP, fallback to tcp:"
                             << s->description();
                rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
            }
        }
    }

    ep->_state.store(S_HELLO_SEND, butil::memory_order_relaxed);
    if (handshake->SendLocalHello() < 0) {
        int saved_errno = errno;
        PLOG(WARNING) << "Fail to send Hello Message to client:"
                      << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                     s->description().c_str(), berror(saved_errno));
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        return NULL;
    }

    ep->_state.store(S_ACK_WAIT, butil::memory_order_relaxed);
    uint32_t flags_be = 0;
    if (ep->ReadFromFd(&flags_be, HELLO_ACK_LEN) < 0) {
        int saved_errno = errno;
        PLOG(WARNING) << "Fail to read ack message from client:"
                      << s->description();
        s->SetFailed(saved_errno, "Fail to complete rdma handshake from %s: %s",
                     s->description().c_str(), berror(saved_errno));
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        return NULL;
    }
    uint32_t flags = butil::NetToHost32(flags_be);
    bool client_ack_ok = (flags & HELLO_ACK_RDMA_OK) != 0;
    if (client_ack_ok) {
        if (rdma_transport->_rdma_state == RdmaTransport::RDMA_OFF) {
            // Client asked for RDMA but we are falling back: protocol
            // breakdown, abort the connection so the client sees a
            // clean error rather than a half-up RDMA channel.
            LOG(WARNING) << "Client wants RDMA in ACK but server is in "
                         << "RDMA_OFF state: " << s->description();
            s->SetFailed(EPROTO, "Fail to complete rdma handshake from %s: %s",
                         s->description().c_str(), berror(EPROTO));
            ep->_state.store(FAILED, butil::memory_order_relaxed);
            return NULL;
        }
        rdma_transport->_rdma_state = RdmaTransport::RDMA_ON;
        ep->_state.store(ESTABLISHED, butil::memory_order_release);
        LOG_IF(INFO, FLAGS_rdma_trace_verbose)
            << "Server handshake ends (use rdma v" << ep->_handshake_version
            << ") on " << s->description();
    } else {
        rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
        ep->_state.store(FALLBACK_TCP, butil::memory_order_release);
        LOG_IF(INFO, FLAGS_rdma_trace_verbose)
            << "Server handshake ends (use tcp) on " << s->description();
    }

    ep->TryReadOnTcp();

    return NULL;
}

bool RdmaEndpoint::IsWritable() const {
    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        // Just for UT
        return false;
    }

    return _remote_rq_window_size.load(butil::memory_order_relaxed) > 0 &&
           _sq_window_size.load(butil::memory_order_relaxed) > 0;
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
                                      butil::IOBuf* to, size_t max_sge,
                                      size_t max_len) {
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
    uint32_t remote_rq_window_size =
        _remote_rq_window_size.load(butil::memory_order_relaxed);
    uint32_t sq_window_size =
        _sq_window_size.load(butil::memory_order_relaxed);
    ibv_send_wr wr;
    int max_sge = GetRdmaMaxSge();
    ibv_sge sglist[max_sge];
    while (current < ndata) {
        if (remote_rq_window_size == 0 || sq_window_size == 0) {
            // There is no space left in SQ or remote RQ.
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
            if (data->empty()) {
                // The current IOBuf is empty, find next one
                ++current;
                if (current == ndata) {
                    break;
                }
                data = (RdmaIOBuf*)from[current];
                continue;
            }

            ssize_t len = data->cut_into_sglist_and_iobuf(
                sglist, &sge_index, to, max_sge, _remote_recv_block_size - this_len);
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
        if (remote_rq_window_size == 1 || sq_window_size == 1 || current + 1 >= ndata) {
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
            wr.wr_id = _sq_unsignaled;
            _sq_unsignaled = 0;
        }

        ibv_send_wr* bad = NULL;
        int err = ibv_post_send(_resource->qp, &wr, &bad);
        if (err != 0) {
            // We use other way to guarantee the Send Queue is not full.
            // So we just consider this error as an unrecoverable error.
            std::ostringstream oss;
            DebugInfo(oss, ", ");
            LOG(WARNING) << "Fail to ibv_post_send: " << berror(err) << " " << oss.str();
            errno = err;
            return -1;
        }

        ++_sq_current;
        if (_sq_current == _sq_size - RESERVED_WR_NUM) {
            _sq_current = 0;
        }

        // Update `_remote_rq_window_size' and `_sq_window_size'. Note that
        // `_remote_rq_window_size' and `_sq_window_size' will never be negative.
        // Because there is at most one thread can enter this function for each
        // Socket, and the other thread of HandleCompletion can only add these
        // counters.
        remote_rq_window_size =
            _remote_rq_window_size.fetch_sub(1, butil::memory_order_relaxed) - 1;
        sq_window_size = _sq_window_size.fetch_sub(1, butil::memory_order_relaxed) - 1;
    }

    return total_len;
}

int RdmaEndpoint::SendAck(int num) {
    if (_new_rq_wrs.fetch_add(num, butil::memory_order_relaxed) > _remote_window_capacity / 2 &&
        _sq_imm_window_size > 0) {
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
    wr.send_flags |= IBV_SEND_SOLICITED | IBV_SEND_SIGNALED;
    wr.wr_id = 0;

    ibv_send_wr* bad = NULL;
    int err = ibv_post_send(_resource->qp, &wr, &bad);
    if (err != 0) {
        std::ostringstream oss;
        DebugInfo(oss, ", ");
        // We use other way to guarantee the Send Queue is not full.
        // So we just consider this error as an unrecoverable error.
        LOG(WARNING) << "Fail to ibv_post_send: " << berror(err) << " " << oss.str();
        return -1;
    }

    // `_sq_imm_window_size' will never be negative.
    // Because IMM can only be sent if
    // `_sq_imm_window_size` is greater than 0.
    _sq_imm_window_size -= 1;
    return 0;
}

ssize_t RdmaEndpoint::HandleCompletion(ibv_wc& wc) {
    bool zerocopy = FLAGS_rdma_recv_zerocopy;
    switch (wc.opcode) {
    case IBV_WC_SEND: {  // send completion
        if (0 == wc.wr_id) {
            _sq_imm_window_size += 1;
            // If there are any unacknowledged recvs, send an ack.
            SendAck(0);
            return 0;
        }
        // Update SQ window.
        uint16_t wnd_to_update = wc.wr_id;
        for (uint16_t i = 0; i < wnd_to_update; ++i) {
            _sbuf[_sq_sent++].clear();
            if (_sq_sent == _sq_size - RESERVED_WR_NUM) {
                _sq_sent = 0;
            }
        }
        butil::subtle::MemoryBarrier();

        _sq_window_size.fetch_add(wnd_to_update, butil::memory_order_relaxed);
        if (_remote_rq_window_size.load(butil::memory_order_relaxed) >=
            _local_window_capacity / 8) {
            // Do not wake up writing thread right after polling IBV_WC_SEND.
            // Otherwise the writing thread may switch to background too quickly.
            _socket->WakeAsEpollOut();
        }
        return 0;
    }
    case IBV_WC_RECV: {  // recv completion
        // Please note that only the first wc.byte_len bytes is valid
        if (wc.byte_len > 0) {
            if (wc.byte_len < (uint32_t)FLAGS_rdma_zerocopy_min_size) {
                zerocopy = false;
            }
            CHECK_NE(_state.load(butil::memory_order_acquire), FALLBACK_TCP);
            if (zerocopy) {
                _rbuf[_rq_received].cutn(&_socket->_read_buf, wc.byte_len);
            } else {
                // Copy data when the receive data is really small
                _socket->_read_buf.append(_rbuf_data[_rq_received], wc.byte_len);
            }
        }
        if (0 != (wc.wc_flags & IBV_WC_WITH_IMM) && wc.imm_data > 0) {
            // Update window
            uint32_t acks = butil::NetToHost32(wc.imm_data);
            uint32_t wnd_thresh = _local_window_capacity / 8;
            uint32_t remote_rq_window_size =
                _remote_rq_window_size.fetch_add(acks, butil::memory_order_relaxed);
            if (_sq_window_size.load(butil::memory_order_relaxed) > 0 &&
                (remote_rq_window_size >= wnd_thresh || acks >= wnd_thresh)) {
                // Do not wake up writing thread right after _remote_rq_window_size > 0.
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
    int err = ibv_post_recv(_resource->qp, &wr, &bad);
    if (err != 0) {
        LOG(WARNING) << "Fail to ibv_post_recv: " << berror(err);
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
                CHECK_EQ(static_cast<uint32_t>(size), g_rdma_recv_block_size);
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

static ibv_qp* AllocateQp(ibv_cq* send_cq, ibv_cq* recv_cq, uint32_t sq_size, uint32_t rq_size) {
    ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.send_cq = send_cq;
    attr.recv_cq = recv_cq;
    attr.cap.max_send_wr = sq_size;
    attr.cap.max_recv_wr = rq_size;
    attr.cap.max_send_sge = GetRdmaMaxSge();
    attr.cap.max_recv_sge = 1;
    attr.qp_type = IBV_QPT_RC;
    return IbvCreateQp(GetRdmaPd(), &attr);
}

static RdmaResource* AllocateQpCq(uint16_t sq_size, uint16_t rq_size) {
    std::unique_ptr<RdmaResource> resource(new RdmaResource);
    if (!FLAGS_rdma_use_polling) {
        resource->comp_channel = IbvCreateCompChannel(GetRdmaContext());
        if (NULL == resource->comp_channel) {
            PLOG(WARNING) << "Fail to create comp channel for CQ";
            return NULL;
        }

        if (butil::make_close_on_exec(resource->comp_channel->fd) < 0) {
            PLOG(WARNING) << "Fail to set comp channel close-on-exec";
            return NULL;
        }
        if (butil::make_non_blocking(resource->comp_channel->fd) < 0) {
            PLOG(WARNING) << "Fail to set comp channel nonblocking";
            return NULL;
        }

        resource->send_cq = IbvCreateCq(GetRdmaContext(), FLAGS_rdma_prepared_qp_size,
                                        NULL, resource->comp_channel, GetRdmaCompVector());
        if (NULL == resource->send_cq) {
            PLOG(WARNING) << "Fail to create send CQ";
            return NULL;
        }

        resource->recv_cq = IbvCreateCq(GetRdmaContext(), FLAGS_rdma_prepared_qp_size,
                                        NULL, resource->comp_channel, GetRdmaCompVector());
        if (NULL == resource->recv_cq) {
            PLOG(WARNING) << "Fail to create recv CQ";
            return NULL;
        }

        resource->qp = AllocateQp(resource->send_cq, resource->recv_cq, sq_size, rq_size);
        if (NULL == resource->qp) {
            PLOG(WARNING) << "Fail to create QP";
            return NULL;
        }
    } else {
        resource->polling_cq =
            IbvCreateCq(GetRdmaContext(), 2 * FLAGS_rdma_prepared_qp_size, NULL, NULL, 0);
        if (NULL == resource->polling_cq) {
            PLOG(WARNING) << "Fail to create polling CQ";
            return NULL;
        }
        resource->qp = AllocateQp(resource->polling_cq,
                                  resource->polling_cq,
                                  sq_size, rq_size);
        if (NULL == resource->qp) {
            PLOG(WARNING) << "Fail to create QP";
            return NULL;
        }
    }

    return resource.release();
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

    if (!FLAGS_rdma_use_polling) {
        if (0 != ReqNotifyCq(true)) {
            return -1;
        }
        if (0 != ReqNotifyCq(false)) {
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
    } else {
        SocketOptions options;
        options.user = this;
        options.keytable_pool = _socket->_keytable_pool;
        if (Socket::Create(options, &_cq_sid) < 0) {
            PLOG(WARNING) << "Fail to create socket for cq";
            return -1;
        }
        PollerAddCqSid();
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
    int err = IbvModifyQp(_resource->qp, &attr, (ibv_qp_attr_mask)(
                IBV_QP_STATE |
                IBV_QP_PKEY_INDEX |
                IBV_QP_PORT |
                IBV_QP_ACCESS_FLAGS));
    if (err != 0) {
        LOG(WARNING) << "Fail to modify QP from RESET to INIT: " << berror(err);
        return -1;
    }

    if (FLAGS_rdma_ece) {
        struct ibv_ece ece;
        int err = IbvQueryEce(_resource->qp, &ece);
        if (err != 0) {
            LOG(WARNING) << "Fail to IbvQueryEce: " << berror(err);
            return -1;
        }
        // ToDo: should check if remote qp support ece
        err = IbvSetEce(_resource->qp, &ece);
        if (err != 0) {
            LOG(WARNING) << "Fail to IbvSetEce: " << berror(err);
            return -1;
        }
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
    err = IbvModifyQp(_resource->qp, &attr, (ibv_qp_attr_mask)(
                IBV_QP_STATE |
                IBV_QP_PATH_MTU |
                IBV_QP_MIN_RNR_TIMER |
                IBV_QP_AV |
                IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN));
    if (err != 0) {
        LOG(WARNING) << "Fail to modify QP from INIT to RTR: " << berror(err);
        return -1;
    }

    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = TIMEOUT;
    attr.retry_cnt = RETRY_CNT;
    attr.rnr_retry = 0;  // We do not allow rnr error
    attr.sq_psn = 0;
    attr.max_rd_atomic = 0;
    err = IbvModifyQp(_resource->qp, &attr, (ibv_qp_attr_mask)(
                IBV_QP_STATE |
                IBV_QP_RNR_RETRY |
                IBV_QP_RETRY_CNT |
                IBV_QP_TIMEOUT |
                IBV_QP_SQ_PSN |
                IBV_QP_MAX_QP_RD_ATOMIC));
    if (err != 0) {
        LOG(WARNING) << "Fail to modify QP from RTR to RTS: " << berror(err);
        return -1;
    }

    return 0;
}

static void DeallocateCq(ibv_cq* cq) {
    if (NULL == cq) {
        return;
    }

    int err = IbvDestroyCq(cq);
    LOG_IF(WARNING, 0 != err) << "Fail to destroy CQ: " << berror(err);
}

static int DrainCq(ibv_cq* cq) {
    if (NULL == cq) {
        return 0;
    }

    ibv_wc wc;
    int ret;
    do {
        ret = ibv_poll_cq(cq, 1, &wc);
    } while (ret > 0);

    LOG_IF(ERROR, ret < 0) << "drain CQ failed: " << ret;
    return ret;
}

void RdmaEndpoint::DeallocateResources() {
    if (!_resource) {
        return;
    }
    if (FLAGS_rdma_use_polling) {
        PollerRemoveCqSid();
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

    if (NULL != _resource->send_cq) {
        IbvAckCqEvents(_resource->send_cq, _send_cq_events);
    }
    if (NULL != _resource->recv_cq) {
        IbvAckCqEvents(_resource->recv_cq, _recv_cq_events);
    }

    bool remove_consumer = true;
_reclaim:
    if (!move_to_rdma_resource_list) {
        if (NULL != _resource->qp) {
            int err = IbvDestroyQp(_resource->qp);
            LOG_IF(WARNING, 0 != err) << "Fail to destroy QP: " << berror(err);
            _resource->qp = NULL;
        }

        DeallocateCq(_resource->polling_cq);
        DeallocateCq(_resource->send_cq);
        DeallocateCq(_resource->recv_cq);

        if (NULL != _resource->comp_channel) {
            // Destroy send_comp_channel will destroy this fd,
            // so that we should remove it from epoll fd first
            int fd = _resource->comp_channel->fd;
            GetGlobalEventDispatcher(fd, _socket->_io_event.bthread_tag()).RemoveConsumer(fd);
            remove_consumer = false;
            int err = IbvDestroyCompChannel(_resource->comp_channel);
            LOG_IF(WARNING, 0 != err) << "Fail to destroy CQ channel: " << berror(err);

        }

        _resource->polling_cq = NULL;
        _resource->send_cq = NULL;
        _resource->recv_cq = NULL;
        _resource->comp_channel = NULL;
        delete _resource;
        _resource = NULL;
    }

    if (INVALID_SOCKET_ID != _cq_sid) {
        SocketUniquePtr s;
        if (Socket::Address(_cq_sid, &s) == 0) {
            if (remove_consumer) {
                s->_io_event.RemoveConsumer(s->_fd);
            }
            s->_user = NULL;  // Do not release user (this RdmaEndpoint).
            s->_fd = -1;  // Already remove fd from epoll fd.
            s->SetFailed();
        }
    }

    if (move_to_rdma_resource_list) {
        // When a QP is moved to the RESET state, all associated send and
        // receive queues are flushed, meaning any outstanding WRs are effectively
        // abandoned by the hardware.
        //
        // However, the CQ associated with that QP is *not* cleared automatically,
        // meaning that it will still contain entries for WRs that completed before
        // the reset.
        //
        // The application should finish polling the CQ to remove these obsolete
        // entries before reusing the QP.
        int ret = DrainCq(_resource->polling_cq);
        ret += DrainCq(_resource->send_cq);
        ret += DrainCq(_resource->recv_cq);
        if (ret < 0) {
            move_to_rdma_resource_list = false;
            goto _reclaim;
        }

        BAIDU_SCOPED_LOCK(*g_rdma_resource_mutex);
        _resource->next = g_rdma_resource_list;
        g_rdma_resource_list = _resource;
    }
}

static const int MAX_CQ_EVENTS = 128;

int RdmaEndpoint::GetAndAckEvents(SocketUniquePtr& s) {
    void* context = NULL;
    ibv_cq* cq = NULL;
    while (true) {
        if (IbvGetCqEvent(_resource->comp_channel, &cq, &context) != 0) {
            if (errno != EAGAIN) {
                const int saved_errno = errno;
                PLOG(ERROR) << "Fail to get cq event from " << s->description();
                s->SetFailed(saved_errno, "Fail to get cq event from %s: %s",
                             s->description().c_str(), berror(saved_errno));
                return -1;
            }
            break;
        }
        if (cq == _resource->send_cq) {
            ++_send_cq_events;
        } else if (cq == _resource->recv_cq) {
            ++_recv_cq_events;
        } else {
            // Unexpected CQ event that does not belong to
            // this endpoint's send/recv CQs.
            LOG(WARNING) << "Unexpected CQ event from cq=" << cq
                         << " of " << s->description();
            // Acknowledge this single event immediately
            // to avoid leaking unacknowledged events.
            IbvAckCqEvents(cq, 1);
        }
    }
    if (_send_cq_events >= MAX_CQ_EVENTS) {
        IbvAckCqEvents(_resource->send_cq, _send_cq_events);
        _send_cq_events = 0;
    }
    if (_recv_cq_events >= MAX_CQ_EVENTS) {
        IbvAckCqEvents(_resource->recv_cq, _recv_cq_events);
        _recv_cq_events = 0;
    }
    return 0;
}

int RdmaEndpoint::ReqNotifyCq(bool send_cq) {
    errno = ibv_req_notify_cq(
        send_cq ? _resource->send_cq : _resource->recv_cq,
        send_cq ? 0 : 1);
    if (0 != errno) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to arm " << (send_cq ? "send" : "recv")
                      << " CQ comp channel from " << _socket->description();
        _socket->SetFailed(saved_errno, "Fail to arm %s CQ channel from %s: %s",
                           send_cq ? "send" : "recv", _socket->description().c_str(),
                           berror(saved_errno));
        return -1;
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
    auto* rdma_transport = static_cast<RdmaTransport*>(s->_transport.get());
    CHECK(ep == rdma_transport->_rdma_ep);

    bool send = false;
    ibv_cq* cq = ep->_resource->recv_cq;

    if (!FLAGS_rdma_use_polling) {
        if (ep->GetAndAckEvents(s) < 0) {
            return;
        }
    } else {
        // Polling is considered as non-send, so no need to change `send'.
        // Only need to poll polling_cq.
        cq = ep->_resource->polling_cq;
    }

    int progress = Socket::PROGRESS_INIT;
    bool notified = false;
    InputMessageClosure last_msg;
    ibv_wc wc[FLAGS_rdma_cqe_poll_once];
    while (true) {
        int cnt = ibv_poll_cq(cq, FLAGS_rdma_cqe_poll_once, wc);
        if (cnt < 0) {
            const int saved_errno = errno;
            PLOG(WARNING) << "Fail to poll cq: " << s->description();
            s->SetFailed(saved_errno, "Fail to poll cq from %s: %s",
                    s->description().c_str(), berror(saved_errno));
            return;
        }
        if (cnt == 0) {
            if (FLAGS_rdma_use_polling) {
                return;
            }

            if (!send) {
                // It's send cq's turn.
                send = true;
                cq = ep->_resource->send_cq;
                continue;
            }
            // `recv_cq' and `send_cq' have been polled.
            if (!notified) {
                // Since RDMA only provides one shot event, we have to call the
                // notify function every time. Because there is a possibility
                // that the event arrives after the poll but before the notify,
                // we should re-poll the CQ once after the notify to check if
                // there is an available CQE.
                if (0 != ep->ReqNotifyCq(true)) {
                    return;
                }
                if (0 != ep->ReqNotifyCq(false)) {
                    return;
                }
                notified = true;
                continue;
            }
            if (!m->MoreReadEvents(&progress)) {
                break;
            }

            if (0 != ep->GetAndAckEvents(s)) {
                return;
            }

            // Restart polling from `recv_cq'.
            send = false;
            cq = ep->_resource->recv_cq;
            notified = false;
            continue;
        }
        notified = false;

        ssize_t bytes = 0;
        for (int i = 0; i < cnt; ++i) {
            if (s->Failed()) {
                return;
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
        // Send CQE has no messages to process.
        if (send) {
            continue;
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
    switch (_state.load(butil::memory_order_relaxed)) {
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

void RdmaEndpoint::DebugInfo(std::ostream& os, butil::StringPiece connector) const {
    os << "rdma_state=ON"
       << connector << "handshake_state=" << GetStateStr()
       << connector << "handshake_version=" << static_cast<int>(_handshake_version)
       << connector << "rdma_sq_imm_window_size=" << _sq_imm_window_size
       << connector << "rdma_remote_rq_window_size=" << _remote_rq_window_size.load(butil::memory_order_relaxed)
       << connector << "rdma_sq_window_size=" << _sq_window_size.load(butil::memory_order_relaxed)
       << connector << "rdma_local_window_capacity=" << _local_window_capacity
       << connector << "rdma_remote_window_capacity=" << _remote_window_capacity
       << connector << "rdma_sbuf_head=" << _sq_current
       << connector << "rdma_sbuf_tail=" << _sq_sent
       << connector << "rdma_rbuf_head=" << _rq_received
       << connector << "rdma_unacked_rq_wr=" << _new_rq_wrs.load(butil::memory_order_relaxed)
       << connector << "rdma_received_ack=" << _accumulated_ack
       << connector << "rdma_unsolicited_sent=" << _unsolicited
       << connector << "rdma_unsignaled_sq_wr=" << _sq_unsignaled;
}

int RdmaEndpoint::GlobalInitialize() {
    g_rdma_recv_block_size = GetRdmaBlockSize() - IOBUF_BLOCK_HEADER_LEN;
    if (g_rdma_recv_block_size <= 0) {
        LOG(ERROR) << "rdma_recv_block_type incorrect "
                   << "(valid value: default/large/huge)";
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

    if (FLAGS_rdma_use_polling) {
        _poller_groups = std::vector<PollerGroup>(FLAGS_task_group_ntags);
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
    // release polling mode at exit or call RdmaEndpoint::PollingModeRelease
    // explicitly
    if (FLAGS_rdma_use_polling) {
        for (int i = 0; i < FLAGS_task_group_ntags; ++i) {
            PollingModeRelease(i);
        }
    }
}

std::vector<RdmaEndpoint::PollerGroup> RdmaEndpoint::_poller_groups;

int RdmaEndpoint::PollingModeInitialize(bthread_tag_t tag,
                                        std::function<void()> callback,
                                        std::function<void()> init_fn,
                                        std::function<void()> release_fn) {
    if (!FLAGS_rdma_use_polling) {
        return 0;
    }
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
        std::unordered_set<SocketId> cq_sids;
        CqSidOp op;

        if (poller->init_fn) {
            poller->init_fn();
        }

        while (running->load(std::memory_order_relaxed)) {
            while (poller->op_queue.Dequeue(op)) {
                if (op.type == CqSidOp::ADD) {
                    cq_sids.emplace(op.sid);
                } else if (op.type == CqSidOp::REMOVE) {
                    cq_sids.erase(op.sid);
                }
            }
            for (auto sid : cq_sids) {
                SocketUniquePtr s;
                if (Socket::Address(sid, &s) < 0) {
                    continue;
                }
                PollCq(s.get());
            }
            if (poller->callback) {
                poller->callback();
            }
            if (FLAGS_rdma_poller_yield) {
                bthread_yield();
            }
        }

        if (poller->release_fn) {
            poller->release_fn();
        }

        return nullptr;
    };
    for (int i = 0; i < FLAGS_rdma_poller_num; ++i) {
        auto args = new FnArgs{&pollers[i], &running};
        auto attr = FLAGS_rdma_disable_bthread ? BTHREAD_ATTR_PTHREAD
                                               : BTHREAD_ATTR_NORMAL;
        attr.tag = tag;
        bthread_attr_set_name(&attr, "RdmaPolling");
        pollers[i].callback = callback;
        pollers[i].init_fn = init_fn;
        pollers[i].release_fn = release_fn;
        auto rc = bthread_start_background(&pollers[i].tid, &attr, fn, args);
        if (rc != 0) {
            LOG(ERROR) << "Fail to start rdma polling bthread";
            return -1;
        }
    }
    return 0;
}

void RdmaEndpoint::PollingModeRelease(bthread_tag_t tag) {
    if (!FLAGS_rdma_use_polling) {
        return;
    }
    auto& group = _poller_groups[tag];
    auto& pollers = group.pollers;
    auto& running = group.running;
    running.store(false, std::memory_order_relaxed);
    for (int i = 0; i < FLAGS_rdma_poller_num; ++i) {
        bthread_join(pollers[i].tid, NULL);
    }
}

void RdmaEndpoint::PollerAddCqSid() {
    auto index = butil::fmix32(_cq_sid) % FLAGS_rdma_poller_num;
    auto& group = _poller_groups[bthread_self_tag()];
    auto& pollers = group.pollers;
    auto& poller = pollers[index];
    if (INVALID_SOCKET_ID != _cq_sid) {
        poller.op_queue.Enqueue(CqSidOp{_cq_sid, CqSidOp::ADD});
    }
}

void RdmaEndpoint::PollerRemoveCqSid() {
    auto index = butil::fmix32(_cq_sid) % FLAGS_rdma_poller_num;
    auto& group = _poller_groups[bthread_self_tag()];
    auto& pollers = group.pollers;
    auto& poller = pollers[index];
    if (INVALID_SOCKET_ID != _cq_sid) {
        poller.op_queue.Enqueue(CqSidOp{_cq_sid, CqSidOp::REMOVE});
    }
}

}  // namespace rdma
}  // namespace brpc

#endif  // if BRPC_WITH_RDMA
