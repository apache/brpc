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


#ifndef BRPC_SOCKET_H
#define BRPC_SOCKET_H

#include <iostream>                            // std::ostream
#include <deque>                               // std::deque
#include <set>                                 // std::set
#include "butil/atomicops.h"                    // butil::atomic
#include "bthread/types.h"                      // bthread_id_t
#include "butil/iobuf.h"                        // butil::IOBuf, IOPortal
#include "butil/macros.h"                       // DISALLOW_COPY_AND_ASSIGN
#include "butil/endpoint.h"                     // butil::EndPoint
#include "butil/resource_pool.h"                // butil::ResourceId
#include "bthread/butex.h"                      // butex_create_checked
#include "brpc/authenticator.h"           // Authenticator
#include "brpc/errno.pb.h"                // EFAILEDSOCKET
#include "brpc/details/ssl_helper.h"      // SSLState
#include "brpc/stream.h"                  // StreamId
#include "brpc/destroyable.h"             // Destroyable
#include "brpc/options.pb.h"              // ConnectionType
#include "brpc/socket_id.h"               // SocketId
#include "brpc/socket_message.h"          // SocketMessagePtr
#include "bvar/bvar.h"

namespace brpc {
namespace policy {
class ConsistentHashingLoadBalancer;
class RtmpContext;
class H2GlobalStreamCreator;
}  // namespace policy
namespace schan {
class ChannelBalancer;
}
namespace rdma {
class RdmaEndpoint;
class RdmaConnect;
}

class Socket;
class AuthContext;
class EventDispatcher;
class Stream;

// A special closure for processing the about-to-recycle socket. Socket does
// not delete SocketUser, if you want, `delete this' at the end of
// BeforeRecycle().
class SocketUser {
public:
    virtual ~SocketUser() {}
    virtual void BeforeRecycle(Socket*) {}

    // Will be periodically called in a dedicated thread to check the
    // health.
    // If the return value is 0, the socket is revived.
    // If the return value is ESTOP, the health-checking thread quits.
    // The default impl is testing health by connection.
    virtual int CheckHealth(Socket*);

    // Called after revived.
    virtual void AfterRevived(Socket*);
};

// TODO: Remove this class which is replace-able with SocketMessage
// A special closure for handling fd related connection. The Socket does
// not delete SocketConnection, if you want, `delete this' at the end of
// BeforeRecycle().
class SocketConnection {
public:
    virtual ~SocketConnection() {}
    virtual void BeforeRecycle(Socket*) = 0;

    // Establish a connection, call on_connect after connection finishes
    virtual int Connect(Socket*, const timespec*,
                        int (*on_connect)(int, int, void*), void*) = 0;

    // Cut IOBufs into fd or SSL Channel
    virtual ssize_t CutMessageIntoFileDescriptor(int, butil::IOBuf**, size_t) = 0;
    virtual ssize_t CutMessageIntoSSLChannel(SSL*, butil::IOBuf**, size_t) = 0;
};

// Application-level connect. After TCP connected, the client sends some
// sort of "connect" message to the server to establish application-level
// connection.
// Instances of AppConnect may be shared by multiple sockets and often
// created by std::make_shared<T>() where T implements AppConnect
class AppConnect {
public:
    virtual ~AppConnect() {}

    // Called after TCP connected. Call done(error, data) when
    // the application-level connection is established.
    // Notice that `socket' can only be used for getting information of
    // the connection. To write into the socket, write socket->fd() with
    // sys_write directly. This is because Socket::Write() does not really
    // write out until AppConnect is done.
    virtual void StartConnect(const Socket* socket,
                              void (*done)(int err, void* data),
                              void* data) = 0;

    // Called when the host socket is setfailed or about to be recycled.
    // If the AppConnect is still in-progress, it should be canceled properly.
    virtual void StopConnect(Socket*) = 0;
};

// _s = per second, _m = per minute
struct SocketStat {
    uint32_t in_size_s;
    uint32_t out_size_s;
    uint32_t in_num_messages_s;
    uint32_t out_num_messages_s;
    uint64_t in_size_m; // must be 64-bit
    uint64_t out_size_m;
    uint32_t in_num_messages_m;
    uint32_t out_num_messages_m;
};

struct SocketVarsCollector {
    SocketVarsCollector()
        : nsocket("rpc_socket_count")
        , channel_conn("rpc_channel_connection_count")
        , neventthread_second("rpc_event_thread_second", &neventthread)
        , nhealthcheck("rpc_health_check_count")
        , nkeepwrite_second("rpc_keepwrite_second", &nkeepwrite)
        , nwaitepollout("rpc_waitepollout_count")
        , nwaitepollout_second("rpc_waitepollout_second", &nwaitepollout)
    {}

