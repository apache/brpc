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

// Process-related Info

// Date: Wed Apr 11 14:35:56 CST 2018

#include <fcntl.h>                      // open
#include <stdio.h>                      // snprintf
#include <sys/types.h>  
#include <sys/uio.h>
#include <unistd.h>                     // read, gitpid
#include <sstream>                      // std::ostringstream
#include "butil/fd_guard.h"             // butil::fd_guard
#include "butil/logging.h"
#include "butil/popen.h"                // read_command_output
#include "butil/process_util.h"

namespace butil {

static size_t ReadCommandLineImpl(char* buf, size_t len, bool with_args) {
#if defined(OS_LINUX)
    butil::fd_guard fd(open("/proc/self/cmdline", O_RDONLY));
    if (fd < 0) {
        LOG(ERROR) << "Fail to open /proc/self/cmdline";
        return -1;
    }
    ssize_t nr = read(fd, buf, len);
    if (nr <= 0) {
        LOG(ERROR) << "Fail to read /proc/self/cmdline";
        return -1;
    }
#elif defined(OS_MACOSX)
    static pid_t pid = getpid();
    std::ostringstream oss;
    char cmdbuf[32];
    snprintf(cmdbuf, sizeof(cmdbuf), "ps -p %ld -o command=", (long)pid);
    if (butil::read_command_output(oss, cmdbuf) != 0) {
        LOG(ERROR) << "Fail to read cmdline";
        return -1;
    }
    const std::string& result = oss.str();
    ssize_t nr = std::min(result.size(), len);
    memcpy(buf, result.data(), nr);
#else
    #error Not Implemented
#endif

    if (with_args) {
        if ((size_t)nr == len) {
            return len;
        }
        for (ssize_t i = 0; i < nr; ++i) {
            if (buf[i] == '\0') {
                buf[i] = '\n';
            }
        }
        return nr;
    } else {
        for (ssize_t i = 0; i < nr; ++i) {
            // The command in macos is separated with space and ended with '\n'
            if (buf[i] == '\0' || buf[i] == '\n' || buf[i] == ' ') {
                return i;
            }
        }
        if ((size_t)nr == len) {
            LOG(ERROR) << "buf is not big enough";
            return -1;
        }
        return nr;
    }
}

std::string ReadCommandLine(bool with_args) {
    char buf[4096];
    const ssize_t nr = ReadCommandLineImpl(buf, sizeof(buf), with_args);
    if (nr < 0) {
        return std::string();
    }
    return std::string(buf);
}

std::string ReadCommandName() {
    std::string command_line = ReadCommandLine(false);
    if (command_line.empty()) {
        return command_line;
    }

    std::size_t pos = command_line.find_last_of('/');
    if (pos == std::string::npos) {
        return command_line;
    }

    return command_line.substr(pos + 1);
}

} // namespace butil
