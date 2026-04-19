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

#include "brpc/ub_transport.h"
#include "brpc/tcp_transport.h"
#include "brpc/ubring/ub_endpoint.h"
#include "brpc/ubring/ub_helper.h"

namespace brpc {
DECLARE_bool(usercode_in_coroutine);
DECLARE_bool(usercode_in_pthread);

extern SocketVarsCollector *g_vars;

void UBShmTransport::Init(Socket *socket, const SocketOptions &options) {
    CHECK(_ub_ep == NULL);
    if (options.socket_mode == SOCKET_MODE_UBRING) {
        _ub_ep = new(std::nothrow)ubring::UBShmEndpoint(socket);
        if (!_ub_ep) {
            const int saved_errno = errno;
            PLOG(ERROR) << "Fail to create UBShmEndpoint";
            socket->SetFailed(
                saved_errno, "Fail to create UBShmEndpoint: %s", berror(saved_errno));
        }
        _ub_state = UB_UNKNOWN;
    } else {
        _ub_state = UB_OFF;
        socket->_socket_mode = SOCKET_MODE_TCP;
    }
    _socket = socket;
    _default_connect = options.app_connect;
    _on_edge_trigger = options.on_edge_triggered_events;
    if (options.need_on_edge_trigger && _on_edge_trigger == NULL) {
        _on_edge_trigger = ubring::UBShmEndpoint::OnNewDataFromTcp;
    }
    _tcp_transport = std::unique_ptr<TcpTransport>(new TcpTransport());
    _tcp_transport->Init(socket, options);
}

void UBShmTransport::Release() {
    if (_ub_ep) {
        delete _ub_ep;
        _ub_ep = NULL;
        _ub_state = UB_UNKNOWN;
    }
}

int UBShmTransport::Reset(int32_t expected_nref) {
    if (_ub_ep) {
        _ub_ep->Reset();
        _ub_state = UB_UNKNOWN;
    }
    return 0;
}

std::shared_ptr<AppConnect> UBShmTransport::Connect() {
    if (_default_connect == nullptr) {
        return  std::make_shared<ubring::UBConnect>();
    }
    return _default_connect;
}

int UBShmTransport::CutFromIOBuf(butil::IOBuf *buf) {
    if (_ub_ep && _ub_state != UB_OFF) {
        butil::IOBuf *data_arr[1] = {buf};
        return _ub_ep->CutFromIOBufList(data_arr, 1);
    } else {
        return _tcp_transport->CutFromIOBuf(buf);
    }
}

ssize_t UBShmTransport::CutFromIOBufList(butil::IOBuf **buf, size_t ndata) {
    if (_ub_ep && _ub_state != UB_OFF) {
        return _ub_ep->CutFromIOBufList(buf, ndata);
    }
    return _tcp_transport->CutFromIOBufList(buf, ndata);
}

int UBShmTransport::WaitEpollOut(butil::atomic<int> *_epollout_butex,
                                    bool pollin, const timespec duetime) {
    // LOG(INFO) << "mwj pollin4=" << pollin << " duetime=" << butil::timespec_to_microseconds(duetime);
    if (_ub_state == UB_ON) {
        // LOG(INFO) << "mwj pollin1=" << pollin;
        const int expected_val = _epollout_butex->load(butil::memory_order_acquire);
        CHECK(_ub_ep != NULL);
        if (!_ub_ep->IsWritable()) {
            g_vars->nwaitepollout << 1;
            _ub_ep->PollerRegisterEpollOut(pollin);
            auto mwj_ret = bthread::butex_wait(_epollout_butex, expected_val, &duetime);
            // LOG(INFO) << "mwj pollin2=" << pollin << " mwj_ret=" << mwj_ret;
            if (mwj_ret < 0) {
                if (errno != EAGAIN && errno != ETIMEDOUT) {
                    const int saved_errno = errno;
                    PLOG(WARNING) << "Fail to wait ub window of " << _socket;
                    _socket->SetFailed(saved_errno,
                                       "Fail to wait ub window of %s: %s",
                                       _socket->description().c_str(),
                                       berror(saved_errno));
                }
                if (_socket->Failed()) {
                    // NOTE:
                    // Different from TCP, we cannot find the UB channel
                    // failed by writing to it. Thus we must check if it
                    // is already failed here.
                    return 1;
                }
            }
            _ub_ep->PollerUnRegisterEpollOut(pollin);
        }
    } else {
        return _tcp_transport->WaitEpollOut(_epollout_butex, pollin, duetime);
    }
    // LOG(INFO) << "mwj return 0";
    return 0;
}

void UBShmTransport::ProcessEvent(bthread_attr_t attr) {
    bthread_t tid;
    if (FLAGS_usercode_in_coroutine) {
        OnEdge(_socket);
    } else if (ubring::FLAGS_ub_edisp_unsched == false) {
        auto rc = bthread_start_background(&tid, &attr, OnEdge, _socket);
        if (rc != 0) {
            LOG(FATAL) << "Fail to start ProcessEvent";
            OnEdge(_socket);
        }
    } else if (bthread_start_urgent(&tid, &attr, OnEdge, _socket) != 0) {
        LOG(FATAL) << "Fail to start ProcessEvent";
        OnEdge(_socket);
    }
}

void UBShmTransport::QueueMessage(InputMessageClosure& input_msg,
                                 int* num_bthread_created, bool last_msg) {
    if (last_msg) {
        return;
    }
    InputMessageBase* to_run_msg = input_msg.release();
    if (!to_run_msg) {
        return;
    }

    if (ubring::FLAGS_ub_disable_bthread) {
        ProcessInputMessage(to_run_msg);
        return;
    }
    // Create bthread for last_msg. The bthread is not scheduled
    // until bthread_flush() is called (in the worse case).

    // TODO(gejun): Join threads.
    bthread_t th;
    bthread_attr_t tmp = (FLAGS_usercode_in_pthread ?
                                      BTHREAD_ATTR_PTHREAD :
                                                                    BTHREAD_ATTR_NORMAL) | BTHREAD_NOSIGNAL;
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

void UBShmTransport::Debug(std::ostream &os) {}

int UBShmTransport::ContextInitOrDie(bool serverOrNot, const void* _options) {
    if (serverOrNot) {
        if (!OptionsAvailableOverUB(static_cast<const ServerOptions *>(_options))) {
            return -1;
        }
        ubring::GlobalUBInitializeOrDie();
        if (!ubring::InitPollingModeWithTag(static_cast<const ServerOptions *>(_options)->bthread_tag)) {
            return -1;
        }
    } else {
        if (!OptionsAvailableForUB(static_cast<const ChannelOptions *>(_options))) {
            return -1;
        }
        ubring::GlobalUBInitializeOrDie();
        if (!ubring::InitPollingModeWithTag(bthread_self_tag())) {
            return -1;
        }
        return 0;
    }

    return 0;
}

bool UBShmTransport::OptionsAvailableForUB(const ChannelOptions* opt) {
    if (opt->has_ssl_options()) {
        LOG(WARNING) << "Cannot use SSL and UB at the same time";
        return false;
    }
    if (!ubring::SupportedByUB(opt->protocol.name())) {
        LOG(WARNING) << "Cannot use " << opt->protocol.name()
                     << " over UB";
        return false;
    }
    return true;
}

bool UBShmTransport::OptionsAvailableOverUB(const ServerOptions* opt) {
    if (opt->rtmp_service) {
        LOG(WARNING) << "RTMP is not supported by UB";
        return false;
    }
    if (opt->has_ssl_options()) {
        LOG(WARNING) << "SSL is not supported by UB";
        return false;
    }
    if (opt->nshead_service) {
        LOG(WARNING) << "NSHEAD is not supported by UB";
        return false;
    }
    if (opt->mongo_service_adaptor) {
        LOG(WARNING) << "MONGO is not supported by UB";
        return false;
    }
    return true;
}
} // namespace brpc
#endif