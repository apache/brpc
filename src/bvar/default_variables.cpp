// Copyright (c) 2015 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Author: Ge,Jun (gejun@baidu.com)
// Date: Thu Jul 30 17:44:54 CST 2015

#include <unistd.h>                        // getpagesize
#include <sys/types.h>
#include <sys/resource.h>                  // getrusage
#include <dirent.h>                        // dirent
#include <iomanip>                         // setw
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/resource.h>
#else
#endif

#include "butil/time.h"
#include "butil/memory/singleton_on_pthread_once.h"
#include "butil/scoped_lock.h"
#include "butil/files/scoped_file.h"
#include "butil/files/dir_reader_posix.h"
#include "butil/file_util.h"
#include "butil/process_util.h"            // ReadCommandLine
#include "butil/popen.h"                   // read_command_output
#include "bvar/passive_status.h"

namespace bvar {

template <class T, class M> M get_member_type(M T::*);

#define BVAR_MEMBER_TYPE(member) BAIDU_TYPEOF(bvar::get_member_type(member))

int do_link_default_variables = 0;
const int64_t CACHED_INTERVAL_US = 100000L; // 100ms

// ======================================
struct ProcStat {
    int pid;
    //std::string comm;
    char state;
    int ppid;
    int pgrp;
    int session;
    int tty_nr;
    int tpgid;
    unsigned flags;
    unsigned long minflt;
    unsigned long cminflt;
    unsigned long majflt;
    unsigned long cmajflt;
    unsigned long utime;
    unsigned long stime;
    unsigned long cutime;
    unsigned long cstime;
    long priority;
    long nice;
    long num_threads;
};

static bool read_proc_status(ProcStat &stat) {
    stat = ProcStat();
    errno = 0;
#if defined(OS_LINUX)
    // Read status from /proc/self/stat. Information from `man proc' is out of date,
    // see http://man7.org/linux/man-pages/man5/proc.5.html
    butil::ScopedFILE fp("/proc/self/stat", "r");
    if (NULL == fp) {
        PLOG_ONCE(WARNING) << "Fail to open /proc/self/stat";
        return false;
    }
    if (fscanf(fp, "%d %*s %c "
               "%d %d %d %d %d "
               "%u %lu %lu %lu "
               "%lu %lu %lu %lu %lu "
               "%ld %ld %ld",
               &stat.pid, &stat.state,
               &stat.ppid, &stat.pgrp, &stat.session, &stat.tty_nr, &stat.tpgid,
               &stat.flags, &stat.minflt, &stat.cminflt, &stat.majflt,
               &stat.cmajflt, &stat.utime, &stat.stime, &stat.cutime, &stat.cstime,
               &stat.priority, &stat.nice, &stat.num_threads) != 19) {
        PLOG(WARNING) << "Fail to fscanf";
        return false;
    }
    return true;
#elif defined(OS_MACOSX)
    // TODO(zhujiashun): get remaining state in MacOS.
    memset(&stat, 0, sizeof(stat));
    static pid_t pid = getpid();
    std::ostringstream oss;
    char cmdbuf[128];
    snprintf(cmdbuf, sizeof(cmdbuf),
            "ps -p %ld -o pid,ppid,pgid,sess"
            ",tpgid,flags,pri,nice | tail -n1", (long)pid);
    if (butil::read_command_output(oss, cmdbuf) != 0) {
        LOG(ERROR) << "Fail to read stat";
        return -1;
    }
    const std::string& result = oss.str();
    if (sscanf(result.c_str(), "%d %d %d %d"
                              "%d %u %ld %ld",
               &stat.pid, &stat.ppid, &stat.pgrp, &stat.session,
               &stat.tpgid, &stat.flags, &stat.priority, &stat.nice) != 8) {
        PLOG(WARNING) << "Fail to sscanf";
        return false;
    }
    return true;
#else
    return false;
#endif
}

// Reduce pressures to functions to get system metrics.
template <typename T>
class CachedReader {
public:
    CachedReader() : _mtime_us(0) {
        CHECK_EQ(0, pthread_mutex_init(&_mutex, NULL));
    }
    ~CachedReader() {
        pthread_mutex_destroy(&_mutex);
    }

