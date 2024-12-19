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

#ifndef BTHREAD_TASK_TRACER_H
#define BTHREAD_TASK_TRACER_H

#ifdef BRPC_BTHREAD_TRACER

#include <signal.h>
#include <semaphore.h>
#include <vector>
#include <algorithm>
#include <libunwind.h>
#include "butil/strings/safe_sprintf.h"
#include "butil/synchronization/condition_variable.h"
#include "butil/shared_object.h"
#include "butil/fd_utility.h"
#include "bthread/task_meta.h"
#include "bthread/mutex.h"

namespace bthread {

// Tracer for bthread.
class TaskTracer {
public:
    // Returns 0 on success, -1 otherwise.
    bool Init();
    // Set the status to `s'.
    void set_status(TaskStatus s, TaskMeta* meta);
    static void set_running_status(pid_t worker_tid, TaskMeta* meta);
    static bool set_end_status_unsafe(TaskMeta* m);

    // Trace the bthread of `tid'.
    std::string Trace(bthread_t tid);
    void Trace(std::ostream& os, bthread_t tid);

    // When the worker is jumping stack from a bthread to another,
    void WaitForTracing(TaskMeta* m);

private:
    // Error number guard used in signal handler.
    class ErrnoGuard {
    public:
        ErrnoGuard() : _errno(errno) {}
        ~ErrnoGuard() { errno = _errno; }
    private:
        int _errno;
    };

    struct Result {
        template<typename... Args>
        static Result MakeErrorResult(const char* fmt, Args... args) {
            Result result{};
            result.SetError(fmt, std::forward<Args>(args)...);
            return result;
        }

        template<typename... Args>
        void SetError(const char* fmt, Args... args) {
            err_count = std::max(err_count + 1, MAX_ERROR_NUM);
            butil::strings::SafeSPrintf(err_msg[err_count - 1], fmt, args...);
        }

        std::string OutputToString();
        void OutputToStream(std::ostream& os);

        bool OK() const { return err_count == 0; }

        static const size_t MAX_TRACE_NUM = 64;
        static const size_t MAX_ERROR_NUM = 2;

        unw_word_t ips[MAX_TRACE_NUM];
        char mangled[MAX_TRACE_NUM][256]{};
        size_t frame_count{0};
        char err_msg[MAX_ERROR_NUM][64]{};
        size_t err_count{0};

        bool fast_unwind{false};
    };

    // For signal trace.
    struct SignalSync : public butil::SharedObject {
        ~SignalSync() override;
        bool Init();

        unw_context_t* context{NULL};
        sem_t sem{};
        int pipe_fds[2]{};

    private:
        bool _pipe_init{false};
        bool _sem_init{false};
    };

    static TaskStatus WaitForJumping(TaskMeta* m);
    Result TraceImpl(bthread_t tid);

    unw_cursor_t MakeCursor(bthread_fcontext_t fcontext);
    Result ContextTrace(bthread_fcontext_t fcontext);

    static bool RegisterSignalHandler();
    static void SignalHandler(int sig, siginfo_t* info, void* context);
    static bool WaitForSignalHandler(butil::intrusive_ptr<SignalSync> signal_sync,
                                     const timespec* abs_timeout, Result& result);
    static void WakeupSignalHandler(
        butil::intrusive_ptr<SignalSync> signal_sync, Result& result);
    Result SignalTrace(pid_t worker_tid);

    static Result TraceCore(unw_cursor_t& cursor);

    // Make sure only one bthread is traced at a time.
    bthread::Mutex _trace_request_mutex;

    // For signal trace.
    // Make sure bthread does not jump stack when it is being traced.
    butil::Mutex _mutex;
    butil::ConditionVariable _cond{&_mutex};

    // For context trace.
    unw_context_t _context{};

    bvar::LatencyRecorder _trace_time{"bthread_trace_time"};
};

} // namespace bthread

#endif // BRPC_BTHREAD_TRACER

#endif // BRPC_BTHREAD_TRACER_H
