// Copyright (c) 2014 Baidu, Inc.
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

#if !BRPC_WITH_GLOG

#include "brpc/log.h"
#include "brpc/controller.h"           // Controller
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/builtin/vlog_service.h"
#include "brpc/builtin/common.h"

namespace brpc {

class VLogPrinter : public VLogSitePrinter {
public:
    VLogPrinter(bool use_html, std::ostream& os)
        : _use_html(use_html), _os(&os) { }
    
    void print(const VLogSitePrinter::Site& site) {
        const char* const bar = (_use_html ? "</td><td>" : " | ");
        if (_use_html) {
            *_os << "<tr><td>";
        }
        *_os << site.full_module << ":" << site.line_no << bar
             << site.current_verbose_level << bar << site.required_verbose_level
             << bar;
        if (site.current_verbose_level >= site.required_verbose_level) {
            if (_use_html) {
                *_os << "<span style='font-weight:bold;color:#00A000'>"
                     << "enabled</span>";
            } else {
                *_os << "enabled";
            }
        } else {
            *_os << "disabled";
        }
        if (_use_html) {
            *_os << "</td></tr>";
        }
        *_os << '\n';
    }
    
private:
    bool _use_html;
    std::ostream* _os;

};

void VLogService::default_method(::google::protobuf::RpcController* cntl_base,
                                 const ::brpc::VLogRequest*,
                                 ::brpc::VLogResponse*,
                                 ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    const bool use_html = UseHTML(cntl->http_request());
    butil::IOBufBuilder os;

    cntl->http_response().set_content_type(
        use_html ? "text/html" : "text/plain");
    if (use_html) {
        os << "<!DOCTYPE html><html><head>" << gridtable_style()
           << "<script src=\"/js/sorttable\"></script></head><body>"
            "<table class=\"gridtable\" border=\"1\"><tr>"
            "<th>Module</th><th>Current</th><th>Required</th>"
            "<th>Status</th></tr>\n";
    } else {
        os << "Module | Current | Required | Status\n";
    }
    VLogPrinter printer(use_html, os);
    print_vlog_sites(&printer);
    if (use_html) {
        os << "</table>\n";
    }
        
    if (use_html) {
        os << "</body></html>\n";
    }
    os.move_to(cntl->response_attachment());
}

} // namespace brpc

#endif

