// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Sep  2 12:22:11 CST 2014

#ifndef BRPC_EVENT_DISPATCHER_H
#define BRPC_EVENT_DISPATCHER_H

#include "base/macros.h"                     // DISALLOW_COPY_AND_ASSIGN
#include "bthread/types.h"                   // bthread_t, bthread_attr_t
#include "brpc/socket.h"                     // Socket, SocketId


namespace brpc {

// Dispatch edge-triggered events of file descriptors to consumers
// running in separate bthreads.
class EventDispatcher {
friend class Socket;
public:
    EventDispatcher();
    
    virtual ~EventDispatcher();

    // Start this dispatcher in a bthread.
    // Use |*consumer_thread_attr| (if it's not NULL) as the attribute to
    // create bthreads running user callbacks.
    // Returns 0 on success, -1 otherwise.
    virtual int Start(const bthread_attr_t* consumer_thread_attr);

    // True iff this dispatcher is running in a bthread
    bool Running() const;

    // Stop bthread of this dispatcher.
    void Stop();

    // Suspend calling thread until bthread of this dispatcher stops.
    void Join();

    // When edge-triggered events happen on `fd', call
    // `on_edge_triggered_events' of `socket_id'.
    // Notice that this function also transfers ownership of `socket_id',
    // When the file descriptor is removed from internal epoll, the Socket
    // will be dereferenced once additionally.
    // Returns 0 on success, -1 otherwise.
    int AddConsumer(SocketId socket_id, int fd);

    // Watch EPOLLOUT event on `fd' into epoll device. If `pollin' is
    // true, EPOLLIN event will also be included and EPOLL_CTL_MOD will
    // be used instead of EPOLL_CTL_ADD. When event arrives,
    // `Socket::HandleEpollOut' will be called with `socket_id'
    // Returns 0 on success, -1 otherwise and errno is set
    int AddEpollOut(SocketId socket_id, int fd, bool pollin);
    
    // Remove EPOLLOUT event on `fd'. If `pollin' is true, EPOLLIN event
    // will be kept and EPOLL_CTL_MOD will be used instead of EPOLL_CTL_DEL
    // Returns 0 on success, -1 otherwise and errno is set
    int RemoveEpollOut(SocketId socket_id, int fd, bool pollin);

private:
    DISALLOW_COPY_AND_ASSIGN(EventDispatcher);

    // Calls Run()
    static void* RunThis(void* arg);

    // Thread entry.
    void Run();

    // Remove the file descriptor `fd' from epoll.
    int RemoveConsumer(int fd);

    // The epoll to watch events.
    int _epfd;

    // false unless Stop() is called.
    volatile bool _stop;

    // identifier of hosting bthread
    bthread_t _tid;

    // The attribute of bthreads calling user callbacks.
    bthread_attr_t _consumer_thread_attr;

    // Pipe fds to wakeup EventDispatcher from `epoll_wait' in order to quit
    int _wakeup_fds[2];
};

EventDispatcher& GetGlobalEventDispatcher(int fd);

} // namespace brpc


#endif  // BRPC_EVENT_DISPATCHER_H
