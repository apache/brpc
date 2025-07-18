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

// Date: 2012-10-08 23:53:50

#include "butil/logging.h"

#include <gflags/gflags.h>
DEFINE_bool(log_as_json, false, "Print log as a valid JSON");
DEFINE_bool(escape_log, false, "Escape log content before printing");

#if !BRPC_WITH_GLOG

#if defined(OS_WIN)
#include <io.h>
#include <windows.h>
typedef HANDLE FileHandle;
typedef HANDLE MutexHandle;
// Windows warns on using write().  It prefers _write().
#define write(fd, buf, count) _write(fd, buf, static_cast<unsigned int>(count))
// Windows doesn't define STDERR_FILENO.  Define it here.
#define STDERR_FILENO 2
#elif defined(OS_MACOSX)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#elif defined(OS_POSIX)
#if defined(OS_NACL) || defined(OS_LINUX)
#include <sys/time.h> // timespec doesn't seem to be in <time.h>
#else
#include <sys/syscall.h>
#endif
#include <time.h>
#endif
#if defined(OS_POSIX)
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define MAX_PATH PATH_MAX
typedef FILE* FileHandle;
typedef pthread_mutex_t* MutexHandle;
#endif

#include <algorithm>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <string>

#include "butil/file_util.h"
#include "butil/debug/alias.h"
#include "butil/debug/debugger.h"
#include "butil/debug/stack_trace.h"
#include "butil/posix/eintr_wrapper.h"
#include "butil/strings/string_util.h"
#include "butil/strings/stringprintf.h"
#include "butil/strings/utf_string_conversions.h"
#include "butil/synchronization/condition_variable.h"
#include "butil/threading/platform_thread.h"
#include "butil/threading/simple_thread.h"
#include "butil/object_pool.h"

#if defined(OS_POSIX)
#include "butil/errno.h"
#include "butil/fd_guard.h"
#endif
#if defined(OS_LINUX)
#include <fcntl.h>
#endif

#if defined(OS_ANDROID)
#include <android/log.h>
#endif

#include <map>
#include <vector>
#include <deque>
#include <limits>
#include "butil/atomicops.h"
#include "butil/thread_local.h"
#include "butil/scoped_lock.h"                        // BAIDU_SCOPED_LOCK
#include "butil/string_splitter.h"
#include "butil/time.h"
#include "butil/containers/doubly_buffered_data.h"
#include "butil/memory/singleton.h"
#include "butil/endpoint.h"
#include "butil/reloadable_flags.h"
#ifdef BAIDU_INTERNAL
#include "butil/comlog_sink.h"
#endif

extern "C" {
uint64_t BAIDU_WEAK bthread_self();
typedef struct {
    uint32_t index;    // index in KeyTable
    uint32_t version;  // ABA avoidance
} bthread_key_t;
int BAIDU_WEAK bthread_key_create(bthread_key_t* key,
                                  void (*destructor)(void* data));
int BAIDU_WEAK bthread_setspecific(bthread_key_t key, void* data);
void* BAIDU_WEAK bthread_getspecific(bthread_key_t key);
}

namespace logging {

DEFINE_bool(crash_on_fatal_log, false,
            "Crash process when a FATAL log is printed");
BUTIL_VALIDATE_GFLAG(crash_on_fatal_log, butil::PassValidate);

DEFINE_bool(print_stack_on_check, true,
            "Print the stack trace when a CHECK was failed");
BUTIL_VALIDATE_GFLAG(print_stack_on_check, butil::PassValidate);

DEFINE_int32(v, 0, "Show all VLOG(m) messages for m <= this."
             " Overridable by --vmodule.");
DEFINE_string(vmodule, "", "per-module verbose level."
              " Argument is a comma-separated list of MODULE_NAME=LOG_LEVEL."
              " MODULE_NAME is a glob pattern, matched against the filename base"
              " (that is, name ignoring .cpp/.h)."
              " LOG_LEVEL overrides any value given by --v.");

DEFINE_bool(log_pid, false, "Log process id");

DEFINE_bool(log_bid, true, "Log bthread id");

DEFINE_int32(minloglevel, 0, "Any log at or above this level will be "
             "displayed. Anything below this level will be silently ignored. "
             "0=INFO 1=NOTICE 2=WARNING 3=ERROR 4=FATAL");
BUTIL_VALIDATE_GFLAG(minloglevel, butil::NonNegativeInteger);

DEFINE_bool(log_hostname, false, "Add host after pid in each log so"
            " that we know where logs came from when using aggregation tools"
            " like ELK.");

DEFINE_bool(log_year, false, "Log year in datetime part in each log");

DEFINE_bool(log_func_name, false, "[DEPRECATED]Log function name in each log. "
                                  "Now DefaultLogSink logs function names by default. "
                                  "Customized LogSink can also log function names through "
                                  "corresponding OnLogMessage.");

DEFINE_bool(async_log, false, "Use async log");

DEFINE_bool(async_log_in_background_always, false, "Async log written in background always.");

DEFINE_int32(max_async_log_queue_size, 100000, "Max async log size. "
             "If current log count of async log > max_async_log_size, "
             "Use sync log to protect process.");

DEFINE_int32(sleep_to_flush_async_log_s, 0,
             "If the value > 0, sleep before atexit to flush async log");

namespace {

LoggingDestination logging_destination = LOG_DEFAULT;

// For BLOG_ERROR and above, always print to stderr.
const int kAlwaysPrintErrorLevel = BLOG_ERROR;

// Which log file to use? This is initialized by InitLogging or
// will be lazily initialized to the default value when it is
// first needed.
#if defined(OS_WIN)
typedef std::wstring PathString;
#else
typedef std::string PathString;
#endif
PathString* log_file_name = NULL;

// this file is lazily opened and the handle may be NULL
FileHandle log_file = NULL;

// Should we pop up fatal debug messages in a dialog?
bool show_error_dialogs = false;

// An assert handler override specified by the client to be called instead of
// the debug message dialog and process termination.
LogAssertHandler log_assert_handler = NULL;

// Helper functions to wrap platform differences.

int32_t CurrentProcessId() {
#if defined(OS_WIN)
    return GetCurrentProcessId();
#elif defined(OS_POSIX)
    return getpid();
#endif
}

void DeleteFilePath(const PathString& log_name) {
#if defined(OS_WIN)
    DeleteFile(log_name.c_str());
#elif defined (OS_NACL)
    // Do nothing; unlink() isn't supported on NaCl.
#else
    unlink(log_name.c_str());
#endif
}

#if defined(OS_LINUX)
static PathString GetProcessName() {
    butil::fd_guard fd(open("/proc/self/cmdline", O_RDONLY));
    if (fd < 0) {
        return "unknown";
    }
    char buf[512];
    const ssize_t len = read(fd, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return "unknown";
    }
    buf[len] = '\0';
    // Not string(buf, len) because we needs to buf to be truncated at first \0.
    // Under gdb, the first part of cmdline may include path.
    return butil::FilePath(std::string(buf)).BaseName().value();
}
#endif

PathString GetDefaultLogFile() {
#if defined(OS_WIN)
    // On Windows we use the same path as the exe.
    wchar_t module_name[MAX_PATH];
    GetModuleFileName(NULL, module_name, MAX_PATH);

    PathString log_file = module_name;
    PathString::size_type last_backslash =
        log_file.rfind('\\', log_file.size());
    if (last_backslash != PathString::npos)
        log_file.erase(last_backslash + 1);
    log_file += L"debug.log";
    return log_file;
#elif defined(OS_LINUX)
    return GetProcessName() + ".log";
#elif defined(OS_POSIX)    
    // On other platforms we just use the current directory.
    return PathString("debug.log");
#endif
}

// This class acts as a wrapper for locking the logging files.
// LoggingLock::Init() should be called from the main thread before any logging
// is done. Then whenever logging, be sure to have a local LoggingLock
// instance on the stack. This will ensure that the lock is unlocked upon
// exiting the frame.
// LoggingLocks can not be nested.
class LoggingLock {
public:
    LoggingLock() {
        LockLogging();
    }