    bvar::Adder<int64_t> nsocket;
    bvar::Adder<int64_t> channel_conn;
    bvar::Adder<int> neventthread;
    bvar::PerSecond<bvar::Adder<int> > neventthread_second;
    bvar::Adder<int64_t> nhealthcheck;
    bvar::Adder<int64_t> nkeepwrite;
    bvar::PerSecond<bvar::Adder<int64_t> > nkeepwrite_second;
    bvar::Adder<int64_t> nwaitepollout;
    bvar::PerSecond<bvar::Adder<int64_t> > nwaitepollout_second;
};

struct PipelinedInfo {
    PipelinedInfo() { reset(); }
    void reset() {
        count = 0;
        auth_flags = 0;
        id_wait = INVALID_BTHREAD_ID;
    }
    uint32_t count;
    uint32_t auth_flags;
    bthread_id_t id_wait;
};

struct SocketSSLContext {
    SocketSSLContext();
    ~SocketSSLContext();

    SSL_CTX* raw_ctx;           // owned
    std::string sni_name;       // useful for clients
};

struct SocketKeepaliveOptions {
    SocketKeepaliveOptions()
        : keepalive_idle_s(-1)
        , keepalive_interval_s(-1)
        , keepalive_count(-1)
        {}
    // Start keeplives after this period.
    int keepalive_idle_s;
    // Interval between keepalives.
    int keepalive_interval_s;
    // Number of keepalives before death.
    int keepalive_count;
};

// TODO: Comment fields
struct SocketOptions {
    SocketOptions();

    // If `fd' is non-negative, set `fd' to be non-blocking and take the
    // ownership. Socket will close the fd(if needed) and call
    // user->BeforeRecycle() before recycling.
    int fd;
    butil::EndPoint remote_side;
    SocketUser* user;
    // When *edge-triggered* events happen on the file descriptor, callback
    // `on_edge_triggered_events' will be called. Inside the callback, user
    // shall read fd() in non-blocking mode until all data has been read
    // or EAGAIN is met, otherwise the callback will not be called again
    // until new data arrives. The callback will not be called from more than
    // one thread at any time.
    void (*on_edge_triggered_events)(Socket*);
    int health_check_interval_s;
    // Only accept ssl connection.
    bool force_ssl;
    std::shared_ptr<SocketSSLContext> initial_ssl_ctx;
    bool use_rdma;
    bthread_keytable_pool_t* keytable_pool;
    SocketConnection* conn;
    std::shared_ptr<AppConnect> app_connect;
    // The created socket will set parsing_context with this value.
    Destroyable* initial_parsing_context;

    // Socket keepalive related options.
    // Refer to `SocketKeepaliveOptions' for details.
    std::shared_ptr<SocketKeepaliveOptions> keepalive_options;
};

// Abstractions on reading from and writing into file descriptors.
// NOTE: accessed by multiple threads(frequently), align it by cacheline.
class BAIDU_CACHELINE_ALIGNMENT/*note*/ Socket {
friend class EventDispatcher;
friend class InputMessenger;
friend class Acceptor;
friend class ConnectionsService;
friend class SocketUser;
friend class Stream;
friend class Controller;
friend class policy::ConsistentHashingLoadBalancer;
friend class policy::RtmpContext;
friend class schan::ChannelBalancer;
friend class rdma::RdmaEndpoint;
friend class rdma::RdmaConnect;
friend class HealthCheckTask;
friend class OnAppHealthCheckDone;
friend class HealthCheckManager;
friend class policy::H2GlobalStreamCreator;
    class SharedPart;
    struct Forbidden {};
    struct WriteRequest;

public:
    const static int STREAM_FAKE_FD = INT_MAX;
    // NOTE: User cannot create Socket from constructor. Use Create()
    // instead. It's public just because of requirement of ResourcePool.
    Socket(Forbidden);
    ~Socket();