    // NOTE: may return a volatile value that may be overwritten at any time.
    // This is acceptable right now. Both 32-bit and 64-bit numbers are atomic
    // to fetch in 64-bit machines(most of baidu machines) and the code inside
    // this .cpp utilizing this class generally return a struct with 32-bit
    // and 64-bit numbers.
    template <typename ReadFn>
    static const T& get_value(const ReadFn& fn) {
        CachedReader* p = butil::get_leaky_singleton<CachedReader>();
        const int64_t now = butil::gettimeofday_us();
        if (now > p->_mtime_us + CACHED_INTERVAL_US) {
            pthread_mutex_lock(&p->_mutex);
            if (now > p->_mtime_us + CACHED_INTERVAL_US) {
                p->_mtime_us = now;
                pthread_mutex_unlock(&p->_mutex);
                // don't run fn inside lock otherwise a slow fn may
                // block all concurrent bvar dumppers. (e.g. /vars)
                T result;
                if (fn(&result)) {
                    pthread_mutex_lock(&p->_mutex);
                    p->_cached = result;
                } else {
                    pthread_mutex_lock(&p->_mutex);
                }
            }
            pthread_mutex_unlock(&p->_mutex);
        }
        return p->_cached;
    }

private:
    int64_t _mtime_us;
    pthread_mutex_t _mutex;
    T _cached;
};

class ProcStatReader {
public:
    bool operator()(ProcStat* stat) const {
        return read_proc_status(*stat);
    }
    template <typename T, size_t offset>
    static T get_field(void*) {
        return *(T*)((char*)&CachedReader<ProcStat>::get_value(
                         ProcStatReader()) + offset);
    }
};

#define BVAR_DEFINE_PROC_STAT_FIELD(field)                              \
    PassiveStatus<BVAR_MEMBER_TYPE(&ProcStat::field)> g_##field(        \
        ProcStatReader::get_field<BVAR_MEMBER_TYPE(&ProcStat::field),   \
        offsetof(ProcStat, field)>, NULL);

#define BVAR_DEFINE_PROC_STAT_FIELD2(field, name)                       \
    PassiveStatus<BVAR_MEMBER_TYPE(&ProcStat::field)> g_##field(        \
        name,                                                           \
        ProcStatReader::get_field<BVAR_MEMBER_TYPE(&ProcStat::field),   \
        offsetof(ProcStat, field)>, NULL);

// ==================================================

struct ProcMemory {
    long size;      // total program size
    long resident;  // resident set size
    long share;     // shared pages
    long trs;       // text (code)
    long lrs;       // library
    long drs;       // data/stack
    long dt;        // dirty pages
};

static bool read_proc_memory(ProcMemory &m) {
    m = ProcMemory();
    errno = 0;
#if defined(OS_LINUX)
    butil::ScopedFILE fp("/proc/self/statm", "r");
    if (NULL == fp) {
        PLOG_ONCE(WARNING) << "Fail to open /proc/self/statm";
        return false;
    }
    if (fscanf(fp, "%ld %ld %ld %ld %ld %ld %ld",
               &m.size, &m.resident, &m.share,
               &m.trs, &m.lrs, &m.drs, &m.dt) != 7) {
        PLOG(WARNING) << "Fail to fscanf /proc/self/statm";
        return false;
    }
    return true;
#elif defined(OS_MACOSX)
    // TODO(zhujiashun): get remaining memory info in MacOS.
    memset(&m, 0, sizeof(m));
    static pid_t pid = getpid();
    static int64_t pagesize = getpagesize();
    std::ostringstream oss;
    char cmdbuf[128];
    snprintf(cmdbuf, sizeof(cmdbuf), "ps -p %ld -o rss=,vsz=", (long)pid);
    if (butil::read_command_output(oss, cmdbuf) != 0) {
        LOG(ERROR) << "Fail to read memory state";
        return -1;
    }
    const std::string& result = oss.str();
    if (sscanf(result.c_str(), "%ld %ld", &m.resident, &m.size) != 2) {
        PLOG(WARNING) << "Fail to sscanf";
        return false;
    }
    // resident and size in Kbytes
    m.resident = m.resident * 1024 / pagesize;
    m.size = m.size * 1024 / pagesize;
    return true;
#else
    return false;
#endif
}

