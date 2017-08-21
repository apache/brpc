// Copyright (c) 2010 Baidu.com, Inc. All Rights Reserved
//
// Implement base/errno.h
// 
// Author: Ge,Jun (gejun@baidu.com)
// Date: Fri Sep 10 13:34:25 CST 2010

#include <errno.h>                                     // errno
#include <string.h>                                    // strerror_r
#include <stdlib.h>                                    // EXIT_FAILURE
#include <stdio.h>                                     // snprintf
#include <pthread.h>                                   // pthread_mutex_t
#include <error.h>                                     // error
#include "base/scoped_lock.h"                         // BAIDU_SCOPED_LOCK

namespace base {

const int ERRNO_BEGIN = -32768;
const int ERRNO_END = 32768;
static const char* errno_desc[ERRNO_END - ERRNO_BEGIN] = {};
static pthread_mutex_t modify_desc_mutex = PTHREAD_MUTEX_INITIALIZER;

const size_t ERROR_BUFSIZE = 64;
__thread char tls_error_buf[ERROR_BUFSIZE];

int DescribeCustomizedErrno(
    int error_code, const char* error_name, const char* description) {
    BAIDU_SCOPED_LOCK(modify_desc_mutex);
    if (error_code < ERRNO_BEGIN || error_code >= ERRNO_END) {
        error(EXIT_FAILURE, 0,
              "Fail to define %s(%d) which is out of range, abort.",
              error_name, error_code);
    }
    const char* desc = errno_desc[error_code - ERRNO_BEGIN];
    if (desc) {
        if (strcmp(desc, description) == 0) {
            fprintf(stderr, "WARNING: Detected shared library loading\n");
            return -1;
        }
    } else {
        desc = strerror_r(error_code, tls_error_buf, ERROR_BUFSIZE);
        if (desc && strncmp(desc, "Unknown error", 13) != 0) {
            error(EXIT_FAILURE, 0,
                    "Fail to define %s(%d) which is already defined as `%s', abort.",
                    error_name, error_code, desc);
        }
    }
    errno_desc[error_code - ERRNO_BEGIN] = description;
    return 0;  // must
}

}  // namespace base

const char* berror(int error_code) {
    if (error_code == -1) {
        return "General Error -1";
    }
    if (error_code >= base::ERRNO_BEGIN && error_code < base::ERRNO_END) {
        const char* s = base::errno_desc[error_code - base::ERRNO_BEGIN];
        if (s) {
            return s;
        }
        s = strerror_r(error_code, base::tls_error_buf, base::ERROR_BUFSIZE);
        if (s) {  // strerror_r returns NULL if error_code is unknown
            return s;
        }
    }
    snprintf(base::tls_error_buf, base::ERROR_BUFSIZE,
             "Unknown Error %d", error_code);
    return base::tls_error_buf;
}

const char* berror() {
    return berror(errno);
}

