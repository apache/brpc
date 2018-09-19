// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Rujie Jiang(jiangrujie@baidu.com)
//          Ge,Jun(gejun@baidu.com)

#include <inttypes.h>
#include <gflags/gflags.h>
#include "butil/fd_guard.h"                 // fd_guard 
#include "butil/fd_utility.h"               // make_close_on_exec
#include "butil/time.h"                     // gettimeofday_us
#include "brpc/acceptor.h"


namespace brpc {

static const int INITIAL_CONNECTION_CAP = 65536;

Acceptor::Acceptor(bthread_keytable_pool_t* pool)
    : InputMessenger()
    , _keytable_pool(pool)
    , _status(UNINITIALIZED)
    , _idle_timeout_sec(-1)
    , _close_idle_tid(INVALID_BTHREAD)
    , _listened_fd(-1)
    , _acception_id(0)
    , _empty_cond(&_map_mutex)
    , _ssl_ctx(NULL) {
}

Acceptor::~Acceptor() {
    StopAccept(0);
    Join();
}

int Acceptor::StartAccept(int listened_fd, int idle_timeout_sec,
                          const std::shared_ptr<SocketSSLContext>& ssl_ctx) {
    if (listened_fd < 0) {
        LOG(FATAL) << "Invalid listened_fd=" << listened_fd;
        return -1;
    }
    
    BAIDU_SCOPED_LOCK(_map_mutex);
    if (_status == UNINITIALIZED) {
        if (Initialize() != 0) {
            LOG(FATAL) << "Fail to initialize Acceptor";
            return -1;
        }
        _status = READY;
    }
    if (_status != READY) {
        LOG(FATAL) << "Acceptor hasn't stopped yet: status=" << status();
        return -1;
    }
    if (idle_timeout_sec > 0) {
        if (bthread_start_background(&_close_idle_tid, NULL,
                                     CloseIdleConnections, this) != 0) {
            LOG(FATAL) << "Fail to start bthread";
            return -1;
        }
    }
    _idle_timeout_sec = idle_timeout_sec;
    _ssl_ctx = ssl_ctx;
    
    // Creation of _acception_id is inside lock so that OnNewConnections
    // (which may run immediately) should see sane fields set below.
    SocketOptions options;
    options.fd = listened_fd;
    options.user = this;
    options.on_edge_triggered_events = OnNewConnections;
    if (Socket::Create(options, &_acception_id) != 0) {
        // Close-idle-socket thread will be stopped inside destructor
        LOG(FATAL) << "Fail to create _acception_id";
        return -1;
    }
    
    _listened_fd = listened_fd;
    _status = RUNNING;
    return 0;
}

void* Acceptor::CloseIdleConnections(void* arg) {
    Acceptor* am = static_cast<Acceptor*>(arg);
    std::vector<SocketId> checking_fds;
    const uint64_t CHECK_INTERVAL_US = 1000000UL;
    while (bthread_usleep(CHECK_INTERVAL_US) == 0) {
        // TODO: this is not efficient for a lot of connections(>100K)
        am->ListConnections(&checking_fds);
        for (size_t i = 0; i < checking_fds.size(); ++i) {
            SocketUniquePtr s;
            if (Socket::Address(checking_fds[i], &s) == 0) {
                s->ReleaseReferenceIfIdle(am->_idle_timeout_sec);
            }
        }
    }
    return NULL;
}

void Acceptor::StopAccept(int /*closewait_ms*/) {
    // Currently `closewait_ms' is useless since we have to wait until 
    // existing requests are finished. Otherwise, contexts depended by 
    // the requests may be deleted and invalid.

    {
        BAIDU_SCOPED_LOCK(_map_mutex);
        if (_status != RUNNING) {
            return;
        }
        _status = STOPPING;
    }

    // Don't set _acception_id to 0 because BeforeRecycle needs it.
    Socket::SetFailed(_acception_id);

    // SetFailed all existing connections. Connections added after this piece
    // of code will be SetFailed directly in OnNewConnectionsUntilEAGAIN
    std::vector<SocketId> erasing_ids;
    ListConnections(&erasing_ids);
    
    for (size_t i = 0; i < erasing_ids.size(); ++i) {
        SocketUniquePtr socket;
        if (Socket::Address(erasing_ids[i], &socket) == 0) {
            if (socket->shall_fail_me_at_server_stop()) {
                // Mainly streaming connections, should be SetFailed() to
                // trigger callbacks to NotifyOnFailed() to remove references,
                // otherwise the sockets are often referenced by corresponding
                // objects and delay server's stopping which requires all
                // existing sockets to be recycled.
                socket->SetFailed(ELOGOFF, "Server is stopping");
            } else {
                // Message-oriented RPC connections. Just release the addtional
                // reference in the socket, which will be recycled when current
                // requests have been processed.
                socket->ReleaseAdditionalReference();
            }
        } // else: This socket already called `SetFailed' before
    }
}

int Acceptor::Initialize() {
    if (_socket_map.init(INITIAL_CONNECTION_CAP) != 0) {
        LOG(FATAL) << "Fail to initialize FlatMap, size="
                   << INITIAL_CONNECTION_CAP;
        return -1;
    }
    return 0;    
}

// NOTE: Join() can happen before StopAccept()
void Acceptor::Join() {
    std::unique_lock<butil::Mutex> mu(_map_mutex);
    if (_status != STOPPING && _status != RUNNING) {  // no need to join.
        return;
    }
    // `_listened_fd' will be set to -1 once it has been recycled
    while (_listened_fd > 0 || !_socket_map.empty()) {
        _empty_cond.Wait();
    }
    const int saved_idle_timeout_sec = _idle_timeout_sec;
    _idle_timeout_sec = 0;
    const bthread_t saved_close_idle_tid = _close_idle_tid;
    mu.unlock();

    // Join the bthread outside lock.
    if (saved_idle_timeout_sec > 0) {
        bthread_stop(saved_close_idle_tid);
        bthread_join(saved_close_idle_tid, NULL);
    }
    
    {
        BAIDU_SCOPED_LOCK(_map_mutex);
        _status = READY;
    }
}

size_t Acceptor::ConnectionCount() const {
    // Notice that _socket_map may be modified concurrently. This actually
    // assumes that size() is safe to call concurrently.
    return _socket_map.size();
}

void Acceptor::ListConnections(std::vector<SocketId>* conn_list,
                               size_t max_copied) {
    if (conn_list == NULL) {
        LOG(FATAL) << "Param[conn_list] is NULL";
        return;
    }
    conn_list->clear();
    // Add additional 10(randomly small number) so that even if
    // ConnectionCount is inaccurate, enough space is reserved
    conn_list->reserve(ConnectionCount() + 10);

    std::unique_lock<butil::Mutex> mu(_map_mutex);
    if (!_socket_map.initialized()) {
        // Optional. Uninitialized FlatMap should be iteratable.
        return;
    }
    // Copy all the SocketId (protected by mutex) into a temporary
    // container to avoid dealing with sockets inside the mutex.
    size_t ntotal = 0;
    size_t n = 0;
    for (SocketMap::const_iterator it = _socket_map.begin();
         it != _socket_map.end(); ++it, ++ntotal) {
        if (ntotal >= max_copied) {
            return;
        }
        if (++n >= 256/*max iterated one pass*/) {
            SocketMap::PositionHint hint;
            _socket_map.save_iterator(it, &hint);
            n = 0;
            mu.unlock();  // yield
            mu.lock();
            it = _socket_map.restore_iterator(hint);
            if (it == _socket_map.begin()) { // resized
                conn_list->clear();
            }
            if (it == _socket_map.end()) {
                break;
            }
        }
        conn_list->push_back(it->first);
    }
}

void Acceptor::ListConnections(std::vector<SocketId>* conn_list) {
    return ListConnections(conn_list, std::numeric_limits<size_t>::max());
}

void Acceptor::OnNewConnectionsUntilEAGAIN(Socket* acception) {
    while (1) {
        struct sockaddr in_addr;
        socklen_t in_len = sizeof(in_addr);
        butil::fd_guard in_fd(accept(acception->fd(), &in_addr, &in_len));
        if (in_fd < 0) {
            // no EINTR because listened fd is non-blocking.
            if (errno == EAGAIN) {
                return;
            }
            // Do NOT return -1 when `accept' failed, otherwise `_listened_fd'
            // will be closed. Continue to consume all the events until EAGAIN
            // instead.
            // If the accept was failed, the error may repeat constantly, 
            // limit frequency of logging.
            PLOG_EVERY_SECOND(ERROR)
                << "Fail to accept from listened_fd=" << acception->fd();
            continue;
        }

        Acceptor* am = dynamic_cast<Acceptor*>(acception->user());
        if (NULL == am) {
            LOG(FATAL) << "Impossible! acception->user() MUST be Acceptor";
            acception->SetFailed(EINVAL, "Impossible! acception->user() MUST be Acceptor");
            return;
        }
        
        SocketId socket_id;
        SocketOptions options;
        options.keytable_pool = am->_keytable_pool;
        options.fd = in_fd;
        options.remote_side = butil::EndPoint(*(sockaddr_in*)&in_addr);
        options.user = acception->user();
        options.on_edge_triggered_events = InputMessenger::OnNewMessages;
        options.initial_ssl_ctx = am->_ssl_ctx;
        if (Socket::Create(options, &socket_id) != 0) {
            LOG(ERROR) << "Fail to create Socket";
            continue;
        }
        in_fd.release(); // transfer ownership to socket_id

        // There's a funny race condition here. After Socket::Create, messages
        // from the socket are already handled and a RPC is possibly done
        // before the socket is added into _socket_map below. This is found in
        // ChannelTest.skip_parallel in test/brpc_channel_unittest.cpp (running
        // on machines with few cores) where the _messenger.ConnectionCount()
        // may surprisingly be 0 even if the RPC is already done.

        SocketUniquePtr sock;
        if (Socket::AddressFailedAsWell(socket_id, &sock) >= 0) {
            bool is_running = true;
            {
                BAIDU_SCOPED_LOCK(am->_map_mutex);
                is_running = (am->status() == RUNNING);
                // Always add this socket into `_socket_map' whether it
                // has been `SetFailed' or not, whether `Acceptor' is
                // running or not. Otherwise, `Acceptor::BeforeRecycle'
                // may be called (inside Socket::OnRecycle) after `Acceptor'
                // has been destroyed
                am->_socket_map.insert(socket_id, ConnectStatistics());
            }
            if (!is_running) {
                LOG(WARNING) << "Acceptor on fd=" << acception->fd()
                    << " has been stopped, discard newly created " << *sock;
                sock->SetFailed(ELOGOFF, "Acceptor on fd=%d has been stopped, "
                        "discard newly created %s", acception->fd(),
                        sock->description().c_str());
                return;
            }
        } // else: The socket has already been destroyed, Don't add its id
          // into _socket_map
    }
}

void Acceptor::OnNewConnections(Socket* acception) {
    int progress = Socket::PROGRESS_INIT;
    do {
        OnNewConnectionsUntilEAGAIN(acception);
        if (acception->Failed()) {
            return;
        }
    } while (acception->MoreReadEvents(&progress));
}

void Acceptor::BeforeRecycle(Socket* sock) {
    BAIDU_SCOPED_LOCK(_map_mutex);
    if (sock->id() == _acception_id) {
        // Set _listened_fd to -1 when acception socket has been recycled
        // so that we are ensured no more events will arrive (and `Join'
        // will return to its caller)
        _listened_fd = -1;
        _empty_cond.Broadcast();
        return;
    }
    // If a Socket could not be addressed shortly after its creation, it
    // was not added into `_socket_map'.
    _socket_map.erase(sock->id());
    if (_socket_map.empty()) {
        _empty_cond.Broadcast();
    }
}

} // namespace brpc
