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


#include <gflags/gflags.h>                  // DECLARE_xxx
#include <google/protobuf/descriptor.h>
#include "butil/time.h"                      // gettimeofday_us
#include "brpc/server.h"                    // Server
#include "brpc/builtin/index_service.h"
#include "brpc/builtin/status_service.h"
#include "brpc/builtin/common.h"
#include "brpc/details/tcmalloc_extension.h"

namespace brpc {

void IndexService::GetTabInfo(TabInfoList* info_list) const {
    TabInfo* info = info_list->add();
    info->path = "/index?as_more";
    info->tab_name = "more";
}

DECLARE_bool(enable_rpcz);
DECLARE_bool(enable_dir_service);
DECLARE_bool(enable_threads_service);

// Set in ProfilerLinker.
bool cpu_profiler_enabled = false;

void IndexService::default_method(::google::protobuf::RpcController* controller,
                                  const IndexRequest*,
                                  IndexResponse*,
                                  ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = (Controller*)controller;
    cntl->http_response().set_content_type("text/plain");
    const Server* server = cntl->server();
    const bool use_html = UseHTML(cntl->http_request());
    const bool as_more = cntl->http_request().uri().GetQuery("as_more");
    if (use_html && !as_more) {
        google::protobuf::Service* svc = server->FindServiceByFullName(
            StatusService::descriptor()->full_name());
        StatusService* st_svc = dynamic_cast<StatusService*>(svc);
        if (st_svc == NULL) {
            cntl->SetFailed("Fail to find StatusService");
            return;
        }
        return st_svc->default_method(cntl, NULL, NULL, done_guard.release());
    }
    cntl->http_response().set_content_type(
        use_html ? "text/html" : "text/plain");
    const butil::EndPoint* const html_addr = (use_html ? Path::LOCAL : NULL);
    const char* const NL = (use_html ? "<br>\n" : "\n");
    const char* const SP = (use_html ? "&nbsp;" : "  ");

    butil::IOBufBuilder os;
    if (use_html) {
        os << "<!DOCTYPE html><html>";
        if (as_more) {
            os << "<head>\n"
                "<script language=\"javascript\" type=\"text/javascript\" src=\"/js/jquery_min\"></script>\n"
               << TabsHead()
               << "</head>\n";
        }
        os << "<body>\n";
        if (as_more) {
            cntl->server()->PrintTabsBody(os, "more");
        };
        os << "<pre>";
    }
    os << logo();
    if (use_html) {
        os << "</pre>";
    }
    os << '\n';
    if (use_html) {
        os << "<a href=\"https://github.com/apache/brpc\">github</a>";
    } else {
        os << "github : https://github.com/apache/brpc";
    }
    os << NL << NL;
    if (!as_more) {
        os << Path("/status", html_addr) << " : Status of services" << NL
           << Path("/connections", html_addr) << " : List all connections" << NL
           << Path("/flags", html_addr) << " : List all gflags" << NL
           << SP << Path("/flags/port", html_addr) << " : List the gflag" << NL
           << SP << Path("/flags/guard_page_size;help*", html_addr)
           << " : List multiple gflags with glob patterns"
            " (Use $ instead of ? to match single character)" << NL << SP
           << "/flags/NAME?setvalue=VALUE : Change a gflag, validator will be called."
            " User is responsible for thread-safety and consistency issues." << NL
            
           << Path("/vars", html_addr) << " : List all exposed bvars" << NL
           << SP << Path("/vars/rpc_num_sockets", html_addr)
           << " : List the bvar" << NL
           << SP << Path("/vars/rpc_server*_count;iobuf_blo$k_*", html_addr)
           <<  " : List multiple bvars with glob patterns"
            " (Use $ instead of ? to match single character)" << NL
            
           << Path("/rpcz", html_addr) << " : Recent RPC calls"
           << (!FLAGS_enable_rpcz ? "(disabled)" : "") << NL
           << SP << Path("/rpcz/stats", html_addr) << " : Statistics of rpcz" << NL;

        std::ostringstream tmp_oss;
        const int64_t seconds_before = butil::gettimeofday_us() - 30 * 1000000L;
        tmp_oss << "/rpcz?" << TIME_STR << '=';
        PrintRealDateTime(tmp_oss, seconds_before, true);
        os << SP << Path(tmp_oss.str().c_str(), html_addr)
           << " : RPC calls before the time" << NL;
        tmp_oss.str("");
        tmp_oss << "/rpcz?" << TIME_STR << '=';
        PrintRealDateTime(tmp_oss, seconds_before, true);
        tmp_oss << '&' << MAX_SCAN_STR << "=10";
        os << SP << Path(tmp_oss.str().c_str(), html_addr)
           << " : N RPC calls at most before the time" << NL << SP
           << "Other filters: " << MIN_LATENCY_STR << ", " << MIN_REQUEST_SIZE_STR
           << ", " << MIN_RESPONSE_SIZE_STR << ", " << LOG_ID_STR
           << ", " << ERROR_CODE_STR << NL 
           << SP << "/rpcz?" << TRACE_ID_STR
           << "=N : Recent RPC calls whose trace_id is N" << NL
           << SP << "/rpcz?" << TRACE_ID_STR << "=N&" << SPAN_ID_STR
           << "=M : Recent RPC calls whose trace_id is N and span_id is M" << NL

           << Path("/hotspots/cpu", html_addr) << " : Profiling CPU"
           << (!cpu_profiler_enabled ? " (disabled)" : "") << NL
           << Path("/hotspots/heap", html_addr) << " : Profiling heap"
           << (!IsHeapProfilerEnabled() ? " (disabled)" : "") << NL
           << Path("/hotspots/growth", html_addr)
           << " : Profiling growth of heap"
           << (!IsHeapProfilerEnabled() ? " (disabled)" : "") << NL;
    }
    os << "curl -H 'Content-Type: application/json' -d 'JSON' ";
    if (butil::is_endpoint_extended(server->listen_address())) {
        os << "<listen_address>";
    } else {
        const butil::EndPoint my_addr(butil::my_ip(), server->listen_address().port);
        os << my_addr;
    }
    os << "/ServiceName/MethodName : Call method by http+json" << NL
    
       << Path("/version", html_addr)
       << " : Version of this server, set by Server::set_version()" << NL
       << Path("/health", html_addr) << " : Test healthy" << NL
       << Path("/vlog", html_addr) << " : List all VLOG callsites" << NL
       << Path("/sockets", html_addr) << " : Check status of a Socket" << NL
       << Path("/bthreads", html_addr) << " : Check status of a bthread" << NL
       << Path("/ids", html_addr) << " : Check status of a bthread_id" << NL
       << Path("/protobufs", html_addr) << " : List all protobuf services and messages" << NL
       << Path("/list", html_addr) << " : json signature of methods" << NL
       << Path("/threads", html_addr) << " : Check pstack"
       << (!FLAGS_enable_threads_service ? " (disabled)" : "") << NL
       << Path("/dir", html_addr) << " : Browse directories and files"
       << (!FLAGS_enable_dir_service ? " (disabled)" : "") << NL;
    if (use_html) {
        os << "</body></html>";
    }
    os.move_to(cntl->response_attachment());
}

} // namespace brpc