    // Write `msg' into this Socket and clear it. The `msg' should be an
    // intact request or response. To prevent messages from interleaving
    // with other messages, the internal file descriptor is written by one
    // thread at any time. Namely when only one thread tries to write, the
    // message is written once directly in the calling thread. If the message
    // is not completely written, a KeepWrite thread is created to continue
    // the writing. When other threads want to write simultaneously (thread
    // contention), they append WriteRequests to the KeepWrite thread in a
    // wait-free manner rather than writing to the file descriptor directly.
    // KeepWrite will not quit until all WriteRequests are complete.
    // Key properties:
    // - all threads have similar opportunities to write, no one is starved.
    // - Write once when uncontended(most cases).
    // - Wait-free when contended.
    struct WriteOptions {
        // `id_wait' is signalled when this Socket is SetFailed. To disable
        // the signal, set this field to INVALID_BTHREAD_ID.
        // `on_reset' of `id_wait' is NOT called when Write() returns non-zero.
        // Default: INVALID_BTHREAD_ID
        bthread_id_t id_wait;
        // If no connection exists, a connection will be established to
        // remote_side() regarding deadline `abstime'. NULL means no timeout.
        // Default: NULL
        const timespec* abstime;

        // Will be queued to implement positional correspondence with responses
        // Default: 0
        uint32_t pipelined_count;

        // [Only effective when pipelined_count is non-zero]
        // The request contains authenticating information which will be
        // responded by the server and processed specially when dealing
        // with the response.
        uint32_t auth_flags;

        // Do not return EOVERCROWDED
        // Default: false
        bool ignore_eovercrowded;

        // The calling thread directly creates KeepWrite thread to write into
        // this socket, skipping writing once.
        // In situations like when you are continually issuing lots of
        // StreamWrite or async RPC calls in only one thread, directly creating
        // KeepWrite thread at first provides batch write effect and better
        // performance. Otherwise, each write only writes one `msg` into socket
        // and no KeepWrite thread can be created, which brings poor
        // performance.
        bool write_in_background;

        WriteOptions()
            : id_wait(INVALID_BTHREAD_ID), abstime(NULL)
            , pipelined_count(0), auth_flags(0)
            , ignore_eovercrowded(false), write_in_background(false) {}
    };
    int Write(butil::IOBuf *msg, const WriteOptions* options = NULL);

    // Write an user-defined message. `msg' is released when Write() is
    // successful and *may* remain unchanged otherwise.
    int Write(SocketMessagePtr<>& msg, const WriteOptions* options = NULL);

    // The file descriptor
    int fd() const { return _fd.load(butil::memory_order_relaxed); }

    // ip/port of the local end of the connection
    butil::EndPoint local_side() const { return _local_side; }

    // ip/port of the other end of the connection.
    butil::EndPoint remote_side() const { return _remote_side; }

    // Positive value enables health checking.
    // Initialized by SocketOptions.health_check_interval_s.
    int health_check_interval() const { return _health_check_interval_s; }

    // When someone holds a health-checking-related reference,
    // this function need to be called to make health checking run normally.
    void SetHCRelatedRefHeld() { _is_hc_related_ref_held = true; }
    // When someone releases the health-checking-related reference,
    // this function need to be called to cancel health checking.
    void SetHCRelatedRefReleased() { _is_hc_related_ref_held = false; }
    bool IsHCRelatedRefHeld() const { return _is_hc_related_ref_held; }

    // After health checking is complete, set _hc_started to false.
    void AfterHCCompleted() { _hc_started.store(false, butil::memory_order_relaxed); }

    // The unique identifier.
    SocketId id() const { return _this_id; }

    // `user' parameter passed to Create().
    SocketUser* user() const { return _user; }

    // `conn' parameter passed to Create()
    void set_conn(SocketConnection* conn) { _conn = conn; }
    SocketConnection* conn() const { return _conn; }

    // Saved contexts for parsing. Reset before trying new protocols and
    // recycling of the socket.
    void reset_parsing_context(Destroyable*);
    Destroyable* release_parsing_context();
    Destroyable* parsing_context() const
    { return _parsing_context.load(butil::memory_order_consume); }
    // Try to set _parsing_context to *ctx when _parsing_context is NULL.
    // If _parsing_context is NULL, the set is successful and true is returned.
    // Otherwise, *ctx is Destroy()-ed and replaced with the value of
    // _parsing_context, and false is returned. This process is thread-safe.
    template <typename T> bool initialize_parsing_context(T** ctx);

    // Connection-specific result of authentication.
    const AuthContext* auth_context() const { return _auth_context; }
    AuthContext* mutable_auth_context();

    // Create a Socket according to `options', put the identifier into `id'.
    // Returns 0 on sucess, -1 otherwise.
    static int Create(const SocketOptions& options, SocketId* id);

    // Place the Socket associated with identifier `id' into unique_ptr `ptr',
    // which will be released automatically when out of scope (w/o explicit
    // std::move). User can still access `ptr' after calling ptr->SetFailed()
    // before release of `ptr'.
    // This function is wait-free.
    // Returns 0 on success, -1 when the Socket was SetFailed().
    static int Address(SocketId id, SocketUniquePtr* ptr);

