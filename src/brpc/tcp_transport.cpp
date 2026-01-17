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

#include "tcp_transport.h"
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
    }

    void TcpTransport::Release(){}

    int TcpTransport::Reset(int32_t expected_nref) {
        return 0;
    }

    int TcpTransport::CutFromIOBuf(butil::IOBuf* buf) {
        return buf->cut_into_file_descriptor(_socket->fd());
    }

    std::shared_ptr<AppConnect> TcpTransport::Connect() {
        return _default_connect;
    }

    ssize_t TcpTransport::CutFromIOBufList(butil::IOBuf** buf, size_t ndata) {
        return butil::IOBuf::cut_multiple_into_file_descriptor(_socket->fd(), buf, ndata);
    }

    int TcpTransport::WaitEpollOut(butil::atomic<int>* _epollout_butex, bool pollin, const timespec duetime) {
        g_vars->nwaitepollout << 1;
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
        } else if (bthread_start_urgent(&tid, &attr, OnEdge, _socket) != 0) {
            LOG(FATAL) << "Fail to start ProcessEvent";
            OnEdge(_socket);
        }
    }
    void TcpTransport::QueueMessage(InputMessageClosure& input_msg, int* num_bthread_created, bool last_msg) {
        InputMessageBase* to_run_msg = input_msg.release();
        if (!to_run_msg) {
            return;
        }
        // Create bthread for last_msg. The bthread is not scheduled
        // until bthread_flush() is called (in the worse case).
        bthread_t th;
        bthread_attr_t tmp = (FLAGS_usercode_in_pthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL) | BTHREAD_NOSIGNAL;
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
    void TcpTransport::Debug(std::ostream &os, Socket* ptr) {}
}