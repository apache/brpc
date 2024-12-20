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

#ifdef BRPC_BTHREAD_TRACER

#include "bthread/task_tracer.h"
#include <unistd.h>
#include <poll.h>
#include <gflags/gflags.h>
#include "butil/debug/stack_trace.h"
#include "butil/memory/scope_guard.h"
#include "butil/reloadable_flags.h"
#include "bthread/task_group.h"
#include "bthread/processor.h"

namespace bthread {

DEFINE_bool(enable_fast_unwind, true, "Whether to enable fast unwind");
DEFINE_uint32(signal_trace_timeout_ms, 50, "Timeout for signal trace in ms");
BUTIL_VALIDATE_GFLAG(signal_trace_timeout_ms, butil::PositiveInteger<uint32_t>);

extern BAIDU_THREAD_LOCAL TaskMeta* pthread_fake_meta;

TaskTracer::SignalSync::~SignalSync() {
    if (_pipe_init) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
    }

    if (_sem_init) {
        sem_destroy(&sem);
    }
}

bool TaskTracer::SignalSync::Init() {
    if (pipe(pipe_fds) != 0) {
        PLOG(ERROR) << "Fail to pipe";
        return false;
    }
    if (butil::make_non_blocking(pipe_fds[0]) != 0) {
        PLOG(ERROR) << "Fail to make_non_blocking";
        return false;
    }
    if (butil::make_non_blocking(pipe_fds[1]) != 0) {
        PLOG(ERROR) << "Fail to make_non_blocking";
        return false;
    }
    _pipe_init = true;

    if (sem_init(&sem, 0, 0) != 0) {
        PLOG(ERROR) << "Fail to sem_init";
        return false;
    }
    _sem_init = true;

    return true;
}

std::string TaskTracer::Result::OutputToString() {
    std::string str;
    if (err_count > 0 || frame_count > 0) {
        str.reserve(1024);
    }
    if (frame_count > 0) {
        if (fast_unwind) {
            butil::debug::StackTrace stack_trace((void**)&ips, frame_count);
            stack_trace.OutputToString(str);
        } else {
            for (size_t i = 0; i < frame_count; ++i) {
                butil::string_appendf(&str, "#%zu 0x%016lx ", i, ips[i]);
                if (mangled[i][0] == '\0') {
                    str.append("<unknown>");
                } else {
                    str.append(butil::demangle(mangled[i]));
                }
                if (i + 1 < frame_count) {
                    str.push_back('\n');
                }
            }
        }
    } else {
        str.append("No frame");
    }

    if (err_count > 0) {
        str.append("\nError message:\n");
    }
    for (size_t i = 0; i < err_count; ++i) {
        str.append(err_msg[i]);
        if (i + 1 < err_count) {
            str.push_back('\n');
        }
    }

    return str;
}

void TaskTracer::Result::OutputToStream(std::ostream& os) {
    if (frame_count > 0) {
        if (fast_unwind) {
            butil::debug::StackTrace stack_trace((void**)&ips, frame_count);
            stack_trace.OutputToStream(&os);
        } else {
            for (size_t i = 0; i < frame_count; ++i) {
                os << "# " << i << " 0x" << std::hex << ips[i] << std::dec << " ";
                if (mangled[i][0] == '\0') {
                    os << "<unknown>";
                } else {
                    os << butil::demangle(mangled[i]);
                }
                if (i + 1 < frame_count) {
                    os << '\n';
                }
            }
        }
    } else {
        os << "No frame";
    }

    if (err_count == 0) {
        return;
    }

    os << "\nError message:\n";
    for (size_t i = 0; i < err_count; ++i) {
        os << err_msg[i];
        if (i + 1 < err_count) {
            os << '\n';
        }
    }
}

bool TaskTracer::Init() {
    if (_trace_time.expose("bthread_trace_time") != 0) {
        return false;
    }
    if (!RegisterSignalHandler()) {
        return false;
    }
    // Warm up the libunwind.
    unw_cursor_t cursor;
    if (unw_getcontext(&_context) == 0 && unw_init_local(&cursor, &_context) == 0) {
        butil::ignore_result(TraceCore(cursor));
    }
    return true;
}