class ProcMemoryReader {
public:
    bool operator()(ProcMemory* stat) const {
        return read_proc_memory(*stat);
    };
    template <typename T, size_t offset>
    static T get_field(void*) {
        static int64_t pagesize = getpagesize();
        return *(T*)((char*)&CachedReader<ProcMemory>::get_value(
                         ProcMemoryReader()) + offset) * pagesize;
    }
};

#define BVAR_DEFINE_PROC_MEMORY_FIELD(field, name)                      \
    PassiveStatus<BVAR_MEMBER_TYPE(&ProcMemory::field)> g_##field(      \
        name,                                                           \
        ProcMemoryReader::get_field<BVAR_MEMBER_TYPE(&ProcMemory::field), \
        offsetof(ProcMemory, field)>, NULL);

// ==================================================

struct LoadAverage {
    double loadavg_1m;
    double loadavg_5m;
    double loadavg_15m;
};

static bool read_load_average(LoadAverage &m) {
#if defined(OS_LINUX)
    butil::ScopedFILE fp("/proc/loadavg", "r");
    if (NULL == fp) {
        PLOG_ONCE(WARNING) << "Fail to open /proc/loadavg";
        return false;
    }
    m = LoadAverage();
    errno = 0;
    if (fscanf(fp, "%lf %lf %lf",
               &m.loadavg_1m, &m.loadavg_5m, &m.loadavg_15m) != 3) {
        PLOG(WARNING) << "Fail to fscanf";
        return false;
    }
    return true;
#elif defined(OS_MACOSX)
    std::ostringstream oss;
    if (butil::read_command_output(oss, "sysctl -n vm.loadavg") != 0) {
        LOG(ERROR) << "Fail to read loadavg";
        return -1;
    }
    const std::string& result = oss.str();
    if (sscanf(result.c_str(), "{ %lf %lf %lf }",
               &m.loadavg_1m, &m.loadavg_5m, &m.loadavg_15m) != 3) {
        PLOG(WARNING) << "Fail to sscanf";
        return false;
    }
    return true;

#else
    return false;
#endif
}

class LoadAverageReader {
public:
    bool operator()(LoadAverage* stat) const {
        return read_load_average(*stat);
    };
    template <typename T, size_t offset>
    static T get_field(void*) {
        return *(T*)((char*)&CachedReader<LoadAverage>::get_value(
                         LoadAverageReader()) + offset);
    }
};

#define BVAR_DEFINE_LOAD_AVERAGE_FIELD(field, name)                     \
    PassiveStatus<BVAR_MEMBER_TYPE(&LoadAverage::field)> g_##field(     \
        name,                                                           \
        LoadAverageReader::get_field<BVAR_MEMBER_TYPE(&LoadAverage::field), \
        offsetof(LoadAverage, field)>, NULL);

// ==================================================

static int get_fd_count(int limit) {
#if defined(OS_LINUX)
    butil::DirReaderPosix dr("/proc/self/fd");
    int count = 0;
    if (!dr.IsValid()) {
        PLOG(WARNING) << "Fail to open /proc/self/fd";
        return -1;
    }
    // Have to limit the scaning which consumes a lot of CPU when #fd
    // are huge (100k+)
    for (; dr.Next() && count <= limit + 3; ++count) {}
    return count - 3 /* skipped ., .. and the fd in dr*/;
#elif defined(OS_MACOSX)
    // TODO(zhujiashun): following code will cause core dump with some 
    // probability under mac when program exits. Fix it.
    /* 
    static pid_t pid = getpid();
    std::ostringstream oss;
    char cmdbuf[128];
    snprintf(cmdbuf, sizeof(cmdbuf),
            "lsof -p %ld | grep -v \"txt\" | wc -l", (long)pid);
    if (butil::read_command_output(oss, cmdbuf) != 0) {
        LOG(ERROR) << "Fail to read open files";
        return -1;
    }
    const std::string& result = oss.str();
    int count = 0;
    if (sscanf(result.c_str(), "%d", &count) != 1) {
        PLOG(WARNING) << "Fail to sscanf";
        return -1;
    }
    // skipped . and first column line
    count = count - 2;
    return std::min(count, limit);
    */
    return 0;
#else
    return 0;
#endif
}

