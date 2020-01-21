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


#include <stdio.h>
#include <thread>
#include <gflags/gflags.h>
#include "butil/files/file_enumerator.h"
#include "butil/file_util.h"                     // butil::FilePath
#include "butil/popen.h"                         // butil::read_command_output
#include "butil/fd_guard.h"                      // butil::fd_guard
#include "brpc/log.h"
#include "brpc/controller.h"
#include "brpc/server.h"
#include "brpc/reloadable_flags.h"
#include "brpc/builtin/pprof_perl.h"
#include "brpc/builtin/flamegraph_perl.h"
#include "brpc/builtin/hotspots_service.h"
#include "brpc/details/tcmalloc_extension.h"

extern "C" {
int __attribute__((weak)) ProfilerStart(const char* fname);
void __attribute__((weak)) ProfilerStop();
}

namespace bthread {
bool ContentionProfilerStart(const char* filename);
void ContentionProfilerStop();
}


namespace brpc {
enum class DisplayType{
    kUnknown,
    kDot,
#if defined(OS_LINUX)
    kFlameGraph,
#endif
    kText
};

static const char* DisplayTypeToString(DisplayType type) {
    switch (type) {
        case DisplayType::kDot: return "dot";
#if defined(OS_LINUX)
        case DisplayType::kFlameGraph: return "flame";
#endif
        case DisplayType::kText: return "text";
        default: return "unknown";
    }
}

static DisplayType StringToDisplayType(const std::string& val) {
    static butil::CaseIgnoredFlatMap<DisplayType>* display_type_map;
    static std::once_flag flag;
    std::call_once(flag, []() {
        display_type_map = new butil::CaseIgnoredFlatMap<DisplayType>;
        display_type_map->init(10);
        (*display_type_map)["dot"] = DisplayType::kDot;
#if defined(OS_LINUX)
        (*display_type_map)["flame"] = DisplayType::kFlameGraph;
#endif
        (*display_type_map)["text"] = DisplayType::kText;
    });
    auto type = display_type_map->seek(val);
    if (type == nullptr) {
      return DisplayType::kUnknown;
    }
    return *type;
}

static std::string DisplayTypeToPProfArgument(DisplayType type) {
    switch (type) {
#if defined(OS_LINUX)
        case DisplayType::kDot: return " --dot ";
        case DisplayType::kFlameGraph: return " --collapsed ";
        case DisplayType::kText: return " --text ";
#elif defined(OS_MACOSX)
        case DisplayType::kDot: return " -dot ";
        case DisplayType::kText: return " -text ";
#endif
        default: return " unknown type ";
    }
}

static std::string GeneratePerlScriptPath(const std::string& filename) {
    std::string path;
    path.reserve(FLAGS_rpc_profiling_dir.size() + 1 + filename.size());
    path += FLAGS_rpc_profiling_dir;
    path.push_back('/');
    path += filename;
    return path;
}

extern bool cpu_profiler_enabled;

DEFINE_int32(max_profiling_seconds, 300, "upper limit of running time of profilers");
BRPC_VALIDATE_GFLAG(max_profiling_seconds, NonNegativeInteger);

DEFINE_int32(max_profiles_kept, 32,
             "max profiles kept for cpu/heap/growth/contention respectively");
BRPC_VALIDATE_GFLAG(max_profiles_kept, PassValidate);

static const char* const PPROF_FILENAME = "pprof.pl";
static const char* const FLAMEGRAPH_FILENAME = "flamegraph.pl";
static int DEFAULT_PROFILING_SECONDS = 10;
static size_t CONCURRENT_PROFILING_LIMIT = 256;

struct ProfilingWaiter {
    Controller* cntl;
    ::google::protobuf::Closure* done;
};

// Information of the client doing profiling.
struct ProfilingClient {
    ProfilingClient() : end_us(0), seconds(0), id(0) {}
    
    int64_t end_us;
    int seconds;
    int64_t id;
    butil::EndPoint point;
};

struct ProfilingResult {
    ProfilingResult() : id(0), status_code(HTTP_STATUS_OK) {}
    
