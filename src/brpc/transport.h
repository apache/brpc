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

#ifndef BRPC_TRANSPORT_H
#define BRPC_TRANSPORT_H
#include "brpc/input_messenger.h"
#include "brpc/socket.h"
#include "server.h"

namespace brpc {
    using OnEdgeTrigger = std::function<void (Socket*)>;
    class Transport {
        friend class TransportFactory;
    public:
        static void* OnEdge(void* arg) {
            // the enclosed Socket is valid and free to access inside this function.
            Socket* socket = static_cast<Socket*>(arg);
            const OnEdgeTrigger on_edge_trigger = socket->_transport->GetOnEdgeTrigger();
            on_edge_trigger(socket);
            return NULL;
        }

        static void* ProcessInputMessage(void* void_arg) {
            InputMessageBase* msg = static_cast<InputMessageBase*>(void_arg);
            msg->_process(msg);
            return NULL;
        }
        virtual ~Transport() = default;
        virtual void Init(Socket* socket, const SocketOptions& options) = 0;
        virtual void Release() = 0;
        virtual int Reset(int32_t expected_nref) = 0;
        virtual std::shared_ptr<AppConnect> Connect() = 0;
        virtual int CutFromIOBuf(butil::IOBuf* buf) = 0;
        virtual ssize_t CutFromIOBufList(butil::IOBuf** buf, size_t ndata) = 0;
        virtual int WaitEpollOut(butil::atomic<int>* _epollout_butex, bool pollin, const timespec duetime) = 0;
        virtual void ProcessEvent(bthread_attr_t attr) = 0;
        virtual void QueueMessage(InputMessageClosure& input_msg, int* num_bthread_created, bool last_msg) = 0;
        virtual void Debug(std::ostream &os, Socket* ptr) = 0;

        bool HasOnEdgeTrigger() {
            return _on_edge_trigger != NULL;
        }
        OnEdgeTrigger GetOnEdgeTrigger() {
            return _on_edge_trigger;
        }
    protected:
        Socket* _socket;
        std::shared_ptr<AppConnect> _default_connect;
        OnEdgeTrigger _on_edge_trigger;
    };
}
#endif //BRPC_TRANSPORT_H