    // Re-address current socket into `ptr'.
    // Always succeed even if this socket is failed.
    void ReAddress(SocketUniquePtr* ptr);

    // Returns 0 on success, 1 on failed socket, -1 on recycled.
    static int AddressFailedAsWell(SocketId id, SocketUniquePtr* ptr);

    // Mark this Socket or the Socket associated with `id' as failed.
    // Any later Address() of the identifier shall return NULL unless the
    // Socket was revivied by HealthCheckThread. The Socket is NOT recycled
    // after calling this function, instead it will be recycled when no one
    // references it. Internal fields of the Socket are still accessible
    // after calling this function. Calling SetFailed() of a Socket more
    // than once is OK.
    // This function is lock-free.
    // Returns -1 when the Socket was already SetFailed(), 0 otherwise.
    int SetFailed();
    int SetFailed(int error_code, const char* error_fmt, ...)
        __attribute__ ((__format__ (__printf__, 3, 4)));
    static int SetFailed(SocketId id);

    void AddRecentError();

    int64_t recent_error_count() const;

    int isolated_times() const;

    void FeedbackCircuitBreaker(int error_code, int64_t latency_us);

    bool Failed() const;

    bool DidReleaseAdditionalRereference() const {
        return _additional_ref_status.load(butil::memory_order_relaxed) == REF_RECYCLED;
    }

    // Notify `id' object (by calling bthread_id_error) when this Socket
    // has been `SetFailed'. If it already has, notify `id' immediately
    void NotifyOnFailed(bthread_id_t id);

    // Release the additional reference which added inside `Create'
    // before so that `Socket' will be recycled automatically once
    // on one is addressing it.
    int ReleaseAdditionalReference();
    // `ReleaseAdditionalReference' this Socket iff it has no data
    // transmission during the last `idle_seconds'
    int ReleaseReferenceIfIdle(int idle_seconds);

    // Set ELOGOFF flag to this `Socket' which means further requests
    // through this `Socket' will receive an ELOGOFF error. This only
    // affects return value of `IsAvailable' and won't close the inner
    // fd. Once set, this flag can only be cleared inside `WaitAndReset'.
    void SetLogOff();

    // Check Whether the socket is available for user requests.
    bool IsAvailable() const;

    // Start to process edge-triggered events from the fd.
    // This function does not block caller.
    static int StartInputEvent(SocketId id, uint32_t events,
                               const bthread_attr_t& thread_attr);

    static const int PROGRESS_INIT = 1;
    bool MoreReadEvents(int* progress);

    // Fight for the right to authenticate this socket. Only one
    // fighter will get 0 as return value. Others will wait until
    // authentication finishes (succeed or not) and the error code
    // will be filled into `auth_error'. The winner MUST call
    // authentication finishes (succeed or not). The winner MUST call
    // `SetAuthentication' (whether authentication succeed or not)
    // to wake up waiters.
    // Return 0 on success, error code on failure.
    int FightAuthentication(int* auth_error);

    // Set the authentication result and signal all the waiters.
    // This function can only be called after a successfule
    // `FightAuthentication', otherwise it's regarded as an error
    void SetAuthentication(int error_code);

    // Since some protocols are not able to store correlation id in their
    // headers (such as nova-pbrpc, http), we have to store it here. Note
    // that there can only be 1 RPC call on this socket at any time, otherwise
    // use PushPipelinedInfo/PopPipelinedInfo instead.
    void set_correlation_id(uint64_t correlation_id)
    { _correlation_id = correlation_id; }
    uint64_t correlation_id() const { return _correlation_id; }

    // For protocols that need positional correspondence between responses
    // and requests.
    void PushPipelinedInfo(const PipelinedInfo&);
    bool PopPipelinedInfo(PipelinedInfo* info);
    // Undo previous PopPipelinedInfo
    void GivebackPipelinedInfo(const PipelinedInfo&);

    void set_preferred_index(int index) { _preferred_index = index; }
    int preferred_index() const { return _preferred_index; }

    void set_type_of_service(int tos) { _tos = tos; }

    // Call this method every second (roughly)
    void UpdateStatsEverySecond(int64_t now_ms);

    // Copy stat into `out'. If UpdateStatsEverySecond was never called, all
    // fields will be zero.
    void GetStat(SocketStat* out) const;

    // Call this when you receive an EOF event. `SetFailed' will be
    // called at last if EOF event is no longer postponed
    void SetEOF();
    // Postpone EOF event until `CheckEOF' has been called
    void PostponeEOF();
    void CheckEOF();