extern PassiveStatus<int> g_fd_num;

const int MAX_FD_SCAN_COUNT = 10003;
static butil::static_atomic<bool> s_ever_reached_fd_scan_limit = BUTIL_STATIC_ATOMIC_INIT(false);
class FdReader {
public:
    bool operator()(int* stat) const {
        if (s_ever_reached_fd_scan_limit.load(butil::memory_order_relaxed)) {
            // Never update the count again.
            return false;
        }
        const int count = get_fd_count(MAX_FD_SCAN_COUNT);
        if (count < 0) {
            return false;
        }
        if (count == MAX_FD_SCAN_COUNT - 2 
                && s_ever_reached_fd_scan_limit.exchange(
                        true, butil::memory_order_relaxed) == false) {
            // Rename the bvar to notify user.
            g_fd_num.hide();
            g_fd_num.expose("process_fd_num_too_many");
        }
        *stat = count;
        return true;
    }
};

static int print_fd_count(void*) {
    return CachedReader<int>::get_value(FdReader());
}

// ==================================================
struct ProcIO {
    // number of bytes the process read, using any read-like system call (from
    // files, pipes, tty...).
    size_t rchar;

    // number of bytes the process wrote using any write-like system call.
    size_t wchar;

    // number of read-like system call invocations that the process performed.
    size_t syscr;

    // number of write-like system call invocations that the process performed.
    size_t syscw;

    // number of bytes the process directly read from disk.
    size_t read_bytes;

    // number of bytes the process originally dirtied in the page-cache
    // (assuming they will go to disk later).
    size_t write_bytes;

    // number of bytes the process "un-dirtied" - e.g. using an "ftruncate"
    // call that truncated pages from the page-cache.
    size_t cancelled_write_bytes;
};

static bool read_proc_io(ProcIO* s) {
#if defined(OS_LINUX)
    butil::ScopedFILE fp("/proc/self/io", "r");
    if (NULL == fp) {
        PLOG_ONCE(WARNING) << "Fail to open /proc/self/io";
        return false;
    }
    errno = 0;
    if (fscanf(fp, "%*s %lu %*s %lu %*s %lu %*s %lu %*s %lu %*s %lu %*s %lu",
               &s->rchar, &s->wchar, &s->syscr, &s->syscw,
               &s->read_bytes, &s->write_bytes, &s->cancelled_write_bytes)
        != 7) {
        PLOG(WARNING) << "Fail to fscanf";
        return false;
    }
    return true;
#elif defined(OS_MACOSX)
    // TODO(zhujiashun): get rchar, wchar, syscr, syscw, cancelled_write_bytes
    // in MacOS.
    memset(s, 0, sizeof(ProcIO));
    static pid_t pid = getpid();
    rusage_info_current rusage;
    if (proc_pid_rusage(pid, RUSAGE_INFO_CURRENT, (void**)&rusage) != 0) {
        PLOG(WARNING) << "Fail to proc_pid_rusage";
        return false;
    }
    s->read_bytes = rusage.ri_diskio_bytesread;
    s->write_bytes = rusage.ri_diskio_byteswritten;
    return true;
#else 
    return false;
#endif
}

class ProcIOReader {
public:
    bool operator()(ProcIO* stat) const {
        return read_proc_io(stat);
    }
    template <typename T, size_t offset>
    static T get_field(void*) {
        return *(T*)((char*)&CachedReader<ProcIO>::get_value(
                         ProcIOReader()) + offset);
    }
};

