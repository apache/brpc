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

#include <time.h>                           // strftime
#include <pthread.h>
#include <map>
#include <limits>
#include <sys/stat.h>
#include <fcntl.h>                          // O_RDONLY
#include "butil/string_printf.h"             // string_printf
#include "butil/string_splitter.h"           // StringSplitter
#include "butil/file_util.h"                 // butil::FilePath
#include "butil/files/scoped_file.h"         // ScopedFILE
#include "butil/time.h"
#include "butil/popen.h"                    // butil::read_command_output
#include "butil/process_util.h"             // butil::ReadCommandLine
#include "brpc/log.h"
#include "brpc/controller.h"                // Controller
#include "brpc/closure_guard.h"             // ClosureGuard
#include "brpc/builtin/pprof_service.h"
#include "brpc/builtin/common.h"
#include "brpc/details/tcmalloc_extension.h"
#include "bthread/bthread.h"                // bthread_usleep
#include "butil/fd_guard.h"

extern "C" {
#if defined(OS_LINUX)
extern char *program_invocation_name;
#endif
int __attribute__((weak)) ProfilerStart(const char* fname);
void __attribute__((weak)) ProfilerStop();
}

namespace bthread {
bool ContentionProfilerStart(const char* filename);
void ContentionProfilerStop();
}


