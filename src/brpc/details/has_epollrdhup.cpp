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


#include "butil/build_config.h"

#if defined(OS_LINUX)

#include <sys/epoll.h>                             // epoll_create
#include <sys/types.h>                             // socketpair
#include <sys/socket.h>                            // ^
#include "butil/fd_guard.h"                         // fd_guard
#include "brpc/details/has_epollrdhup.h"

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif

namespace brpc {

static unsigned int check_epollrdhup() {
    butil::fd_guard epfd(epoll_create(16));
    if (epfd < 0) {
        return 0;
    }
    butil::fd_guard fds[2];
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

#else

namespace brpc {
extern const unsigned int has_epollrdhup = false;
}

#endif // defined(OS_LINUX)
