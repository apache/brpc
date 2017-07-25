// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Wrappers of unix domain sockets, mainly for unit-test of network stuff.
//
// Author: Jiang,Rujie(jiangrujie@baidu.com)  Ge,Jun(gejun@baidu.com)
// Date: Mon. Jan 27  23:08:35 CST 2014

#ifndef BRPC_BASE_UNIX_SOCKET_H
#define BRPC_BASE_UNIX_SOCKET_H

namespace base {

// Create an unix domain socket at `sockname' and listen to it.
// If remove_previous_file is true or absent, remove previous file before
// creating the socket.
// Returns the file descriptor on success, -1 otherwise and errno is set.
int unix_socket_listen(const char* sockname, bool remove_previous_file);
int unix_socket_listen(const char* sockname);

// Create an unix domain socket and connect it to another listening unix domain
// socket at `sockname'.
// Returns the file descriptor on success, -1 otherwise and errno is set.
int unix_socket_connect(const char* sockname);

}  // namespace base

#endif  // BRPC_BASE_UNIX_SOCKET_H