#define BVAR_DEFINE_PROC_IO_FIELD(field)                                \
    PassiveStatus<BVAR_MEMBER_TYPE(&ProcIO::field)> g_##field(          \
        ProcIOReader::get_field<BVAR_MEMBER_TYPE(&ProcIO::field),       \
        offsetof(ProcIO, field)>, NULL);

// ==================================================
// Refs:
//   https://www.kernel.org/doc/Documentation/ABI/testing/procfs-diskstats
//   https://www.kernel.org/doc/Documentation/iostats.txt
// 
// The /proc/diskstats file displays the I/O statistics of block devices.
// Each line contains the following 14 fields:
struct DiskStat {
    long long major_number;
    long long minor_mumber;
    char device_name[64];

    // The total number of reads completed successfully.
    long long reads_completed; // wMB/s wKB/s
    
    // Reads and writes which are adjacent to each other may be merged for
    // efficiency.  Thus two 4K reads may become one 8K read before it is
    // ultimately handed to the disk, and so it will be counted (and queued)
    // as only one I/O.  This field lets you know how often this was done.
    long long reads_merged;     // rrqm/s
    
    // The total number of sectors read successfully.
    long long sectors_read;     // rsec/s

    // The total number of milliseconds spent by all reads (as
    // measured from __make_request() to end_that_request_last()).
    long long time_spent_reading_ms;

    // The total number of writes completed successfully.
    long long writes_completed; // rKB/s rMB/s

    // See description of reads_merged
    long long writes_merged;    // wrqm/s

    // The total number of sectors written successfully.
    long long sectors_written;  // wsec/s

    // The total number of milliseconds spent by all writes (as
    // measured from __make_request() to end_that_request_last()).
    long long time_spent_writing_ms;

    // The only field that should go to zero. Incremented as requests are
    // given to appropriate struct request_queue and decremented as they finish.
    long long io_in_progress;

    // This field increases so long as `io_in_progress' is nonzero.
    long long time_spent_io_ms;

    // This field is incremented at each I/O start, I/O completion, I/O
    // merge, or read of these stats by the number of I/Os in progress
    // `io_in_progress' times the number of milliseconds spent doing
    // I/O since the last update of this field.  This can provide an easy
    // measure of both I/O completion time and the backlog that may be
    // accumulating.
    long long weighted_time_spent_io_ms;
};

static bool read_disk_stat(DiskStat* s) {
#if defined(OS_LINUX)
    butil::ScopedFILE fp("/proc/diskstats", "r");
    if (NULL == fp) {
        PLOG_ONCE(WARNING) << "Fail to open /proc/diskstats";
        return false;
    }
    errno = 0;
    if (fscanf(fp, "%lld %lld %s %lld %lld %lld %lld %lld %lld %lld "
               "%lld %lld %lld %lld",
               &s->major_number,
               &s->minor_mumber,
               s->device_name,
               &s->reads_completed,
               &s->reads_merged,
               &s->sectors_read,
               &s->time_spent_reading_ms,
               &s->writes_completed,
               &s->writes_merged,
               &s->sectors_written,
               &s->time_spent_writing_ms,
               &s->io_in_progress,
               &s->time_spent_io_ms,
               &s->weighted_time_spent_io_ms) != 14) {
        PLOG(WARNING) << "Fail to fscanf";
        return false;
    }
    return true;
#elif defined(OS_MACOSX)
    // TODO(zhujiashun)
    return false;
#else
    return false;
#endif
}

class DiskStatReader {
public:
    bool operator()(DiskStat* stat) const {
        return read_disk_stat(stat);
    }
    template <typename T, size_t offset>
    static T get_field(void*) {
        return *(T*)((char*)&CachedReader<DiskStat>::get_value(
                         DiskStatReader()) + offset);
    }
};

#define BVAR_DEFINE_DISK_STAT_FIELD(field)                              \
    PassiveStatus<BVAR_MEMBER_TYPE(&DiskStat::field)> g_##field(        \
        DiskStatReader::get_field<BVAR_MEMBER_TYPE(&DiskStat::field),   \
        offsetof(DiskStat, field)>, NULL);