    SSLState ssl_state() const { return _ssl_state; }
    bool is_ssl() const { return ssl_state() == SSL_CONNECTED; }
    X509* GetPeerCertificate() const;

    // Print debugging inforamtion of `id' into the ostream.
    static void DebugSocket(std::ostream&, SocketId id);

    // Number of Heahth checking since last socket failure.
    int health_check_count() const { return _hc_count; }

    // True if this socket was created by Connect.
    bool CreatedByConnect() const;

    // Get an UNUSED socket connecting to the same place as this socket
    // from the SocketPool of this socket.
    int GetPooledSocket(SocketUniquePtr* pooled_socket);

    // Return this socket which MUST be got from GetPooledSocket to its
    // main_socket's pool.
    int ReturnToPool();

    // True if this socket has SocketPool
    bool HasSocketPool() const;

    // Put all sockets in _shared_part->socket_pool into `list'.
    void ListPooledSockets(std::vector<SocketId>* list, size_t max_count = 0);

    // Return true on success
    bool GetPooledSocketStats(int* numfree, int* numinflight);

    // Create a socket connecting to the same place as this socket.
    int GetShortSocket(SocketUniquePtr* short_socket);

    // Get and persist a socket connecting to the same place as this socket.
    // If an agent socket was already created and persisted, it's returned
    // directly (provided other constraints are satisfied)
    // If `checkfn' is not NULL, and the checking result on the socket that
    // would be returned is false, the socket is abandoned and the getting
    // process is restarted.
    // For example, http2 connections may run out of stream_id after long time
    // running and a new socket should be created. In order not to affect
    // LoadBalancers or NamingServices that may reference the Socket, agent
    // socket can be used for the communication and replaced periodically but
    // the main socket is unchanged.
    int GetAgentSocket(SocketUniquePtr* out, bool (*checkfn)(Socket*));

    // Take a peek at existing agent socket (no creation).
    // Returns 0 on success.
    int PeekAgentSocket(SocketUniquePtr* out) const;

    // Where the stats of this socket are accumulated to.
    SocketId main_socket_id() const;

    // Share the stats with the socket.
    void ShareStats(Socket* s);

    // Call this method to let the server SetFailed() this socket when the
    // socket becomes idle or Server.Stop() is called. Useful for stopping
    // streaming connections which are often referenced by many places,
    // without SetFailed(), the ref-count may never hit zero.
    void fail_me_at_server_stop() { _fail_me_at_server_stop = true; }
    bool shall_fail_me_at_server_stop() const { return _fail_me_at_server_stop; }

    // Tag the socket so that the response coming back from socket will be
    // parsed progressively. For example: in HTTP, the RPC may end w/o reading
    // the body part fully.
    void read_will_be_progressive(ConnectionType t)
    { _connection_type_for_progressive_read = t; }

    // True if read_will_be_progressive() was called.
    bool is_read_progressive() const
    { return _connection_type_for_progressive_read != CONNECTION_TYPE_UNKNOWN; }

    // Handle the socket according to its connection_type when the progressive
    // reading is finally done.
    void OnProgressiveReadCompleted();

    // Last cpuwide-time at when this socket was read or write.
    int64_t last_active_time_us() const {
        return std::max(
            _last_readtime_us.load(butil::memory_order_relaxed),
            _last_writetime_us.load(butil::memory_order_relaxed));
    }

    // A brief description of this socket, consistent with os << *this
    std::string description() const;

    // Returns true if the remote side is overcrowded.
    bool is_overcrowded() const { return _overcrowded; }

    bthread_keytable_pool_t* keytable_pool() const { return _keytable_pool; }

private:
    DISALLOW_COPY_AND_ASSIGN(Socket);

    // The on/off state of RDMA
    enum RdmaState {
        RDMA_ON,
        RDMA_OFF,
        RDMA_UNKNOWN
    };

    int ConductError(bthread_id_t);
    int StartWrite(WriteRequest*, const WriteOptions&);

    int Dereference();
friend void DereferenceSocket(Socket*);

    static int Status(SocketId, int32_t* nref = NULL);  // for unit-test.

    // Perform SSL handshake after TCP connection has been established.
    // Create SSL session inside and block (in bthread) until handshake
    // has completed. Application layer I/O is forbidden during this
    // process to avoid concurrent I/O on the underlying fd
    // Returns 0 on success, -1 otherwise
    int SSLHandshake(int fd, bool server_mode);

