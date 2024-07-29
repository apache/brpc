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

// Date: Mon. Nov 7 14:47:36 CST 2011

#include "butil/build_config.h"
#include <fcntl.h>                   // fcntl()
#include <netinet/in.h>              // IPPROTO_TCP
#include <sys/types.h>
#include <sys/socket.h>              // setsockopt
#include <netinet/tcp.h>             // TCP_NODELAY
#include <netinet/tcp.h>
#if defined(OS_MACOSX)
#include <netinet/tcp_fsm.h>        // TCPS_ESTABLISHED, TCP6S_ESTABLISHED
#endif
#include "butil/logging.h"

namespace butil {

bool is_blocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && !(flags & O_NONBLOCK);
}

int make_non_blocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return flags;
    }
    if (flags & O_NONBLOCK) {
        return 0;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int make_blocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return flags;
    }
    if (flags & O_NONBLOCK) {
        return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    return 0;
}

int make_close_on_exec(int fd) {
    return fcntl(fd, F_SETFD, FD_CLOEXEC);
}

int make_no_delay(int sockfd) {
    int flag = 1;
    return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
}

int is_connected(int sockfd) {
    errno = 0;
    int err;
    socklen_t errlen = sizeof(err);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0) {
        PLOG(FATAL) << "Fail to getsockopt";
        return -1;
    }
    if (err != 0) {
        errno = err;
        return -1;
    }

#if defined(OS_LINUX)
    struct tcp_info ti{};
    socklen_t len = sizeof(ti);
    if(getsockopt(sockfd, SOL_TCP, TCP_INFO, &ti, &len) < 0) {
        PLOG(FATAL) << "Fail to getsockopt";
        return -1;
    }
    if (ti.tcpi_state != TCP_ESTABLISHED) {
        errno = ENOTCONN;
        return -1;
    }
#elif defined(OS_MACOSX)
    struct tcp_connection_info ti{};
    socklen_t len = sizeof(ti);
    if (getsockopt(sockfd, IPPROTO_TCP, TCP_CONNECTION_INFO, &ti, &len) < 0) {
        PLOG(FATAL) << "Fail to getsockopt";
        return -1;
    }
    if (ti.tcpi_state != TCPS_ESTABLISHED &&
        ti.tcpi_state != TCP6S_ESTABLISHED) {
        errno = ENOTCONN;
        return -1;
    }
#endif

    return 0;
}

}  // namespace butil