// =====================================

struct ReadSelfCmdline {
    std::string content;
    ReadSelfCmdline() {
        char buf[1024];
        const ssize_t nr = butil::ReadCommandLine(buf, sizeof(buf), true);
        content.append(buf, nr);
    }
};
static void get_cmdline(std::ostream& os, void*) {
    os << butil::get_leaky_singleton<ReadSelfCmdline>()->content;
}

struct ReadVersion {
    std::string content;
    ReadVersion() {
        std::ostringstream oss;
        if (butil::read_command_output(oss, "uname -ap") != 0) {
            LOG(ERROR) << "Fail to read kernel version";
            return;
        }
        content.append(oss.str());
    }
};
static void get_kernel_version(std::ostream& os, void*) {
    os << butil::get_leaky_singleton<ReadVersion>()->content;
}

// ======================================

static int64_t g_starting_time = butil::gettimeofday_us();

static timeval get_uptime(void*) {
    int64_t uptime_us = butil::gettimeofday_us() - g_starting_time;
    timeval tm;
    tm.tv_sec = uptime_us / 1000000L;
    tm.tv_usec = uptime_us - tm.tv_sec * 1000000L;
    return tm;
}

// ======================================

class RUsageReader {
public:
    bool operator()(rusage* stat) const {
        const int rc = getrusage(RUSAGE_SELF, stat);
        if (rc < 0) {
            PLOG(WARNING) << "Fail to getrusage";
            return false;
        }
        return true;
    }
    template <typename T, size_t offset>
    static T get_field(void*) {
        return *(T*)((char*)&CachedReader<rusage>::get_value(
                         RUsageReader()) + offset);
    }
};

#define BVAR_DEFINE_RUSAGE_FIELD(field)                                 \
    PassiveStatus<BVAR_MEMBER_TYPE(&rusage::field)> g_##field(          \
        RUsageReader::get_field<BVAR_MEMBER_TYPE(&rusage::field),       \
        offsetof(rusage, field)>, NULL);                                \
    
#define BVAR_DEFINE_RUSAGE_FIELD2(field, name)                          \
    PassiveStatus<BVAR_MEMBER_TYPE(&rusage::field)> g_##field(          \
        name,                                                           \
        RUsageReader::get_field<BVAR_MEMBER_TYPE(&rusage::field),       \
        offsetof(rusage, field)>, NULL);                                \

// ======================================

BVAR_DEFINE_PROC_STAT_FIELD2(pid, "pid");
BVAR_DEFINE_PROC_STAT_FIELD2(ppid, "ppid");
BVAR_DEFINE_PROC_STAT_FIELD2(pgrp, "pgrp");

static void get_username(std::ostream& os, void*) {
    char buf[32];
    if (getlogin_r(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf)-1] = '\0';
        os << buf;
    } else {
        os << "unknown (" << berror() << ')' ;
    }
}

PassiveStatus<std::string> g_username(
    "process_username", get_username, NULL);

BVAR_DEFINE_PROC_STAT_FIELD(minflt);
PerSecond<PassiveStatus<unsigned long> > g_minflt_second(
    "process_faults_minor_second", &g_minflt);
BVAR_DEFINE_PROC_STAT_FIELD2(majflt, "process_faults_major");

BVAR_DEFINE_PROC_STAT_FIELD2(priority, "process_priority");
BVAR_DEFINE_PROC_STAT_FIELD2(nice, "process_nice");

BVAR_DEFINE_PROC_STAT_FIELD2(num_threads, "process_thread_count");
PassiveStatus<int> g_fd_num("process_fd_count", print_fd_count, NULL);

BVAR_DEFINE_PROC_MEMORY_FIELD(size, "process_memory_virtual");
BVAR_DEFINE_PROC_MEMORY_FIELD(resident, "process_memory_resident");
BVAR_DEFINE_PROC_MEMORY_FIELD(share, "process_memory_shared");
BVAR_DEFINE_PROC_MEMORY_FIELD(trs, "process_memory_text");
BVAR_DEFINE_PROC_MEMORY_FIELD(drs, "process_memory_data_and_stack");

