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

// Date: Fri Sep 10 13:34:25 CST 2010

#include "butil/build_config.h"                        // OS_MACOSX
#include <errno.h>                                     // errno
#include <string.h>                                    // strerror_r
#include <stdlib.h>                                    // EXIT_FAILURE
#include <stdio.h>                                     // snprintf
#include <pthread.h>                                   // pthread_mutex_t
#include <unistd.h>                                    // _exit
#include "butil/scoped_lock.h"                         // BAIDU_SCOPED_LOCK

namespace butil {

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
        // error() is a non-portable GNU extension that should not be used.
        fprintf(stderr, "Fail to define %s(%d) which is out of range, abort.",
              error_name, error_code);
        _exit(1);
    }
    const char* desc = errno_desc[error_code - ERRNO_BEGIN];
    if (desc) {
        if (strcmp(desc, description) == 0) {
            fprintf(stderr, "WARNING: Detected shared library loading\n");
            return -1;
        }
    } else {
#if defined(OS_MACOSX)
        const int rc = strerror_r(error_code, tls_error_buf, ERROR_BUFSIZE);
        if (rc != EINVAL)
#else
        desc = strerror_r(error_code, tls_error_buf, ERROR_BUFSIZE);
        if (desc && strncmp(desc, "Unknown error", 13) != 0)
#endif
        {
            fprintf(stderr, "WARNING: Fail to define %s(%d) which is already defined as `%s'",
                    error_name, error_code, desc);
        }
    }
    errno_desc[error_code - ERRNO_BEGIN] = description;
    return 0;  // must
}

}  // namespace butil

const char* berror(int error_code) {
    if (error_code == -1) {
        return "General error -1";
    }
    if (error_code >= butil::ERRNO_BEGIN && error_code < butil::ERRNO_END) {
        const char* s = butil::errno_desc[error_code - butil::ERRNO_BEGIN];
        if (s) {
            return s;
        }
#if defined(OS_MACOSX)
        const int rc = strerror_r(error_code, butil::tls_error_buf, butil::ERROR_BUFSIZE);
        if (rc == 0 || rc == ERANGE/*bufsize is not long enough*/) {
            return butil::tls_error_buf;
        }
#else
        s = strerror_r(error_code, butil::tls_error_buf, butil::ERROR_BUFSIZE);
        if (s) {
            return s;
        }
#endif
    }
    snprintf(butil::tls_error_buf, butil::ERROR_BUFSIZE,
             "Unknown error %d", error_code);
    return butil::tls_error_buf;
}

const char* berror() {
    return berror(errno);
}
