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

// Date: Thu Oct 28 15:27:05 2010

#include <fcntl.h>                                  // open
#include <unistd.h>                                 // close
#include <stdio.h>                                  // snprintf, vdprintf
#include <stdlib.h>                                 // mkstemp
#include <string.h>                                 // strlen 
#include <stdarg.h>                                 // va_list
#include <errno.h>                                  // errno
#include <new>                                      // placement new
#include "temp_file.h"                              // TempFile

// Initializing array. Needs to be macro.
#define BASE_FILES_TEMP_FILE_PATTERN "temp_file_XXXXXX";

namespace butil {

TempFile::TempFile() : _ever_opened(0) {
    char temp_name[] = BASE_FILES_TEMP_FILE_PATTERN;
    _fd = mkstemp(temp_name);
    if (_fd >= 0) {
        _ever_opened = 1;
        snprintf(_fname, sizeof(_fname), "%s", temp_name);
    } else {
        *_fname = '\0';
    }
        
}

TempFile::TempFile(const char* ext) {
    if (NULL == ext || '\0' == *ext) {
        new (this) TempFile();
        return;
    }

    *_fname = '\0';
    _fd = -1;
    _ever_opened = 0;
    
    // Make a temp file to occupy the filename without ext.
    char temp_name[] = BASE_FILES_TEMP_FILE_PATTERN;
    const int tmp_fd = mkstemp(temp_name);
    if (tmp_fd < 0) {
        return;
    }
        
    // Open the temp_file_XXXXXX.ext
    snprintf(_fname, sizeof(_fname), "%s.%s", temp_name, ext);
        
    _fd = open(_fname, O_CREAT | O_WRONLY | O_TRUNC | O_EXCL, 0600);
    if (_fd < 0) {
        *_fname = '\0';
    } else {
        _ever_opened = 1;
    }

    // Close and remove temp_file_XXXXXX anyway.
    close(tmp_fd);
    unlink(temp_name);
}

int TempFile::_reopen_if_necessary() {
    if (_fd < 0) {
        _fd = open(_fname, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    }
    return _fd;
}

TempFile::~TempFile() {
    if (_fd >= 0) {
        close(_fd);
        _fd = -1;
    }
    if (_ever_opened) {
        // Only remove temp file when _fd >= 0, otherwise we may
        // remove another temporary file (occupied the same name)
        unlink(_fname);
    }
}
    
int TempFile::save(const char *content) {
    return save_bin(content, strlen(content));
}

int TempFile::save_format(const char *fmt, ...) {
    if (_reopen_if_necessary() < 0) {
        return -1;
    }
    va_list ap;
    va_start(ap, fmt);
    const int rc = vdprintf(_fd, fmt, ap);
    va_end(ap);

    close(_fd);
    _fd = -1;
    // TODO: is this right?
    return (rc < 0 ? -1 : 0);
}

// Write until all buffer was written or an error except EINTR.
// Returns:
//    -1   error happened, errno is set
// count   all written
static ssize_t temp_file_write_all(int fd, const void *buf, size_t count) {
    size_t off = 0;
    for (;;) {
        ssize_t nw = write(fd, (char*)buf + off, count - off);
        if (nw == (ssize_t)(count - off)) {  // including count==0
            return count;
        }
        if (nw >= 0) {
            off += nw;
        } else if (errno != EINTR) {
            return -1;
        }
    }
}

int TempFile::save_bin(const void *buf, size_t count) {
    if (_reopen_if_necessary() < 0) {
        return -1;
    }

    const ssize_t len = temp_file_write_all(_fd, buf, count);
    
    close(_fd);
    _fd = -1;
    if (len < 0) {
        return -1;
    } else if ((size_t)len != count) {
        errno = ENOSPC;
        return -1;
    }
    return 0;        
}

} // namespace butil