BVAR_DEFINE_LOAD_AVERAGE_FIELD(loadavg_1m, "system_loadavg_1m");
BVAR_DEFINE_LOAD_AVERAGE_FIELD(loadavg_5m, "system_loadavg_5m");
BVAR_DEFINE_LOAD_AVERAGE_FIELD(loadavg_15m, "system_loadavg_15m");

BVAR_DEFINE_PROC_IO_FIELD(rchar);
BVAR_DEFINE_PROC_IO_FIELD(wchar);
PerSecond<PassiveStatus<size_t> > g_io_read_second(
    "process_io_read_bytes_second", &g_rchar);
PerSecond<PassiveStatus<size_t> > g_io_write_second(
    "process_io_write_bytes_second", &g_wchar);

BVAR_DEFINE_PROC_IO_FIELD(syscr);
BVAR_DEFINE_PROC_IO_FIELD(syscw);
PerSecond<PassiveStatus<size_t> > g_io_num_reads_second(
    "process_io_read_second", &g_syscr);
PerSecond<PassiveStatus<size_t> > g_io_num_writes_second(
    "process_io_write_second", &g_syscw);

BVAR_DEFINE_PROC_IO_FIELD(read_bytes);
BVAR_DEFINE_PROC_IO_FIELD(write_bytes);
PerSecond<PassiveStatus<size_t> > g_disk_read_second(
    "process_disk_read_bytes_second", &g_read_bytes);
PerSecond<PassiveStatus<size_t> > g_disk_write_second(
    "process_disk_write_bytes_second", &g_write_bytes);

BVAR_DEFINE_RUSAGE_FIELD(ru_utime);
BVAR_DEFINE_RUSAGE_FIELD(ru_stime);
PassiveStatus<timeval> g_uptime("process_uptime", get_uptime, NULL);

static int get_core_num(void*) {
    return sysconf(_SC_NPROCESSORS_ONLN);
}
PassiveStatus<int> g_core_num("system_core_count", get_core_num, NULL);

struct TimePercent {
    int64_t time_us;
    int64_t real_time_us;

    void operator-=(const TimePercent& rhs) {
        time_us -= rhs.time_us;
        real_time_us -= rhs.real_time_us;
    }
    void operator+=(const TimePercent& rhs) {
        time_us += rhs.time_us;
        real_time_us += rhs.real_time_us;
    }
};
inline std::ostream& operator<<(std::ostream& os, const TimePercent& tp) {
    if (tp.real_time_us <= 0) {
        return os << "0";
    } else {
        return os << std::fixed << std::setprecision(3)
                  << (double)tp.time_us / tp.real_time_us;
    }
}

static TimePercent get_cputime_percent(void*) {
    TimePercent tp = { butil::timeval_to_microseconds(g_ru_stime.get_value()) +
                       butil::timeval_to_microseconds(g_ru_utime.get_value()),
                       butil::timeval_to_microseconds(g_uptime.get_value()) };
    return tp;
}
PassiveStatus<TimePercent> g_cputime_percent(get_cputime_percent, NULL);
Window<PassiveStatus<TimePercent>, SERIES_IN_SECOND> g_cputime_percent_second(
    "process_cpu_usage", &g_cputime_percent, FLAGS_bvar_dump_interval);

static TimePercent get_stime_percent(void*) {
    TimePercent tp = { butil::timeval_to_microseconds(g_ru_stime.get_value()),
                       butil::timeval_to_microseconds(g_uptime.get_value()) };
    return tp;
}
PassiveStatus<TimePercent> g_stime_percent(get_stime_percent, NULL);
Window<PassiveStatus<TimePercent>, SERIES_IN_SECOND> g_stime_percent_second(
    "process_cpu_usage_system", &g_stime_percent, FLAGS_bvar_dump_interval);

