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

#include "brpc/rdma_transport.h"
#include "brpc/adapter_transport.h"
#include "brpc/event_dispatcher.h"
#include "brpc/rdma/rdma_endpoint.h"
#include "brpc/rdma/rdma_helper.h"

namespace brpc {
DECLARE_bool(usercode_in_coroutine);
DECLARE_bool(usercode_in_pthread);

extern SocketVarsCollector *g_vars;

RdmaTransport *RdmaTransport::Get(const Socket *socket) {
  const AdapterTransport *adapter = AdapterTransport::Get(socket);
  Transport *transport = adapter->high_speed_transport();
  CHECK(transport != NULL);
  return static_cast<RdmaTransport *>(transport);
}

void RdmaTransport::Init(Socket *socket, const SocketOptions &options) {
    CHECK(_rdma_ep == nullptr);
    _socket = socket;
    _default_connect = options.app_connect;
  _on_edge_trigger = nullptr;
  _rdma_ep = new (std::nothrow) rdma::RdmaEndpoint(socket);
  if (!_rdma_ep) {
    const int saved_errno = errno != 0 ? errno : ENOMEM;
    PLOG(ERROR) << "Fail to create RdmaEndpoint";
    socket->SetFailed(saved_errno, "Fail to create RdmaEndpoint: %s",
                      berror(saved_errno));
    }
  _rdma_state = RDMA_UNKNOWN;
}

void RdmaTransport::Release() {
    if (_rdma_ep) {
        delete _rdma_ep;
        _rdma_ep = nullptr;
        _rdma_state = RDMA_UNKNOWN;
    }
}

int RdmaTransport::Reset(int32_t expected_nref) {
    if (_rdma_ep) {
        _rdma_ep->Reset();
        _rdma_state = RDMA_UNKNOWN;
    }
    return 0;
}

std::shared_ptr<AppConnect> RdmaTransport::Connect() {
  return _default_connect;
}

void RdmaTransport::SetHighSpeedAvailable(bool available) {
  _rdma_state = available ? RDMA_ON : RDMA_OFF;
}

int RdmaTransport::PrepareUpgradeResources() {
  return _rdma_ep->AllocateResources();
}

int RdmaTransport::NegotiateUpgradeResources(
    const rdma::RdmaConnectionInfo &remote, bool server) {
  _rdma_ep->ApplyRemoteInfo(remote);
  return _rdma_ep->BringUpQp(remote, server);
}

int RdmaTransport::StartUpgradeEvents() {
  return _rdma_ep->StartCqEvents();
}

std::unique_ptr<rdma::RdmaHandshakeAdapter>
RdmaTransport::CreateClientHandshakeAdapter() {
  return rdma::CreateClientHandshakeAdapter(_rdma_ep);
}

std::vector<std::unique_ptr<rdma::RdmaHandshakeAdapter>>
RdmaTransport::CreateServerHandshakeAdapters() {
  return rdma::CreateServerHandshakeAdapters(_rdma_ep);
}

void RdmaTransport::ActivateUpgrade() { SetHighSpeedAvailable(true); }

void RdmaTransport::DeactivateUpgrade() { SetHighSpeedAvailable(false); }

int RdmaTransport::CutFromIOBuf(butil::IOBuf *buf) {
  butil::IOBuf *data[1] = {buf};
  return static_cast<int>(CutFromIOBufList(data, 1));
}

ssize_t RdmaTransport::CutFromIOBufList(butil::IOBuf **buf, size_t ndata) {
  CHECK(_rdma_ep != nullptr);
  return _rdma_ep->CutFromIOBufList(buf, ndata);
}

int RdmaTransport::WaitEpollOut(butil::atomic<int> *_epollout_butex,
                                    bool pollin, const timespec duetime) {
    if (_rdma_state == RDMA_ON) {
        const int expected_val = _epollout_butex->load(butil::memory_order_acquire);
        CHECK(_rdma_ep != nullptr);
        if (!_rdma_ep->IsWritable()) {
            g_vars->nwaitepollout << 1;
            if (bthread::butex_wait(_epollout_butex, expected_val, &duetime) < 0) {
                if (errno != EAGAIN && errno != ETIMEDOUT) {
                    const int saved_errno = errno;
                    PLOG(WARNING) << "Fail to wait rdma window of " << _socket;
          _socket->SetFailed(saved_errno, "Fail to wait rdma window of %s: %s",
                                       _socket->description().c_str(),
                                       berror(saved_errno));
                }
                if (_socket->Failed()) {
                    // NOTE:
                    // Different from TCP, we cannot find the RDMA channel
                    // failed by writing to it. Thus we must check if it
                    // is already failed here.
                    return 1;
                }
            }
        }
    }
    return 0;
}

void RdmaTransport::ProcessEvent(bthread_attr_t attr) {
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

void RdmaTransport::QueueMessage(InputMessageClosure& input_msg,
                                 int* num_bthread_created, bool last_msg) {
    if (last_msg && !rdma::FLAGS_rdma_use_polling) {
        return;
    }
    InputMessageBase* to_run_msg = input_msg.release();
    if (!to_run_msg) {
        return;
    }

    if (rdma::FLAGS_rdma_disable_bthread) {
        ProcessInputMessage(to_run_msg);
        return;
    }
    // Create bthread for last_msg. The bthread is not scheduled
    // until bthread_flush() is called (in the worse case).

    // TODO(gejun): Join threads.
    bthread_t th;
  bthread_attr_t tmp =
      (FLAGS_usercode_in_pthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL) |
      BTHREAD_NOSIGNAL;
    tmp.keytable_pool = _socket->keytable_pool();
    tmp.tag = bthread_self_tag();
    bthread_attr_set_name(&tmp, "ProcessInputMessage");

  if (!FLAGS_usercode_in_coroutine &&
      bthread_start_background(&th, &tmp, ProcessInputMessage, to_run_msg) ==
          0) {
        ++*num_bthread_created;
    } else {
        ProcessInputMessage(to_run_msg);
    }
}

void RdmaTransport::Debug(std::ostream &os) {
    if (_rdma_state == RDMA_ON && _rdma_ep) {
        _rdma_ep->DebugInfo(os);
    }
}

int RdmaTransport::ContextInitOrDie(bool serverOrNot, const void* _options) {
    if (serverOrNot) {
    if (!OptionsAvailableOverRdma(
            static_cast<const ServerOptions *>(_options))) {
            return -1;
        }
        rdma::GlobalRdmaInitializeOrDie();
    if (!rdma::InitPollingModeWithTag(
            static_cast<const ServerOptions *>(_options)->bthread_tag)) {
            return -1;
        }
    } else {
    if (!OptionsAvailableForRdma(
            static_cast<const ChannelOptions *>(_options))) {
            return -1;
        }
        rdma::GlobalRdmaInitializeOrDie();
        if (!rdma::InitPollingModeWithTag(bthread_self_tag())) {
            return -1;
        }
        return 0;
    }

    return 0;
}

bool RdmaTransport::OptionsAvailableForRdma(const ChannelOptions* opt) {
    if (opt->has_ssl_options()) {
        LOG(WARNING) << "Cannot use SSL and RDMA at the same time";
        return false;
    }
    if (!rdma::SupportedByRdma(opt->protocol.name())) {
    LOG(WARNING) << "Cannot use " << opt->protocol.name() << " over RDMA";
        return false;
    }
    return true;
}

bool RdmaTransport::OptionsAvailableOverRdma(const ServerOptions* opt) {
    if (opt->rtmp_service) {
        LOG(WARNING) << "RTMP is not supported by RDMA";
        return false;
    }
    if (opt->has_ssl_options()) {
        LOG(WARNING) << "SSL is not supported by RDMA";
        return false;
    }
    if (opt->nshead_service) {
        LOG(WARNING) << "NSHEAD is not supported by RDMA";
        return false;
    }
    if (opt->mongo_service_adaptor) {
        LOG(WARNING) << "MONGO is not supported by RDMA";
        return false;
    }
    return true;
}
} // namespace brpc
#endif
