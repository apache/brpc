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

#ifndef BRPC_ACCEPTOR_H
#define BRPC_ACCEPTOR_H

#include "bthread/bthread.h"                       // bthread_t
#include "butil/synchronization/condition_variable.h"
#include "butil/containers/flat_map.h"
#include "brpc/input_messenger.h"


namespace brpc {

struct ConnectStatistics {
};

// Accept connections from a specific port and then
// process messages from which it reads
class Acceptor : public InputMessenger {
friend class Server;
public:
    typedef butil::FlatMap<SocketId, ConnectStatistics> SocketMap;

    enum Status {
        UNINITIALIZED = 0,
        READY = 1,
        RUNNING = 2,
        STOPPING = 3,
    };

public:
    explicit Acceptor(bthread_keytable_pool_t* pool = NULL);
    ~Acceptor();

    // [thread-safe] Accept connections from `listened_fd'. Ownership of
    // `listened_fd' is also transferred to `Acceptor'. Can be called
    // multiple times if the last `StartAccept' has been completely stopped
    // by calling `StopAccept' and `Join'. Connections that has no data
    // transmission for `idle_timeout_sec' will be closed automatically iff
    // `idle_timeout_sec' > 0
    // Return 0 on success, -1 otherwise.
    int StartAccept(int listened_fd, int idle_timeout_sec,
                    const std::shared_ptr<SocketSSLContext>& ssl_ctx,
                    bool force_ssl);

    // [thread-safe] Stop accepting connections.
    // `closewait_ms' is not used anymore.
    void StopAccept(int /*closewait_ms*/);

    // Wait until all existing Sockets(defined in socket.h) are recycled.
    void Join();

    // The parameter to StartAccept. Negative when acceptor is stopped.
    int listened_fd() const { return _listened_fd; }

    // Get number of existing connections.
    size_t ConnectionCount() const;

    // Clear `conn_list' and append all connections into it.
    void ListConnections(std::vector<SocketId>* conn_list);

    // Clear `conn_list' and append all most `max_copied' connections into it.
    void ListConnections(std::vector<SocketId>* conn_list, size_t max_copied);

    Status status() const { return _status; }

private:
    // Accept connections.
    static void OnNewConnectionsUntilEAGAIN(Socket* m);
    static void OnNewConnections(Socket* m);

    static void* CloseIdleConnections(void* arg);
    
    // Initialize internal structure. 
    int Initialize();

    // Remove the accepted socket `sock' from inside
    void BeforeRecycle(Socket* sock) override;

    bthread_keytable_pool_t* _keytable_pool; // owned by Server
    Status _status;
    int _idle_timeout_sec;
    bthread_t _close_idle_tid;

    int _listened_fd;
    // The Socket tso accept connections.
    SocketId _acception_id;

    butil::Mutex _map_mutex;
    butil::ConditionVariable _empty_cond;
    
    // The map containing all the accepted sockets
    SocketMap _socket_map;

    bool _force_ssl;
    std::shared_ptr<SocketSSLContext> _ssl_ctx;

    // Whether to use rdma or not
    bool _use_rdma;
};

} // namespace brpc


#endif // BRPC_ACCEPTOR_H
