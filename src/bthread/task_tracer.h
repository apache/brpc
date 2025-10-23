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

#include "bthread/mutex.h"
#include "bthread/task_meta.h"
#include "butil/intrusive_ptr.hpp"
#include "butil/shared_object.h"
#include "butil/strings/safe_sprintf.h"
#include "butil/synchronization/condition_variable.h"
#include <libunwind.h>
#include <signal.h>
#include <vector>

namespace bthread {

// Tracer for bthread.
class TaskTracer {
public:
    // Returns 0 on success, -1 otherwise.
    bool Init();
    // Set the status to `s'.
    void set_status(TaskStatus s, TaskMeta* meta);
    static void set_running_status(pthread_t worker_tid, TaskMeta* meta);
    static bool set_end_status_unsafe(TaskMeta* m);

    // Trace the bthread of `tid`.
    std::string Trace(bthread_t tid);
    void Trace(std::ostream& os, bthread_t tid);

    // When the worker is jumping stack from a bthread to another,
    static void WaitForTracing(TaskMeta* m);

    int get_trace_signal() const {
        return _signal_num;
    }
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
        static Result MakeErrorResult(const char* fmt, Args&&... args) {
            Result result;
            result.SetError(fmt, std::forward<Args>(args)...);
            return result;
        }

        template<typename... Args>
        void SetError(const char* fmt, Args&&... args) {
            error = true;
            butil::ignore_result(butil::strings::SafeSPrintf(
                err_msg, fmt, std::forward<Args>(args)...));
        }

        std::string OutputToString() const;
        void OutputToStream(std::ostream& os) const;

        static constexpr size_t MAX_TRACE_NUM = 64;

        void* ips[MAX_TRACE_NUM];
        size_t frame_count{0};
        char err_msg[64];
        bool error{false};
    };

    // For signal trace.
    struct SignalSync : public butil::SharedObject {
        ~SignalSync() override;
        bool Init();

        int pipe_fds[2]{-1, -1};
        Result result;
    };

    Result TraceImpl(bthread_t tid);

    unw_cursor_t MakeCursor(bthread_fcontext_t fcontext);
    Result ContextTrace(bthread_fcontext_t fcontext);
    static Result TraceByLibunwind(unw_cursor_t& cursor);

    bool RegisterSignalHandler();
    static void SignalHandler(int sig, siginfo_t* info, void* context);
    Result SignalTrace(pthread_t worker_tid);

    // Make sure only one bthread is traced at a time.
    Mutex _trace_request_mutex;

    // For context trace.
    unw_context_t _context{};

    // Hold SignalSync and wait until no one is using it before releasing it.
    // This will avoid deadlock because it will not be released in the signal handler.
    std::vector<butil::intrusive_ptr<SignalSync>> _inuse_signal_syncs;

    bvar::LatencyRecorder _trace_time{"bthread_trace_time"};
    int _signal_num{SIGURG};
};

} // namespace bthread

#endif // BRPC_BTHREAD_TRACER

#endif // BRPC_BTHREAD_TRACER_H
