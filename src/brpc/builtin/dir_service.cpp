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


#include <ostream>
#include <dirent.h>                    // opendir
#include <fcntl.h>                     // O_RDONLY
#include "butil/fd_guard.h"
#include "butil/fd_utility.h"

#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/controller.h"           // Controller
#include "brpc/builtin/common.h"
#include "brpc/builtin/dir_service.h"


namespace brpc {

void DirService::default_method(::google::protobuf::RpcController* cntl_base,
                                const ::brpc::DirRequest*,
                                ::brpc::DirResponse*,
                                ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    std::string open_path;
    
    const std::string& path_str =
        cntl->http_request().unresolved_path();
    if (!path_str.empty()) {
        open_path.reserve(path_str.size() + 2);
        open_path.push_back('/');
        open_path.append(path_str);
    } else {
        open_path = "/";
    }
    DIR* dir = opendir(open_path.c_str());
    if (NULL == dir) {
        butil::fd_guard fd(open(open_path.c_str(), O_RDONLY));
        if (fd < 0) {
            cntl->SetFailed(errno, "Cannot open `%s'", open_path.c_str());
            return;
        }
        butil::make_non_blocking(fd);
        butil::make_close_on_exec(fd);

        butil::IOPortal read_portal;
        size_t total_read = 0;
        do {
            const ssize_t nr = read_portal.append_from_file_descriptor(
                fd, MAX_READ);
            if (nr < 0) {
                cntl->SetFailed(errno, "Cannot read `%s'", open_path.c_str());
                return;
            }
            if (nr == 0) {
                break;
            }
            total_read += nr;
        } while (total_read < MAX_READ);
        butil::IOBuf& resp = cntl->response_attachment();
        resp.swap(read_portal);
        if (total_read >= MAX_READ) {
            std::ostringstream oss;
            oss << " <" << lseek(fd, 0, SEEK_END) - total_read << " more bytes>";
            resp.append(oss.str());
        }
        cntl->http_response().set_content_type("text/plain");
    } else {
        const bool use_html = UseHTML(cntl->http_request());
        const butil::EndPoint* const html_addr = (use_html ? Path::LOCAL : NULL);
        cntl->http_response().set_content_type(
            use_html ? "text/html" : "text/plain");

        std::vector<std::string> files;
        files.reserve(32);
        // readdir_r is marked as deprecated since glibc 2.24. 
#if defined(__GLIBC__) && \
        (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 24))
        for (struct dirent* p = NULL; (p = readdir(dir)) != NULL; ) {
#else
        struct dirent entbuf;
        for (struct dirent* p = NULL; readdir_r(dir, &entbuf, &p) == 0 && p; ) {
#endif
            files.push_back(p->d_name);
        }
        CHECK_EQ(0, closedir(dir));
        
        std::sort(files.begin(), files.end());
        butil::IOBufBuilder os;
        if (use_html) {
            os << "<!DOCTYPE html><html><body><pre>";
        }
        std::string str1;
        std::string str2;
        for (size_t i = 0; i < files.size(); ++i) {
            if (path_str.empty() && files[i] == "..") {
                // back to /index
                os << Path("", html_addr, files[i].c_str()) << '\n';
            } else {
                str1 = open_path;
                AppendFileName(&str1, files[i]);
                str2 = "/dir";
                str2.append(str1);
                os << Path(str2.c_str(), html_addr, files[i].c_str()) << '\n';
            }
        }
        if (use_html) {
            os << "</pre></body></html>";
        }
        os.move_to(cntl->response_attachment());
    }
}


} // namespace brpc
