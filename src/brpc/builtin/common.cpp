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

// Authors: Ge,Jun (gejun@baidu.com)

#include <iomanip>
#include <sys/time.h>
#include <fcntl.h>                           // O_RDONLY
#include <gflags/gflags.h>
#include "butil/logging.h"
#include "butil/fd_guard.h"                  // fd_guard
#include "butil/file_util.h"                 // butil::FilePath
#include "butil/third_party/murmurhash3/murmurhash3.h"
#include "butil/process_util.h"              // ReadCommandLine
#include "brpc/server.h"
#include "brpc/builtin/common.h"

namespace brpc {

DEFINE_string(rpc_profiling_dir, "./rpc_data/profiling",
              "For storing profiling results.");

bool UseHTML(const HttpHeader& header) {
    const std::string* console = header.uri().GetQuery(CONSOLE_STR);
    if (console != NULL) {
        return atoi(console->c_str()) == 0;
    }
    // [curl header]
    // User-Agent: curl/7.12.1 (x86_64-redhat-linux-gnu) libcurl/7.12.1 ...
    const std::string* agent = header.GetHeader(USER_AGENT_STR);
    if (agent == NULL) {  // use text when user-agent is absent
        return false;
    }
    return agent->find("curl/") == std::string::npos;
}

// Written by Jack Handy
// <A href="mailto:jakkhandy@hotmail.com">jakkhandy@hotmail.com</A>
inline bool url_wildcmp(const char* wild, const char* str) {
    const char* cp = NULL;
    const char* mp = NULL;

    while (*str && *wild != '*') {
        if (*wild != *str && *wild != '$') {
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
        } else if (*wild == *str || *wild == '$') {
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

bool MatchAnyWildcard(const std::string& name,
                      const std::vector<std::string>& wildcards) {
    for (size_t i = 0; i < wildcards.size(); ++i) {
        if (url_wildcmp(wildcards[i].c_str(), name.c_str())) {
            return true;
        }
    }
    return false;
}

void PrintRealDateTime(std::ostream& os, int64_t tm) {
    char buf[32];
    const time_t tm_s = tm / 1000000L;
    struct tm lt;
    strftime(buf, sizeof(buf), "%Y/%m/%d-%H:%M:%S.", localtime_r(&tm_s, &lt));
    const char old_fill = os.fill('0');
    os << buf << std::setw(6) << tm % 1000000L;
    os.fill(old_fill);
}

void PrintRealDateTime(std::ostream& os, int64_t tm,
                       bool ignore_microseconds) {
    char buf[32];
    const time_t tm_s = tm / 1000000L;
    struct tm lt;
    strftime(buf, sizeof(buf), "%Y/%m/%d-%H:%M:%S", localtime_r(&tm_s, &lt));
    if (ignore_microseconds) {
        os << buf;
    } else {
        const char old_fill = os.fill('0');
        os << buf << '.' << std::setw(6) << tm % 1000000L;
        os.fill(old_fill);
    }
}

std::ostream& operator<<(std::ostream& os, const PrintedAsDateTime& d) {
    PrintRealDateTime(os, d.realtime);
    return os;
}

std::ostream& operator<<(std::ostream& os, const Path& link) {
    if (link.html_addr) {
        if (link.html_addr != Path::LOCAL) {
            os << "<a href=\"http://" << *link.html_addr << link.uri << "\">";
        } else {
            os << "<a href=\"" << link.uri << "\">";
        }
    }
    if (link.text) {
        os << link.text;
    } else {
        os << link.uri;
    }
    if (link.html_addr) {
        os << "</a>";
    }
    return os;
}

const butil::EndPoint *Path::LOCAL = (butil::EndPoint *)0x01;

void AppendFileName(std::string* dir, const std::string& filename) {
    if (dir->empty()) {
        dir->append(filename);
        return;
    }
    const size_t len = filename.size();
    if (len >= 3) {
        if (butil::back_char(*dir) != '/') {
            dir->push_back('/');
        }
        dir->append(filename);
    } else if (len == 1) {
        if (filename[0] != '.') {
            if (butil::back_char(*dir) != '/') {
                dir->push_back('/');
            }
            dir->append(filename);
        }
    } else if (len == 2) {
        if (filename[0] != '.' || filename[1] != '.') {
            if (butil::back_char(*dir) != '/') {
                dir->push_back('/');
            }
            dir->append(filename);
        } else {
            const bool is_abs = (dir->c_str()[0] == '/');
            int npop = 1;
            while (npop > 0) {
                const char* p = dir->c_str() + dir->size() - 1;
                for (; p != dir->c_str() && *p == '/'; --p);
                if (p == dir->c_str()) {
                    dir->clear();
                    break;
                }
                dir->resize(p - dir->c_str() + 1);

                size_t slash_pos = dir->find_last_of('/');
                if (slash_pos == std::string::npos) {
                    --npop;
                    dir->clear();
                    break;
                }
                if (strcmp(dir->data() + slash_pos + 1, ".") != 0) {
                    if (strcmp(dir->data() + slash_pos + 1, "..") == 0) {
                        ++npop;
                    } else {
                        --npop;
                    }
                }
                ssize_t new_pos = (ssize_t)slash_pos - 1;
                for (; new_pos >= 0 && (*dir)[new_pos] == '/'; --new_pos);
                dir->resize(new_pos + 1);
                if (dir->empty()) {
                    break;
                }
            }
            if (dir->empty()) {
                if (is_abs) {
                    dir->push_back('/');
                } else {
                    if (npop > 0) {
                        dir->append("..");
                        for (int i = 1; i < npop; ++i) {
                            dir->append("/..");
                        }
                    }
                }
            }
        }
    } // else len == 0, nothing to do
}

const char* gridtable_style() {
    return
        "<style type=\"text/css\">\n"
        "table.gridtable {\n"
        "  color:#333333;\n"
        "  border-width:1px;\n"
        "  border-color:#666666;\n"
        "  border-collapse:collapse;\n"
        "}\n"
        "table.gridtable th {\n"
        "  border-width:1px;\n"
        "  padding:3px;\n"
        "  border-style:solid;\n"
        "  border-color:#666666;\n"
        "  background-color:#eeeeee;\n"
        "}\n"
        "table.gridtable td {\n"
        "  border-width:1px;\n"
        "  padding:3px;\n"
        "  border-style:solid;\n"
        "  border-color:#666666;\n"
        "  background-color:#ffffff;\n"
        "}\n"
        "</style>\n";
}

const char* TabsHead() {
    return
        "<style type=\"text/css\">\n"
        "ol,ul { list-style:none; }\n"
        ".tabs-menu {\n"
        "    position: fixed;"
        "    top: 0px;"
        "    left: 0px;"
        "    height: 40px;\n"
        "    width: 100%;\n"
        "    clear: both;\n"
        "    padding: 0px;\n"
        "    margin: 0px;\n"
        "    background-color: #606060;\n"
        "    border:none;\n"
        "    overflow: hidden;\n"
        "    box-shadow: 0px 1px 2px #909090;\n"  
        "    z-index: 5;\n"
        "}\n"
        ".tabs-menu li {\n"
        "    float:left;\n"
        "    fill:none;\n"
        "    border:none;\n"
        "    padding:10px 30px 10px 30px;\n"
        "    text-align:center;\n"
        "    cursor:pointer;\n"
        "    color:#dddddd;\n"
        "    font-weight: bold;\n"
        "    font-family: \"Segoe UI\", Calibri, Arial;\n"
        "}\n"
        ".tabs-menu li.current {\n"
        "    color:#FFFFFF;\n"
        "    background-color: #303030;\n"
        "}\n"
        ".tabs-menu li.help {\n"
        "    float:right;\n"
        "}\n"
        ".tabs-menu li:hover {\n"
        "    background-color: #303030;\n"
        "}\n"
        "</style>\n"
        "<script type=\"text/javascript\">\n"
        "$(function() {\n"
        "  $(\".tabs-menu li\").click(function(event) {\n"
        "    window.location.href = $(this).attr('id');\n"
        "  });\n"
        "});\n"
        "</script>\n";
}

const char* logo() {
    return
        "    __\n"
        "   / /_  _________  _____\n"
        "  / __ \\/ ___/ __ \\/ ___/\n"
        " / /_/ / /  / /_/ / /__\n"
        "/_.___/_/  / .___/\\___/\n"
        "          /_/\n";
}

const char* ProfilingType2String(ProfilingType t) {
    switch (t) {
    case PROFILING_CPU: return "cpu";
    case PROFILING_HEAP: return "heap";
    case PROFILING_GROWTH: return "growth";
    case PROFILING_CONTENTION: return "contention";
    }
    return "unknown";
}

int FileChecksum(const char* file_path, unsigned char* checksum) {
    butil::fd_guard fd(open(file_path, O_RDONLY));
    if (fd < 0) {
        PLOG(ERROR) << "Fail to open `" << file_path << "'";
        return -1;
    }
    char block[16*1024];   // 16k each time
    ssize_t size = 0L;
    butil::MurmurHash3_x64_128_Context mm_ctx;
    butil::MurmurHash3_x64_128_Init(&mm_ctx, 0);
    while ((size = read(fd, block, sizeof(block))) > 0) {
        butil::MurmurHash3_x64_128_Update(&mm_ctx, block, size);
    }
    butil::MurmurHash3_x64_128_Final(checksum, &mm_ctx);
    return 0;
}

static pthread_once_t create_program_name_once = PTHREAD_ONCE_INIT;
static const char* s_program_name = "unknown";
static char s_cmdline[256];
static void CreateProgramName() {
    const ssize_t nr = butil::ReadCommandLine(s_cmdline, sizeof(s_cmdline) - 1, false);
    if (nr > 0) {
        s_cmdline[nr] = '\0';
        s_program_name = s_cmdline;
    }
}
const char* GetProgramName() {
    pthread_once(&create_program_name_once, CreateProgramName);
    return s_program_name;
}

static pthread_once_t compute_program_checksum_once = PTHREAD_ONCE_INIT;
static char s_program_checksum[33];
static const char s_alphabet[] = "0123456789abcdef";
static void ComputeProgramCHECKSUM() {
    unsigned char checksum[16];
    FileChecksum(GetProgramName(), checksum);
    for (size_t i = 0, j = 0; i < 16; ++i, j+=2) {
        s_program_checksum[j] = s_alphabet[checksum[i] >> 4];
        s_program_checksum[j+1] = s_alphabet[checksum[i] & 0xF];
    }
    s_program_checksum[32] = '\0';

}
const char* GetProgramChecksum() {
    pthread_once(&compute_program_checksum_once, ComputeProgramCHECKSUM);
    return s_program_checksum;
}

bool SupportGzip(Controller* cntl) {
    const std::string* encodings =
        cntl->http_request().GetHeader("Accept-Encoding");
    if (encodings == NULL) {
        return false;
    }
    return encodings->find("gzip") != std::string::npos;
}

void Time2GMT(time_t t, char* buf, size_t size) {
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, size, "%a, %d %b %Y %H:%M:%S %Z", &tm);
}

} // namespace brpc