void TaskTracer::set_status(TaskStatus s, TaskMeta* m) {
    CHECK_NE(TASK_STATUS_RUNNING, s) << "Use `set_running_status' instead";
    CHECK_NE(TASK_STATUS_END, s) << "Use `set_end_status_unsafe' instead";

    bool tracing;
    {
        BAIDU_SCOPED_LOCK(m->version_lock);
        if (TASK_STATUS_UNKNOWN == m->status && TASK_STATUS_JUMPING == s) {
            // Do not update status for jumping when bthread is ending.
            return;
        }

        tracing = m->traced;
        // bthread is scheduled for the first time.
        if (TASK_STATUS_READY == s || NULL == m->stack) {
            m->status = TASK_STATUS_FIRST_READY;
        } else {
            m->status = s;
        }
        if (TASK_STATUS_CREATED == s) {
            m->worker_tid = -1;
        }
    }

    // Make sure bthread does not jump stack when it is being traced.
    if (tracing && TASK_STATUS_JUMPING == s) {
        WaitForTracing(m);
    }
}

void TaskTracer::set_running_status(pid_t worker_tid, TaskMeta* m) {
    BAIDU_SCOPED_LOCK(m->version_lock);
    m->worker_tid = worker_tid;
    m->status = TASK_STATUS_RUNNING;
}

bool TaskTracer::set_end_status_unsafe(TaskMeta* m) {
    m->status = TASK_STATUS_END;
    return m->traced;
}

std::string TaskTracer::Trace(bthread_t tid) {
    return TraceImpl(tid).OutputToString();
}

void TaskTracer::Trace(std::ostream& os, bthread_t tid) {
    TraceImpl(tid).OutputToStream(os);
}

void TaskTracer::WaitForTracing(TaskMeta* m) {
    BAIDU_SCOPED_LOCK(_mutex);
    while (m->traced) {
        _cond.Wait();
    }
}

TaskStatus TaskTracer::WaitForJumping(TaskMeta* m) {
    // Reasons for not using locks here:
    // 1. It is necessary to lock before jump_stack, unlock after jump_stack,
    //    which involves two different bthread and is prone to errors.
    // 2. jump_stack is fast.
    int i = 0;
    do {
        // The bthread is jumping now, spin until it finishes.
        if (i++ < 30) {
            cpu_relax();
        } else {
            sched_yield();
        }

        BAIDU_SCOPED_LOCK(m->version_lock);
        if (TASK_STATUS_JUMPING != m->status) {
            return m->status;
        }
    } while (true);
}

TaskTracer::Result TaskTracer::TraceImpl(bthread_t tid) {
    butil::Timer timer(butil::Timer::STARTED);
    BRPC_SCOPE_EXIT {
        timer.stop();
        _trace_time << timer.n_elapsed();
    };

    if (tid == bthread_self() ||
        (NULL != pthread_fake_meta && tid == pthread_fake_meta->tid)) {
        return Result::MakeErrorResult("Can not trace self=%d", tid);
    }

    // Make sure only one bthread is traced at a time.
    BAIDU_SCOPED_LOCK(_trace_request_mutex);

    TaskMeta* m = TaskGroup::address_meta(tid);
    if (NULL == m) {
        return Result::MakeErrorResult("bthread=%d never existed", tid);
    }

    BAIDU_SCOPED_LOCK(_mutex);
    TaskStatus status;
    pid_t worker_tid;
    const uint32_t given_version = get_version(tid);
    {
        BAIDU_SCOPED_LOCK(m->version_lock);
        if (given_version == *m->version_butex) {
            // Start tracing.
            m->traced = true;
            worker_tid = m->worker_tid;
            status = m->status;
        } else {
            return Result::MakeErrorResult("bthread=%d not exist now", tid);
        }
    }

    if (TASK_STATUS_UNKNOWN == status) {
        return Result::MakeErrorResult("bthread=%d not exist now", tid);
    } else if (TASK_STATUS_CREATED == status) {
        return Result::MakeErrorResult("bthread=%d has just been created", tid);
    } else if (TASK_STATUS_FIRST_READY == status) {
        return Result::MakeErrorResult("bthread=%d is scheduled for the first time", tid);
    } else if (TASK_STATUS_END == status) {
        return Result::MakeErrorResult("bthread=%d has ended", tid);
    } else if (TASK_STATUS_JUMPING == status) {
        // Wait for jumping completion.
        status = WaitForJumping(m);
    }

    // After jumping, the status may be RUNNING, SUSPENDED, or READY, which is traceable.
    Result result{};
    if (TASK_STATUS_RUNNING == status) {
        result = SignalTrace(worker_tid);
    } else if (TASK_STATUS_SUSPENDED == status || TASK_STATUS_READY == status) {
        result = ContextTrace(m->stack->context);
    }

    {
        BAIDU_SCOPED_LOCK(m->version_lock);
        // If m->status is BTHREAD_STATUS_END, the bthread also waits for tracing completion,
        // so given_version != *m->version_butex is OK.
        m->traced = false;
    }
    // Wake up the waiting worker thread to jump.
    _cond.Signal();

    return result;
}

