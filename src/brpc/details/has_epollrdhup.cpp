// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Sep 14 22:04:41 CST 2014

#include <sys/epoll.h>                             // epoll_create
#include <sys/types.h>                             // socketpair
#include <sys/socket.h>                            // ^
#include "base/fd_guard.h"                         // fd_guard
#include "brpc/details/has_epollrdhup.h"

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif


namespace brpc {

static unsigned int check_epollrdhup() {
    base::fd_guard epfd(epoll_create(16));
    if (epfd < 0) {
        return 0;
    }
    base::fd_guard fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, (int*)fds) < 0) {
        return 0;
    }
    epoll_event evt = { static_cast<uint32_t>(EPOLLIN | EPOLLRDHUP | EPOLLET),
                        { NULL }};
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fds[0], &evt) < 0) {
        return 0;
    }
    if (close(fds[1].release()) < 0) {
        return 0;
    }
    epoll_event e;
    int n;
    while ((n = epoll_wait(epfd, &e, 1, -1)) == 0);
    if (n < 0) {
        return 0;
    }
    return (e.events & EPOLLRDHUP) ? EPOLLRDHUP : static_cast<EPOLL_EVENTS>(0);
}

extern const unsigned int has_epollrdhup = check_epollrdhup();

} // namespace brpc

