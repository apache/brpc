// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Implement unix_socket.h
//
// Author: Jiang,Rujie(jiangrujie@baidu.com)  Ge,Jun(gejun@baidu.com)
// Date: Mon. Jan 27  23:08:35 CST 2014

#include <sys/types.h>                          // socket
#include <sys/socket.h>                         // ^
#include <sys/un.h>                             // unix domain socket
#include "base/fd_guard.h"                     // fd_guard
#include "base/logging.h"

namespace base {

int unix_socket_listen(const char* sockname, bool remove_previous_file) {
    struct sockaddr_un addr;
    addr.sun_family = AF_LOCAL;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sockname);

    fd_guard fd(socket(AF_LOCAL, SOCK_STREAM, 0));
    if (fd < 0) {
        PLOG(ERROR) << "Fail to create unix socket";
        return -1;
    }
    if (remove_previous_file) {
        remove(sockname);
    }
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        PLOG(ERROR) << "Fail to bind sockfd=" << fd << " as unix socket="
                    << sockname;
        return -1;
    }
    if (listen(fd, SOMAXCONN) != 0) {
        PLOG(ERROR) << "Fail to listen to sockfd=" << fd;
        return -1;
    }
    return fd.release();
}

int unix_socket_listen(const char* sockname) {
    return unix_socket_listen(sockname, true);
}

int unix_socket_connect(const char* sockname) {
    struct sockaddr_un addr;
    addr.sun_family = AF_LOCAL;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sockname);

    fd_guard fd(socket(AF_LOCAL, SOCK_STREAM, 0));
    if (fd < 0) {
        PLOG(ERROR) << "Fail to create unix socket";
        return -1;
    }
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        PLOG(ERROR) << "Fail to connect to unix socket=" << sockname
                    << " via sockfd=" << fd;
        return -1;
    }
    return fd.release();
}

}  // namespace base
