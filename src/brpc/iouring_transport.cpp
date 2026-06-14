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

#if BRPC_WITH_IOURING

#include <gflags/gflags.h>
#include "butil/logging.h"
#include "brpc/event_dispatcher.h"
#include "brpc/input_messenger.h"
#include "brpc/socket.h"
#include "brpc/reloadable_flags.h"
#include "brpc/iouring/iouring_helper.h"
#include "brpc/iouring/iouring_endpoint.h"
#include "brpc/iouring_transport.h"

namespace brpc {

DECLARE_bool(usercode_in_coroutine);
DECLARE_bool(usercode_in_pthread);

extern SocketVarsCollector* g_vars;

// -----------------------------------------------------------------------
// IouringTransport::Init
// -----------------------------------------------------------------------

void IouringTransport::Init(Socket* socket, const SocketOptions& options) {
    _socket          = socket;
    _default_connect = options.app_connect;

    // Tentatively adopt the caller's edge-trigger callback (or the default
    // OnNewMessages).  This may be cleared below if io_uring polling takes
    // over the read path.
    _on_edge_trigger = options.on_edge_triggered_events;
    if (options.need_on_edge_trigger && _on_edge_trigger == nullptr) {
        _on_edge_trigger = InputMessenger::OnNewMessages;
    }

    // Create the endpoint
    _iouring_ep = new iouring::IouringEndpoint(socket);

    // Register this socket with the Poller.  AllocateResources enqueues an
    // ADD SidOp; the Poller thread picks it up, acquires a read slot (when
    // --iouring_register_buffers=true) and issues the first SubmitRead.
    if (iouring::IsIouringAvailable()) {
        if (_iouring_ep->AllocateResources() < 0) {
            LOG(WARNING) << "Fail to allocate io_uring resources for "
                         << socket->description() << ", falling back to TCP";
            delete _iouring_ep;
            _iouring_ep = nullptr;
        } else {
            // io_uring owns the read path regardless of polling mode.
            // The Poller thread reaps READ / READ_FIXED CQEs and appends data
            // to socket->_read_buf, then calls ProcessNewMessage directly
            // (see PollCq).
            //
            // We must NOT register an epoll edge-trigger callback here,
            // because OnNewMessages (the default callback) would call
            // DoRead() = read(fd), racing with io_uring's reads and stealing
            // data from the ring – causing partial reads, stalled connections
            // and protocol parse failures.
            //
            // Clearing _on_edge_trigger makes HasOnEdgeTrigger() return
            // false, so socket.cpp skips AddConsumer(fd) and this fd is
            // never added to epoll for read events.
            _on_edge_trigger = nullptr;
        }
    }

    // Create the TCP fallback transport (always available).
    // When _iouring_ep is null (AllocateResources failed) _on_edge_trigger
    // is still set, so the TCP path works normally via epoll + OnNewMessages.
    _tcp_transport = std::make_shared<TcpTransport>();
    _tcp_transport->Init(socket, options);
}

// -----------------------------------------------------------------------
// IouringTransport::Release
// -----------------------------------------------------------------------

void IouringTransport::Release() {
    if (_iouring_ep) {
        delete _iouring_ep;
        _iouring_ep = nullptr;
    }
}

// -----------------------------------------------------------------------
// IouringTransport::Reset
// -----------------------------------------------------------------------

int IouringTransport::Reset(int32_t /*expected_nref*/) {
    if (_iouring_ep) {
        _iouring_ep->Reset();
    }
    return 0;
}

// -----------------------------------------------------------------------
// IouringTransport::Connect
// -----------------------------------------------------------------------

std::shared_ptr<AppConnect> IouringTransport::Connect() {
    return _default_connect;
}

// -----------------------------------------------------------------------
// IouringTransport::CutFromIOBuf  (single-buffer write)
// -----------------------------------------------------------------------

int IouringTransport::CutFromIOBuf(butil::IOBuf* buf) {
    // If io_uring is available and has capacity, use it.
    if (_iouring_ep && iouring::IsIouringAvailable() &&
        _iouring_ep->IsWritable()) {
        butil::IOBuf* bufs[1] = {buf};
        ssize_t nw = _iouring_ep->CutFromIOBufList(bufs, 1);
        if (nw >= 0) {
            return 0;
        }
        if (errno != EAGAIN) {
            return -1;
        }
        // Fall through to TCP fallback on EAGAIN
    }
    // Fallback: synchronous write via the fd
    return buf->cut_into_file_descriptor(_socket->fd());
}

// -----------------------------------------------------------------------
// IouringTransport::CutFromIOBufList  (multi-buffer write)
// -----------------------------------------------------------------------

ssize_t IouringTransport::CutFromIOBufList(butil::IOBuf** buf, size_t ndata) {
    if (_iouring_ep && iouring::IsIouringAvailable() &&
        _iouring_ep->IsWritable()) {
        ssize_t nw = _iouring_ep->CutFromIOBufList(buf, ndata);
        if (nw >= 0 || errno != EAGAIN) {
            return nw;
        }
        // EAGAIN: fall through to synchronous path
    }
    return butil::IOBuf::cut_multiple_into_file_descriptor(
        _socket->fd(), buf, ndata);
}

// -----------------------------------------------------------------------
// IouringTransport::WaitEpollOut
// -----------------------------------------------------------------------
//
// When io_uring polling is active the write path is fully async: SQEs are
// submitted to the ring and completed by the Poller thread.  There is no
// need to block on epoll(EPOLLOUT) – instead we wait on _epollout_butex,
// which is incremented by Socket::WakeAsEpollOut() each time a write CQE
// is reaped in IouringEndpoint::PollCq.  This keeps the wait/wake path
// entirely within io_uring / bthread primitives and avoids the extra epoll
// file descriptor overhead.
//
// If io_uring is unavailable (e.g. AllocateResources failed) we fall back
// to the traditional epoll(EPOLLOUT) path via Socket::WaitEpollOut().
// -----------------------------------------------------------------------

int IouringTransport::WaitEpollOut(butil::atomic<int>* epollout_butex,
                                   bool pollin, timespec duetime) {
    g_vars->nwaitepollout << 1;

    if (_iouring_ep && iouring::IsIouringAvailable()) {
        // io_uring path: wait for a write completion to wake us.
        // PollCq calls dep->_socket->WakeAsEpollOut() on every WRITE/
        // WRITE_FIXED CQE, which does:
        //   _epollout_butex->fetch_add(1)  +  butex_wake_except(...)
        // So we just need to butex_wait here.
        const int expected =
            epollout_butex->load(butil::memory_order_acquire);
        if (bthread::butex_wait(epollout_butex, expected, &duetime) < 0) {
            if (errno != EAGAIN && errno != ETIMEDOUT) {
                const int saved_errno = errno;
                PLOG(WARNING) << "butex_wait failed for " << _socket;
                _socket->SetFailed(saved_errno,
                                   "butex_wait failed for %s: %s",
                                   _socket->description().c_str(),
                                   berror(saved_errno));
                return 1;
            }
        }
        return 0;
    }

    // Fallback: io_uring is unavailable for this socket (AllocateResources
    // failed at Init time, so _iouring_ep is null and writes go through the
    // synchronous fd path which can return EAGAIN).  Fall back to epoll so
    // that KeepWrite does not busy-spin when the send buffer is full.
    const int rc = _socket->WaitEpollOut(_socket->fd(), pollin, &duetime);
    if (rc < 0 && errno != ETIMEDOUT) {
        const int saved_errno = errno;
        PLOG(WARNING) << "Fail to wait epollout of " << _socket;
        _socket->SetFailed(saved_errno, "Fail to wait epollout of %s: %s",
                           _socket->description().c_str(), berror(saved_errno));
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------
// IouringTransport::ProcessEvent
// -----------------------------------------------------------------------

void IouringTransport::ProcessEvent(bthread_attr_t attr) {
    bthread_t tid;
    if (FLAGS_usercode_in_coroutine) {
        OnEdge(_socket);
    } else if (bthread_start_background(&tid, &attr, OnEdge, _socket) != 0) {
        LOG(FATAL) << "Fail to start ProcessEvent bthread";
        OnEdge(_socket);
    }
}

// -----------------------------------------------------------------------
// IouringTransport::QueueMessage
// -----------------------------------------------------------------------

void IouringTransport::QueueMessage(InputMessageClosure& input_msg,
                                    int* num_bthread_created,
                                    bool /*last_msg*/) {
    InputMessageBase* to_run_msg = input_msg.release();
    if (!to_run_msg) {
        return;
    }
    bthread_t th;
    bthread_attr_t tmp =
        (FLAGS_usercode_in_pthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL) |
        BTHREAD_NOSIGNAL;
    tmp.keytable_pool = _socket->keytable_pool();
    tmp.tag           = bthread_self_tag();
    bthread_attr_set_name(&tmp, "ProcessInputMessage");
    if (!FLAGS_usercode_in_coroutine &&
        bthread_start_background(&th, &tmp, ProcessInputMessage, to_run_msg) == 0) {
        ++*num_bthread_created;
    } else {
        ProcessInputMessage(to_run_msg);
    }
}

// -----------------------------------------------------------------------
// IouringTransport::Debug
// -----------------------------------------------------------------------

void IouringTransport::Debug(std::ostream& os) {
    if (_iouring_ep) {
        _iouring_ep->DebugInfo(os);
    }
}

// -----------------------------------------------------------------------
// IouringTransport::ContextInitOrDie  (called once at server/channel start)
// -----------------------------------------------------------------------

int IouringTransport::ContextInitOrDie(bool /*serverOrNot*/,
                                       const void* /*_options*/) {
    iouring::GlobalIouringInitializeOrDie();
    return 0;
}

}  // namespace brpc

#endif  // BRPC_WITH_IOURING