namespace brpc {

static int ReadSeconds(Controller* cntl) {
    int seconds = 0;
    const std::string* param =
        cntl->http_request().uri().GetQuery("seconds");
    if (param != NULL) {
        char* endptr = NULL;
        const long sec = strtol(param->c_str(), &endptr, 10);
        if (endptr == param->c_str() + param->length()) {
            seconds = sec;
        } else {
            cntl->SetFailed(EINVAL, "Invalid seconds=%s", param->c_str());
        }
    }

    return seconds;
}

int MakeProfName(ProfilingType type, char* buf, size_t buf_len) {
    // Add pprof_ prefix to separate from /hotspots
    int nr = snprintf(buf, buf_len, "%s/pprof_%s/", FLAGS_rpc_profiling_dir.c_str(),
                      GetProgramChecksum());
    if (nr < 0) {
        return -1;
    }
    buf += nr;
    buf_len -= nr;
    
    time_t rawtime;
    time(&rawtime);
    struct tm* timeinfo = localtime(&rawtime);
    const size_t nw = strftime(buf, buf_len, "%Y%m%d.%H%M%S", timeinfo);
    buf += nw;
    buf_len -= nw;

    // We have checksum in the path, getpid() is not necessary now.
    snprintf(buf, buf_len, ".%s", ProfilingType2String(type));
    return 0;
}

void PProfService::profile(
    ::google::protobuf::RpcController* controller_base,
    const ::brpc::ProfileRequest* /*request*/,
    ::brpc::ProfileResponse* /*response*/,
    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller* cntl = static_cast<Controller*>(controller_base);
    cntl->http_response().set_content_type("text/plain");
    if ((void*)ProfilerStart == NULL || (void*)ProfilerStop == NULL) {
        cntl->SetFailed(ENOMETHOD, "%s, to enable cpu profiler, check out "
                        "docs/cn/cpu_profiler.md",
                        berror(ENOMETHOD));
        return;
    }    
    int sleep_sec = ReadSeconds(cntl);
    if (sleep_sec <= 0) {
        if (!cntl->Failed()) {
            cntl->SetFailed(EINVAL, "You have to specify ?seconds=N. If you're "
                            "using pprof, add --seconds=N");
        }
        return;
    }
    // Log requester
    std::ostringstream client_info;
    client_info << cntl->remote_side();
    if (cntl->auth_context()) {
        client_info << "(auth=" << cntl->auth_context()->user() << ')';
    } else {
        client_info << "(no auth)";
    }
    LOG(INFO) << client_info.str() << " requests for cpu profile for "
              << sleep_sec << " seconds";

    char prof_name[256];
    if (MakeProfName(PROFILING_CPU, prof_name, sizeof(prof_name)) != 0) {
        cntl->SetFailed(errno, "Fail to create .prof file, %s", berror());
        return;
    }
    butil::File::Error error;
    const butil::FilePath dir = butil::FilePath(prof_name).DirName();
    if (!butil::CreateDirectoryAndGetError(dir, &error)) {
        cntl->SetFailed(EPERM, "Fail to create directory=`%s'",dir.value().c_str());
        return;
    }
    if (!ProfilerStart(prof_name)) {
        cntl->SetFailed(EAGAIN, "Another profiler is running, try again later");
        return;
    }
    if (bthread_usleep(sleep_sec * 1000000L) != 0) {
        PLOG(WARNING) << "Profiling has been interrupted";
    }
    ProfilerStop();

    butil::fd_guard fd(open(prof_name, O_RDONLY));
    if (fd < 0) {
        cntl->SetFailed(ENOENT, "Fail to open %s", prof_name);
        return;
    }
    butil::IOPortal portal;
    portal.append_from_file_descriptor(fd, ULONG_MAX);
    cntl->response_attachment().swap(portal);
}

void PProfService::contention(
    ::google::protobuf::RpcController* controller_base,
    const ::brpc::ProfileRequest* /*request*/,
    ::brpc::ProfileResponse* /*response*/,
    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller* cntl = static_cast<Controller*>(controller_base);
    cntl->http_response().set_content_type("text/plain");
    int sleep_sec = ReadSeconds(cntl);
    if (sleep_sec <= 0) {
        if (!cntl->Failed()) {
            cntl->SetFailed(EINVAL, "You have to specify ?seconds=N. If you're "
                            "using pprof, add --seconds=N");
        }
        return;
    }
    // Log requester
    std::ostringstream client_info;
    client_info << cntl->remote_side();
    if (cntl->auth_context()) {
        client_info << "(auth=" << cntl->auth_context()->user() << ')';
    } else {
        client_info << "(no auth)";
    }
    LOG(INFO) << client_info.str() << " requests for contention profile for "
              << sleep_sec << " seconds";

    char prof_name[256];
    if (MakeProfName(PROFILING_CONTENTION, prof_name, sizeof(prof_name)) != 0) {
        cntl->SetFailed(errno, "Fail to create .prof file, %s", berror());
        return;
    }
    if (!bthread::ContentionProfilerStart(prof_name)) {
        cntl->SetFailed(EAGAIN, "Another profiler is running, try again later");
        return;
    }
    if (bthread_usleep(sleep_sec * 1000000L) != 0) {
        PLOG(WARNING) << "Profiling has been interrupted";
    }
    bthread::ContentionProfilerStop();

    butil::fd_guard fd(open(prof_name, O_RDONLY));
    if (fd < 0) {
        cntl->SetFailed(ENOENT, "Fail to open %s", prof_name);
        return;
    }
    butil::IOPortal portal;
    portal.append_from_file_descriptor(fd, ULONG_MAX);
    cntl->response_attachment().swap(portal);
}

void PProfService::heap(
    ::google::protobuf::RpcController* controller_base,
    const ::brpc::ProfileRequest* /*request*/,
    ::brpc::ProfileResponse* /*response*/,
    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller* cntl = static_cast<Controller*>(controller_base);
    MallocExtension* malloc_ext = MallocExtension::instance();
    if (malloc_ext == NULL || !has_TCMALLOC_SAMPLE_PARAMETER()) {
        const char* extra_desc = "";
        if (malloc_ext != NULL) {
            extra_desc = " (no TCMALLOC_SAMPLE_PARAMETER in env)";
        }
        cntl->SetFailed(ENOMETHOD, "Heap profiler is not enabled%s,"
                        "check out https://github.com/apache/incubator-brpc/blob/master/docs/cn/heap_profiler.md",
                        extra_desc);
        return;
    }
    // Log requester
    std::ostringstream client_info;
    client_info << cntl->remote_side();
    if (cntl->auth_context()) {
        client_info << "(auth=" << cntl->auth_context()->user() << ')';
    } else {
        client_info << "(no auth)";
    }
    LOG(INFO) << client_info.str() << " requests for heap profile";

    std::string obj;
    malloc_ext->GetHeapSample(&obj);
    cntl->http_response().set_content_type("text/plain");
    cntl->response_attachment().append(obj);    
}

void PProfService::growth(
    ::google::protobuf::RpcController* controller_base,
    const ::brpc::ProfileRequest* /*request*/,
    ::brpc::ProfileResponse* /*response*/,
    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller* cntl = static_cast<Controller*>(controller_base);
    MallocExtension* malloc_ext = MallocExtension::instance();
    if (malloc_ext == NULL) {
        cntl->SetFailed(ENOMETHOD, "%s, to enable growth profiler, check out "
                        "docs/cn/heap_profiler.md",
                        berror(ENOMETHOD));
        return;
    }
    // Log requester
    std::ostringstream client_info;
    client_info << cntl->remote_side();
    if (cntl->auth_context()) {
        client_info << "(auth=" << cntl->auth_context()->user() << ')';
    } else {
        client_info << "(no auth)";
    }
    LOG(INFO) << client_info.str() << " requests for growth profile";

    std::string obj;
    malloc_ext->GetHeapGrowthStacks(&obj);
    cntl->http_response().set_content_type("text/plain");
    cntl->response_attachment().append(obj);    
}

typedef std::map<uintptr_t, std::string> SymbolMap;
struct LibInfo {
    uintptr_t start_addr;
    uintptr_t end_addr;
    size_t offset;
    std::string path;
};
static SymbolMap symbol_map;
static pthread_once_t s_load_symbolmap_once = PTHREAD_ONCE_INIT;

static bool HasExt(const std::string& name, const std::string& ext) {
    size_t index = name.find(ext);
    if (index == std::string::npos) {
        return false;
    }
    return (index + ext.size() == name.size() ||
            name[index + ext.size()] == '.');
}

static int ExtractSymbolsFromBinary(
    std::map<uintptr_t, std::string>& addr_map,
    const LibInfo& lib_info) {
    butil::Timer tm;
    tm.start();
    std::string cmd = "nm -C -p ";
    cmd.append(lib_info.path);
    std::stringstream ss;
    const int rc = butil::read_command_output(ss, cmd.c_str());
    if (rc < 0) {
        LOG(ERROR) << "Fail to popen `" << cmd << "'";
        return -1;
    }
    std::string line;
    while (std::getline(ss, line)) {
        butil::StringSplitter sp(line.c_str(), ' ');
        if (sp == NULL) {
            continue;
        }
        char* endptr = NULL;
        uintptr_t addr = strtoull(sp.field(), &endptr, 16);
        if (*endptr != ' ') {
            continue;
        }
        if (addr < lib_info.start_addr) {
            addr = addr + lib_info.start_addr - lib_info.offset;
        }
        if (addr >= lib_info.end_addr) {
            continue;
        }
        ++sp;
        if (sp == NULL) {
            continue;
        }
        if (sp.length() != 1UL) {
            continue;
        }
        //const char c = *sp.field();
        
        ++sp;
        if (sp == NULL) {
            continue;
        }
        const char* name_begin = sp.field();
        if (strncmp(name_begin, "typeinfo ", 9) == 0 ||
            strncmp(name_begin, "VTT ", 4) == 0 ||
            strncmp(name_begin, "vtable ", 7) == 0 ||
            strncmp(name_begin, "global ", 7) == 0 ||
            strncmp(name_begin, "guard ", 6) == 0) {
            addr_map[addr] = std::string();
            continue;
        }
        
        const char* name_end = sp.field();
        bool stop = false;
        char last_char = '\0';
        while (1) {
            switch (*name_end) {
            case 0:
            case '\r':
            case '\n':
                stop = true;
                break;
            case '(':
            case '<':
                // \(.*\w\)[(<]...    -> \1
                // foo(..)            -> foo
                // foo<...>(...)      -> foo
                // a::b::foo(...)     -> a::b::foo
                // a::(b)::foo(...)   -> a::(b)::foo
                if (isalpha(last_char) || isdigit(last_char) ||
                    last_char == '_') {
                    stop = true;
                }
            default:
                break;
            }
            if (stop) {
                break;
            }
            last_char = *name_end++;
        }
        // If address conflicts, choose a shorter name (not necessarily to be
        // T type in nm). This works fine because aliases often have more
        // prefixes.
        const size_t name_len = name_end - name_begin;
        SymbolMap::iterator it = addr_map.find(addr);
        if (it != addr_map.end()) {
            if (name_len < it->second.size()) {
                it->second.assign(name_begin, name_len);
            }
        } else {
            addr_map[addr] = std::string(name_begin, name_len);
        }
    }
    if (addr_map.find(lib_info.end_addr) == addr_map.end()) {
        addr_map[lib_info.end_addr] = std::string();
    }
    tm.stop();
    RPC_VLOG << "Loaded " << lib_info.path << " in " << tm.m_elapsed() << "ms";
    return 0;
}

static void LoadSymbols() {
    butil::Timer tm;
    tm.start();
    butil::ScopedFILE fp(fopen("/proc/self/maps", "r"));
    if (fp == NULL) {
        return;
    }
    char* line = NULL;
    size_t line_len = 0;
    ssize_t nr = 0;
    while ((nr = getline(&line, &line_len, fp.get())) != -1) {
        butil::StringSplitter sp(line, line + nr, ' ');
        if (sp == NULL) {
            continue;
        }
        char* endptr;
        uintptr_t start_addr = strtoull(sp.field(), &endptr, 16);
        if (*endptr != '-') {
            continue;
        }
        ++endptr;
        uintptr_t end_addr = strtoull(endptr, &endptr, 16);
        if (*endptr != ' ') {
            continue;
        }
        ++sp;
        // ..x. must be executable
        if (sp == NULL || sp.length() != 4 || sp.field()[2] != 'x') {
            continue;
        }
        ++sp;
        if (sp == NULL) {
            continue;
        }
        size_t offset = strtoull(sp.field(), &endptr, 16);
        if (*endptr != ' ') {
            continue;
        }        
        //skip $4~$5
        for (int i = 0; i < 3; ++i) {
            ++sp;
        }
        if (sp == NULL) {
            continue;
        }
        size_t n = sp.length();
        if (sp.field()[n-1] == '\n') {
            --n;
        }
        std::string path(sp.field(), n);
        if (!HasExt(path, ".so") && !HasExt(path, ".dll") &&
            !HasExt(path, ".dylib") && !HasExt(path, ".bundle")) {
            continue;
        }
        LibInfo info;
        info.start_addr = start_addr;
        info.end_addr = end_addr;
        info.offset = offset;
        info.path = path;
        ExtractSymbolsFromBinary(symbol_map, info);
    }
    free(line);

    LibInfo info;
    info.start_addr = 0;
    info.end_addr = std::numeric_limits<uintptr_t>::max();
    info.offset = 0;
#if defined(OS_LINUX)
    info.path = program_invocation_name;
#elif defined(OS_MACOSX)
    info.path = getprogname();
#endif
    ExtractSymbolsFromBinary(symbol_map, info);

    butil::Timer tm2;
    tm2.start();
    size_t num_removed = 0;
    bool last_is_empty = false;
    for (SymbolMap::iterator
             it = symbol_map.begin(); it != symbol_map.end();) {
        if (it->second.empty()) {
            if (last_is_empty) {
                symbol_map.erase(it++);
                ++num_removed;
            } else {
                ++it;
            }
            last_is_empty = true;
        } else {
            ++it;
        }
    }
    tm2.stop();
    RPC_VLOG_IF(num_removed) << "Removed " << num_removed << " entries in "
        << tm2.m_elapsed() << "ms";

    tm.stop();
    RPC_VLOG << "Loaded all symbols in " << tm.m_elapsed() << "ms";
}

static void FindSymbols(butil::IOBuf* out, std::vector<uintptr_t>& addr_list) {
    char buf[32];
    for (size_t i = 0; i < addr_list.size(); ++i) {
        int len = snprintf(buf, sizeof(buf), "0x%08lx\t", addr_list[i]);
        out->append(buf, len);
        SymbolMap::const_iterator it = symbol_map.lower_bound(addr_list[i]);
        if (it == symbol_map.end() || it->first != addr_list[i]) {
            if (it != symbol_map.begin()) {
                --it;
            } else {
                len = snprintf(buf, sizeof(buf), "0x%08lx\n", addr_list[i]);
                out->append(buf, len);
                continue;
            }
        }
        if (it->second.empty()) {
            len = snprintf(buf, sizeof(buf), "0x%08lx\n", addr_list[i]);
            out->append(buf, len);
        } else {
            out->append(it->second);
            out->push_back('\n');
        }
    }
}

void PProfService::symbol(
    ::google::protobuf::RpcController* controller_base,
    const ::brpc::ProfileRequest* /*request*/,
    ::brpc::ProfileResponse* /*response*/,
    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller* cntl = static_cast<Controller*>(controller_base);
    cntl->http_response().set_content_type("text/plain");

    // Load /proc/self/maps
    pthread_once(&s_load_symbolmap_once, LoadSymbols);

    if (cntl->http_request().method() != HTTP_METHOD_POST) {
        char buf[64];
        snprintf(buf, sizeof(buf), "num_symbols: %lu\n", symbol_map.size());
        cntl->response_attachment().append(buf);
    } else {
        // addr_str is addressed separated by +
        std::string addr_str = cntl->request_attachment().to_string();
        // May be quoted
        const char* addr_cstr = addr_str.c_str();
        if (*addr_cstr == '\'' || *addr_cstr == '"') {
            ++addr_cstr;
        }
        std::vector<uintptr_t> addr_list;
        addr_list.reserve(32);
        butil::StringSplitter sp(addr_cstr, '+');
        for ( ; sp != NULL; ++sp) {
            char* endptr;
            uintptr_t addr = strtoull(sp.field(), &endptr, 16);
            addr_list.push_back(addr);
        }
        FindSymbols(&cntl->response_attachment(), addr_list);
    }
}

void PProfService::cmdline(::google::protobuf::RpcController* controller_base,
                           const ::brpc::ProfileRequest* /*request*/,
                           ::brpc::ProfileResponse* /*response*/,
                           ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller* cntl = static_cast<Controller*>(controller_base);
    cntl->http_response().set_content_type("text/plain" /*FIXME*/);
    char buf[1024];  // should be enough?
    const ssize_t nr = butil::ReadCommandLine(buf, sizeof(buf), true);
    if (nr < 0) {
        cntl->SetFailed(ENOENT, "Fail to read cmdline");
        return;
    }
    cntl->response_attachment().append(buf, nr);
}

} // namespace brpc