    ~LoggingLock() {
        UnlockLogging();
    }

    static void Init(LogLockingState lock_log, const LogChar* new_log_file) {
        if (initialized)
            return;
        lock_log_file = lock_log;
        if (lock_log_file == LOCK_LOG_FILE) {
#if defined(OS_WIN)
            if (!log_mutex) {
                std::wstring safe_name;
                if (new_log_file)
                    safe_name = new_log_file;
                else
                    safe_name = GetDefaultLogFile();
                // \ is not a legal character in mutex names so we replace \ with /
                std::replace(safe_name.begin(), safe_name.end(), '\\', '/');
                std::wstring t(L"Global\\");
                t.append(safe_name);
                log_mutex = ::CreateMutex(NULL, FALSE, t.c_str());

                if (log_mutex == NULL) {
#if DEBUG
                    // Keep the error code for debugging
                    int error = GetLastError();  // NOLINT
                    butil::debug::BreakDebugger();
#endif
                    // Return nicely without putting initialized to true.
                    return;
                }
            }
#endif
        } else {
            log_lock = new butil::Mutex;
        }
        initialized = true;
    }

private:
    static void LockLogging() {
        if (lock_log_file == LOCK_LOG_FILE) {
#if defined(OS_WIN)
            ::WaitForSingleObject(log_mutex, INFINITE);
            // WaitForSingleObject could have returned WAIT_ABANDONED. We don't
            // abort the process here. UI tests might be crashy sometimes,
            // and aborting the test binary only makes the problem worse.
            // We also don't use LOG macros because that might lead to an infinite
            // loop. For more info see http://crbug.com/18028.
#elif defined(OS_POSIX)
            pthread_mutex_lock(&log_mutex);
#endif
        } else {
            // use the lock
            log_lock->lock();
        }
    }

    static void UnlockLogging() {
        if (lock_log_file == LOCK_LOG_FILE) {
#if defined(OS_WIN)
            ReleaseMutex(log_mutex);
#elif defined(OS_POSIX)
            pthread_mutex_unlock(&log_mutex);
#endif
        } else {
            log_lock->unlock();
        }
    }

    // The lock is used if log file locking is false. It helps us avoid problems
    // with multiple threads writing to the log file at the same time.
    static butil::Mutex* log_lock;

    // When we don't use a lock, we are using a global mutex. We need to do this
    // because LockFileEx is not thread safe.
#if defined(OS_WIN)
    static MutexHandle log_mutex;
#elif defined(OS_POSIX)
    static pthread_mutex_t log_mutex;
#endif

