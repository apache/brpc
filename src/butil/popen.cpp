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

// Date: 2017/11/04 17:37:43

#include <gflags/gflags.h>
#include "butil/build_config.h"
#include "butil/logging.h"

#if defined(OS_LINUX)
// clone is a linux specific syscall
#include <sched.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

extern "C" {
uint64_t BAIDU_WEAK bthread_usleep(uint64_t microseconds);
}
#endif

namespace butil {

#if defined(OS_LINUX)

const int CHILD_STACK_SIZE = 256 * 1024;

struct ChildArgs {
    const char* cmd;
    int pipe_fd0;
    int pipe_fd1;
};

int launch_child_process(void* args) {
    ChildArgs* cargs = (ChildArgs*)args;
    dup2(cargs->pipe_fd1, STDOUT_FILENO);
    close(cargs->pipe_fd0);
    close(cargs->pipe_fd1);
    execl("/bin/sh", "sh", "-c", cargs->cmd, NULL);
    _exit(1);
}

int read_command_output_through_clone(std::ostream& os, const char* cmd) {
    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) {
        PLOG(ERROR) << "Fail to pipe";
        return -1;
    }
    int saved_errno = 0;
    int wstatus = 0;
    pid_t cpid;
    int rc = 0;
    ChildArgs args = { cmd, pipe_fd[0], pipe_fd[1] };
    char buffer[1024];

    char* child_stack = NULL;
    char* child_stack_mem = (char*)malloc(CHILD_STACK_SIZE);
    if (!child_stack_mem) {
        LOG(ERROR) << "Fail to alloc stack for the child process";
        rc = -1;
        goto END;
    }
    child_stack = child_stack_mem + CHILD_STACK_SIZE;  
                               // ^ Assume stack grows downward
    cpid = clone(launch_child_process, child_stack,
                 __WCLONE | CLONE_VM | SIGCHLD | CLONE_UNTRACED, &args);
    if (cpid < 0) {
        PLOG(ERROR) << "Fail to clone child process";
        rc = -1;
        goto END;
    }
    close(pipe_fd[1]);
    pipe_fd[1] = -1;

    for (;;) {
        const ssize_t nr = read(pipe_fd[0], buffer, sizeof(buffer));
        if (nr > 0) {
            os.write(buffer, nr);
            continue;
        } else if (nr == 0) {
            break;
        } else if (errno != EINTR) {
            LOG(ERROR) << "Encountered error while reading for the pipe";
            break;
        }
    }

    close(pipe_fd[0]);
    pipe_fd[0] = -1;

    for (;;) {
        pid_t wpid = waitpid(cpid, &wstatus, WNOHANG | __WALL);
        if (wpid > 0) {
            break;
        }
        if (wpid == 0) {
            if (bthread_usleep != NULL) {
                bthread_usleep(1000);
            } else {
                usleep(1000);
            }
            continue;
        }
        rc = -1;
        goto END;
    }

    if (WIFEXITED(wstatus)) {
        rc = WEXITSTATUS(wstatus);
        goto END;
    }

    if (WIFSIGNALED(wstatus)) {
        os << "Child process(" << cpid << ") was killed by signal "
           << WTERMSIG(wstatus);
    }

    rc = -1;
    errno = ECHILD;

END:
    saved_errno = errno;
    if (child_stack_mem) {
        free(child_stack_mem);
    }
    if (pipe_fd[0] >= 0) {
        close(pipe_fd[0]);
    }
    if (pipe_fd[1] >= 0) {
        close(pipe_fd[1]);
    }
    errno = saved_errno;
    return rc;
}

DEFINE_bool(run_command_through_clone, false,
            "(Linux specific) Run command with clone syscall to "
            "avoid the costly page table duplication");

#endif // OS_LINUX

#include <stdio.h>

int read_command_output_through_popen(std::ostream& os, const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (pipe == NULL) {
        return -1;
    }
    char buffer[1024];
    for (;;) {
        size_t nr = fread(buffer, 1, sizeof(buffer), pipe);
        if (nr != 0) {
            os.write(buffer, nr);
        }
        if (nr != sizeof(buffer)) {
            if (feof(pipe)) {
                break;
            } else if (ferror(pipe)) {
                LOG(ERROR) << "Encountered error while reading for the pipe";
                break;
            }
            // retry;
        }
    }

    const int wstatus = pclose(pipe);

    if (wstatus < 0) {
        return wstatus;
    }
    if (WIFEXITED(wstatus)) {
        return WEXITSTATUS(wstatus);
    }
    if (WIFSIGNALED(wstatus)) {
        os << "Child process was killed by signal "
           << WTERMSIG(wstatus);
    }
    errno = ECHILD;
    return -1;
}

int read_command_output(std::ostream& os, const char* cmd) {
#if !defined(OS_LINUX)
    return read_command_output_through_popen(os, cmd);
#else
    return FLAGS_run_command_through_clone
        ? read_command_output_through_clone(os, cmd)
        : read_command_output_through_popen(os, cmd);
#endif
}

}  // namespace butil
