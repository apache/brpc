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

#ifndef BRPC_BTHREAD_TRACER_H
#define BRPC_BTHREAD_TRACER_H

#ifdef BRPC_BTHREAD_TRACER

#include <signal.h>
#include <vector>
#include <libunwind.h>
#include "butil/strings/safe_sprintf.h"
#include "butil/synchronization/condition_variable.h"
#include "bthread/task_meta.h"
#include "bthread/mutex.h"

namespace bthread {

// Tracer for bthread.
class TaskTracer {
public:
    // Returns 0 on success, -1 otherwise.
    int Init();
    // Set the status to `s'.
    void set_status(TaskStatus s, TaskMeta* meta);
    static void set_running_status(pid_t worker_tid, TaskMeta* meta);
    static bool set_end_status_unsafe(TaskMeta* m);

    // Async signal safe usleep.
    static void SignalSafeUsleep(unsigned int microseconds);

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

    enum SignalTraceStatus {
        SIGNAL_TRACE_STATUS_UNKNOWN = 0,
        SIGNAL_TRACE_STATUS_START,
        SIGNAL_TRACE_STATUS_TRACING,
    };

    struct Result {
        template<typename... Args>
        static Result MakeErrorResult(const char* fmt, Args... args) {
            Result result{};
            result.error = true;
            butil::strings::SafeSPrintf(result.err_msg, fmt, args...);
            return result;
        }

        static const size_t MAX_TRACE_NUM = 64;
        unw_word_t ips[MAX_TRACE_NUM];
        size_t frame_count{0};
        bool error{false};
        union {
            char mangled[MAX_TRACE_NUM][256]{};
            char err_msg[256];
        };

        bool fast_unwind{false};
    };

    Result TraceImpl(bthread_t tid);

    static TaskStatus WaitForJumping(TaskMeta* m);

    Result ContextTrace(bthread_fcontext_t fcontext);

    // Register signal handler for signal trace.
    static int RegisterSignalHandler();
    static void SignalHandler(int sig, siginfo_t* info, void* context);
    void SignalTraceHandler(unw_context_t* context);
    Result SignalTrace(pid_t worker_tid);

    unw_cursor_t MakeCursor(bthread_fcontext_t fcontext);
    Result TraceCore(unw_cursor_t& cursor);

    // Make sure only one bthread is traced at a time.
    bthread::Mutex _trace_request_mutex;

    // For signal trace.
    // Make sure bthread does not jump stack when it is being traced.
    butil::Mutex _mutex;
    butil::ConditionVariable _cond{&_mutex};

    // For context trace.
    unw_context_t _context{};
    // For signal trace.
    unw_context_t* _signal_handler_context{NULL};
    butil::atomic<SignalTraceStatus> _signal_handler_flag{SIGNAL_TRACE_STATUS_UNKNOWN};

    // Protect `_worker_tids'.
    butil::Mutex _worker_mutex;
    std::vector<pid_t> _worker_tids;

    bvar::LatencyRecorder _trace_time{"bthread_trace_time"};
    bvar::LatencyRecorder _unwind_time{"bthread_unwind_time"};
    bvar::LatencyRecorder _signal_handler_time{"bthread_signal_handler_time"};
};

} // namespace bthread

#endif // BRPC_BTHREAD_TRACER

#endif // BRPC_BTHREAD_TRACER_H