    static bool initialized;
    static LogLockingState lock_log_file;
};

// static
bool LoggingLock::initialized = false;
// static
butil::Mutex* LoggingLock::log_lock = NULL;
// static
LogLockingState LoggingLock::lock_log_file = LOCK_LOG_FILE;

#if defined(OS_WIN)
// static
MutexHandle LoggingLock::log_mutex = NULL;
#elif defined(OS_POSIX)
pthread_mutex_t LoggingLock::log_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

// Called by logging functions to ensure that debug_file is initialized
// and can be used for writing. Returns false if the file could not be
// initialized. debug_file will be NULL in this case.
bool InitializeLogFileHandle() {
    if (log_file)
        return true;

    if (!log_file_name) {
        // Nobody has called InitLogging to specify a debug log file, so here we
        // initialize the log file name to a default.
        log_file_name = new PathString(GetDefaultLogFile());
    }

    if ((logging_destination & LOG_TO_FILE) != 0) {
#if defined(OS_WIN)
        log_file = CreateFile(log_file_name->c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (log_file == INVALID_HANDLE_VALUE || log_file == NULL) {
            // try the current directory
            log_file = CreateFile(L".\\debug.log", GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (log_file == INVALID_HANDLE_VALUE || log_file == NULL) {
                log_file = NULL;
                return false;
            }
        }
        SetFilePointer(log_file, 0, 0, FILE_END);
#elif defined(OS_POSIX)
        log_file = fopen(log_file_name->c_str(), "a");
        if (log_file == NULL) {
            fprintf(stderr, "Fail to fopen %s: %s", log_file_name->c_str(), berror());
            return false;
        }
#endif
    }

    return true;
}

void CloseFile(FileHandle log) {
#if defined(OS_WIN)
    CloseHandle(log);
#else
    fclose(log);
#endif
}

void CloseLogFileUnlocked() {
    if (!log_file)
        return;

    CloseFile(log_file);
    log_file = NULL;
}

void Log2File(const std::string& log) {
    // We can have multiple threads and/or processes, so try to prevent them
    // from clobbering each other's writes.
    // If the client app did not call InitLogging, and the lock has not
    // been created do it now. We do this on demand, but if two threads try
    // to do this at the same time, there will be a race condition to create
    // the lock. This is why InitLogging should be called from the main
    // thread at the beginning of execution.
    LoggingLock::Init(LOCK_LOG_FILE, NULL);
    LoggingLock logging_lock;
    if (InitializeLogFileHandle()) {
#if defined(OS_WIN)
        SetFilePointer(log_file, 0, 0, SEEK_END);
        DWORD num_written;
        WriteFile(log_file, static_cast<const void*>(log.data()),
                  static_cast<DWORD>(log.size()), &num_written, NULL);
#else
        fwrite(log.data(), log.size(), 1, log_file);
        fflush(log_file);
#endif
    }
}

}  // namespace

#if defined(OS_LINUX) || defined(OS_MACOSX)
typedef timeval TimeVal;
#else
struct TimeVal {
    time_t tv_sec;
};
#endif

TimeVal GetTimestamp() {
#if defined(OS_LINUX) || defined(OS_MACOSX)
    timeval tv;
    gettimeofday(&tv, NULL);
    return tv;
#else
    return { time(NULL) };
#endif
}

struct BAIDU_CACHELINE_ALIGNMENT LogInfo {
    ~LogInfo() = default;
    void clear() {
        file.clear();
        func.clear();
        content.clear();
    }

    std::string file;
    std::string func;
    std::string content;
    TimeVal timestamp{};
    int severity{0};
    int line{0};
    // If `raw' is false, content has been a complete log.
    // If raw is true, a complete log consists of all properties of LogInfo.
    bool raw{false};
};

struct BAIDU_CACHELINE_ALIGNMENT LogRequest {
    static LogRequest* const UNCONNECTED;

    LogRequest* next{NULL};
    LogInfo log_info;
};

LogRequest* const LogRequest::UNCONNECTED = (LogRequest*)(intptr_t)-1;

class AsyncLogger : public butil::SimpleThread {
public:
    static AsyncLogger* GetInstance();

    void Log(const LogInfo& log_info);
    void Log(LogInfo&& log_info);
    void StopAndJoin();

private:
friend struct DefaultSingletonTraits<AsyncLogger>;

    static LogRequest _stop_req;

    AsyncLogger();
    ~AsyncLogger() override;

    static void AtExit() {
        GetInstance()->StopAndJoin();
        if (FLAGS_sleep_to_flush_async_log_s > 0) {
            ::sleep(FLAGS_sleep_to_flush_async_log_s);
        }
    }

    void LogImpl(LogRequest* log_req);

    void Run() override;

    void LogTask(LogRequest* req);

    bool IsLogComplete(LogRequest* old_head);

    void DoLog(LogRequest* req);
    void DoLog(const LogInfo& log_info);

    butil::atomic<LogRequest*> _log_head;
    butil::Mutex _mutex;
    butil::ConditionVariable _cond;
    LogRequest* _current_log_request;
    butil::atomic<int32_t> _log_request_count;
    butil::atomic<bool> _stop;
};

AsyncLogger* AsyncLogger::GetInstance() {
    return Singleton<AsyncLogger,
                     LeakySingletonTraits<AsyncLogger>>::get();
}

AsyncLogger::AsyncLogger()
    : butil::SimpleThread("async_log_thread")
    , _log_head(NULL)
    , _cond(&_mutex)
    , _current_log_request(NULL)
    , _stop(false) {
    Start();
    // We need to stop async logger and
    // flush all async log before exit.
    atexit(AtExit);
}

AsyncLogger::~AsyncLogger() {
    StopAndJoin();
}

std::string LogInfoToLogStr(int severity, butil::StringPiece file,
                            int line, butil::StringPiece func,
                            butil::StringPiece content) {
    // There's a copy here to concatenate prefix and content. Since
    // DefaultLogSink is hardly used right now, the copy is irrelevant.
    // A LogSink focused on performance should also be able to handle
    // non-continuous inputs which is a must to maximize performance.
    std::ostringstream os;
    PrintLog(os, severity, file.data(), line, func.data(), content);
    os << '\n';
    return os.str();
}

std::string LogInfo2LogStr(const LogInfo& log_info) {
    return LogInfoToLogStr(log_info.severity, log_info.file, log_info.line,
                           log_info.func, log_info.content);
}

void AsyncLogger::Log(const LogInfo& log_info) {
    if (log_info.content.empty()) {
        return;
    }

    bool is_full = FLAGS_max_async_log_queue_size > 0 &&
        _log_request_count.fetch_add(1, butil::memory_order_relaxed) >
        FLAGS_max_async_log_queue_size;
    if (is_full || _stop.load(butil::memory_order_relaxed)) {
        // Async logger is full or stopped, fallback to sync log.
        DoLog(log_info);
        return;
    }

    auto log_req = butil::get_object<LogRequest>();
    if (!log_req) {
        // Async log failed, fallback to sync log.
        DoLog(log_info);
        return;
    }
    log_req->log_info = log_info;
    LogImpl(log_req);
}

void AsyncLogger::Log(LogInfo&& log_info) {
    if (log_info.content.empty()) {
        return;
    }

    bool is_full = FLAGS_max_async_log_queue_size > 0 &&
        _log_request_count.fetch_add(1, butil::memory_order_relaxed) >
        FLAGS_max_async_log_queue_size;
    if (is_full || _stop.load(butil::memory_order_relaxed)) {
        // Async logger is full or stopped, fallback to sync log.
        DoLog(log_info);
        return;
    }

    auto log_req = butil::get_object<LogRequest>();
    if (!log_req) {
        // Async log failed, fallback to sync log.
        DoLog(log_info);
        return;
    }
    log_req->log_info = std::move(log_info);
    LogImpl(log_req);
}

void AsyncLogger::LogImpl(LogRequest* log_req) {
    log_req->next = LogRequest::UNCONNECTED;
    // Release fence makes sure the thread getting request sees *req
    LogRequest* const prev_head =
        _log_head.exchange(log_req, butil::memory_order_release);
    if (prev_head != NULL) {
        // Someone is logging. The async_log_thread thread may spin
        // until req->next to be non-UNCONNECTED. This process is not
        // lock-free, but the duration is so short(1~2 instructions,
        // depending on compiler) that the spin rarely occurs in practice
        // (I've not seen any spin in highly contended tests).
        log_req->next = prev_head;
        return;
    }
    // We've got the right to write.
    log_req->next = NULL;

    if (!FLAGS_async_log_in_background_always) {
        // Use sync log for the LogRequest
        // which has got the right to write.
        DoLog(log_req);
        // Return when there's no more LogRequests.
        if (IsLogComplete(log_req)) {
            butil::return_object(log_req);
            return;
        }
    }

    BAIDU_SCOPED_LOCK(_mutex);
    if (_stop.load(butil::memory_order_relaxed)) {
        // Async logger is stopped, fallback to sync log.
        LogTask(log_req);
    } else {
        // Wake up async logger.
        _current_log_request = log_req;
        _cond.Signal();
    }
}

void AsyncLogger::StopAndJoin() {
    if (!_stop.exchange(true, butil::memory_order_relaxed)) {
        BAIDU_SCOPED_LOCK(_mutex);
        _cond.Signal();
    }
    if (!HasBeenJoined()) {
        Join();
    }
}

void AsyncLogger::Run() {
    while (true) {
        BAIDU_SCOPED_LOCK(_mutex);
        while (!_stop.load(butil::memory_order_relaxed) &&
               !_current_log_request) {
            _cond.Wait();
        }
        if (_stop.load(butil::memory_order_relaxed) &&
            !_current_log_request) {
            break;
        }

        LogTask(_current_log_request);
        _current_log_request = NULL;
    }
}

void AsyncLogger::LogTask(LogRequest* req) {
    do {
        // req was logged, skip it.
        if (req->next != NULL && req->log_info.content.empty()) {
            LogRequest* const saved_req = req;
            req = req->next;
            butil::return_object(saved_req);
        }

        // Log all requests to file.
        while (req->next != NULL) {
            LogRequest* const saved_req = req;
            req = req->next;
            if (!saved_req->log_info.content.empty()) {
                DoLog(saved_req);
            }
            // Release LogRequests until last request.
            butil::return_object(saved_req);
        }
        if (!req->log_info.content.empty()) {
            DoLog(req);
        }

        // Return when there's no more LogRequests.
        if (IsLogComplete(req)) {
            butil::return_object(req);
            return;
        }
    } while (true);
}

bool AsyncLogger::IsLogComplete(LogRequest* old_head) {
    if (old_head->next) {
        fprintf(stderr, "old_head->next should be NULL\n");
    }
    LogRequest* new_head = old_head;
    LogRequest* desired = NULL;
    if (_log_head.compare_exchange_strong(
        new_head, desired, butil::memory_order_acquire)) {
        // No one added new requests.
        return true;
    }
    if (new_head == old_head) {
        fprintf(stderr, "new_head should not be equal to old_head\n");
    }
    // Above acquire fence pairs release fence of exchange in Log() to make
    // sure that we see all fields of requests set.

    // Someone added new requests.
    // Reverse the list until old_head.
    LogRequest* tail = NULL;
    LogRequest* p = new_head;
    do {
        while (p->next == LogRequest::UNCONNECTED) {
            sched_yield();
        }
        LogRequest* const saved_next = p->next;
        p->next = tail;
        tail = p;
        p = saved_next;
        if (!p) {
            fprintf(stderr, "p should not be NULL\n");
        }
    } while (p != old_head);

    // Link old list with new list.
    old_head->next = tail;
    return false;
}

void AsyncLogger::DoLog(LogRequest* req) {
    DoLog(req->log_info);
    req->log_info.clear();
}

void AsyncLogger::DoLog(const LogInfo& log_info) {
    if (log_info.raw) {
        Log2File(LogInfo2LogStr(log_info));
    } else {
        Log2File(log_info.content);
    }
    _log_request_count.fetch_sub(1, butil::memory_order_relaxed);
}

LoggingSettings::LoggingSettings()
    : logging_dest(LOG_DEFAULT),
      log_file(NULL),
      lock_log(LOCK_LOG_FILE),
      delete_old(APPEND_TO_OLD_LOG_FILE) {}

bool BaseInitLoggingImpl(const LoggingSettings& settings) {
#if defined(OS_NACL)
    // Can log only to the system debug log.
    CHECK_EQ(settings.logging_dest & ~LOG_TO_SYSTEM_DEBUG_LOG, 0);
#endif

    logging_destination = settings.logging_dest;

    // ignore file options unless logging to file is set.
    if ((logging_destination & LOG_TO_FILE) == 0)
        return true;

    LoggingLock::Init(settings.lock_log, settings.log_file);
    LoggingLock logging_lock;

    // Calling InitLogging twice or after some log call has already opened the
    // default log file will re-initialize to the new options.
    CloseLogFileUnlocked();

    if (!log_file_name)
        log_file_name = new PathString();
    if (settings.log_file) {
        *log_file_name = settings.log_file;
    } else {
        *log_file_name = GetDefaultLogFile();
    }
    if (settings.delete_old == DELETE_OLD_LOG_FILE)
        DeleteFilePath(*log_file_name);

    return InitializeLogFileHandle();
}

void SetMinLogLevel(int level) {
    FLAGS_minloglevel = std::min(BLOG_FATAL, level);
}

int GetMinLogLevel() {
    return FLAGS_minloglevel;
}

void SetShowErrorDialogs(bool enable_dialogs) {
    show_error_dialogs = enable_dialogs;
}

void SetLogAssertHandler(LogAssertHandler handler) {
    log_assert_handler = handler;
}

const char* const log_severity_names[LOG_NUM_SEVERITIES] = {
    "INFO", "NOTICE", "WARNING", "ERROR", "FATAL" };

static void PrintLogSeverity(std::ostream& os, int severity) {
    if (severity < 0) {
        // Add extra space to separate from following datetime.
        os << 'V' << -severity << ' ';
    } else if (severity < LOG_NUM_SEVERITIES) {
        os << log_severity_names[severity][0];
    } else {
        os << 'U';
    }
}

void PrintLogPrefix(std::ostream& os, int severity,
                    butil::StringPiece file, int line,
                    butil::StringPiece func, TimeVal tv) {
    PrintLogSeverity(os, severity);
    time_t t = tv.tv_sec;
    struct tm local_tm = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL};
#if _MSC_VER >= 1400
    localtime_s(&local_tm, &t);
#else
    localtime_r(&t, &local_tm);
#endif
    const char prev_fill = os.fill('0');
    if (FLAGS_log_year) {
        os << std::setw(4) << local_tm.tm_year + 1900;
    }
    os << std::setw(2) << local_tm.tm_mon + 1
       << std::setw(2) << local_tm.tm_mday << ' '
       << std::setw(2) << local_tm.tm_hour << ':'
       << std::setw(2) << local_tm.tm_min << ':'
       << std::setw(2) << local_tm.tm_sec;
#if defined(OS_LINUX) || defined(OS_MACOSX)
    os << '.' << std::setw(6) << tv.tv_usec;
#endif
    if (FLAGS_log_pid) {
        os << ' ' << std::setfill(' ') << std::setw(5) << CurrentProcessId();
    }
    os << ' ' << std::setfill(' ') << std::setw(5)
       << butil::PlatformThread::CurrentId() << std::setfill('0');
    if (FLAGS_log_bid && bthread_self) {
        os << ' ' << std::setfill(' ') << std::setw(5) << bthread_self();
    }
    if (FLAGS_log_hostname) {
        butil::StringPiece hostname(butil::my_hostname());
        if (hostname.ends_with(".baidu.com")) { // make it shorter
            hostname.remove_suffix(10);
        }
        os << ' ' << hostname;
    }
    os << ' ' << file << ':' << line;
    if (!func.empty()) {
        os << " " << func;
    }
    os << "] ";

    os.fill(prev_fill);
}

void PrintLogPrefix(std::ostream& os, int severity,
                    const char* file, int line) {
    PrintLogPrefix(os, severity, file, line, "", GetTimestamp());
}

static void PrintLogPrefixAsJSON(std::ostream& os, int severity,
                                 butil::StringPiece file,
                                 butil::StringPiece func,
                                 int line, TimeVal tv) {
    // severity
    os << "\"L\":\"";
    if (severity < 0) {
        os << 'V' << -severity;
    } else if (severity < LOG_NUM_SEVERITIES) {
        os << log_severity_names[severity][0];
    } else {
        os << 'U';
    }
    // time
    os << "\",\"T\":\"";
    time_t t = tv.tv_sec;
    struct tm local_tm = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL};
#if _MSC_VER >= 1400
    localtime_s(&local_tm, &t);
#else
    localtime_r(&t, &local_tm);
#endif
    const char prev_fill = os.fill('0');
    if (FLAGS_log_year) {
        os << std::setw(4) << local_tm.tm_year + 1900;
    }
    os << std::setw(2) << local_tm.tm_mon + 1
       << std::setw(2) << local_tm.tm_mday << ' '
       << std::setw(2) << local_tm.tm_hour << ':'
       << std::setw(2) << local_tm.tm_min << ':'
       << std::setw(2) << local_tm.tm_sec;
#if defined(OS_LINUX) || defined(OS_MACOSX)
    os << '.' << std::setw(6) << tv.tv_usec;
#endif
    os << "\",";
    os.fill(prev_fill);