static TimePercent get_utime_percent(void*) {
    TimePercent tp = { butil::timeval_to_microseconds(g_ru_utime.get_value()),
                       butil::timeval_to_microseconds(g_uptime.get_value()) };
    return tp;
}
PassiveStatus<TimePercent> g_utime_percent(get_utime_percent, NULL);
Window<PassiveStatus<TimePercent>, SERIES_IN_SECOND> g_utime_percent_second(
    "process_cpu_usage_user", &g_utime_percent, FLAGS_bvar_dump_interval);

// According to http://man7.org/linux/man-pages/man2/getrusage.2.html
// Unsupported fields in linux:
//   ru_ixrss
//   ru_idrss
//   ru_isrss 
//   ru_nswap 
//   ru_nsignals 
BVAR_DEFINE_RUSAGE_FIELD(ru_inblock);
BVAR_DEFINE_RUSAGE_FIELD(ru_oublock);
BVAR_DEFINE_RUSAGE_FIELD(ru_nvcsw);
BVAR_DEFINE_RUSAGE_FIELD(ru_nivcsw);
PerSecond<PassiveStatus<long> > g_ru_inblock_second(
    "process_inblocks_second", &g_ru_inblock);
PerSecond<PassiveStatus<long> > g_ru_oublock_second(
    "process_outblocks_second", &g_ru_oublock);
PerSecond<PassiveStatus<long> > cs_vol_second(
    "process_context_switches_voluntary_second", &g_ru_nvcsw);
PerSecond<PassiveStatus<long> > cs_invol_second(
    "process_context_switches_involuntary_second", &g_ru_nivcsw);

PassiveStatus<std::string> g_cmdline("process_cmdline", get_cmdline, NULL);
PassiveStatus<std::string> g_kernel_version(
    "kernel_version", get_kernel_version, NULL);

static std::string* s_gcc_version = NULL;
pthread_once_t g_gen_gcc_version_once = PTHREAD_ONCE_INIT;

void gen_gcc_version() {

#if defined(__GNUC__)
    const int gcc_major = __GNUC__;
#else 
    const int gcc_major = -1;
#endif

#if defined(__GNUC_MINOR__)
    const int gcc_minor = __GNUC_MINOR__;
#else
    const int gcc_minor = -1;
#endif

#if defined(__GNUC_PATCHLEVEL__)
    const int gcc_patchlevel = __GNUC_PATCHLEVEL__;
#else
    const int gcc_patchlevel = -1;
#endif

    s_gcc_version = new std::string;

    if (gcc_major == -1) {
        *s_gcc_version = "unknown";
        return;
    }
    std::ostringstream oss;
    oss << gcc_major;
    if (gcc_minor == -1) {
        return;
    }
    oss << '.' << gcc_minor;

    if (gcc_patchlevel == -1) {
        return;
    }
    oss << '.' << gcc_patchlevel;

    *s_gcc_version = oss.str();

}

void get_gcc_version(std::ostream& os, void*) {
    pthread_once(&g_gen_gcc_version_once, gen_gcc_version);
    os << *s_gcc_version;
}

// =============================================
PassiveStatus<std::string> g_gcc_version("gcc_version", get_gcc_version, NULL);

void get_work_dir(std::ostream& os, void*) {
    butil::FilePath path;
    const bool rc = butil::GetCurrentDirectory(&path);
    LOG_IF(WARNING, !rc) << "Fail to GetCurrentDirectory";
    os << path.value();
}
PassiveStatus<std::string> g_work_dir("process_work_dir", get_work_dir, NULL);

#undef BVAR_MEMBER_TYPE
#undef BVAR_DEFINE_PROC_STAT_FIELD
#undef BVAR_DEFINE_PROC_STAT_FIELD2
#undef BVAR_DEFINE_PROC_MEMORY_FIELD
#undef BVAR_DEFINE_RUSAGE_FIELD
#undef BVAR_DEFINE_RUSAGE_FIELD2

}  // namespace bvar

// In the same scope where timeval is defined. Required by clang.
inline std::ostream& operator<<(std::ostream& os, const timeval& tm) {
    return os << tm.tv_sec << '.' << std::setw(6) << std::setfill('0') << tm.tv_usec;
}