    // Based upon whether the underlying channel is using SSL (if
    // SSLState is SSL_UNKNOWN, try to detect at first), read data
    // using the corresponding method into `_read_buf'. Returns read
    // bytes on success, 0 on EOF, -1 otherwise and errno is set
    ssize_t DoRead(size_t size_hint);

    // Based upon whether the underlying channel is using SSL, write
    // `req' using the corresponding method. Returns written bytes on
    // success, -1 otherwise and errno is set
    ssize_t DoWrite(WriteRequest* req);

    // Called before returning to pool.
    void OnRecycle();

    // [Not thread-safe] Wait for EPOLLOUT event on `fd'. If `pollin' is
    // true, EPOLLIN event will also be included and EPOLL_CTL_MOD will
    // be used instead of EPOLL_CTL_ADD. Note that spurious wakeups may
    // occur when this function returns, so make sure to check whether fd
    // is writable or not even when it returns 0
    int WaitEpollOut(int fd, bool pollin, const timespec* abstime);

    // [Not thread-safe] Establish a tcp connection to `remote_side()'
    // If `on_connect' is NULL, this function blocks current thread
    // until connected/timeout. Otherwise, it returns immediately after
    // starting a connection request and `on_connect' will be called
    // when connecting completes (whether it succeeds or not)
    // Returns the socket fd on success, -1 otherwise
    int Connect(const timespec* abstime,
                int (*on_connect)(int fd, int err, void* data), void* data);
    int CheckConnected(int sockfd);

    // [Not thread-safe] Only used by `Write'.
    // Returns:
    //   0  - Already connected
    //   1  - Trying to establish connection
    //   -1 - Failed to connect to remote side
    int ConnectIfNot(const timespec* abstime, WriteRequest* req);

    int ResetFileDescriptor(int fd);

    void EnableKeepaliveIfNeeded(int fd);

    // Wait until nref hits `expected_nref' and reset some internal resources.
    int WaitAndReset(int32_t expected_nref);

    // Make this socket addressable again.
    void Revive();

    static void* ProcessEvent(void*);

    static void* KeepWrite(void*);

    bool IsWriteComplete(WriteRequest* old_head, bool singular_node,
                         WriteRequest** new_tail);

    void ReturnFailedWriteRequest(
        WriteRequest*, int error_code, const std::string& error_text);
    void ReturnSuccessfulWriteRequest(WriteRequest*);
    WriteRequest* ReleaseWriteRequestsExceptLast(
        WriteRequest*, int error_code, const std::string& error_text);
    void ReleaseAllFailedWriteRequests(WriteRequest*);

    // Try to wake socket just like epollout has arrived
    void WakeAsEpollOut();

    // Generic callback for Socket to handle epollout event
    static int HandleEpollOut(SocketId socket_id);

    class EpollOutRequest;
    // Callback to handle epollout event whose request data
    // is `EpollOutRequest'
    int HandleEpollOutRequest(int error_code, EpollOutRequest* req);

    // Callback when an EpollOutRequest reaches timeout
    static void HandleEpollOutTimeout(void* arg);

    // Callback when connection event reaches (succeeded or not)
    // This callback will be passed to `Connect'
    static int KeepWriteIfConnected(int fd, int err, void* data);
    static void CheckConnectedAndKeepWrite(int fd, int err, void* data);
    static void AfterAppConnected(int err, void* data);

    static void CreateVarsOnce();

    // Default impl. of health checking.
    int CheckHealth();

    // Add a stream over this Socket. And |stream_id| would be automatically
    // closed when this socket fails.
    // Retuns 0 on success. -1 otherwise, indicating that this is currently a
    // broken socket.
    int AddStream(StreamId stream_id);
    int RemoveStream(StreamId stream_id);
    void ResetAllStreams();

    bool ValidFileDescriptor(int fd);

    // For stats.
    void AddInputBytes(size_t bytes);
    void AddInputMessages(size_t count);
    void AddOutputBytes(size_t bytes);
    void AddOutputMessages(size_t count);

    SharedPart* GetSharedPart() const;
    SharedPart* GetOrNewSharedPart();
    SharedPart* GetOrNewSharedPartSlower();

    void CheckEOFInternal();

    // _error_code is set after a socket becomes failed, during the time
    // gap, _error_code is 0. The race condition is by-design and acceptable.
    // To always get a non-zero error_code, readers should call this method
    // instead of reading _error_code directly.
    int non_zero_error_code() const {
        const int tmp = _error_code;
        return tmp ? tmp : EFAILEDSOCKET;
    }

