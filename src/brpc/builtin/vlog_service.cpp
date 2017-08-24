// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/11/06 15:28:43

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
    base::IOBufBuilder os;

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

