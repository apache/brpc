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

#include "brpc/tcp_transport.h"
#include "brpc/event_dispatcher.h"
#include "brpc/input_messenger.h"

#if BRPC_WITH_IOURING
#include "brpc/iouring/iouring_helper.h"
#include "brpc/iouring/iouring_endpoint.h"
#endif  // BRPC_WITH_IOURING

namespace brpc {
DECLARE_bool(usercode_in_coroutine);
DECLARE_bool(usercode_in_pthread);

extern SocketVarsCollector* g_vars;

void TcpTransport::Init(Socket* socket, const SocketOptions& options) {
    _socket = socket;
    _default_connect = options.app_connect;
    _on_edge_trigger = options.on_edge_triggered_events;
    if (options.need_on_edge_trigger && _on_edge_trigger == NULL) {
        _on_edge_trigger = InputMessenger::OnNewMessages;
    }

#if BRPC_WITH_IOURING
    // io_uring is not a separate transport medium: it only changes how this
    // process submits I/O for the same TCP fd, so it is safe to opportunistically
    // enable it here whenever the global io_uring context has been brought up
    // (see iouring::GlobalIouringInitializeOrDie() / InitPollingModeWithTag()).
    // No handshake with the peer is required.
    if (iouring::IsIouringAvailable()) {
        _iouring_ep = new iouring::IouringEndpoint(socket);
        if (_iouring_ep->AllocateResources() < 0) {
            LOG(WARNING) << "Fail to allocate io_uring resources for "
                         << socket->description() << ", falling back to epoll";
            delete _iouring_ep;
            _iouring_ep = nullptr;
        } else {
            // io_uring owns the read path regardless of polling mode. The
            // Poller thread reaps READ / READ_FIXED CQEs, appends the data
            // to socket->_read_buf and calls ProcessNewMessage directly (see
            // IouringEndpoint::PollCq).
            //
            // We must NOT register an epoll edge-trigger callback here,
            // because OnNewMessages (the default callback) would call
            // DoRead() = read(fd), racing with io_uring's reads and stealing
            // data from the ring -- causing partial reads, stalled
            // connections and protocol parse failures.
            //
            // Clearing _on_edge_trigger makes HasOnEdgeTrigger() return
            // false, so socket.cpp skips AddConsumer(fd) and this fd is
            // never added to epoll for read events.
            _on_edge_trigger = nullptr;
        }
    }
#endif  // BRPC_WITH_IOURING
}

void TcpTransport::Release() {
#if BRPC_WITH_IOURING
    if (_iouring_ep) {
        delete _iouring_ep;
        _iouring_ep = nullptr;
    }
#endif  // BRPC_WITH_IOURING
}

int TcpTransport::Reset(int32_t expected_nref) {
#if BRPC_WITH_IOURING
    if (_iouring_ep) {
        _iouring_ep->Reset();
    }
#endif  // BRPC_WITH_IOURING
    return 0;
}

int TcpTransport::CutFromIOBuf(butil::IOBuf* buf) {
#if BRPC_WITH_IOURING
    if (_iouring_ep && iouring::IsIouringAvailable() && _iouring_ep->IsWritable()) {
        butil::IOBuf* bufs[1] = {buf};
        ssize_t nw = _iouring_ep->CutFromIOBufList(bufs, 1);
        if (nw >= 0) {
            // Like cut_into_file_descriptor(), the return value is the
            // number of bytes actually cut/written, not a 0/-1 status --
            // callers (e.g. Socket::Write()) rely on it for output-byte
            // accounting and to decide whether the write is complete.
            return nw;
        }
        if (errno != EAGAIN) {
            return -1;
        }
        // Fall through to the synchronous fd path on EAGAIN.
    }
#endif  // BRPC_WITH_IOURING
    return buf->cut_into_file_descriptor(_socket->fd());
}

std::shared_ptr<AppConnect> TcpTransport::Connect() {
    return _default_connect;
}

ssize_t TcpTransport::CutFromIOBufList(butil::IOBuf** buf, size_t ndata) {
#if BRPC_WITH_IOURING
    if (_iouring_ep && iouring::IsIouringAvailable() && _iouring_ep->IsWritable()) {
        ssize_t nw = _iouring_ep->CutFromIOBufList(buf, ndata);
        if (nw >= 0 || errno != EAGAIN) {
            return nw;
        }
        // EAGAIN: fall through to the synchronous fd path.
    }
#endif  // BRPC_WITH_IOURING
    return butil::IOBuf::cut_multiple_into_file_descriptor(_socket->fd(), buf, ndata);
}

int TcpTransport::WaitEpollOut(butil::atomic<int>* _epollout_butex,
                               bool pollin, timespec duetime) {
    g_vars->nwaitepollout << 1;

#if BRPC_WITH_IOURING
    if (_iouring_ep && iouring::IsIouringAvailable()) {
        // io_uring path: the write side is fully async (SQEs submitted to
        // the ring, completed by the Poller thread), so there is no need to
        // block on epoll(EPOLLOUT). Instead wait on _epollout_butex, which
        // is incremented by Socket::WakeAsEpollOut() every time a write CQE
        // is reaped in IouringEndpoint::PollCq.
        const int expected = _epollout_butex->load(butil::memory_order_acquire);
        if (bthread::butex_wait(_epollout_butex, expected, &duetime) < 0) {
            if (errno != EAGAIN && errno != ETIMEDOUT) {
                const int saved_errno = errno;
                PLOG(WARNING) << "butex_wait failed for " << _socket;
                _socket->SetFailed(saved_errno, "butex_wait failed for %s: %s",
                                   _socket->description().c_str(), berror(saved_errno));
                return 1;
            }
        }
        return 0;
    }
#endif  // BRPC_WITH_IOURING

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

void TcpTransport::ProcessEvent(bthread_attr_t attr) {
    bthread_t tid;
    if (FLAGS_usercode_in_coroutine) {
        OnEdge(_socket);
    } else if (!EventDispatcherUnsched()) {
        auto rc = bthread_start_urgent(&tid, &attr, OnEdge, _socket);
        if (rc != 0) {
            LOG(FATAL) << "Fail to start ProcessEvent";
            OnEdge(_socket);
        }
    } else if (bthread_start_background(&tid, &attr, OnEdge, _socket) != 0) {
        LOG(FATAL) << "Fail to start ProcessEvent";
        OnEdge(_socket);
    }
}
void TcpTransport::QueueMessage(InputMessageClosure& input_msg,
                                int* num_bthread_created, bool) {
    InputMessageBase* to_run_msg = input_msg.release();
    if (!to_run_msg) {
        return;
    }
    // Create bthread for last_msg. The bthread is not scheduled
    // until bthread_flush() is called (in the worse case).
    bthread_t th;
    bthread_attr_t tmp =
        (FLAGS_usercode_in_pthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL) |
        BTHREAD_NOSIGNAL;
    tmp.keytable_pool = _socket->keytable_pool();
    tmp.tag = bthread_self_tag();
    bthread_attr_set_name(&tmp, "ProcessInputMessage");
    if (!FLAGS_usercode_in_coroutine && bthread_start_background(
            &th, &tmp, ProcessInputMessage, to_run_msg) == 0) {
        ++*num_bthread_created;
    } else {
        ProcessInputMessage(to_run_msg);
    }
}

void TcpTransport::Debug(std::ostream &os) {
#if BRPC_WITH_IOURING
    if (_iouring_ep) {
        _iouring_ep->DebugInfo(os);
    }
#endif  // BRPC_WITH_IOURING
}

int TcpTransport::ContextInitOrDie(bool /*serverOrNot*/, const void* /*_options*/) {
    // Unlike RdmaTransport/UBShmTransport::ContextInitOrDie (triggered by
    // choosing SOCKET_MODE_RDMA/UBRING for a given socket), io_uring is not
    // tied to a socket_mode: it is a process-wide, opt-in I/O submission
    // mechanism for ordinary TCP sockets. Bringing up the global io_uring
    // context here unconditionally would silently change the behavior (and
    // failure mode -- ProbeOpcodes() calls exit(1) on unsupported kernels)
    // of every plain-TCP Server/Channel, even ones that never asked for
    // io_uring. So this is intentionally a no-op: callers who want io_uring
    // must explicitly call iouring::GlobalIouringInitializeOrDie() and
    // iouring::InitPollingModeWithTag() before starting the Server/Channel
    // (see example/iouring_echo_c++). TcpTransport::Init() then picks it up
    // automatically via iouring::IsIouringAvailable().
    return 0;
}

} // namespace brpc