unw_cursor_t TaskTracer::MakeCursor(bthread_fcontext_t fcontext) {
    unw_cursor_t cursor;
    unw_init_local(&cursor, &_context);
    auto regs = reinterpret_cast<uintptr_t*>(fcontext);

    // Only need RBP, RIP, RSP on x86_64.
    // The base pointer (RBP).
    if (unw_set_reg(&cursor, UNW_X86_64_RBP, regs[6]) != 0) {
        LOG(ERROR) << "Fail to set RBP";
    }
    // The instruction pointer (RIP).
    if (unw_set_reg(&cursor, UNW_REG_IP, regs[7]) != 0) {
        LOG(ERROR) << "Fail to set RIP";
    }
#if UNW_VERSION_MAJOR >= 1 && UNW_VERSION_MINOR >= 7
    // The stack pointer (RSP).
    if (unw_set_reg(&cursor, UNW_REG_SP, regs[8]) != 0) {
        LOG(ERROR) << "Fail to set RSP";
    }
#endif

    return cursor;
}

TaskTracer::Result TaskTracer::ContextTrace(bthread_fcontext_t fcontext) {
    unw_cursor_t cursor = MakeCursor(fcontext);
    return TraceCore(cursor);
}

bool TaskTracer::RegisterSignalHandler() {
    // Set up the signal handler.
    struct sigaction old_sa{};
    struct sigaction sa{};
    sa.sa_sigaction = SignalHandler;
    sa.sa_flags = SA_SIGINFO;
    sigfillset(&sa.sa_mask);
    if (sigaction(SIGURG, &sa, &old_sa) != 0) {
        PLOG(ERROR) << "Failed to sigaction";
        return false;
    }
    if (NULL != old_sa.sa_handler || NULL != old_sa.sa_sigaction) {
        LOG(ERROR) << "Signal handler of SIGURG is already registered";
        return false;
    }

    return true;
}

// Caution: This function should be async-signal-safety.
void TaskTracer::SignalHandler(int, siginfo_t* info, void* context) {
    ErrnoGuard guard;
    butil::intrusive_ptr<SignalSync> signal_sync(
        static_cast<SignalSync*>(info->si_value.sival_ptr));
    if (NULL == signal_sync) {
        // The signal is not from Tracer, such as TaskControl, do nothing.
        return;
    }

    signal_sync->context = static_cast<unw_context_t*>(context);
    // Notify SignalTrace that SignalTraceHandler has started.
    // Binary semaphore do not fail, so no need to check return value.
    // sem_post() is async-signal-safe.
    sem_post(&signal_sync->sem);

    butil::Timer timer;
    if (FLAGS_signal_trace_timeout_ms > 0) {
        timer.start();
    }
    int timeout = -1;
    pollfd poll_fd = {signal_sync->pipe_fds[0], POLLIN, 0};
    // Wait for tracing to complete.
    while (true) {
        if (FLAGS_signal_trace_timeout_ms > 0) {
            timer.stop();
            // At least 1ms timeout.
            timeout = std::max(
                (int64_t)FLAGS_signal_trace_timeout_ms - timer.m_elapsed(), (int64_t)1);
        }
        // poll() is async-signal-safe.
        // Similar to self-pipe trick: https://man7.org/tlpi/code/online/dist/altio/self_pipe.c.html
        int rc = poll(&poll_fd, 1, timeout);
        if (-1 == rc && EINTR == errno) {
            continue;
        }
        // No need to read the pipe or handle errors, just return.
        return;
    }
}

// Caution: This fnction should be async-signal-safety.
bool TaskTracer::WaitForSignalHandler(butil::intrusive_ptr<SignalSync> signal_sync,
                                      const timespec* abs_timeout, Result& result) {
    // It is safe to sem_timedwait() here and sem_post() in SignalHandler.
    while (sem_timedwait(&signal_sync->sem, abs_timeout) != 0) {
        if (EINTR == errno) {
            continue;
        }
        if (ETIMEDOUT == errno) {
            result.SetError("Timeout exceed %dms", FLAGS_signal_trace_timeout_ms);
        } else {
            // During the process of signal handler,
            // can not use berro() which is not async-signal-safe.
            result.SetError("Fail to sem_timedwait, errno=%d", errno);
        }
        return false;
    }
    return true;
}