    void CancelUnwrittenBytes(size_t bytes);

private:
    // unsigned 32-bit version + signed 32-bit referenced-count.
    // Meaning of version:
    // * Created version: no SetFailed() is called on the Socket yet. Must be
    //   same evenness with initial _versioned_ref because during lifetime of
    //   a Socket on the slot, the version is added with 1 twice. This is
    //   also the version encoded in SocketId.
    // * Failed version: = created version + 1, SetFailed()-ed but returned.
    // * Other versions: the socket is already recycled.
    butil::atomic<uint64_t> _versioned_ref;

    // In/Out bytes/messages, SocketPool etc
    // _shared_part is shared by a main socket and all its pooled sockets.
    // Can't use intrusive_ptr because the creation is based on optimistic
    // locking and relies on atomic CAS. We manage references manually.
    butil::atomic<SharedPart*> _shared_part;

    // [ Set in dispatcher ]
    // To keep the callback in at most one bthread at any time. Read comments
    // about ProcessEvent in socket.cpp to understand the tricks.
    butil::atomic<int> _nevent;

    // May be set by Acceptor to share keytables between reading threads
    // on sockets created by the Acceptor.
    bthread_keytable_pool_t* _keytable_pool;

    // [ Set in ResetFileDescriptor ]
    butil::atomic<int> _fd;  // -1 when not connected.
    int _tos;                // Type of service which is actually only 8bits.
    int64_t _reset_fd_real_us; // When _fd was reset, in microseconds.

    // Address of peer. Initialized by SocketOptions.remote_side.
    butil::EndPoint _remote_side;

    // Address of self. Initialized in ResetFileDescriptor().
    butil::EndPoint _local_side;

    // Called when edge-triggered events happened on `_fd'. Read comments
    // of EventDispatcher::AddConsumer (event_dispatcher.h)
    // carefully before implementing the callback.
    void (*_on_edge_triggered_events)(Socket*);

    // A set of callbacks to monitor important events of this socket.
    // Initialized by SocketOptions.user
    SocketUser* _user;

    // Customize creation of the connection. Initialized by SocketOptions.conn
    SocketConnection* _conn;

    // User-level connection after TCP-connected.
    // Initialized by SocketOptions.app_connect.
    std::shared_ptr<AppConnect> _app_connect;

    // Identifier of this Socket in ResourcePool
    SocketId _this_id;

    // last chosen index of the protocol as a heuristic value to avoid
    // iterating all protocol handlers each time.
    int _preferred_index;

    // Number of HC since the last SetFailed() was called. Set to 0 when the
    // socket is revived. Only set in HealthCheckTask::OnTriggeringTask()
    int _hc_count;

    // Size of current incomplete message, set to 0 on complete.
    uint32_t _last_msg_size;
    // Average message size of last #MSG_SIZE_WINDOW messages (roughly)
    uint32_t _avg_msg_size;

    // Storing data read from `_fd' but cut-off yet.
    butil::IOPortal _read_buf;

    // Set with cpuwide_time_us() at last read operation
    butil::atomic<int64_t> _last_readtime_us;

    // Saved context for parsing, reset before trying other protocols.
    butil::atomic<Destroyable*> _parsing_context;

    // Saving the correlation_id of RPC on protocols that cannot put
    // correlation_id on-wire and do not send multiple requests on one
    // connection simultaneously.
    uint64_t _correlation_id;

    // Non-zero when health-checking is on.
    int _health_check_interval_s;

    // The variable indicates whether the reference related
    // to the health checking is held by someone. It can be
    // synchronized via _versioned_ref atomic variable.
    bool _is_hc_related_ref_held;

    // Default: false.
    // true, if health checking is started.
    butil::atomic<bool> _hc_started;

    // +-1 bit-+---31 bit---+
    // |  flag |   counter  |
    // +-------+------------+
    // 1-bit flag to ensure `SetEOF' to be called only once
    // 31-bit counter of requests that are currently being processed
    butil::atomic<uint32_t> _ninprocess;

    // +---32 bit---+---32 bit---+
    // |  auth flag | auth error |
    // +------------+------------+
    // Meanings of `auth flag':
    // 0 - not authenticated yet
    // 1 - authentication completed (whether it succeeded or not
    //     depends on `auth error')
    butil::atomic<uint64_t> _auth_flag_error;
    bthread_id_t _auth_id;

    // Stores authentication result/context of this socket. This only
    // exists in server side
    AuthContext* _auth_context;

