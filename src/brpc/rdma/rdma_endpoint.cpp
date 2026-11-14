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

#include <algorithm>                            // std::min
#include <gflags/gflags.h>
#include "butil/fd_utility.h"
#include "butil/memory/scope_guard.h"
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
#include "brpc/rdma/srq_worker.h"
#include "brpc/rdma/rq_connection.h"
#include "brpc/rdma_transport.h"
#include "brpc/rdma/rdma_handshake.h"
#include "brpc/rdma/rdma_handshake_constants.h"

DECLARE_int32(task_group_ntags);

namespace brpc {
namespace rdma {

extern bool g_skip_rdma_init;
// Fault injection for hardware-free handshake tests.
bool g_fail_resource_alloc_for_test = false;

DEFINE_bool(rdma_recv_zerocopy, true, "Enable zerocopy for receive side");
DEFINE_int32(rdma_zerocopy_min_size, 512, "The minimal size for receive zerocopy");
DEFINE_bool(rdma_trace_verbose, false, "Print log message verbosely");
BRPC_VALIDATE_GFLAG(rdma_trace_verbose, brpc::PassValidate);
DEFINE_bool(rdma_disable_bthread, false, "Disable bthread in RDMA");

RdmaEndpoint::RdmaEndpoint(Socket* s)
    : _socket(s)
    , _state(UNINIT)
    , _handshake_version(0)
    , _local_info(DefaultRdmaLocalInfo(RdmaQueueMode::PRIVATE_RQ)) {
    _read_butex = bthread::butex_create_checked<butil::atomic<int> >();
    _input_processor.Init(s, InputMessengerProcessor::STREAM_RDMA_QP);
}

RdmaEndpoint::~RdmaEndpoint() {
    Reset();
    bthread::butex_destroy(_read_butex);
}

void RdmaEndpoint::Reset() {
    DeallocateResources();
    SelectQueueMode(RdmaQueueMode::PRIVATE_RQ);
    _state.store(UNINIT, butil::memory_order_relaxed);
    _handshake_version = 0;
    _outgoing_ece.reset();
    _outgoing_mtu.reset();
    _input_processor.Reset();
}

void RdmaEndpoint::SelectQueueMode(RdmaQueueMode mode) {
    _queue_mode = mode;
    _local_info = DefaultRdmaLocalInfo(mode);
}

void RdmaEndpoint::GetLocalInfo(RdmaLocalInfo* info) const {
    if (_connection) {
        _connection->GetLocalInfo(info);
    } else {
        *info = _local_info;
        info->block_size = g_rdma_recv_block_size;
    }
}

namespace {
// AppConnect is inherited by pooled/short sockets. Completion state must be
// owned by each handshake task, never by the shared RdmaConnect instance.
struct ClientHandshakeTask {
    // Keeps the exact Socket generation alive until its completion is reported.
    SocketUniquePtr socket;
    // Invoked once after this task's handshake, with its own WriteRequest.
    void (*done)(int, void*);
    // Opaque completion argument supplied by StartConnect.
    void* data;
};
}  // namespace

void RdmaConnect::StartConnect(const Socket* socket,
                               void (*done)(int err, void* data),
                               void* data) {
    auto* rdma_transport = static_cast<RdmaTransport*>(socket->_transport.get());
    CHECK(rdma_transport->_rdma_ep != nullptr);
    SocketUniquePtr s;
    if (Socket::Address(socket->id(), &s) != 0) {
        // WriteRequest still holds the Socket. Complete the callback even on
        // failure to release the write request and its reference.
        done(EFAILEDSOCKET, data);
        return;
    }
    if (!IsRdmaAvailable()) {
        rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
        rdma_transport->_rdma_ep->_state.store(
            RdmaEndpoint::FALLBACK_TCP, butil::memory_order_release);
        done(s->Failed() ? EFAILEDSOCKET : 0, data);
        return;
    }
    auto task = std::make_unique<ClientHandshakeTask>();
    task->socket = std::move(s);
    task->done = done;
    task->data = data;
    bthread_t tid;
    bthread_attr_t attr = BTHREAD_ATTR_NORMAL;
    bthread_attr_set_name(&attr, "RdmaProcessHandshakeAtClient");
    int rc = bthread_start_background(
        &tid, &attr, RdmaEndpoint::ProcessHandshakeAtClient, task.get());
    if (rc != 0) {
        LOG(ERROR) << "Fail to start handshake bthread: " << berror(rc);
        done(rc, data);
    } else {
        task.release();
    }
}

void RdmaEndpoint::OnNewDataFromTcp(Socket* s) {
    if (s->CreatedByConnect()) {
        OnNewDataFromTcpAtClient(s);
    } else {
        OnNewDataFromTcpAtServer(s);
    }
}

void RdmaEndpoint::OnNewDataFromTcpAtClient(Socket* s) {
    auto* rdma_transport = static_cast<RdmaTransport*>(s->_transport.get());
    RdmaEndpoint* ep = rdma_transport->GetRdmaEp();
    CHECK(ep != nullptr);

    int progress = Socket::PROGRESS_INIT;
    while (true) {
        // Pair with release stores of FALLBACK_TCP so RDMA_OFF is visible
        // before normal TCP message processing starts.
        const State state = ep->_state.load(butil::memory_order_acquire);
        if (state == UNINIT) {
            // The connection may be closed or reset before the client starts
            // handshake. This will be handled by client handshake. Ignore here.
        } else if (state < ESTABLISHED) {  // during handshake
            ep->_read_butex->fetch_add(1, butil::memory_order_release);
            bthread::butex_wake(ep->_read_butex);
        } else if (state == FALLBACK_TCP){  // handshake finishes
            InputMessenger::OnNewMessages(s);
            return;
        } else if (state == ESTABLISHED) {
            if (!ep->HandleTcpEventAfterEstablished()) {
                return;
            }
        }
        if (!s->MoreReadEvents(&progress)) {
            break;
        }
    }
}

void RdmaEndpoint::OnNewDataFromTcpAtServer(Socket* s) {
    auto* rdma_transport = static_cast<RdmaTransport*>(s->_transport.get());
    RdmaEndpoint* ep = rdma_transport->GetRdmaEp();
    CHECK(ep != nullptr);

    int progress = Socket::PROGRESS_INIT;
    while (true) {
        if (s->Failed()) {
            return;
        }

        // Pair with the release stores of ESTABLISHED / FALLBACK_TCP.
        if (ep->_state.load(butil::memory_order_acquire) != ESTABLISHED) {
            InputMessenger::OnNewMessages(s);
            // That call may have just finished the handshake and turned RDMA
            // on. Start consuming CQ events here rather than inside the parse
            // callback: by now OnNewMessages is done with the Socket's
            // `parsing_context` / `preferred_index`, so the QP stream can take
            // them over without ever overlapping with the fd stream. This is
            // the ordering StartCqEvents() asks for.
            if (!s->Failed() &&
                ep->_state.load(butil::memory_order_acquire) == ESTABLISHED &&
                ep->StartCqEvents() < 0) {
                const int saved_errno = errno;
                PLOG(WARNING) << "Fail to start cq events on " << *s;
                ep->_state.store(FAILED, butil::memory_order_relaxed);
                s->SetFailed(saved_errno, "Fail to start cq events on %s: %s",
                             s->description().c_str(), berror(saved_errno));
            }
            return;
        }
        // RDMA carries the RPCs now, so the fd is watched for EOF only and must
        // not be parsed: `preferred_index' / `parsing_context' live on the Socket
        // and the QP stream is driving them (https://github.com/apache/brpc/issues/3479).
        if (!ep->HandleTcpEventAfterEstablished()) {
            return;
        }
        if (!s->MoreReadEvents(&progress)) {
            break;
        }
    }
}

bool RdmaEndpoint::HandleTcpEventAfterEstablished() {
    uint8_t tmp;
    ssize_t nr = read(_socket->fd(), &tmp, 1);
    if (nr == 0) {
        _socket->SetEOF();
        return false;
    }
    if (nr > 0) {
        LOG(WARNING) << "Read unexpected data from " << *_socket;
        _socket->SetFailed(EPROTO, "Read unexpected data from %s",
                           _socket->description().c_str());
        return false;
    }

    if (errno != EAGAIN) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to read from " << *_socket;
        _socket->SetFailed(saved_errno, "Fail to read from %s: %s",
                           _socket->description().c_str(),
                           berror(saved_errno));
        // The socket is dead now, so do not come back for another read of it.
        return false;
    }
    return true;
}

static const int WAIT_TIMEOUT_MS = 50;

// Drive an EAGAIN-aware read loop to completion (exactly `len` bytes).
// `read_once(offset, remaining)` performs ONE underlying read attempt:
//   Returns > 0  : number of bytes consumed (added to running total).
//   Returns = 0  : end-of-stream (the loop fails with EEOF).
//   Returns < 0  : errno set. EAGAIN is handled here via butex_wait,
//                   any other errno bubbles up.
// `offset` is bytes already received in THIS call (initially 0). The
// callable uses it to choose the next write target (e.g. `(char*)buf
// + offset`). Callables that don't need offset (e.g. IOPortal append)
// can ignore it.
//
// Centralizes the EAGAIN/butex/EOF loop so the two ReadFromFd
// overloads below stay one-liners. Any future read source (memory-
// mapped, scatter-vector, etc.) can plug in by passing its own
// `read_once`.
template <class ReadOnce>
static int ReadFromFdLoop(Socket* socket, butil::atomic<int>* read_butex,
                          size_t len, ReadOnce&& read_once) {
    size_t received = 0;
    while (received < len) {
        // The task retains the Socket, so failure cannot close its fd yet.
        // Recheck after each bounded wait instead of relying on EOF or the
        // recycling-only `StopConnect()` hook to cancel a silent peer.
        if (socket->Failed()) {
            errno = EFAILEDSOCKET;
            return -1;
        }
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
    CHECK(data != nullptr);
    const int fd = _socket->fd();
    return ReadFromFdLoop(_socket, _read_butex, len,
        [data, fd](size_t offset, size_t remaining) {
            return read(fd, (uint8_t*)data + offset, remaining);
        });
}

int RdmaEndpoint::ReadFromFd(butil::IOPortal* data, size_t len) {
    CHECK(data != nullptr);
    const int fd = _socket->fd();
    return ReadFromFdLoop(_socket, _read_butex, len,
        [data, fd](size_t /*offset*/, size_t remaining) {
            return data->append_from_file_descriptor(fd, remaining);
        });
}

// Drive an EAGAIN-aware write loop to completion (exactly `len` bytes).
//
// `write_once(offset, remaining)` performs ONE underlying write attempt:
//   - returns >= 0 : number of bytes consumed (added to running total).
//   - returns < 0  : errno set. EAGAIN triggers `wait_writable(duetime)`,
//                   any other errno bubbles up.
// `offset` is bytes already written in THIS call (initially 0). The
// callable uses it to choose the next read source (e.g. `(char*)buf
// + offset`). Callables that drain a self-tracking sink (e.g.
// IOBuf::cut_into_file_descriptor) can ignore both args.
//
// `wait_writable(duetime)` is invoked on EAGAIN to park until the fd
// becomes writable again. It returns 0 on wake-up (or ETIMEDOUT),
// non-zero on hard failure.
template <class WriteOnce, class WaitWritable>
static int WriteToFdLoop(Socket* socket, size_t len, WriteOnce&& write_once,
                         WaitWritable&& wait_writable) {
    size_t written = 0;
    while (written < len) {
        // A failed Socket may still have an open but unwritable fd while this
        // handshake owns its reference. Do not retry indefinitely after wakeup.
        if (socket->Failed()) {
            errno = EFAILEDSOCKET;
            return -1;
        }
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
    CHECK(data != nullptr);
    Socket* s = _socket;
    const int fd = s->fd();
    return WriteToFdLoop(s, len,
        [data, fd](size_t offset, size_t remaining) {
            return write(fd, (uint8_t*)data + offset, remaining);
        },
        [s, fd](const timespec* duetime) {
            return s->WaitEpollOut(fd, true, duetime) == 0 || errno == ETIMEDOUT;
        });
}

int RdmaEndpoint::WriteToFd(butil::IOBuf* data) {
    CHECK(data != nullptr);
    Socket* s = _socket;
    const int fd = s->fd();
    return WriteToFdLoop(s, data->size(),
        [data, fd](size_t /*offset*/, size_t /*remaining*/) {
            return data->cut_into_file_descriptor(fd);
        },
        [s, fd](const timespec* duetime) {
            return s->WaitEpollOut(fd, true, duetime) == 0 || errno == ETIMEDOUT;
        });
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
//   [negotiation + C_BRINGUP_QP]
//     |
//     v
//   C_ACK_SEND
//     |
//     v
//   ESTABLISHED / FALLBACK_TCP
void* RdmaEndpoint::ProcessHandshakeAtClient(void* arg) {
    std::unique_ptr<ClientHandshakeTask> task(static_cast<ClientHandshakeTask*>(arg));
    Socket* s = task->socket.get();
    auto rdma_transport = static_cast<RdmaTransport*>(s->_transport.get());
    RdmaEndpoint* ep = rdma_transport->GetRdmaEp();
    int error = 0;
    auto complete = butil::MakeScopeGuard([&] {
        // Completion releases the WriteRequest exactly once. Keep its result
        // separate from `errno`, which cleanup may change, and recheck failure
        // before reporting success to the pending writer.
        if (s->Failed()) {
            if (error == 0) {
                error = EFAILEDSOCKET;
            }
            ep->_state.store(FAILED, butil::memory_order_release);
        }
        task->done(error, task->data);
    });

    if (s->Failed()) {
        return nullptr;
    }

    LOG_IF(INFO, FLAGS_rdma_trace_verbose)
        << "Start handshake on " << s->description();

    std::unique_ptr<RdmaHandshake> handshake = CreateClientHandshake(ep);
    CHECK(handshake != nullptr);
    ep->_handshake_version = handshake->ProtocolVersion();
    // SRQ requires the v3 profile and credit fields. v2 always keeps private RQ,
    // even when the local SRQ flag is enabled.
    ep->SelectQueueMode(FLAGS_rdma_use_srq && ep->_handshake_version == 3 ?
                        RdmaQueueMode::SHARED_RQ : RdmaQueueMode::PRIVATE_RQ);

    // First initialize CQ and QP resources.
    ep->_state.store(C_ALLOC_QPCQ, butil::memory_order_relaxed);
    if (ep->AllocateResources() < 0) {
        PLOG(WARNING) << "Fail to allocate rdma resources, fallback to tcp:"
                      << s->description();
        rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
        ep->_state.store(FALLBACK_TCP, butil::memory_order_release);
        return nullptr;
    }

    // Send hello message to server
    ep->_state.store(C_HELLO_SEND, butil::memory_order_relaxed);
    if (handshake->SendLocalHello() < 0) {
        error = errno;
        PLOG(WARNING) << "Fail to send hello message to server:"
                      << s->description();
        s->SetFailed(error, "Fail to complete rdma handshake from %s: %s",
                     s->description().c_str(), berror(error));
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        return nullptr;
    }

    // Receive and parse remote hello.
    ep->_state.store(C_HELLO_WAIT, butil::memory_order_relaxed);
    ParsedHello remote{};
    const RemoteHelloResult r = handshake->ReceiveAndParseRemoteHello(&remote);
    if (r == RemoteHelloResult::ERROR) {
        error = errno;
        PLOG(WARNING) << "Fail to receive hello from server:"
                      << s->description();
        s->SetFailed(error, "Fail to complete rdma handshake from %s: %s",
                     s->description().c_str(), berror(error));
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        return nullptr;
    }

    if (s->Failed()) {
        return nullptr;
    }

    if (r != RemoteHelloResult::NEGOTIATED ||
        (ep->_queue_mode == RdmaQueueMode::SHARED_RQ) != (remote.srq_protocol == 1)) {
        LOG(WARNING) << "Fail to negotiate with server, fallback to tcp:"
                     << s->description();
        rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
    } else {
        ep->_state.store(C_BRINGUP_QP, butil::memory_order_relaxed);
        if (ep->BringUpQp(remote, /*is_server=*/false) < 0) {
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
        error = errno;
        PLOG(WARNING) << "Fail to send Ack Message to server:"
                      << s->description();
        s->SetFailed(error, "Fail to complete rdma handshake from %s: %s",
                     s->description().c_str(), berror(error));
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        return nullptr;
    }

    if (s->Failed()) {
        return nullptr;
    }

    if (rdma_transport->_rdma_state == RdmaTransport::RDMA_ON) {
        ep->_state.store(ESTABLISHED, butil::memory_order_release);
        // The handshake is over, so the QP stream may start parsing now.
        if (ep->StartCqEvents() < 0) {
            error = errno;
            PLOG(WARNING) << "Fail to start cq events on " << s->description();
            s->SetFailed(error, "Fail to complete rdma handshake from %s: %s",
                         s->description().c_str(), berror(error));
            ep->_state.store(FAILED, butil::memory_order_relaxed);
            return nullptr;
        }
        LOG_IF(INFO, FLAGS_rdma_trace_verbose)
            << "Client handshake ends (use rdma v" << ep->_handshake_version
            << ") on " << s->description();
    } else {
        ep->DeallocateResources();
        ep->_state.store(FALLBACK_TCP, butil::memory_order_release);
        LOG_IF(INFO, FLAGS_rdma_trace_verbose)
            << "Client handshake ends (use tcp) on " << s->description();
    }

    return nullptr;
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
ParseResult RdmaEndpoint::ExecuteServerHandshake(butil::IOBuf* source, Socket* s) {
    RdmaTransport* rdma_transport = static_cast<RdmaTransport*>(s->_transport.get());
    RdmaEndpoint* ep = rdma_transport->_rdma_ep;
    CHECK(ep != nullptr);

    const State state = ep->_state.load(butil::memory_order_acquire);
    if (state >= ESTABLISHED) {
        // The handshake is over (ESTABLISHED / FALLBACK_TCP / FAILED). Data
        // arriving now belongs to a real protocol, yet CutInputMessage() still
        // reaches us.
        if (state == ESTABLISHED &&
            s->parsing_stream_type() == InputMessengerProcessor::STREAM_TCP_FD) {
            // RDMA is on, so the fd is not an RPC channel any more and whatever
            // shows up on it is a protocol error. Reached even though
            // OnNewDataFromTcpAtServer() stops handing the fd to OnNewMessages()
            // once RDMA is on, because the handshake completes inside OnNewMessages():
            // that round keeps reading the fd until it goes quiet.
            if (source->empty()) {
                // Nothing to reject yet. Asking for more data keeps the pin, so
                // the rest of this round comes back here rather than reaching a
                // real protocol, and lets OnNewMessages() report EOF as usual.
                return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
            }
            LOG(WARNING) << "Unexpected " << source->size() << " bytes on the tcp "
                            "fd of an RDMA connection, drop connection: "
                         << s->description();
            ep->_state.store(FAILED, butil::memory_order_relaxed);
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }
        // Anything else, for the real protocol to parse: the stream carried by
        // the QP, or an fd that stayed a normal RPC stream because the handshake
        // fell back or failed.
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }

    if (s->parsing_context() == nullptr) {
        // Phase 1: read the client hello, negotiate, reply server hello.
        if (source->size() < HELLO_MAGIC_LEN) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        uint8_t magic[HELLO_MAGIC_LEN];
        CHECK_EQ(source->copy_to(magic, HELLO_MAGIC_LEN), HELLO_MAGIC_LEN);

        // Pick the version-specific server handshake from the peeked magic (the
        // magic is NOT consumed. ReceiveAndParseRemoteHello() reads it again
        // from `source`).
        std::unique_ptr<RdmaHandshake> hs = CreateServerHandshakeByMagic(ep, source, magic);
        if (hs == nullptr) {
            return MakeParseError(PARSE_ERROR_TRY_OTHERS);
        }
        ep->_handshake_version = hs->ProtocolVersion();
        ep->_state.store(S_HELLO_WAIT, butil::memory_order_relaxed);

        ParsedHello remote{};
        const RemoteHelloResult r = hs->ReceiveAndParseRemoteHello(&remote);
        if (r == RemoteHelloResult::NEED_MORE) {
            return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
        }
        if (r == RemoteHelloResult::ERROR) {
            ep->_state.store(FAILED, butil::memory_order_relaxed);
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }

        // Negotiate + allocate resources.
        bool negotiated = r == RemoteHelloResult::NEGOTIATED;
        if (negotiated && remote.srq_protocol == 1) {
            negotiated = FLAGS_rdma_use_srq && ep->_handshake_version == 3;
            if (negotiated) {
                ep->SelectQueueMode(RdmaQueueMode::SHARED_RQ);
            }
        }
        if (negotiated) {
            ep->_state.store(S_ALLOC_QPCQ, butil::memory_order_relaxed);
            if (ep->AllocateResources() < 0) {
                PLOG(WARNING) << "Fail to allocate rdma resources, fallback to tcp:"
                              << s->description();
                negotiated = false;
            } else {
                ep->_state.store(S_BRINGUP_QP, butil::memory_order_relaxed);
                if (ep->BringUpQp(remote, /*is_server=*/true) < 0) {
                    LOG(WARNING) << "Fail to bringup QP, fallback to tcp:"
                                 << s->description();
                    negotiated = false;
                }
            }
        }
        if (!negotiated) {
            rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
        }

        // Reply with a usable hello when _rdma_state != RDMA_OFF, or a hello
        // that disables RDMA negotiation otherwise.
        ep->_state.store(S_HELLO_SEND, butil::memory_order_relaxed);
        if (hs->SendLocalHello() < 0) {
            PLOG(WARNING) << "Fail to send server hello to " << s->description();
            ep->_state.store(FAILED, butil::memory_order_relaxed);
            return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
        }

        // Enter the wait-ACK phase. Whether negotiation succeeded is already
        // recorded in rdma_transport->_rdma_state (RDMA_OFF iff negotiation
        // failed), so the context itself needs no extra flag.
        s->reset_parsing_context(ServerHandshakeContext::Create());
        ep->_state.store(S_ACK_WAIT, butil::memory_order_relaxed);
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    // Phase 2: drain the 4B ACK and finalize.
    if (source->size() < HELLO_ACK_LEN) {
        return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
    }

    uint32_t flags_be = 0;
    CHECK_EQ(source->cutn(&flags_be, HELLO_ACK_LEN), HELLO_ACK_LEN);
    uint32_t flags = butil::NetToHost32(flags_be);
    bool client_ack_ok = (flags & HELLO_ACK_RDMA_OK) != 0;
    if (!client_ack_ok) {
        ep->DeallocateResources();
        LOG_IF(INFO, FLAGS_rdma_trace_verbose)
            << "Server handshake ends (use tcp) on " << s->description();
        rdma_transport->_rdma_state = RdmaTransport::RDMA_OFF;
        ep->_state.store(FALLBACK_TCP, butil::memory_order_release);
        s->reset_parsing_context(nullptr);
        return MakeParseError(PARSE_ERROR_TRY_OTHERS);
    }

    if (rdma_transport->_rdma_state == RdmaTransport::RDMA_OFF) {
        LOG(WARNING) << "Client wants RDMA in ACK but server fell back: "
                     << s->description();
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        s->reset_parsing_context(nullptr);
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
    }

    if (!source->empty()) {
        // RDMA is on, so the TCP fd is no longer an RPC channel. Anything
        // trailing the ACK on it can only be a protocol error. This catches what
        // arrived in the same read as the ACK.
        LOG(WARNING) << "Unexpected " << source->size() << " bytes after the "
                        "handshake ACK of an RDMA connection, drop connection: "
                     << s->description();
        ep->_state.store(FAILED, butil::memory_order_relaxed);
        s->reset_parsing_context(nullptr);
        return MakeParseError(PARSE_ERROR_ABSOLUTELY_WRONG);
    }

    LOG_IF(INFO, FLAGS_rdma_trace_verbose)
        << "Server handshake ends (use rdma v" << ep->_handshake_version
        << ") on " << s->description();
    rdma_transport->_rdma_state = RdmaTransport::RDMA_ON;
    ep->_state.store(ESTABLISHED, butil::memory_order_release);
    s->reset_parsing_context(nullptr);

    // Two things are deliberately not done here.
    //
    // The CQ events are not started: this runs inside CutInputMessage, which
    // keeps touching `preferred_index` / `parsing_context` after we return,
    // and PollCq would race it for those. OnNewDataFromTcpAtServer() starts
    // them once that is over.
    //
    // TRY_OTHERS is not returned: it would hand `preferred_index` to the real
    // protocol, and the remaining reads of this OnNewMessages() round would
    // parse the fd as an RPC stream although RDMA has just taken over. Asking
    // for more data keeps this handler pinned, so those reads come back to the
    // guard at the top of this function and are rejected there.
    return MakeParseError(PARSE_ERROR_NOT_ENOUGH_DATA);
}

bool RdmaEndpoint::IsWritable() const {
    return _connection && _connection->IsWritable();
}

ssize_t RdmaEndpoint::CutFromIOBufList(butil::IOBuf** data, size_t count) {
    if (!_connection) {
        errno = g_skip_rdma_init ? EAGAIN : ENOTCONN;
        return -1;
    }
    return _connection->Send(data, count);
}

int RdmaEndpoint::AllocateResources() {
    CHECK(!_connection);
    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        if (g_fail_resource_alloc_for_test) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }
    _connection = CreateRdmaConnection(
        _socket, _queue_mode, _socket->_io_event.bthread_tag());
    return _connection ? 0 : -1;
}

void RdmaEndpoint::DeallocateResources() {
    // Detach before Close can release references or schedule late callbacks.
    auto connection = std::move(_connection);
    if (connection) {
        connection->Detach();
    }
}

int RdmaEndpoint::StartCqEvents() {
    if (InputMessengerProcessor::STREAM_NONE != _socket->parsing_stream_type()) {
        LOG(WARNING) << "StartCqEvents() called while " << *_socket << " is parsing";
        errno = ERDMA;
        return -1;
    }

    if (!_connection) {
        if (g_skip_rdma_init) {
            return 0;
        }
        errno = ENOTCONN;
        return -1;
    }
    return _connection->EnableReceive();
}

int RdmaEndpoint::BringUpQp(const ParsedHello& remote, bool is_server) {
    // MTU negotiation (server side): compute the minimum MTU and store it
    // so FillLocalRdmaHello can advertise the negotiated value in the server
    // hello reply. This is done before the `g_skip_rdma_init` early return so
    // that UT (which skips real QP bring-up) still sets `_outgoing_mtu`.
    if (is_server && remote.path_mtu.has_value()) {
        uint32_t local_mtu = GetRdmaActiveMtu();
        _outgoing_mtu = std::min(local_mtu, *remote.path_mtu);
    }

    if (BAIDU_UNLIKELY(g_skip_rdma_init)) {
        return 0;
    }
    return _connection->Activate(remote, is_server, &_outgoing_ece);
}

bool RdmaEndpoint::OwnsConnection(const RdmaConnection* connection) const {
    // Delivery retains the owning Socket. Recycling and health-check Reset
    // wait for that reference, while handshake fallback detaches before CQ
    // delivery is enabled. Thus `_connection` cannot be detached concurrently.
    return connection && _connection.get() == connection;
}

int RdmaEndpoint::Deliver(RdmaConnection* source, butil::IOBuf* data,
                          ssize_t bytes, InputMessageClosure& last_msg) {
    if (!OwnsConnection(source)) {
        errno = ENOTCONN;
        return -1;
    }
    _input_processor.read_buf().append(data->movable());
    const int64_t received_us = butil::cpuwide_time_us();
    const int64_t base_realtime = butil::gettimeofday_us() - received_us;
    return _input_processor.ProcessNewMessage(
        bytes, false, received_us, base_realtime, last_msg);
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
       << connector << "handshake_version=" << _handshake_version
       << connector << "rdma_read_buf=" << _input_processor.read_buf().size();
    if (_connection != nullptr) {
        os << connector;
        _connection->Debug(os, connector);
    }
}

int RdmaEndpoint::GlobalInitialize() {
    if (!ValidateSrqOptions()) {
        errno = EINVAL;
        return -1;
    }
    g_rdma_recv_block_size = GetRdmaBlockSize() - IOBUF_BLOCK_HEADER_LEN;
    if (g_rdma_recv_block_size <= 0) {
        LOG(ERROR) << "rdma_recv_block_type incorrect "
                   << "(valid value: default/large/huge)";
        errno = EINVAL;
        return -1;
    }
    return RqConnection::GlobalInitialize();
}

void RdmaEndpoint::GlobalRelease() {
    StopAllSrqWorkers();
    RqConnection::GlobalRelease();
}

int RdmaEndpoint::PollingModeInitialize(bthread_tag_t tag,
                                        std::function<void()> callback,
                                        std::function<void()> init_fn,
                                        std::function<void()> release_fn) {
    return RqConnection::PollingModeInitialize(tag, std::move(callback),
                                               std::move(init_fn),
                                               std::move(release_fn));
}

void RdmaEndpoint::PollingModeRelease(bthread_tag_t tag) {
    RqConnection::PollingModeRelease(tag);
}

}  // namespace rdma
}  // namespace brpc
#endif  // BRPC_WITH_RDMA
