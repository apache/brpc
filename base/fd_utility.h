// Copyright (c) 2011 Baidu.com, Inc. All Rights Reserved
//
// Utility functions on file descriptor.
//
// Author: Ge,Jun (gejun@baidu.com)
// Date: Mon. Nov 7 14:47:36 CST 2011

#ifndef BRPC_BASE_FD_UTILITY_H
#define BRPC_BASE_FD_UTILITY_H

namespace base {

// Make file descriptor |fd| non-blocking
// Returns 0 on success, -1 otherwise and errno is set (by fcntl)
int make_non_blocking(int fd);

// Make file descriptor |fd| blocking
// Returns 0 on success, -1 otherwise and errno is set (by fcntl)
int make_blocking(int fd);

// Make file descriptor |fd| automatically closed during exec()
// Returns 0 on success, -1 when error and errno is set (by fcntl)
int make_close_on_exec(int fd);

// Disable nagling on file descriptor |socket|.
// Returns 0 on success, -1 when error and errno is set (by setsockopt)
int make_no_delay(int socket);

}  // namespace base

#endif  // BRPC_BASE_FD_UTILITY_H
