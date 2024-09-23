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


#include "brpc/controller.h"
#include "brpc/http_header.h"
#include "brpc/reloadable_flags.h"
#include "brpc/uri.h"
#include "butil/files/file_path.h"
#include "butil/iobuf.h"
#include "butil/popen.h"
#include "gflags/gflags.h"
#include "gflags/gflags_declare.h"
#include <brpc/details/jemalloc_profiler.h>
#include <brpc/builtin/common.h>
#include <butil/file_util.h>
#include <butil/process_util.h>
#include <bthread/bthread.h>
#include <cerrno>
#include <cstdlib>

extern "C" {
// weak symbol: resolved at runtime by the linker if we are using jemalloc, nullptr otherwise
int BAIDU_WEAK mallctl(const char*, void*, size_t*, void*, size_t);
void BAIDU_WEAK malloc_stats_print(void (*write_cb)(void *, const char *), void *cbopaque, const char *opts);
}

namespace brpc {

// https://jemalloc.net/jemalloc.3.html
DEFINE_bool(je_prof_active, false, "control jemalloc prof.active, jemalloc profiling enabled but inactive,"
            "it toggle profiling at any time during process running");
DEFINE_int32(je_prof_dump, 0, "control jemalloc prof.dump, change this only dump profile");
DEFINE_int32(je_prof_reset, 19, "control jemalloc prof.reset, reset all memory profile statistics, "
             "and optionally update the sample rate, default 2^19 B");

DECLARE_int32(max_flame_graph_width);

// define in src/brpc/builtin/pprof_service.cpp
extern int MakeProfName(ProfilingType type, char* buf, size_t buf_len);

static bool HasInit(const std::string& fn) {
    static std::set<std::string> fns;
    if (fns.count(fn) > 0) {
        return true;
    }
    fns.insert(fn);
    return false;
}

bool HasJemalloc() {
    return mallctl != nullptr;
}

// env need MALLOC_CONF="prof:true" before process start
static bool HasEnableJemallocProfile() {
    bool prof = false;
    size_t len = sizeof(prof);
    int ret = mallctl("opt.prof", &prof, &len, nullptr, 0);
    if (ret != 0) {
        LOG(WARNING) << "mallctl get opt.prof err, ret:" << ret;
        return false;
    }
    return prof;
}

static void WriteCb(void *opaque, const char *str) {
    // maybe call n times WriteCb by single malloc_stats_print
    static_cast<std::string*>(opaque)->append(str);
}

std::string StatsPrint(const std::string& opts) {
    if (malloc_stats_print == nullptr) {
        return "your jemalloc no support malloc_stats_print";
    }

    std::string stat_str;
    malloc_stats_print(WriteCb, &stat_str, opts.c_str());
    return stat_str;
}

static int JeProfileActive(bool active) {
    if (!HasJemalloc()) {
        LOG(WARNING) << "no jemalloc";
        return -1;
    }

    if (!HasEnableJemallocProfile()) {
        LOG(WARNING) << "jemalloc have not set opt.prof before start";
        return -1;
    }

    int ret = mallctl("prof.active", nullptr, nullptr, &active, sizeof(active));
    if (ret != 0) {
        LOG(WARNING) << "mallctl set prof.active:" << active << " err, ret:" << ret;
        return ret;
    }
    LOG(INFO) << "mallctl set prof.active:" << active << " succ";
    return 0;
}

static std::string JeProfileDump() {
    if (!HasJemalloc()) {
        LOG(WARNING) << "no jemalloc";
        return "";
    }

    if (!HasEnableJemallocProfile()) {
        LOG(WARNING) << "jemalloc have not set opt.prof before start";
        return "";
    }

    char prof_name[256];
    if (MakeProfName(PROFILING_HEAP, prof_name, sizeof(prof_name)) != 0) {
        LOG(WARNING) << "Fail to create .prof file, " << berror();
        return "";
    }
    butil::File::Error error;
    const butil::FilePath dir = butil::FilePath(prof_name).DirName();
    if (!butil::CreateDirectoryAndGetError(dir, &error)) {
        LOG(WARNING) << "Fail to create directory= " << dir.value();
        return "";
    }

    const char* p_prof_name = prof_name;
    int ret = mallctl("prof.dump", NULL, NULL, (void*)&p_prof_name, sizeof(p_prof_name));
    if (ret != 0) {
        LOG(WARNING) << "mallctl set prof.dump:" << p_prof_name << " err, ret:" << ret;
        return "";
    }
    LOG(INFO) << "heap profile dump:" << prof_name << " succ";
    return prof_name;
}

static int JeProfileReset(size_t lg_sample) {
    if (!HasJemalloc()) {
        LOG(WARNING) << "no jemalloc";
        return -1;
    }

    if (!HasEnableJemallocProfile()) {
        LOG(WARNING) << "jemalloc have not set opt.prof before start";
        return -1;
    }

    int ret = mallctl("prof.reset", nullptr, nullptr, &lg_sample, sizeof(lg_sample));
    if (ret != 0) {
        LOG(WARNING) << "mallctl set prof.reset:" << lg_sample << " err, ret:" << ret;
        return ret;
    }
    LOG(INFO) << "mallctl set prof.reset:" << lg_sample << " succ";

    FLAGS_je_prof_active = false;
    if (FLAGS_je_prof_active) {
        LOG(WARNING) << "reset FLAGS_je_prof_active fail";
        return -1;
    }

    return 0;
}

void JeControlProfile(Controller* cntl) {
    const brpc::URI& uri = cntl->http_request().uri();
    // http:ip:port/pprof/heap?display=(text|svg|stats|flamegraph)
    const std::string* uri_display = uri.GetQuery("display");

    butil::IOBuf& buf = cntl->response_attachment();
    cntl->http_response().set_content_type("text/plain");

    // support ip:port/pprof/heap?display=stats
    if (uri_display != nullptr && *uri_display == "stats") {
        const std::string* uri_opts = uri.GetQuery("opts");
        std::string opts = !uri_opts || uri_opts->empty() ? "Ja" : *uri_opts;
        buf.append(StatsPrint(opts));
        return;
    }

    if (!HasEnableJemallocProfile()) {
        cntl->SetFailed(ENOMETHOD, "Heap profiler is not enabled, (no MALLOC_CONF=prof:true in env)");
        return;
    }

    // only dump profile
    const std::string prof_name = JeProfileDump();
    if (prof_name.empty()) {
        cntl->SetFailed(-1, "Fail to dump profile");
        buf.append("\nFail to dump profile");
        return;
    }

    // support jeprof ip:port/pprof/heap
    if (uri_display == nullptr || uri_display->empty()) {
        std::string content;
        if (!butil::ReadFileToString(butil::FilePath(prof_name), &content)) {
            LOG(WARNING) << "read " << prof_name << " fail";
            return;
        }
        buf.append(content);
        return;
    }

    // support curl/browser
    buf.append(prof_name);

    std::string jeprof;
    const char* f_jeprof = std::getenv("JEPROF_FILE");
    if (f_jeprof != nullptr && butil::PathExists(butil::FilePath(f_jeprof))) {
        jeprof = f_jeprof;
    } else {
        LOG(WARNING) << "env JEPROF_FILE invalid";
        buf.append("\nenv JEPROF_FILE invalid");
        jeprof = "jeprof "; // use PATH
    }
    
    char process_path[500] = {};
    ssize_t len = butil::GetProcessAbsolutePath(process_path, sizeof(process_path));
    if (len == -1) {
        LOG(WARNING) << "GetProcessAbsolutePath of process err";
        buf.append("\nGetProcessAbsolutePath of process err");
        return;
    }
    const std::string process_file(process_path, len);

    std::string cmd_str = jeprof + " " + process_file + " " + prof_name;
    bool display_img = false;
    if (*uri_display == "svg") {
        cmd_str += " --svg ";
        display_img = true;
    } else if (*uri_display == "flamegraph") {
        const char* flamegraph_tool = getenv("FLAMEGRAPH_PL_PATH");
        if (!flamegraph_tool) {
            LOG(WARNING) << " display: " << *uri_display << " invalid, env FLAMEGRAPH_PL_PATH invalid";
            buf.append("\ndisplay:" + *uri_display + " invalid, env FLAMEGRAPH_PL_PATH invalid"); 
            return;
        }
        const int width_size = FLAGS_max_flame_graph_width > 0 ? FLAGS_max_flame_graph_width : 1200;
        cmd_str += " --collapsed | " + std::string(flamegraph_tool) + " --colors mem --width " + std::to_string(width_size);
        display_img = true;
    } else if (*uri_display == "text") {
        cmd_str += " --text ";
    } else {
        LOG(WARNING) << " display: " << *uri_display << " invalid";
        buf.append("\ndisplay:" + *uri_display + " invalid");
        return;
    }
    butil::IOBufBuilder builder;
    if (butil::read_command_output(builder, cmd_str.c_str()) != 0) {
        buf.append("\nread_command_output <" + cmd_str + "> fail");
        LOG(WARNING) << "read_command_output <" + cmd_str + "> fail";
        return;
    }

    if (display_img) {
        buf.swap(builder.buf());
        cntl->http_response().set_content_type("image/svg+xml");
    } else {
        buf.append("\ncmd: " + cmd_str + "\n");
        buf.append(builder.buf());
    }
}

static bool validate_je_prof_active(const char*, bool enable) {
    if (!HasJemalloc()) {
        return true;
    }

    if (!HasInit(__func__)) {
        return true;
    }

    if (JeProfileActive(enable) != 0) {
        LOG(WARNING) << "JeControlSample err";
        return false;
    }

    return true;
}

static bool validate_je_prof_dump(const char*, int32_t val) {
    if (!HasJemalloc()) {
        return true;
    }
    if (!HasInit(__func__)) {
        return true;
    }

    const std::string prof_name = JeProfileDump();
    if (prof_name.empty()) {
        LOG(WARNING) << "Fail to dump profile";
        return false;
    }
    return true;
}

static bool validate_je_prof_reset(const char*, int32_t val) {
    if (!HasJemalloc()) {
        return true;
    }
    if (!HasInit(__func__)) {
        return true;
    }

    if (JeProfileReset(val) != 0) {
        LOG(WARNING) << "JeProfileReset err";
        return false;
    }

    return true;
}

// e.g: curl ip:port/flags/je_prof_active?setvalue=true or update flags in web
BRPC_VALIDATE_GFLAG(je_prof_active, validate_je_prof_active);
BRPC_VALIDATE_GFLAG(je_prof_dump, validate_je_prof_dump);
BRPC_VALIDATE_GFLAG(je_prof_reset, validate_je_prof_reset);

}