// Caution: This fnction should be async-signal-safety.
void TaskTracer::WakeupSignalHandler(butil::intrusive_ptr<SignalSync> signal_sync, Result& result) {
    while (true) {
        ssize_t nw = write(signal_sync->pipe_fds[1], "1", 1);
        if (0 < nw) {
            break;
        } else if (-1 == nw && EINTR == errno) {
            // Only EINTR is allowed. Even EAGAIN should not be returned.
            continue;
        }
        // During the process of signal handler,
        // can not use berro() which is not async-signal-safe.
        result.SetError("Fail to write pipe to notify signal handler, errno=%d", errno);
    }
}

TaskTracer::Result TaskTracer::SignalTrace(pid_t tid) {
    // CAUTION:
    // The signal handler will wait for the backtrace to complete.
    // If the worker thread is interrupted when holding a resource(lock, etc),
    // and this function waits for the resource during capturing backtraces,
    // it may cause a deadlock.
    //
    // https://github.com/gperftools/gperftools/wiki/gperftools'-stacktrace-capturing-methods-and-their-issues#libunwind
    // Generally, libunwind promises async-signal-safety for capturing backtraces.
    // But in practice, it is only partially async-signal-safe due to reliance on
    // dl_iterate_phdr API, which is used to enumerate all loaded ELF modules
    // (.so files and main executable binary). No libc offers dl_iterate_pdhr that
    // is async-signal-safe. In practice, the issue may happen if we take tracing
    // signal during an existing dl_iterate_phdr call (such as when the program
    // throws an exception) or during dlopen/dlclose-ing some .so module.
    // Deadlock call stack:
    // #0  __lll_lock_wait (futex=futex@entry=0x7f0d3d7f0990 <_rtld_global+2352>, private=0) at lowlevellock.c:52
    // #1  0x00007f0d3a73c131 in __GI___pthread_mutex_lock (mutex=0x7f0d3d7f0990 <_rtld_global+2352>) at ../nptl/pthread_mutex_lock.c:115
    // #2  0x00007f0d38eb0231 in __GI___dl_iterate_phdr (callback=callback@entry=0x7f0d38c456a0 <_ULx86_64_dwarf_callback>, data=data@entry=0x7f0d07defad0) at dl-iteratephdr.c:40
    // #3  0x00007f0d38c45d79 in _ULx86_64_dwarf_find_proc_info (as=0x7f0d38c4f340 <local_addr_space>, ip=ip@entry=139694791966897, pi=pi@entry=0x7f0d07df0498, need_unwind_info=need_unwind_info@entry=1, arg=0x7f0d07df0340) at dwarf/Gfind_proc_info-lsb.c:759
    // #4  0x00007f0d38c43260 in fetch_proc_info (c=c@entry=0x7f0d07df0340, ip=139694791966897) at dwarf/Gparser.c:461
    // #5  0x00007f0d38c44e46 in find_reg_state (sr=0x7f0d07defd10, c=0x7f0d07df0340) at dwarf/Gparser.c:925
    // #6  _ULx86_64_dwarf_step (c=c@entry=0x7f0d07df0340) at dwarf/Gparser.c:972
    // #7  0x00007f0d38c40c14 in _ULx86_64_step (cursor=cursor@entry=0x7f0d07df0340) at x86_64/Gstep.c:71
    // #8  0x00007f0d399ed8f6 in GetStackTraceWithContext_libunwind (result=<optimized out>, max_depth=63, skip_count=132054887, ucp=<optimized out>) at src/stacktrace_libunwind-inl.h:138
    // #9  0x00007f0d399ee083 in GetStackTraceWithContext (result=0x7f0d07df07b8, max_depth=63, skip_count=3, uc=0x7f0d07df0a40) at src/stacktrace.cc:305
    // #10 0x00007f0d399ea992 in CpuProfiler::prof_handler (signal_ucontext=<optimized out>, cpu_profiler=0x7f0d399f6600, sig=<optimized out>) at src/profiler.cc:359
    // #11 0x00007f0d399eb633 in ProfileHandler::SignalHandler (sig=27, sinfo=0x7f0d07df0b70, ucontext=0x7f0d07df0a40) at src/profile-handler.cc:530
    // #12 <signal handler called>
    // #13 0x00007f0d3a73c0b1 in __GI___pthread_mutex_lock (mutex=0x7f0d3d7f0990 <_rtld_global+2352>) at ../nptl/pthread_mutex_lock.c:115
    // #14 0x00007f0d38eb0231 in __GI___dl_iterate_phdr (callback=0x7f0d38f525f0, data=0x7f0d07df10c0) at dl-iteratephdr.c:40
    // #15 0x00007f0d38f536c1 in _Unwind_Find_FDE () from /lib/x86_64-linux-gnu/libgcc_s.so.1
    // #16 0x00007f0d38f4f868 in ?? () from /lib/x86_64-linux-gnu/libgcc_s.so.1
    // #17 0x00007f0d38f50a20 in ?? () from /lib/x86_64-linux-gnu/libgcc_s.so.1
    // #18 0x00007f0d38f50f99 in _Unwind_RaiseException () from /lib/x86_64-linux-gnu/libgcc_s.so.1
    // #19 0x00007f0d390088dc in __cxa_throw () from /lib/x86_64-linux-gnu/libstdc++.so.6
    // #20 0x00007f0d3b5b2245 in __cxxabiv1::__cxa_throw (thrownException=0x7f0d114ea8c0, type=0x7f0d3d6dd830 <typeinfo for rockset::GRPCError>, destructor=<optimized out>) at /src/folly/folly/experimental/exception_tracer/ExceptionTracerLib.cpp:107
    //
    // Therefore, we do not capture backtracks in the signal handler to avoid mutex
    // reentry and deadlock. Instead, we capture backtracks in this function and
    // ends the signal handler after capturing backtraces is complete.
    // Even so, there is still a deadlock problem:
    // the worker thread is interrupted when during an existing dl_iterate_phdr call,
    // and wait for the capturing backtraces to complete. This function capture
    // backtracks with dl_iterate_phdr. We introduce a timeout mechanism in signal
    // handler to avoid deadlock.

    // Each signal trace has an independent SignalSync to
    // prevent the previous SignalHandler from affecting the new SignalTrace.
    butil::intrusive_ptr<SignalSync> signal_sync(new SignalSync());
    if (!signal_sync->Init()) {
        return Result::MakeErrorResult("Fail to init SignalSync");
    }
    // Add reference for SignalHandler.
    signal_sync->AddRefManually();

    union sigval value{};
    value.sival_ptr = signal_sync.get();
    size_t sigqueue_try = 0;
    while (sigqueue(tid, SIGURG, value) != 0) {
        if (errno != EAGAIN || sigqueue_try++ >= 3) {
            return Result::MakeErrorResult("Fail to sigqueue: %s", berror());
        }
    }

    // Caution: Start here, need to ensure async-signal-safety.
    Result result;
    // Wakeup the signal handler at the end.
    BRPC_SCOPE_EXIT {
        WakeupSignalHandler(signal_sync, result);
    };

    timespec abs_timeout{};
    timespec* abs_timeout_ptr = NULL;
    if (FLAGS_signal_trace_timeout_ms > 0) {
        abs_timeout = butil::milliseconds_from_now(FLAGS_signal_trace_timeout_ms);
        abs_timeout_ptr = &abs_timeout;
    }
    // Wait for the signal handler to start.
    if (!WaitForSignalHandler(signal_sync, abs_timeout_ptr, result)) {
        return result;
    }

    if (NULL == signal_sync->context) {
        result.SetError("context is NULL");
        return result;
    }
    unw_cursor_t cursor;
    int rc = unw_init_local(&cursor, signal_sync->context);
    if (0 != rc) {
        result.SetError("Failed to init local, rc=%d", rc);
        return result;
    }

    return TraceCore(cursor);
}

TaskTracer::Result TaskTracer::TraceCore(unw_cursor_t& cursor) {
    Result result{};
    result.fast_unwind = FLAGS_enable_fast_unwind;
    for (result.frame_count = 0; result.frame_count < arraysize(result.ips); ++result.frame_count) {
        int rc = unw_step(&cursor);
        if (0 == rc) {
            break;
        } else if (rc < 0) {
            return Result::MakeErrorResult("Fail to unw_step, rc=%d", rc);
        }

        unw_word_t ip = 0;
        // Fast unwind do not care about the return value.
        rc = unw_get_reg(&cursor, UNW_REG_IP, &ip);
        result.ips[result.frame_count] = ip;

        if (result.fast_unwind) {
            continue;
        }

        if (0 != rc) {
            result.mangled[result.frame_count][0] = '\0';
            continue;
        }

        // Slow path.
        rc = unw_get_proc_name(&cursor, result.mangled[result.frame_count],
                               sizeof(result.mangled[result.frame_count]), NULL);
        // UNW_ENOMEM is OK.
        if (0 != rc && UNW_ENOMEM != rc) {
            result.mangled[result.frame_count][0] = '\0';
        }
    }

    return result;
}

} // namespace bthread

#endif // BRPC_BTHREAD_TRACER