    if (FLAGS_log_pid) {
        os << "\"pid\":\"" << CurrentProcessId() << "\",";
    }
    os << "\"tid\":\"" << butil::PlatformThread::CurrentId() << "\",";
    if (FLAGS_log_hostname) {
        butil::StringPiece hostname(butil::my_hostname());
        if (hostname.ends_with(".baidu.com")) { // make it shorter
            hostname.remove_suffix(10);
        }
        os << "\"host\":\"" << hostname << "\",";
    }
    os << "\"C\":\"" << file << ':' << line;
    if (!func.empty()) {
        os << " " << func;
    }
    os << "\"";
}

void EscapeJson(std::ostream& os, const butil::StringPiece& s) {
    for (auto it = s.begin(); it != s.end(); it++) {
        auto c = *it;
        switch (c) {
        case '"': os << "\\\""; break;
        case '\\': os << "\\\\"; break;
        case '\b': os << "\\b"; break;
        case '\f': os << "\\f"; break;
        case '\n': os << "\\n"; break;
        case '\r': os << "\\r"; break;
        case '\t': os << "\\t"; break;
        default: os << c;
        }
    }
}

inline void OutputLog(std::ostream& os, const butil::StringPiece& s) {
    if (FLAGS_escape_log) {
        EscapeJson(os, s);
    } else {
        os.write(s.data(), s.length());
    }
}

void PrintLog(std::ostream& os, int severity, const char* file, int line,
              const char* func, const butil::StringPiece& content) {
    if (!FLAGS_log_as_json) {
        PrintLogPrefix(os, severity, file, line, func, GetTimestamp());
        OutputLog(os, content);
    } else {
        os << '{';
        PrintLogPrefixAsJSON(os, severity, file, func, line, GetTimestamp());
        bool pair_quote = false;
        if (content.empty() || content[0] != '"') {
            // not a json, add a 'M' field
            os << ",\"M\":\"";
            pair_quote = true;
        } else {
            os << ',';
        }
        OutputLog(os, content);
        if (pair_quote) {
            os << '"';
        } else if (!content.empty() && content[content.size() -1 ] != '"') {
            // Controller may write `"M":"...` which misses the last quote
            os << '"';
        }
        os << '}';
    }
}

void PrintLog(std::ostream& os,
              int severity, const char* file, int line,
              const butil::StringPiece& content) {
    PrintLog(os, severity, file, line, "", content);
}

// A log message handler that gets notified of every log message we process.
class DoublyBufferedLogSink : public butil::DoublyBufferedData<LogSink*> {
public:
    DoublyBufferedLogSink() {}
    static DoublyBufferedLogSink* GetInstance();
private:
friend struct DefaultSingletonTraits<DoublyBufferedLogSink>;
    DISALLOW_COPY_AND_ASSIGN(DoublyBufferedLogSink);
};

DoublyBufferedLogSink* DoublyBufferedLogSink::GetInstance() {
    return Singleton<DoublyBufferedLogSink,
                     LeakySingletonTraits<DoublyBufferedLogSink> >::get();
}

struct SetLogSinkFn {
    LogSink* new_sink;
    LogSink* old_sink;