    int64_t id;
    int status_code;
    butil::IOBuf result;
};

static bool g_written_pprof_perl = false;

struct ProfilingEnvironment {
    pthread_mutex_t mutex;
    int64_t cur_id;
    ProfilingClient* client;
    std::vector<ProfilingWaiter>* waiters;
    ProfilingResult* cached_result;
};

// Different ProfilingType have different env.
static ProfilingEnvironment g_env[4] = {
    { PTHREAD_MUTEX_INITIALIZER, 0, NULL, NULL, NULL },
    { PTHREAD_MUTEX_INITIALIZER, 0, NULL, NULL, NULL },
    { PTHREAD_MUTEX_INITIALIZER, 0, NULL, NULL, NULL },
    { PTHREAD_MUTEX_INITIALIZER, 0, NULL, NULL, NULL }
};

// The `content' should be small so that it can be written into file in one
// fwrite (at most time).
static bool WriteSmallFile(const char* filepath_in,
                           const butil::StringPiece& content) {
    butil::File::Error error;
    butil::FilePath path(filepath_in);
    butil::FilePath dir = path.DirName();
    if (!butil::CreateDirectoryAndGetError(dir, &error)) {
        LOG(ERROR) << "Fail to create directory=`" << dir.value()
                   << "', " << error;
        return false;
    }
    FILE* fp = fopen(path.value().c_str(), "w");
    if (NULL == fp) {
        LOG(ERROR) << "Fail to open `" << path.value() << '\'';
        return false;
    }
    bool ret = true;
    if (fwrite(content.data(), content.size(), 1UL, fp) != 1UL) {
        LOG(ERROR) << "Fail to write into " << path.value();
        ret = false;
    }
    CHECK_EQ(0, fclose(fp));
    return ret;
}

static bool WriteSmallFile(const char* filepath_in,
                           const butil::IOBuf& content) {
    butil::File::Error error;
    butil::FilePath path(filepath_in);
    butil::FilePath dir = path.DirName();
    if (!butil::CreateDirectoryAndGetError(dir, &error)) {
        LOG(ERROR) << "Fail to create directory=`" << dir.value()
                   << "', " << error;
        return false;
    }
    FILE* fp = fopen(path.value().c_str(), "w");
    if (NULL == fp) {
        LOG(ERROR) << "Fail to open `" << path.value() << '\'';
        return false;
    }
    butil::IOBufAsZeroCopyInputStream iter(content);
    const void* data = NULL;
    int size = 0;
    while (iter.Next(&data, &size)) {
        if (fwrite(data, size, 1UL, fp) != 1UL) {
            LOG(ERROR) << "Fail to write into " << path.value();
            fclose(fp);
            return false;
        }
    }
    fclose(fp);
    return true;
}

static int ReadSeconds(const Controller* cntl) {
    int seconds = DEFAULT_PROFILING_SECONDS;
    const std::string* param =
        cntl->http_request().uri().GetQuery("seconds");
    if (param != NULL) {
        char* endptr = NULL;
        const long sec = strtol(param->c_str(), &endptr, 10);
        if (endptr == param->c_str() + param->length()) {
            seconds = sec;
        } else {
            return -1;
        }
    }
    seconds = std::min(seconds, FLAGS_max_profiling_seconds);
    return seconds;
}

static const char* GetBaseName(const std::string* full_base_name) {
    if (full_base_name == NULL) {
        return NULL;
    }
    size_t offset = full_base_name->find_last_of('/');
    if (offset == std::string::npos) {
        offset = 0;
    } else {
        ++offset;
    }
    return full_base_name->c_str() + offset;
}

static const char* GetBaseName(const char* full_base_name) {
    butil::StringPiece s(full_base_name);
    size_t offset = s.find_last_of('/');
    if (offset == butil::StringPiece::npos) {
        offset = 0;
    } else {
        ++offset;
    }
    return s.data() + offset;
}

// Test if path of the profile is valid.
// NOTE: this function MUST be applied to all parameters finally passed to
// system related functions (popen/system/exec ...) to avoid potential
// injections from URL and other user inputs.
static bool ValidProfilePath(const butil::StringPiece& path) {
    if (!path.starts_with(FLAGS_rpc_profiling_dir)) {
        // Must be under the directory.
        return false;
    }
    int consecutive_dot_count = 0;
    for (size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        if (c == '.') {
            ++consecutive_dot_count;
            if (consecutive_dot_count >= 2) {
                // Disallow consecutive dots to go to upper level directories.
                return false;
            } else {
                continue;
            }
        } else {
            consecutive_dot_count = 0;
        }
        if (!isalpha(c) && !isdigit(c) &&
            c != '_' && c != '-' && c != '/') {
            return false;
        }
    }
    return true;
}

static int MakeCacheName(char* cache_name, size_t len,
                         const char* prof_name,
                         const char* base_name,
                         DisplayType display_type,
                         bool show_ccount) {
    if (base_name) {
        return snprintf(cache_name, len, "%s.cache/base_%s.%s%s", prof_name,
                        base_name,
                        DisplayTypeToString(display_type),
                        (show_ccount ? ".ccount" : ""));
    } else {
        return snprintf(cache_name, len, "%s.cache/%s%s", prof_name,
                        DisplayTypeToString(display_type),
                        (show_ccount ? ".ccount" : ""));

    }
}

static int MakeProfName(ProfilingType type, char* buf, size_t buf_len) {
    int nr = snprintf(buf, buf_len, "%s/%s/", FLAGS_rpc_profiling_dir.c_str(),
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

static void ConsumeWaiters(ProfilingType type, const Controller* cur_cntl,
                           std::vector<ProfilingWaiter>* waiters) {
    waiters->clear();
    if ((int)type >= (int)arraysize(g_env)) {
        LOG(ERROR) << "Invalid type=" << type;
        return;
    }
    ProfilingEnvironment& env = g_env[type];
    if (env.client) {
        BAIDU_SCOPED_LOCK(env.mutex);
        if (env.client == NULL) {
            return;
        }
        if (env.cached_result == NULL) {
            env.cached_result = new ProfilingResult;
        }
        env.cached_result->id = env.client->id;
        env.cached_result->status_code =
            cur_cntl->http_response().status_code();
        env.cached_result->result = cur_cntl->response_attachment();

        delete env.client;
        env.client = NULL;
        if (env.waiters) {
            env.waiters->swap(*waiters);
        }
    }
}

// This function is always called with g_env[type].mutex UNLOCKED.
static void NotifyWaiters(ProfilingType type, const Controller* cur_cntl,
                          const std::string* view) {
    if (view != NULL) {
        return;
    }
    std::vector<ProfilingWaiter> saved_waiters;
    CHECK(g_env[type].client);
    ConsumeWaiters(type, cur_cntl, &saved_waiters);
    for (size_t i = 0; i < saved_waiters.size(); ++i) {
        Controller* cntl = saved_waiters[i].cntl;
        ::google::protobuf::Closure* done = saved_waiters[i].done;
        cntl->http_response().set_status_code(
            cur_cntl->http_response().status_code());
        cntl->response_attachment().append(cur_cntl->response_attachment());
        done->Run();
    }
}

#if defined(OS_MACOSX)
static bool check_GOOGLE_PPROF_BINARY_PATH() {
    char* str = getenv("GOOGLE_PPROF_BINARY_PATH");
    if (str == NULL) {
        return false;
    }
    butil::fd_guard fd(open(str, O_RDONLY));
    if (fd < 0) {
        return false;
    }
    return true;
}

static bool has_GOOGLE_PPROF_BINARY_PATH() {
    static bool val = check_GOOGLE_PPROF_BINARY_PATH();
    return val;
}
#endif

static void DisplayResult(Controller* cntl,
                          google::protobuf::Closure* done,
                          const char* prof_name,
                          const butil::IOBuf& result_prefix) {
    ClosureGuard done_guard(done);
    butil::IOBuf prof_result;
    if (cntl->IsCanceled()) {
        // If the page is refreshed, older connections are likely to be
        // already closed by browser.
        return;
    }
    butil::IOBuf& resp = cntl->response_attachment();
    const bool use_html = UseHTML(cntl->http_request());
    const bool show_ccount = cntl->http_request().uri().GetQuery("ccount");
    const std::string* base_name = cntl->http_request().uri().GetQuery("base");
    const std::string* display_type_query = cntl->http_request().uri().GetQuery("display_type");
    DisplayType display_type = DisplayType::kDot;
    if (display_type_query) {
        display_type = StringToDisplayType(*display_type_query);
        if (display_type == DisplayType::kUnknown) {
            return cntl->SetFailed(EINVAL, "Invalid display_type=%s", display_type_query->c_str());
        }
    }
    if (base_name != NULL) {
        if (!ValidProfilePath(*base_name)) {
            return cntl->SetFailed(EINVAL, "Invalid query `base'");
        }
        if (!butil::PathExists(butil::FilePath(*base_name))) {
            return cntl->SetFailed(
                EINVAL, "The profile denoted by `base' does not exist");
        }
    }
    butil::IOBufBuilder os;
    os << result_prefix;
    char expected_result_name[256];
    MakeCacheName(expected_result_name, sizeof(expected_result_name),
                  prof_name, GetBaseName(base_name),
                  display_type, show_ccount);
    // Try to read cache first.
    FILE* fp = fopen(expected_result_name, "r");
    if (fp != NULL) {
        bool succ = false;
        char buffer[1024];
        while (1) {
            size_t nr = fread(buffer, 1, sizeof(buffer), fp);
            if (nr != 0) {
                prof_result.append(buffer, nr);
            }
            if (nr != sizeof(buffer)) {
                if (feof(fp)) {
                    succ = true;
                    break;
                } else if (ferror(fp)) {
                    LOG(ERROR) << "Encountered error while reading for "
                               << expected_result_name;
                    break;
                }
                // retry;
            }
        }
        PLOG_IF(ERROR, fclose(fp) != 0) << "Fail to close fp";
        if (succ) {
            RPC_VLOG << "Hit cache=" << expected_result_name;
            os.move_to(resp);
            if (use_html) {
                resp.append("<pre>");
            }
            resp.append(prof_result);
            if (use_html) {
                resp.append("</pre></body></html>");
            }
            return;
        }
    }
    
    std::ostringstream cmd_builder;

    std::string pprof_tool{GeneratePerlScriptPath(PPROF_FILENAME)};
    std::string flamegraph_tool{GeneratePerlScriptPath(FLAMEGRAPH_FILENAME)};

#if defined(OS_LINUX)
    cmd_builder << "perl " << pprof_tool
                << DisplayTypeToPProfArgument(display_type)
                << (show_ccount ? " --contention " : "");
    if (base_name) {
        cmd_builder << "--base " << *base_name << ' ';
    }

    cmd_builder << GetProgramName() << " " << prof_name;

    if (display_type == DisplayType::kFlameGraph) {
        // For flamegraph, we don't care about pprof error msg, 
        // which will cause confusing messages in the final result.
        cmd_builder << " 2>/dev/null " << " | " << "perl " << flamegraph_tool;
    }
    cmd_builder << " 2>&1 ";
#elif defined(OS_MACOSX)
    cmd_builder << getenv("GOOGLE_PPROF_BINARY_PATH") << " "
                << DisplayTypeToPProfArgument(display_type)
                << (show_ccount ? " -contentions " : "");
    if (base_name) {
        cmd_builder << "-base " << *base_name << ' ';
    }
    cmd_builder << prof_name << " 2>&1 ";
#endif

    const std::string cmd = cmd_builder.str();
    for (int ntry = 0; ntry < 2; ++ntry) {
        if (!g_written_pprof_perl) {
            if (!WriteSmallFile(pprof_tool.c_str(), pprof_perl()) ||
                !WriteSmallFile(flamegraph_tool.c_str(), flamegraph_perl())) {
                os << "Fail to write " << pprof_tool
                   << (use_html ? "</body></html>" : "\n");
                os.move_to(resp);
                cntl->http_response().set_status_code(
                    HTTP_STATUS_INTERNAL_SERVER_ERROR);
                return;
            }
            g_written_pprof_perl = true;
        }
        errno = 0; // read_command_output may not set errno, clear it to make sure if
                   // we see non-zero errno, it's real error.
        butil::IOBufBuilder pprof_output;
        const int rc = butil::read_command_output(pprof_output, cmd.c_str());
        if (rc != 0) {
            butil::FilePath pprof_path(pprof_tool);
            if (!butil::PathExists(pprof_path)) {
                // Write the script again.
                g_written_pprof_perl = false;
                // tell user.
                os << pprof_path.value() << " was removed, recreate ...\n\n";
                continue;
            }
            butil::FilePath flamegraph_path(flamegraph_tool);
            if (!butil::PathExists(flamegraph_path)) {
                // Write the script again.
                g_written_pprof_perl = false;
                // tell user.
                os << flamegraph_path.value() << " was removed, recreate ...\n\n";
                continue;
            }
            if (rc < 0) {
                os << "Fail to execute `" << cmd << "', " << berror()
                   << (use_html ? "</body></html>" : "\n");
                os.move_to(resp);
                cntl->http_response().set_status_code(
                    HTTP_STATUS_INTERNAL_SERVER_ERROR);
                return;
            }
            // cmd returns non zero, quit normally 
        }
        pprof_output.move_to(prof_result);
        // Cache result in file.
        char result_name[256];
        MakeCacheName(result_name, sizeof(result_name), prof_name,
                      GetBaseName(base_name), display_type, show_ccount);

        // Append the profile name as the visual reminder for what
        // current profile is.
        butil::IOBuf before_label;
        butil::IOBuf tmp;
        if (cntl->http_request().uri().GetQuery("view") == NULL) {
            tmp.append(prof_name);
            tmp.append("[addToProfEnd]");
        }
        if (prof_result.cut_until(&before_label, ",label=\"") == 0) {
            tmp.append(before_label);
            tmp.append(",label=\"[");
            tmp.append(GetBaseName(prof_name));
            if (base_name) {
                tmp.append(" - ");
                tmp.append(GetBaseName(base_name));
            }
            tmp.append("]\\l");
            tmp.append(prof_result);
            tmp.swap(prof_result);
        } else {
            // Assume it's text. append before result directly.
            tmp.append("[");
            tmp.append(GetBaseName(prof_name));
            if (base_name) {
                tmp.append(" - ");
                tmp.append(GetBaseName(base_name));
            }
            tmp.append("]\n");
            tmp.append(prof_result);
            tmp.swap(prof_result);
        }
        
        if (!WriteSmallFile(result_name, prof_result)) {
            LOG(ERROR) << "Fail to write " << result_name;
            CHECK(butil::DeleteFile(butil::FilePath(result_name), false));
        }
        break;
    }
    CHECK(!use_html);
    // NOTE: not send prof_result to os first which does copying.
    os.move_to(resp);
    if (use_html) {
        resp.append("<pre>");
    }
    resp.append(prof_result);
    if (use_html) {
        resp.append("</pre></body></html>");
    }
}

static void DoProfiling(ProfilingType type,
                        ::google::protobuf::RpcController* cntl_base,
                        ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    butil::IOBuf& resp = cntl->response_attachment();
    const bool use_html = UseHTML(cntl->http_request());
    cntl->http_response().set_content_type(use_html ? "text/html" : "text/plain");

    butil::IOBufBuilder os;
    if (use_html) {
        os << "<!DOCTYPE html><html><head>\n"
            "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\" />\n"
            "<script language=\"javascript\" type=\"text/javascript\" src=\"/js/jquery_min\"></script>\n"
            << TabsHead()
            << "<style type=\"text/css\">\n"
            ".logo {position: fixed; bottom: 0px; right: 0px; }\n"
            ".logo_text {color: #B0B0B0; }\n"
            "</style>\n"
            "</head>\n"
            "<body>\n";
        cntl->server()->PrintTabsBody(os, ProfilingType2String(type));
    }

    const std::string* view = cntl->http_request().uri().GetQuery("view");
    if (view) {
        if (!ValidProfilePath(*view)) {
            return cntl->SetFailed(EINVAL, "Invalid query `view'");
        }
        if (!butil::PathExists(butil::FilePath(*view))) {
            return cntl->SetFailed(
                EINVAL, "The profile denoted by `view' does not exist");
        }
        DisplayResult(cntl, done_guard.release(), view->c_str(), os.buf());
        return;
    }

    const int seconds = ReadSeconds(cntl);
    if ((type == PROFILING_CPU || type == PROFILING_CONTENTION)) {
        if (seconds < 0) {
            os << "Invalid seconds" << (use_html ? "</body></html>" : "\n");
            os.move_to(cntl->response_attachment());
            cntl->http_response().set_status_code(HTTP_STATUS_BAD_REQUEST);
            return;
        }
    }

    // Log requester
    std::ostringstream client_info;
    client_info << cntl->remote_side();
    if (cntl->auth_context()) {
        client_info << "(auth=" << cntl->auth_context()->user() << ')';
    } else {
        client_info << "(no auth)";
    }
    client_info << " requests for profiling " << ProfilingType2String(type);
    if (type == PROFILING_CPU || type == PROFILING_CONTENTION) {
        LOG(INFO) << client_info.str() << " for " << seconds << " seconds";
    } else {
        LOG(INFO) << client_info.str();
    }
    int64_t prof_id = 0;
    const std::string* prof_id_str =
        cntl->http_request().uri().GetQuery("profiling_id");
    if (prof_id_str != NULL) {
        char* endptr = NULL;
        prof_id = strtoll(prof_id_str->c_str(), &endptr, 10);
        LOG_IF(ERROR, *endptr != '\0') << "Invalid profiling_id=" << prof_id;
    }

    {
        BAIDU_SCOPED_LOCK(g_env[type].mutex);
        if (g_env[type].client) {
            if (NULL == g_env[type].waiters) {
                g_env[type].waiters = new std::vector<ProfilingWaiter>;
            }
            ProfilingWaiter waiter = { cntl, done_guard.release() };
            g_env[type].waiters->push_back(waiter);
            RPC_VLOG << "Queue request from " << cntl->remote_side();
            return;
        }
        if (g_env[type].cached_result != NULL &&
            g_env[type].cached_result->id == prof_id) {
            cntl->http_response().set_status_code(
                g_env[type].cached_result->status_code);
            cntl->response_attachment().append(
                g_env[type].cached_result->result);
            RPC_VLOG << "Hit cached result, id=" << prof_id;
            return;
        }
        CHECK(NULL == g_env[type].client);
        g_env[type].client = new ProfilingClient;
        g_env[type].client->end_us = butil::cpuwide_time_us() + seconds * 1000000L;
        g_env[type].client->seconds = seconds;
        // This id work arounds an issue of chrome (or jquery under chrome) that
        // the ajax call in another tab may be delayed until ajax call in
        // current tab finishes. We assign a increasing-only id to each
        // profiling and save last profiling result along with the assigned id.
        // If the delay happens, the viewr should send the ajax call with an
        // id matching the id in cached result, then the result will be returned
        // directly instead of running another profiling which may take long
        // time.
        if (0 == ++ g_env[type].cur_id) { // skip 0
            ++ g_env[type].cur_id;
        }
        g_env[type].client->id = g_env[type].cur_id;
        g_env[type].client->point = cntl->remote_side();
    }

    RPC_VLOG << "Apply request from " << cntl->remote_side();

    char prof_name[128];
    if (MakeProfName(type, prof_name, sizeof(prof_name)) != 0) {
        os << "Fail to create prof name: " << berror()
           << (use_html ? "</body></html>" : "\n");
        os.move_to(resp);
        cntl->http_response().set_status_code(HTTP_STATUS_INTERNAL_SERVER_ERROR);
        return NotifyWaiters(type, cntl, view);
    }

#if defined(OS_MACOSX)
    if (!has_GOOGLE_PPROF_BINARY_PATH()) {
        os << "no GOOGLE_PPROF_BINARY_PATH in env"
           << (use_html ? "</body></html>" : "\n");
        os.move_to(resp);
        cntl->http_response().set_status_code(HTTP_STATUS_FORBIDDEN);
        return NotifyWaiters(type, cntl, view);
    }
#endif
    if (type == PROFILING_CPU) {
        if ((void*)ProfilerStart == NULL || (void*)ProfilerStop == NULL) {
            os << "CPU profiler is not enabled"
               << (use_html ? "</body></html>" : "\n");
            os.move_to(resp);
            cntl->http_response().set_status_code(HTTP_STATUS_FORBIDDEN);
            return NotifyWaiters(type, cntl, view);
        }
        butil::File::Error error;
        const butil::FilePath dir = butil::FilePath(prof_name).DirName();
        if (!butil::CreateDirectoryAndGetError(dir, &error)) {
            os << "Fail to create directory=`" << dir.value() << ", "
               << error << (use_html ? "</body></html>" : "\n");
            os.move_to(resp);
            cntl->http_response().set_status_code(
                HTTP_STATUS_INTERNAL_SERVER_ERROR);
            return NotifyWaiters(type, cntl, view);
        }
        if (!ProfilerStart(prof_name)) {
            os << "Another profiler (not via /hotspots/cpu) is running, "
                "try again later" << (use_html ? "</body></html>" : "\n");
            os.move_to(resp);
            cntl->http_response().set_status_code(HTTP_STATUS_SERVICE_UNAVAILABLE);
            return NotifyWaiters(type, cntl, view);
        }
        if (bthread_usleep(seconds * 1000000L) != 0) {
            PLOG(WARNING) << "Profiling has been interrupted";
        }
        ProfilerStop();
    } else if (type == PROFILING_CONTENTION) {
        if (!bthread::ContentionProfilerStart(prof_name)) {
            os << "Another profiler (not via /hotspots/contention) is running, "
                "try again later" << (use_html ? "</body></html>" : "\n");
            os.move_to(resp);
            cntl->http_response().set_status_code(HTTP_STATUS_SERVICE_UNAVAILABLE);
            return NotifyWaiters(type, cntl, view);
        }
        if (bthread_usleep(seconds * 1000000L) != 0) {
            PLOG(WARNING) << "Profiling has been interrupted";
        }
        bthread::ContentionProfilerStop();
    } else if (type == PROFILING_HEAP) {
        MallocExtension* malloc_ext = MallocExtension::instance();
        if (malloc_ext == NULL || !has_TCMALLOC_SAMPLE_PARAMETER()) {
            os << "Heap profiler is not enabled";
            if (malloc_ext != NULL) {
                os << " (no TCMALLOC_SAMPLE_PARAMETER in env)";
            }
            os << '.' << (use_html ? "</body></html>" : "\n");
            os.move_to(resp);
            cntl->http_response().set_status_code(HTTP_STATUS_FORBIDDEN);
            return NotifyWaiters(type, cntl, view);
        }
        std::string obj;
        malloc_ext->GetHeapSample(&obj);
        if (!WriteSmallFile(prof_name, obj)) {
            os << "Fail to write " << prof_name
               << (use_html ? "</body></html>" : "\n");
            os.move_to(resp);
            cntl->http_response().set_status_code(
                HTTP_STATUS_INTERNAL_SERVER_ERROR);
            return NotifyWaiters(type, cntl, view);
        }
    } else if (type == PROFILING_GROWTH) {
        MallocExtension* malloc_ext = MallocExtension::instance();
        if (malloc_ext == NULL) {
            os << "Growth profiler is not enabled."
               << (use_html ? "</body></html>" : "\n");
            os.move_to(resp);
            cntl->http_response().set_status_code(HTTP_STATUS_FORBIDDEN);
            return NotifyWaiters(type, cntl, view);
        }
        std::string obj;
        malloc_ext->GetHeapGrowthStacks(&obj);
        if (!WriteSmallFile(prof_name, obj)) {
            os << "Fail to write " << prof_name
               << (use_html ? "</body></html>" : "\n");
            os.move_to(resp);
            cntl->http_response().set_status_code(
                HTTP_STATUS_INTERNAL_SERVER_ERROR);
            return NotifyWaiters(type, cntl, view);
        }
    } else {
        os << "Unknown ProfilingType=" << type
           << (use_html ? "</body></html>" : "\n");
        os.move_to(resp);
        cntl->http_response().set_status_code(
            HTTP_STATUS_INTERNAL_SERVER_ERROR);
        return NotifyWaiters(type, cntl, view);
    }

    std::vector<ProfilingWaiter> waiters;
    // NOTE: Must be called before DisplayResult which calls done->Run() and
    // deletes cntl.
    ConsumeWaiters(type, cntl, &waiters);    
    DisplayResult(cntl, done_guard.release(), prof_name, os.buf());

    for (size_t i = 0; i < waiters.size(); ++i) {
        DisplayResult(waiters[i].cntl, waiters[i].done, prof_name, os.buf());
    }
}

static void StartProfiling(ProfilingType type,
                           ::google::protobuf::RpcController* cntl_base,
                           ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    butil::IOBuf& resp = cntl->response_attachment();
    const bool use_html = UseHTML(cntl->http_request());
    butil::IOBufBuilder os;
    bool enabled = false;
    const char* extra_desc = "";
    if (type == PROFILING_CPU) {
        enabled = cpu_profiler_enabled;
    } else if (type == PROFILING_CONTENTION) {
        enabled = true;
    } else if (type == PROFILING_HEAP) {
        enabled = IsHeapProfilerEnabled();
        if (enabled && !has_TCMALLOC_SAMPLE_PARAMETER()) {
            enabled = false;
            extra_desc = " (no TCMALLOC_SAMPLE_PARAMETER in env)";
        }
    } else if (type == PROFILING_GROWTH) {
        enabled = IsHeapProfilerEnabled();
    }
    const char* const type_str = ProfilingType2String(type);

#if defined(OS_MACOSX)
    if (!has_GOOGLE_PPROF_BINARY_PATH()) {
        enabled = false;
        extra_desc = "(no GOOGLE_PPROF_BINARY_PATH in env)";
    }
#endif
    
    if (!use_html) {
        if (!enabled) {
            os << "Error: " << type_str << " profiler is not enabled."
               << extra_desc << "\n"
                "Read the docs: docs/cn/{cpu_profiler.md,heap_profiler.md}\n";
            os.move_to(cntl->response_attachment());
            cntl->http_response().set_status_code(HTTP_STATUS_FORBIDDEN);
            return;
        }
        // Console can only use non-responsive version, namely the curl
        // blocks until profiling is done.
        return DoProfiling(type, cntl, done_guard.release());
    }

    const int seconds = ReadSeconds(cntl);
    const std::string* view = cntl->http_request().uri().GetQuery("view");
    const bool show_ccount = cntl->http_request().uri().GetQuery("ccount");
    const std::string* base_name = cntl->http_request().uri().GetQuery("base");
    const std::string* display_type_query = cntl->http_request().uri().GetQuery("display_type");
    DisplayType display_type = DisplayType::kDot;
    if (display_type_query) {
        display_type = StringToDisplayType(*display_type_query);
        if (display_type == DisplayType::kUnknown) {
            return cntl->SetFailed(EINVAL, "Invalid display_type=%s", display_type_query->c_str());
        }
    }

    ProfilingClient profiling_client;
    size_t nwaiters = 0;
    ProfilingEnvironment & env = g_env[type];
    if (view == NULL) {
        BAIDU_SCOPED_LOCK(env.mutex);
        if (env.client) {
            profiling_client = *env.client;
            nwaiters = (env.waiters ? env.waiters->size() : 0);
        }
    }
    
    cntl->http_response().set_content_type("text/html");
    os << "<!DOCTYPE html><html><head>\n"
        "<script language=\"javascript\" type=\"text/javascript\""
        " src=\"/js/jquery_min\"></script>\n"
       << TabsHead()
       << "<style type=\"text/css\">\n"
        ".logo {position: fixed; bottom: 0px; right: 0px; }\n"
        ".logo_text {color: #B0B0B0; }\n"
        " </style>\n"
        "<script type=\"text/javascript\">\n"
        "function generateURL() {\n"
        "  var past_prof = document.getElementById('view_prof').value;\n"
        "  var base_prof = document.getElementById('base_prof').value;\n"
        "  var display_type = document.getElementById('display_type').value;\n";
    if (type == PROFILING_CONTENTION) {
        os << "  var show_ccount = document.getElementById('ccount_cb').checked;\n";
    }
    os << "  var targetURL = '/hotspots/" << type_str << "';\n"
        "  targetURL += '?display_type=' + display_type;\n"
        "  if (past_prof != '') {\n"
        "    targetURL += '&view=' + past_prof;\n"
        "  }\n"
        "  if (base_prof != '') {\n"
        "    targetURL += '&base=' + base_prof;\n"
        "  }\n";
    if (type == PROFILING_CONTENTION) {
        os <<
        "  if (show_ccount) {\n"
        "    targetURL += '&ccount';\n"
        "  }\n";
    }
    os << "  return targetURL;\n"
        "}\n"
        "$(function() {\n"
        "  function onDataReceived(data) {\n";
    if (view == NULL) {
        os <<
        "    var selEnd = data.indexOf('[addToProfEnd]');\n"
        "    if (selEnd != -1) {\n"
        "      var sel = document.getElementById('view_prof');\n"
        "      var option = document.createElement('option');\n"
        "      option.value = data.substring(0, selEnd);\n"
        "      option.text = option.value;\n"
        "      var slash_index = option.value.lastIndexOf('/');\n"
        "      if (slash_index != -1) {\n"
        "        option.text = option.value.substring(slash_index + 1);\n"
        "      }\n"
        "      var option1 = sel.options[1];\n"
        "      if (option1 == null || option1.text != option.text) {\n"
        "        sel.add(option, 1);\n"
        "      } else if (option1 != null) {\n"
        "        console.log('merged ' + option.text);\n"
        "      }\n"
        "      sel.selectedIndex = 1;\n"
        "      window.history.pushState('', '', generateURL());\n"
        "      data = data.substring(selEnd + '[addToProfEnd]'.length);\n"
        "   }\n";
    }
    os <<
        "    var index = data.indexOf('digraph ');\n"
        "    if (index == -1) {\n"
        "      var selEnd = data.indexOf('[addToProfEnd]');\n"
        "      if (selEnd != -1) {\n"
        "        data = data.substring(selEnd + '[addToProfEnd]'.length);\n"
        "      }\n"
        "      $(\"#profiling-result\").html('<pre>' + data + '</pre>');\n"
        "      if (data.indexOf('FlameGraph') != -1) { init(); }"
        "    } else {\n"
        "      $(\"#profiling-result\").html('Plotting ...');\n"
        "      var svg = Viz(data.substring(index), \"svg\");\n"
        "      $(\"#profiling-result\").html(svg);\n"
        "    }\n"
        "  }\n"
        "  function onErrorReceived(xhr, ajaxOptions, thrownError) {\n"
        "    $(\"#profiling-result\").html(xhr.responseText);\n"
        "  }\n"
        "  $.ajax({\n"
        "    url: \"/hotspots/" << type_str << "_non_responsive?console=1";
    if (type == PROFILING_CPU || type == PROFILING_CONTENTION) {
        os << "&seconds=" << seconds;
    }
    if (profiling_client.id != 0) {
        os << "&profiling_id=" << profiling_client.id;
    }
    os << "&display_type=" << DisplayTypeToString(display_type);
    if (show_ccount) {
        os << "&ccount";
    }
    if (view) {
        os << "&view=" << *view;
    }
    if (base_name) {
        os << "&base=" << *base_name;
    }
    os << "\",\n"
        "    type: \"GET\",\n"
        "    dataType: \"html\",\n"
        "    success: onDataReceived,\n"
        "    error: onErrorReceived\n"
        "  });\n"
        "});\n"
        "function onSelectProf() {\n"
        "  window.location.href = generateURL();\n"
        "}\n"
        "function onChangedCB(cb) {\n"
        "  onSelectProf();\n"
        "}\n"
        "</script>\n"
        "</head>\n"
        "<body>\n";
    cntl->server()->PrintTabsBody(os, type_str);

    TRACEPRINTF("Begin to enumerate profiles");
    std::vector<std::string> past_profs;
    butil::FilePath prof_dir(FLAGS_rpc_profiling_dir);
    prof_dir = prof_dir.Append(GetProgramChecksum());
    std::string file_pattern;
    file_pattern.reserve(15);
    file_pattern.append("*.");
    file_pattern.append(type_str);
    butil::FileEnumerator prof_enum(prof_dir, false/*non recursive*/,
                                   butil::FileEnumerator::FILES,
                                   file_pattern);
    std::string file_path;
    for (butil::FilePath name = prof_enum.Next(); !name.empty();
         name = prof_enum.Next()) {
        // NOTE: name already includes dir.
        if (past_profs.empty()) {
            past_profs.reserve(16);
        }
        past_profs.push_back(name.value());
    }
    if (!past_profs.empty()) {
        TRACEPRINTF("Sort %lu profiles in decending order", past_profs.size());
        std::sort(past_profs.begin(), past_profs.end(), std::greater<std::string>());
        int max_profiles = FLAGS_max_profiles_kept/*may be reloaded*/;
        if (max_profiles < 0) {
            max_profiles = 0;
        }
        if (past_profs.size() > (size_t)max_profiles) {
            TRACEPRINTF("Remove %lu profiles",
                        past_profs.size() - (size_t)max_profiles);
            for (size_t i = max_profiles; i < past_profs.size(); ++i) {
                CHECK(butil::DeleteFile(butil::FilePath(past_profs[i]), false));
                std::string cache_path;
                cache_path.reserve(past_profs[i].size() + 7);
                cache_path += past_profs[i];
                cache_path += ".cache";
                CHECK(butil::DeleteFile(butil::FilePath(cache_path), true));
            }
            past_profs.resize(max_profiles);
        }
    }
    TRACEPRINTF("End enumeration");

    os << "<pre style='display:inline'>View: </pre>"
        "<select id='view_prof' onchange='onSelectProf()'>";
    os << "<option value=''>&lt;new profile&gt;</option>";
    for (size_t i = 0; i < past_profs.size(); ++i) {
        os << "<option value='" << past_profs[i] << "' ";
        if (view != NULL && past_profs[i] == *view) {
            os << "selected";
        }
        os << '>' << GetBaseName(&past_profs[i]);
    }
    os << "</select>";
    os << "<div><pre style='display:inline'>Display: </pre>"
        "<select id='display_type' onchange='onSelectProf()'>"
        "<option value=dot" << (display_type == DisplayType::kDot ? " selected" : "") << ">dot</option>"
#if defined(OS_LINUX)
        "<option value=flame" << (display_type == DisplayType::kFlameGraph ? " selected" : "") << ">flame</option>"
#endif
        "<option value=text" << (display_type == DisplayType::kText ? " selected" : "") << ">text</option></select>";
    if (type == PROFILING_CONTENTION) {
        os << "&nbsp;&nbsp;&nbsp;<label for='ccount_cb'>"
            "<input id='ccount_cb' type='checkbox'"
           << (show_ccount ? " checked=''" : "") <<
            " onclick='onChangedCB(this);'>count</label>";
    }
    os << "</div><div><pre style='display:inline'>Diff: </pre>"
        "<select id='base_prof' onchange='onSelectProf()'>"
        "<option value=''>&lt;none&gt;</option>";
    for (size_t i = 0; i < past_profs.size(); ++i) {
        os << "<option value='" << past_profs[i] << "' ";
        if (base_name != NULL && past_profs[i] == *base_name) {
            os << "selected";
        }
        os << '>' << GetBaseName(&past_profs[i]);
    }
    os << "</select></div>";
    
    if (!enabled && view == NULL) {
        os << "<p><span style='color:red'>Error:</span> "
           << type_str << " profiler is not enabled." << extra_desc << "</p>"
            "<p>To enable all profilers, link tcmalloc and define macros BRPC_ENABLE_CPU_PROFILER"
            "</p><p>Or read docs: <a href='https://github.com/brpc/brpc/blob/master/docs/cn/cpu_profiler.md'>cpu_profiler</a>"
            " and <a href='https://github.com/brpc/brpc/blob/master/docs/cn/heap_profiler.md'>heap_profiler</a>"
            "</p></body></html>";
        os.move_to(cntl->response_attachment());
        cntl->http_response().set_status_code(HTTP_STATUS_FORBIDDEN);
        return;
    }

    if ((type == PROFILING_CPU || type == PROFILING_CONTENTION) && view == NULL) {
        if (seconds < 0) {
            os << "Invalid seconds</body></html>";
            os.move_to(cntl->response_attachment());
            cntl->http_response().set_status_code(HTTP_STATUS_BAD_REQUEST);
            return;
        }
    }

    if (nwaiters >= CONCURRENT_PROFILING_LIMIT) {
        os << "Your profiling request is rejected because of "
            "too many concurrent profiling requests</body></html>";
        os.move_to(cntl->response_attachment());
        cntl->http_response().set_status_code(HTTP_STATUS_SERVICE_UNAVAILABLE);
        return;
    }

    os << "<div id=\"profiling-result\">";
    if (profiling_client.seconds != 0) {
        const int wait_seconds =
            (int)ceil((profiling_client.end_us - butil::cpuwide_time_us())
                      / 1000000.0);
        os << "Your request is merged with the request from "
           << profiling_client.point;
        if (type == PROFILING_CPU || type == PROFILING_CONTENTION) {
            os << ", showing in about " << wait_seconds << " seconds ...";
        }
    } else {
        if ((type == PROFILING_CPU || type == PROFILING_CONTENTION) && view == NULL) {
            os << "Profiling " << ProfilingType2String(type) << " for "
               << seconds << " seconds ...";
        } else {
            os << "Generating " << type_str << " profile ...";
        }
    }
    os << "</div><pre class='logo'><span class='logo_text'>" << logo()
       << "</span></pre></body>\n";
    if (display_type == DisplayType::kDot) {
        // don't need viz.js in text mode.
        os << "<script language=\"javascript\" type=\"text/javascript\""
            " src=\"/js/viz_min\"></script>\n";
    }
    os << "</html>";
    os.move_to(resp);
}

void HotspotsService::cpu(
    ::google::protobuf::RpcController* cntl_base,
    const ::brpc::HotspotsRequest*,
    ::brpc::HotspotsResponse*,
    ::google::protobuf::Closure* done) {
    return StartProfiling(PROFILING_CPU, cntl_base, done);
}

void HotspotsService::heap(
    ::google::protobuf::RpcController* cntl_base,
    const ::brpc::HotspotsRequest*,
    ::brpc::HotspotsResponse*,
    ::google::protobuf::Closure* done) {
    return StartProfiling(PROFILING_HEAP, cntl_base, done);
}

void HotspotsService::growth(
    ::google::protobuf::RpcController* cntl_base,
    const ::brpc::HotspotsRequest*,
    ::brpc::HotspotsResponse*,
    ::google::protobuf::Closure* done) {
    return StartProfiling(PROFILING_GROWTH, cntl_base, done);
}

void HotspotsService::contention(
    ::google::protobuf::RpcController* cntl_base,
    const ::brpc::HotspotsRequest*,
    ::brpc::HotspotsResponse*,
    ::google::protobuf::Closure* done) {
    return StartProfiling(PROFILING_CONTENTION, cntl_base, done);
}

void HotspotsService::cpu_non_responsive(
    ::google::protobuf::RpcController* cntl_base,
    const ::brpc::HotspotsRequest*,
    ::brpc::HotspotsResponse*,
    ::google::protobuf::Closure* done) {
    return DoProfiling(PROFILING_CPU, cntl_base, done);
}

void HotspotsService::heap_non_responsive(
    ::google::protobuf::RpcController* cntl_base,
    const ::brpc::HotspotsRequest*,
    ::brpc::HotspotsResponse*,
    ::google::protobuf::Closure* done) {
    return DoProfiling(PROFILING_HEAP, cntl_base, done);
}

void HotspotsService::growth_non_responsive(
    ::google::protobuf::RpcController* cntl_base,
    const ::brpc::HotspotsRequest*,
    ::brpc::HotspotsResponse*,
    ::google::protobuf::Closure* done) {
    return DoProfiling(PROFILING_GROWTH, cntl_base, done);
}

void HotspotsService::contention_non_responsive(
    ::google::protobuf::RpcController* cntl_base,
    const ::brpc::HotspotsRequest*,
    ::brpc::HotspotsResponse*,
    ::google::protobuf::Closure* done) {
    return DoProfiling(PROFILING_CONTENTION, cntl_base, done);
}

void HotspotsService::GetTabInfo(TabInfoList* info_list) const {
    TabInfo* info = info_list->add();
    info->path = "/hotspots/cpu";
    info->tab_name = "cpu";
    info = info_list->add();
    info->path = "/hotspots/heap";
    info->tab_name = "heap";
    info = info_list->add();
    info->path = "/hotspots/growth";
    info->tab_name = "growth";
    info = info_list->add();
    info->path = "/hotspots/contention";
    info->tab_name = "contention";
}

} // namespace brpc