    // Only accept ssl connection.
    bool _force_ssl;
    SSLState _ssl_state;
    // SSL objects cannot be read and written at the same time.
    // Use mutex to protect SSL objects when ssl_state is SSL_CONNECTED.
    mutable butil::Mutex _ssl_session_mutex;
    SSL* _ssl_session;               // owner
    std::shared_ptr<SocketSSLContext> _ssl_ctx;

    // The RdmaEndpoint
    rdma::RdmaEndpoint* _rdma_ep;
    // Should use RDMA or not
    RdmaState _rdma_state;

    // Pass from controller, for progressive reading.
    ConnectionType _connection_type_for_progressive_read;
    butil::atomic<bool> _controller_released_socket;

    // True if the socket is too full to write.
    volatile bool _overcrowded;

    bool _fail_me_at_server_stop;

    // Set by SetLogOff
    butil::atomic<bool> _logoff_flag;

    // Status flag used to mark that
    enum AdditionalRefStatus {
        REF_USING,        // additional reference has been increased
        REF_REVIVING,     // additional reference is increasing
        REF_RECYCLED      // additional reference has been decreased
    };

    // Indicates whether additional reference has increased,
    // decreased, or is increasing.
    // additional ref status:
    // `Socket'、`Create': REF_USING
    // `SetFailed': REF_USING -> REF_RECYCLED
    // `Revive' REF_RECYCLED -> REF_REVIVING -> REF_USING
    butil::atomic<AdditionalRefStatus> _additional_ref_status;

    // Concrete error information from SetFailed()
    // Accesses to these 2 fields(especially _error_text) must be protected
    // by _id_wait_list_mutex
    int _error_code;
    std::string _error_text;

    butil::atomic<SocketId> _agent_socket_id;

    butil::Mutex _pipeline_mutex;
    std::deque<PipelinedInfo>* _pipeline_q;

    // For storing call-id of in-progress RPC.
    pthread_mutex_t _id_wait_list_mutex;
    bthread_id_list_t _id_wait_list;

    // Set with cpuwide_time_us() at last write operation
    butil::atomic<int64_t> _last_writetime_us;
    // Queued but written
    butil::atomic<int64_t> _unwritten_bytes;

    // Butex to wait for EPOLLOUT event
    butil::atomic<int>* _epollout_butex;

    // Storing data that are not flushed into `fd' yet.
    butil::atomic<WriteRequest*> _write_head;

    butil::Mutex _stream_mutex;
    std::set<StreamId> *_stream_set;
    butil::atomic<int64_t> _total_streams_unconsumed_size;

    butil::atomic<int64_t> _ninflight_app_health_check;

    // Socket keepalive related options.
    // Refer to `SocketKeepaliveOptions' for details.
    // non-NULL means that keepalive is on.
    std::shared_ptr<SocketKeepaliveOptions> _keepalive_options;
};

} // namespace brpc


// Sleep a while when `write_expr' returns negative with errno=EOVERCROWDED
// Implemented as a macro rather than a field of Socket.WriteOptions because
// the macro works for other functions besides Socket.Write as well.
#define BRPC_HANDLE_EOVERCROWDED(write_expr)                       \
    ({                                                                  \
        int64_t __ret_code__;                                           \
        int sleep_time = 250;                                           \
        while (true) {                                                  \
            __ret_code__ = (write_expr);                                \
            if (__ret_code__ >= 0 || errno != ::brpc::EOVERCROWDED) { \
                break;                                                  \
            }                                                           \
            sleep_time *= 2;                                            \
            if (sleep_time > 2000) { sleep_time = 2000; }               \
            ::bthread_usleep(sleep_time);                               \
        }                                                               \
        __ret_code__;                                                   \
    })

// Sleep a while when `write_expr' returns negative with errno=EOVERCROWDED.
// The sleep is done for at most `nretry' times.
#define BRPC_HANDLE_EOVERCROWDED_N(write_expr, nretry)                  \
    ({                                                                  \
        int64_t __ret_code__ = 0;                                       \
        int sleep_time = 250;                                           \
        for (int i = static_cast<int>(nretry); i >= 0; --i) {           \
            __ret_code__ = (write_expr);                                \
            if (__ret_code__ >= 0 || errno != ::brpc::EOVERCROWDED) { \
                break;                                                  \
            }                                                           \
            sleep_time *= 2;                                            \
            if (sleep_time > 2000) { sleep_time = 2000; }               \
            ::bthread_usleep(sleep_time);                               \
        }                                                               \
        __ret_code__;                                                   \
    })

namespace std {
ostream& operator<<(ostream& os, const brpc::Socket& sock);
}

#include "brpc/socket_inl.h"

#endif  // BRPC_SOCKET_H