    bool operator()(LogSink*& ptr) {
        old_sink = ptr;
        ptr = new_sink;
        return true;
    }
};

LogSink* SetLogSink(LogSink* sink) {
    SetLogSinkFn fn = { sink, NULL };
    CHECK(DoublyBufferedLogSink::GetInstance()->Modify(fn));
    return fn.old_sink;
}

// MSVC doesn't like complex extern templates and DLLs.
#if !defined(COMPILER_MSVC)
// Explicit instantiations for commonly used comparisons.
template std::string* MakeCheckOpString<int, int>(
    const int&, const int&, const char* names);
template std::string* MakeCheckOpString<unsigned long, unsigned long>(
    const unsigned long&, const unsigned long&, const char* names);
template std::string* MakeCheckOpString<unsigned long, unsigned int>(
    const unsigned long&, const unsigned int&, const char* names);
template std::string* MakeCheckOpString<unsigned int, unsigned long>(
    const unsigned int&, const unsigned long&, const char* names);
template std::string* MakeCheckOpString<std::string, std::string>(
    const std::string&, const std::string&, const char* name);
#endif

#if !defined(NDEBUG)
// Displays a message box to the user with the error message in it.
// Used for fatal messages, where we close the app simultaneously.
// This is for developers only; we don't use this in circumstances
// (like release builds) where users could see it, since users don't
// understand these messages anyway.
void DisplayDebugMessageInDialog(const std::string& str) {
    if (str.empty())
        return;

    if (!show_error_dialogs)
        return;

#if defined(OS_WIN)
    // For Windows programs, it's possible that the message loop is
    // messed up on a fatal error, and creating a MessageBox will cause
    // that message loop to be run. Instead, we try to spawn another
    // process that displays its command line. We look for "Debug
    // Message.exe" in the same directory as the application. If it
    // exists, we use it, otherwise, we use a regular message box.
    wchar_t prog_name[MAX_PATH];
    GetModuleFileNameW(NULL, prog_name, MAX_PATH);
    wchar_t* backslash = wcsrchr(prog_name, '\\');
    if (backslash)
        backslash[1] = 0;
    wcscat_s(prog_name, MAX_PATH, L"debug_message.exe");

    std::wstring cmdline = butil::UTF8ToWide(str);
    if (cmdline.empty())
        return;

    STARTUPINFO startup_info;
    memset(&startup_info, 0, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info;
    if (CreateProcessW(prog_name, &cmdline[0], NULL, NULL, false, 0, NULL,
                       NULL, &startup_info, &process_info)) {
        WaitForSingleObject(process_info.hProcess, INFINITE);
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
    } else {
        // debug process broken, let's just do a message box
        MessageBoxW(NULL, &cmdline[0], L"Fatal error",
                    MB_OK | MB_ICONHAND | MB_TOPMOST);
    }
#else
    // We intentionally don't implement a dialog on other platforms.
    // You can just look at stderr.
#endif
}
#endif  // !defined(NDEBUG)


bool StringSink::OnLogMessage(int severity, const char* file, int line,
                              const butil::StringPiece& content) {
    return OnLogMessage(severity, file, line, "", content);
}

bool StringSink::OnLogMessage(int severity, const char* file,
                              int line, const char* func,
                              const butil::StringPiece& content) {
    std::ostringstream os;
    PrintLog(os, severity, file, line, func, content);
    const std::string msg = os.str();
    {
        butil::AutoLock lock_guard(_lock);
        append(msg);
    }
    return true;
}

CharArrayStreamBuf::~CharArrayStreamBuf() {
    free(_data);
}

int CharArrayStreamBuf::overflow(int ch) {
    if (ch == std::streambuf::traits_type::eof()) {
        return ch;
    }
    size_t new_size = std::max(_size * 3 / 2, (size_t)64);
    char* new_data = (char*)malloc(new_size);
    if (BAIDU_UNLIKELY(new_data == NULL)) {
        setp(NULL, NULL);
        return std::streambuf::traits_type::eof();
    }
    memcpy(new_data, _data, _size);
    free(_data);
    _data = new_data;
    const size_t old_size = _size;
    _size = new_size;
    setp(_data, _data + new_size);
    pbump(old_size);
    // if size == 1, this function will call overflow again.
    return sputc(ch);
}

int CharArrayStreamBuf::sync() {
    // data are already there.
    return 0;
}

void CharArrayStreamBuf::reset() {
    setp(_data, _data + _size);
}

LogStream& LogStream::SetPosition(const LogChar* file, int line,
                                  LogSeverity severity) {
    _file = file;
    _line = line;
    _severity = severity;
    return *this;
}

LogStream& LogStream::SetPosition(const LogChar* file, int line,
                                  const LogChar* func,
                                  LogSeverity severity) {
    _file = file;
    _line = line;
    _func = func;
    _severity = severity;
    return *this;
}

#if defined(__GNUC__)
static bthread_key_t stream_bkey;
static pthread_key_t stream_pkey;
static pthread_once_t create_stream_key_once = PTHREAD_ONCE_INIT;
inline bool is_bthread_linked() { return bthread_key_create != NULL; }
static void destroy_tls_streams(void* data) {
    if (data == NULL) {
        return;
    }
    LogStream** a = (LogStream**)data;
    for (int i = 0; i <= LOG_NUM_SEVERITIES; ++i) {
        delete a[i];
    }
    delete[] a;
}
static void create_stream_key_or_die() {
    if (is_bthread_linked()) {
        int rc = bthread_key_create(&stream_bkey, destroy_tls_streams);
        if (rc) {
            fprintf(stderr, "Fail to bthread_key_create");
            exit(1);
        }
    } else {
        int rc = pthread_key_create(&stream_pkey, destroy_tls_streams);
        if (rc) {
            fprintf(stderr, "Fail to pthread_key_create");
            exit(1);
        }
    }
}
static LogStream** get_tls_stream_array() {
    pthread_once(&create_stream_key_once, create_stream_key_or_die);
    if (is_bthread_linked()) {
        return (LogStream**)bthread_getspecific(stream_bkey);
    } else {
        return (LogStream**)pthread_getspecific(stream_pkey);
    }
}

static LogStream** get_or_new_tls_stream_array() {
    LogStream** a = get_tls_stream_array();
    if (a == NULL) {
        a = new LogStream*[LOG_NUM_SEVERITIES + 1];
        memset(a, 0, sizeof(LogStream*) * (LOG_NUM_SEVERITIES + 1));
        if (is_bthread_linked()) {
            bthread_setspecific(stream_bkey, a);
        } else {
            pthread_setspecific(stream_pkey, a);
        }
    }
    return a;
}

inline LogStream* CreateLogStream(const LogChar* file,
                                  int line,
                                  const LogChar* func,
                                  LogSeverity severity) {
    int slot = 0;
    if (severity >= 0) {
        DCHECK_LT(severity, LOG_NUM_SEVERITIES);
        slot = severity + 1;
    } // else vlog
    LogStream** stream_array = get_or_new_tls_stream_array();
    LogStream* stream = stream_array[slot];
    if (stream == NULL) {
        stream = new LogStream;
        stream_array[slot] = stream;
    }
    if (stream->empty()) {
        stream->SetPosition(file, line, func, severity);
    }
    return stream;
}

inline LogStream* CreateLogStream(const LogChar* file,
                                  int line,
                                  LogSeverity severity) {
    return CreateLogStream(file, line, "", severity);
}

inline void DestroyLogStream(LogStream* stream) {
    if (stream != NULL) {
        stream->Flush();
    }
}

#else

inline LogStream* CreateLogStream(const LogChar* file, int line,
                                  LogSeverity severity) {
    return CreateLogStream(file, line, "", severity);
}


inline LogStream* CreateLogStream(const LogChar* file, int line,
                                  const LogChar* func,
                                  LogSeverity severity) {
    LogStream* stream = new LogStream;
    stream->SetPosition(file, line, func, severity);
    return stream;
}

inline void DestroyLogStream(LogStream* stream) {
    delete stream;
}

#endif  // __GNUC__

class DefaultLogSink : public LogSink {
public:
    static DefaultLogSink* GetInstance() {
        return Singleton<DefaultLogSink,
                         LeakySingletonTraits<DefaultLogSink> >::get();
    }

    bool OnLogMessage(int severity, const char* file, int line,
                      const butil::StringPiece& content) override {
        return OnLogMessage(severity, file, line, "", content);
    }

    bool OnLogMessage(int severity, const char* file,
                      int line, const char* func,
                      const butil::StringPiece& content) override {
        std::string log;
        if ((logging_destination & LOG_TO_SYSTEM_DEBUG_LOG) != 0 ||
            severity >= kAlwaysPrintErrorLevel) {
            log = LogInfoToLogStr(severity, file, line, func, content);
            // When we're only outputting to a log file, above a certain log level, we
            // should still output to stderr so that we can better detect and diagnose
            // problems with unit tests, especially on the buildbots.
            fwrite(log.data(), log.size(), 1, stderr);
            fflush(stderr);
        }
        // write to log file
        if ((logging_destination & LOG_TO_FILE) != 0) {
            if ((FLAGS_crash_on_fatal_log && severity == BLOG_FATAL) ||
                !FLAGS_async_log) {
                if (log.empty()) {
                    log = LogInfoToLogStr(severity, file, line, func, content);
                }
                Log2File(log);
            } else {
                LogInfo info;
                if (log.empty()) {
                    info.severity = severity;
                    info.timestamp = GetTimestamp();
                    info.file = file;
                    info.func = func;
                    info.line = line;
                    info.content = content.as_string();
                    info.raw = true;
                } else {
                    info.content = std::move(log);
                    info.raw = false;
                }
                AsyncLogger::GetInstance()->Log(std::move(info));
            }
        }
        return true;
    }
private:
    DefaultLogSink() = default;
    ~DefaultLogSink() override = default;
friend struct DefaultSingletonTraits<DefaultLogSink>;
};

void LogStream::FlushWithoutReset() {
    if (empty()) {
        // Nothing to flush.
        return;
    }

#if !defined(OS_NACL) && !defined(__UCLIBC__)
    if ((FLAGS_print_stack_on_check && _is_check && _severity == BLOG_FATAL) || _backtrace) {
        // Include a stack trace on a fatal.
        butil::debug::StackTrace trace;
        size_t count = 0;
        const void* const* addrs = trace.Addresses(&count);

        *this << std::endl;  // Newline to separate from log message.
        if (count > 3) {
            // Remove top 3 frames which are useless to users.
            // #2 may be ~LogStream
            //   #0 0x00000059ccae butil::debug::StackTrace::StackTrace()
            //   #1 0x0000005947c7 logging::LogStream::FlushWithoutReset()
            //   #2 0x000000594b88 logging::LogMessage::~LogMessage()
            butil::debug::StackTrace trace_stripped(addrs + 3, count - 3);
            trace_stripped.OutputToStream(this);
        } else {
            trace.OutputToStream(this);
        }
    }
#endif
    // End the data with zero because sink is likely to assume this.
    *this << std::ends;
    // Move back one step because we don't want to count the zero.
    pbump(-1); 

    // Give any logsink first dibs on the message.
#ifdef BAIDU_INTERNAL
    // If the logsink fails and it's not comlog, try comlog. stderr on last try.
    bool tried_comlog = false;
#endif
    bool tried_default = false;
    {
        DoublyBufferedLogSink::ScopedPtr ptr;
        if (DoublyBufferedLogSink::GetInstance()->Read(&ptr) == 0 &&
            (*ptr) != NULL) {
            bool result = (*ptr)->OnLogMessage(
                _severity, _file, _line, _func, content());
            if (result) {
                goto FINISH_LOGGING;
            }
#ifdef BAIDU_INTERNAL
            tried_comlog = (*ptr == ComlogSink::GetInstance());
#endif
            tried_default = (*ptr == DefaultLogSink::GetInstance());
        }
    }

#ifdef BAIDU_INTERNAL
    if (!tried_comlog) {
        if (ComlogSink::GetInstance()->OnLogMessage(
                _severity, _file, _line, _func, content())) {
            goto FINISH_LOGGING;
        }
    }
#endif
    if (!tried_default) {
        DefaultLogSink::GetInstance()->OnLogMessage(
            _severity, _file, _line, _func, content());
    }

FINISH_LOGGING:
    if (FLAGS_crash_on_fatal_log && _severity == BLOG_FATAL) {
        // Ensure the first characters of the string are on the stack so they
        // are contained in minidumps for diagnostic purposes.
        butil::StringPiece str = content();
        char str_stack[1024];
        str.copy(str_stack, arraysize(str_stack));
        butil::debug::Alias(str_stack);

        if (log_assert_handler) {
            // Make a copy of the string for the handler out of paranoia.
            log_assert_handler(str.as_string());
        } else {
            // Don't use the string with the newline, get a fresh version to send to
            // the debug message process. We also don't display assertions to the
            // user in release mode. The enduser can't do anything with this
            // information, and displaying message boxes when the application is
            // hosed can cause additional problems.
#ifndef NDEBUG
            DisplayDebugMessageInDialog(str.as_string());
#endif
            // Crash the process to generate a dump.
            butil::debug::BreakDebugger();
        }
    }
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity)
    : LogMessage(file, line, "", severity) {}

LogMessage::LogMessage(const char* file, int line,
                       const char* func, LogSeverity severity) {
    _stream = CreateLogStream(file, line, func, severity);
}

LogMessage::LogMessage(const char* file, int line, std::string* result)
    : LogMessage(file, line, "", result) {}

LogMessage::LogMessage(const char* file, int line,
                       const char* func, std::string* result) {
    _stream = CreateLogStream(file, line, func, BLOG_FATAL);
    *_stream << "Check failed: " << *result;
    delete result;
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity,
                       std::string* result)
   : LogMessage(file, line, "", severity, result) {}

LogMessage::LogMessage(const char* file, int line, const char* func,
                       LogSeverity severity, std::string* result) {
    _stream = CreateLogStream(file, line, func, severity);
    *_stream << "Check failed: " << *result;
    delete result;
}

LogMessage::~LogMessage() {
    DestroyLogStream(_stream);
}

#if defined(OS_WIN)
// This has already been defined in the header, but defining it again as DWORD
// ensures that the type used in the header is equivalent to DWORD. If not,
// the redefinition is a compile error.
typedef DWORD SystemErrorCode;
#endif

SystemErrorCode GetLastSystemErrorCode() {
#if defined(OS_WIN)
    return ::GetLastError();
#elif defined(OS_POSIX)
    return errno;
#else
#error Not implemented
#endif
}

void SetLastSystemErrorCode(SystemErrorCode err) {
#if defined(OS_WIN)
    ::SetLastError(err);
#elif defined(OS_POSIX)
    errno = err;
#else
#error Not implemented
#endif
}

#if defined(OS_WIN)
BUTIL_EXPORT std::string SystemErrorCodeToString(SystemErrorCode error_code) {
    const int error_message_buffer_size = 256;
    char msgbuf[error_message_buffer_size];
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageA(flags, NULL, error_code, 0, msgbuf,
                               arraysize(msgbuf), NULL);
    if (len) {
        // Messages returned by system end with line breaks.
        return butil::CollapseWhitespaceASCII(msgbuf, true) +
            butil::StringPrintf(" (0x%X)", error_code);
    }
    return butil::StringPrintf("Error (0x%X) while retrieving error. (0x%X)",
                              GetLastError(), error_code);
}
#elif defined(OS_POSIX)
BUTIL_EXPORT std::string SystemErrorCodeToString(SystemErrorCode error_code) {
    return berror(error_code);
}
#else
#error Not implemented
#endif


#if defined(OS_WIN)
Win32ErrorLogMessage::Win32ErrorLogMessage(const char* file,
                                           int line,
                                           LogSeverity severity,
                                           SystemErrorCode err)
   : Win32ErrorLogMessage(file, line, "", severity, err) {
}

Win32ErrorLogMessage::Win32ErrorLogMessage(const char* file,
                                           int line,
                                           const char* func,
                                           LogSeverity severity,
                                           SystemErrorCode err)
    : err_(err)
    , log_message_(file, line, func, severity) {
}

Win32ErrorLogMessage::~Win32ErrorLogMessage() {
    stream() << ": " << SystemErrorCodeToString(err_);
    // We're about to crash (CHECK). Put |err_| on the stack (by placing it in a
    // field) and use Alias in hopes that it makes it into crash dumps.
    DWORD last_error = err_;
    butil::debug::Alias(&last_error);
}
#elif defined(OS_POSIX)
ErrnoLogMessage::ErrnoLogMessage(const char* file,
                                 int line,
                                 LogSeverity severity,
                                 SystemErrorCode err)
    : ErrnoLogMessage(file, line, "", severity, err) {}

ErrnoLogMessage::ErrnoLogMessage(const char* file,
                                 int line,
                                 const char* func,
                                 LogSeverity severity,
                                 SystemErrorCode err)
    : err_(err)
    , log_message_(file, line, func, severity) {}

ErrnoLogMessage::~ErrnoLogMessage() {
    stream() << ": " << SystemErrorCodeToString(err_);
}
#endif  // OS_WIN

void CloseLogFile() {
    LoggingLock logging_lock;
    CloseLogFileUnlocked();
}

void RawLog(int level, const char* message) {
    if (level >= FLAGS_minloglevel) {
        size_t bytes_written = 0;
        const size_t message_len = strlen(message);
        int rv;
        while (bytes_written < message_len) {
            rv = HANDLE_EINTR(
                write(STDERR_FILENO, message + bytes_written,
                      message_len - bytes_written));
            if (rv < 0) {
                // Give up, nothing we can do now.
                break;
            }
            bytes_written += rv;
        }

        if (message_len > 0 && message[message_len - 1] != '\n') {
            do {
                rv = HANDLE_EINTR(write(STDERR_FILENO, "\n", 1));
                if (rv < 0) {
                    // Give up, nothing we can do now.
                    break;
                }
            } while (rv != 1);
        }
    }

    if (FLAGS_crash_on_fatal_log && level == BLOG_FATAL)
        butil::debug::BreakDebugger();
}

// This was defined at the beginning of this file.
#undef write

#if defined(OS_WIN)
std::wstring GetLogFileFullPath() {
    if (log_file_name)
        return *log_file_name;
    return std::wstring();
}
#endif


// ----------- VLOG stuff -----------------
struct VLogSite;
struct VModuleList;

extern const int VLOG_UNINITIALIZED = std::numeric_limits<int>::max();

static pthread_mutex_t vlog_site_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static VLogSite* vlog_site_list = NULL;
static VModuleList* vmodule_list = NULL;

static pthread_mutex_t reset_vmodule_and_v_mutex = PTHREAD_MUTEX_INITIALIZER;

static const int64_t DELAY_DELETION_SEC = 10;
static std::deque<std::pair<VModuleList*, int64_t> >*
deleting_vmodule_list = NULL;

struct VLogSite {
    VLogSite(const char* filename, int required_v, int line_no)
        : _next(0), _v(0), _required_v(required_v), _line_no(line_no) {
        // Remove dirname/extname.
        butil::StringPiece s(filename);
        size_t pos = s.find_last_of("./");
        if (pos != butil::StringPiece::npos) {
            if (s[pos] == '.') {
                s.remove_suffix(s.size() - pos);
                _full_module.assign(s.data(), s.size());
                size_t pos2 = s.find_last_of('/');
                if (pos2 != butil::StringPiece::npos) {
                    s.remove_prefix(pos2 + 1);
                }
            } else {
                _full_module.assign(s.data(), s.size());
                s.remove_prefix(pos + 1);
            }
        } // else keep _full_module empty when it equals _module
        _module.assign(s.data(), s.size());
        std::transform(_module.begin(), _module.end(),
                       _module.begin(), ::tolower);
        if (!_full_module.empty()) {
            std::transform(_full_module.begin(), _full_module.end(),
                           _full_module.begin(), ::tolower);
        }
    }

    // The consume/release fence makes the iteration outside lock see
    // newly added VLogSite correctly.
    VLogSite* next() { return (VLogSite*)butil::subtle::Acquire_Load(&_next); }
    const VLogSite* next() const
    { return (VLogSite*)butil::subtle::Acquire_Load(&_next); }
    void set_next(VLogSite* next)
    { butil::subtle::Release_Store(&_next, (butil::subtle::AtomicWord)next); }

    int v() const { return _v; }
    int& v() { return _v; }

    int required_v() const { return  _required_v; }
    int line_no() const { return _line_no; }

    const std::string& module() const { return _module; }
    const std::string& full_module() const { return _full_module; }
    
private:
    // Next site in the list. NULL means no next.
    butil::subtle::AtomicWord _next;

    // --vmodule > --v
    int _v;
    
    // vlog is on iff _v >= _required_v
    int _required_v;

    // line nubmer of the vlog.
    int _line_no;
    
    // Lowered, dirname & extname removed.
    std::string _module;
    // Lowered, extname removed. Empty when it equals to _module.
    std::string _full_module;
};

// Written by Jack Handy
// <A href="mailto:jakkhandy@hotmail.com">jakkhandy@hotmail.com</A>
bool wildcmp(const char* wild, const char* str) {
    const char* cp = NULL;
    const char* mp = NULL;

    while (*str && *wild != '*') {
        if (*wild != *str && *wild != '?') {
            return false;
        }
        ++wild;
        ++str;
    }

    while (*str) {
        if (*wild == '*') {
            if (!*++wild) {
                return true;
            }
            mp = wild;
            cp = str+1;
        } else if (*wild == *str || *wild == '?') {
            ++wild;
            ++str;
        } else {
            wild = mp;
            str = cp++;
        }
    }

    while (*wild == '*') {
        ++wild;
    }
    return !*wild;
}

struct VModuleList {
    VModuleList() {}

    int init(const char* vmodules) {
        _exact_names.clear();
        _wild_names.clear();
                           
        for (butil::StringSplitter sp(vmodules, ','); sp; ++sp) {
            int verbose_level = std::numeric_limits<int>::max();
            size_t off = 0;
            for (; off < sp.length() && sp.field()[off] != '='; ++off) {}
            if (off + 1 < sp.length()) {
                verbose_level = strtol(sp.field() + off + 1, NULL, 10);
                
            }
            const char* name_begin = sp.field();
            const char* name_end = sp.field() + off - 1;
            for (; isspace(*name_begin) && name_begin < sp.field() + off;
                 ++name_begin) {}
            for (; isspace(*name_end) && name_end >= sp.field(); --name_end) {}
            
            if (name_begin > name_end) {  // only has spaces
                continue;
            }
            std::string name(name_begin, name_end - name_begin + 1);
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name.find_first_of("*?") == std::string::npos) {
                _exact_names[name] = verbose_level;
            } else {
                _wild_names.emplace_back(name, verbose_level);
            }
        }
        // Reverse _wild_names so that latter wild cards override former ones.
        if (!_wild_names.empty()) {
            std::reverse(_wild_names.begin(), _wild_names.end());
        }
        return 0;
    }

    bool find_verbose_level(const std::string& module,
                            const std::string& full_module, int* v) const {
        if (!_exact_names.empty()) {
            std::map<std::string, int>::const_iterator
                it = _exact_names.find(module);
            if (it != _exact_names.end()) {
                *v = it->second;
                return true;
            }
            if (!full_module.empty()) {
                it = _exact_names.find(full_module);
                if (it != _exact_names.end()) {
                    *v = it->second;
                    return true;
                }
            }
        }

        for (size_t i = 0; i < _wild_names.size(); ++i) {
            if (wildcmp(_wild_names[i].first.c_str(), module.c_str())) {
                *v = _wild_names[i].second;
                return true;
            }
            if (!full_module.empty() &&
                wildcmp(_wild_names[i].first.c_str(), full_module.c_str())) {
                *v = _wild_names[i].second;
                return true;
            }
        }
        return false;
    }

    void print(std::ostream& os) const {
        os << "exact:";
        for (std::map<std::string, int>::const_iterator
                 it = _exact_names.begin(); it != _exact_names.end(); ++it) {
            os << ' ' << it->first << '=' << it->second;
        }
        os << ", wild:";
        for (size_t i = 0; i < _wild_names.size(); ++i) {
            os << ' ' << _wild_names[i].first << '=' << _wild_names[i].second;
        }
    }

private:
    std::map<std::string, int> _exact_names;
    std::vector<std::pair<std::string, int> > _wild_names;
};

// [ The idea ] 
// Each callsite creates a VLogSite and inserts the site into singly-linked
// vlog_site_list. To keep the critical area small, we use optimistic
// locking : Assign local site w/o locking, then insert the site into
// global list w/ locking, if local_module_list != global_vmodule_list or
// local_default_v != FLAGS_v, repeat the assigment.
// An important property of vlog_site_list is that: It does not remove sites.
// When we need to iterate the list, we don't have to hold the lock. What we
// do is to get the head of the list inside lock and iterate the list w/o
// lock. If new sites is inserted during the iteration, it should see and
// use the updated vmodule_list and FLAGS_v, nothing will be missed.

static int vlog_site_list_add(VLogSite* site,
                              VModuleList** expected_module_list,
                              int* expected_default_v) {
    BAIDU_SCOPED_LOCK(vlog_site_list_mutex);
    if (vmodule_list != *expected_module_list) {
        *expected_module_list = vmodule_list;
        return -1;
    }
    if (*expected_default_v != FLAGS_v) {
        *expected_default_v = FLAGS_v;
        return -1;
    }
    site->set_next(vlog_site_list);
    vlog_site_list = site;
    return 0;
}

bool add_vlog_site(const int** v, const char* filename, int line_no,
                   int required_v) {
    VLogSite* site = new (std::nothrow) VLogSite(filename, required_v, line_no);
    if (site == NULL) {
        return false;
    }
    VModuleList* module_list = vmodule_list;
    int default_v = FLAGS_v;
    do {
        site->v() = default_v;
        if (module_list) {
            module_list->find_verbose_level(
                site->module(), site->full_module(), &site->v());
        }
    } while (vlog_site_list_add(site, &module_list, &default_v) != 0);
    *v = &site->v();
    return site->v() >= required_v;
}

void print_vlog_sites(VLogSitePrinter* printer) {
    VLogSite* head = NULL;
    {
        BAIDU_SCOPED_LOCK(vlog_site_list_mutex);
        head = vlog_site_list;
    }
    VLogSitePrinter::Site site;
    for (const VLogSite* p = head; p; p = p->next()) {
        site.current_verbose_level = p->v();
        site.required_verbose_level = p->required_v();
        site.line_no = p->line_no();
        site.full_module = p->full_module();
        printer->print(site);
    }
}

// [Thread-safe] Reset FLAGS_vmodule.
static int on_reset_vmodule(const char* vmodule) {
    // resetting must be serialized.
    BAIDU_SCOPED_LOCK(reset_vmodule_and_v_mutex);
    
    VModuleList* module_list = new (std::nothrow) VModuleList;
    if (NULL == module_list) {
        LOG(FATAL) << "Fail to new VModuleList";
        return -1;
    }
    if (module_list->init(vmodule) != 0) {
        delete module_list;
        LOG(FATAL) << "Fail to init VModuleList";
        return -1;
    }
    
    VModuleList* old_module_list = NULL;
    VLogSite* old_vlog_site_list = NULL;
    {
        {
            BAIDU_SCOPED_LOCK(vlog_site_list_mutex);
            old_module_list = vmodule_list;
            vmodule_list = module_list;
            old_vlog_site_list = vlog_site_list;
        }
        for (VLogSite* p = old_vlog_site_list; p; p = p->next()) {
            p->v() = FLAGS_v;
            module_list->find_verbose_level(
                p->module(), p->full_module(), &p->v());
        }
    }
    
    if (old_module_list) {
        //delay the deletion.
        if (NULL == deleting_vmodule_list) {
            deleting_vmodule_list =
                new std::deque<std::pair<VModuleList*, int64_t> >;
        }
        deleting_vmodule_list->push_back(
            std::make_pair(old_module_list,
                           butil::gettimeofday_us() + DELAY_DELETION_SEC * 1000000L));
        while (!deleting_vmodule_list->empty() &&
               deleting_vmodule_list->front().second <= butil::gettimeofday_us()) {
            delete deleting_vmodule_list->front().first;
            deleting_vmodule_list->pop_front();
        }
    }
    return 0;
}

static bool validate_vmodule(const char*, const std::string& vmodule) {
    return on_reset_vmodule(vmodule.c_str()) == 0;
}

const bool ALLOW_UNUSED validate_vmodule_dummy = GFLAGS_NAMESPACE::RegisterFlagValidator(
    &FLAGS_vmodule, &validate_vmodule);

// [Thread-safe] Reset FLAGS_v.
static void on_reset_verbose(int default_v) {
    VModuleList* cur_module_list = NULL;
    VLogSite* cur_vlog_site_list = NULL;
    {
        // resetting must be serialized.
        BAIDU_SCOPED_LOCK(reset_vmodule_and_v_mutex);
        {
            BAIDU_SCOPED_LOCK(vlog_site_list_mutex);
            cur_module_list = vmodule_list;
            cur_vlog_site_list = vlog_site_list;
        }
        for (VLogSite* p = cur_vlog_site_list; p; p = p->next()) {
            p->v() = default_v;
            if (cur_module_list) {
                cur_module_list->find_verbose_level(
                    p->module(), p->full_module(), &p->v());
            }
        }
    }
}

static bool validate_v(const char*, int32_t v) {
    on_reset_verbose(v);
    return true;
}
BUTIL_VALIDATE_GFLAG(v, validate_v);

}  // namespace logging

std::ostream& operator<<(std::ostream& out, const wchar_t* wstr) {
    return out << butil::WideToUTF8(std::wstring(wstr));
}

#endif  // BRPC_WITH_GLOG
