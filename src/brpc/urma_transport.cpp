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

#include "brpc/urma_transport.h"

#if BRPC_WITH_URMA

#include <gflags/gflags.h>

#include "butil/iobuf.h"
#include "butil/logging.h"
#include "bthread/bthread.h"
#include "bthread/types.h"

#include "brpc/event_dispatcher.h"
#include "brpc/input_messenger.h"
#include "brpc/socket.h"
#include "brpc/tcp_transport.h"
#include "brpc/urma/urma_endpoint.h"
#include "brpc/urma/urma_helper.h"

namespace brpc {

// Defined in urma_helper.cpp.
DECLARE_bool(urma_use_polling);
DECLARE_bool(urma_disable_bthread);

void UrmaTransport::Init(Socket* socket, const SocketOptions& options) {
    CHECK(_urma_ep == nullptr);
    if (options.socket_mode == SOCKET_MODE_URMA) {
        _urma_ep = new (std::nothrow) urma::UrmaEndpoint(socket);
        if (!_urma_ep) {
            const int saved_errno = errno;
            PLOG(ERROR) << "Fail to create UrmaEndpoint";
            socket->SetFailed(saved_errno, "Fail to create UrmaEndpoint: %s",
                              berror(saved_errno));
        }
        _urma_state = URMA_UNKNOWN;
    } else {
        _urma_state = URMA_OFF;
        socket->_socket_mode = SOCKET_MODE_TCP;
    }
    _socket = socket;
    _default_connect = options.app_connect;
    _on_edge_trigger = options.on_edge_triggered_events;
    if (options.need_on_edge_trigger && _on_edge_trigger == nullptr) {
        _on_edge_trigger = urma::UrmaEndpoint::OnNewDataFromTcp;
    }
    _tcp_transport = std::make_shared<TcpTransport>();
    _tcp_transport->Init(socket, options);
}

void UrmaTransport::Release() {
    if (_urma_ep) {
        delete _urma_ep;
        _urma_ep = nullptr;
    }
}

int UrmaTransport::Reset(int32_t /*expected_nref*/) {
    if (_urma_ep) {
        _urma_ep->Reset();
    }
    _urma_state = URMA_UNKNOWN;
    return 0;
}

std::shared_ptr<AppConnect> UrmaTransport::Connect() {
    if (_default_connect == nullptr) {
        return std::make_shared<urma::UrmaConnect>();
    }
    return _default_connect;
}

int UrmaTransport::CutFromIOBuf(butil::IOBuf* buf) {
    if (_urma_ep &&
        _urma_state.load(butil::memory_order_acquire) != URMA_OFF) {
        butil::IOBuf* data_arr[1] = {buf};
        return _urma_ep->CutFromIOBufList(data_arr, 1);
    } else {
        return _tcp_transport->CutFromIOBuf(buf);
    }
}

ssize_t UrmaTransport::CutFromIOBufList(butil::IOBuf** buf, size_t ndata) {
    if (_urma_ep &&
        _urma_state.load(butil::memory_order_acquire) != URMA_OFF) {
        return _urma_ep->CutFromIOBufList(buf, ndata);
    } else {
        return _tcp_transport->CutFromIOBufList(buf, ndata);
    }
}

int UrmaTransport::WaitEpollOut(butil::atomic<int>* epollout_butex,
                                bool pollin, const timespec duetime) {
    if (_urma_state.load(butil::memory_order_acquire) == URMA_ON) {
        const int expected_val =
            epollout_butex->load(butil::memory_order_acquire);
        CHECK(_urma_ep != nullptr);
        if (!_urma_ep->IsWritable()) {
            // Same caveat as RDMA: URMA cannot detect failure by writing, so
            // after a failed butex wait we must re-check _socket->Failed().
            int rc =
                bthread::butex_wait(epollout_butex, expected_val, &duetime);
            if (rc < 0 && errno != EWOULDBLOCK && errno != ETIMEDOUT) {
                const int saved_errno = errno;
                PLOG(ERROR) << "Fail to wait epollout butex";
                if (_socket->SetFailed(saved_errno, "Fail to wait epollout butex: %s",
                                        berror(saved_errno))) {
                    return 1;
                }
            }
            if (_socket->Failed()) {
                return 1;
            }
            return 0;
        }
        return 0;
    }
    return _tcp_transport->WaitEpollOut(epollout_butex, pollin, duetime);
}

void UrmaTransport::ProcessEvent(bthread_attr_t attr) {
    // Identical to TcpTransport/RdmaTransport: dispatch OnEdge(_socket) on a
    // bthread, falling back to inline invocation on bthread_start failure.
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

void UrmaTransport::QueueMessage(InputMessageClosure& input_msg,
                                 int* num_bthread_created, bool last_msg) {
    if (last_msg && !urma::FLAGS_urma_use_polling) {
        return;
    }
    InputMessageBase* to_run_msg = input_msg.release();
    if (!to_run_msg) {
        return;
    }
    if (urma::FLAGS_urma_disable_bthread) {
        Transport::ProcessInputMessage(to_run_msg);
        return;
    }
    bthread_t th;
    bthread_attr_t tmp =
        (FLAGS_usercode_in_pthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL) |
        BTHREAD_NOSIGNAL;
    tmp.keytable_pool = _socket->keytable_pool();
    tmp.tag = bthread_self_tag();
    bthread_attr_set_name(&tmp, "ProcessInputMessage");
    if (!FLAGS_usercode_in_coroutine && bthread_start_background(
            &th, &tmp, Transport::ProcessInputMessage, to_run_msg) == 0) {
        ++*num_bthread_created;
    } else {
        Transport::ProcessInputMessage(to_run_msg);
    }
}

void UrmaTransport::Debug(std::ostream& os) {
    if (_urma_state.load(butil::memory_order_acquire) == URMA_ON &&
        _urma_ep) {
        _urma_ep->DebugInfo(os);
    }
}

int UrmaTransport::ContextInitOrDie(bool server_or_not, const void* options) {
    if (server_or_not) {
        if (!OptionsAvailableOverUrma(
                static_cast<const ServerOptions*>(options))) {
            return -1;
        }
        urma::GlobalUrmaInitializeOrDie();
        if (!urma::InitPollingModeWithTag(
                static_cast<const ServerOptions*>(options)->bthread_tag)) {
            return -1;
        }
    } else {
        if (!OptionsAvailableForUrma(
                static_cast<const ChannelOptions*>(options))) {
            return -1;
        }
        urma::GlobalUrmaInitializeOrDie();
        if (!urma::InitPollingModeWithTag(bthread_self_tag())) {
            return -1;
        }
    }
    return 0;
}

bool UrmaTransport::OptionsAvailableForUrma(const ChannelOptions* opt) {
    if (opt->has_ssl_options()) {
        LOG(WARNING) << "Cannot use SSL and URMA at the same time";
        return false;
    }
    if (!urma::SupportedByUrma(opt->protocol.name())) {
        LOG(WARNING) << "Cannot use " << opt->protocol.name() << " over URMA";
        return false;
    }
    return true;
}

bool UrmaTransport::OptionsAvailableOverUrma(const ServerOptions* opt) {
    if (opt->rtmp_service) {
        LOG(WARNING) << "RTMP is not supported by URMA";
        return false;
    }
    if (opt->has_ssl_options()) {
        LOG(WARNING) << "SSL is not supported by URMA";
        return false;
    }
    if (opt->nshead_service) {
        LOG(WARNING) << "NSHEAD is not supported by URMA";
        return false;
    }
    if (opt->mongo_service_adaptor) {
        LOG(WARNING) << "MONGO is not supported by URMA";
        return false;
    }
    return true;
}

}  // namespace brpc

#endif  // BRPC_WITH_URMA
