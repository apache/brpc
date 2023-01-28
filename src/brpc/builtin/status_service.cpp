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
#include <google/protobuf/descriptor.h>     // ServiceDescriptor
#include "brpc/controller.h"           // Controller
#include "brpc/server.h"               // Server
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/details/method_status.h"        // MethodStatus
#include "brpc/builtin/status_service.h"
#include "brpc/nshead_service.h"       // NsheadService
#ifdef ENABLE_THRIFT_FRAMED_PROTOCOL
#include "brpc/thrift_service.h"       // ThriftService
#endif
#include "brpc/rtmp.h"                 // RtmpService
#include "brpc/builtin/common.h"


namespace brpc {
namespace policy {
extern MethodStatus* g_client_msg_status;
extern MethodStatus* g_server_msg_status;
}

// Defined in vars_service.cpp
void PutVarsHeading(std::ostream& os, bool expand_all);

void StatusService::default_method(::google::protobuf::RpcController* cntl_base,
                                   const ::brpc::StatusRequest*,
                                   ::brpc::StatusResponse*,
                                   ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    const Server* server = cntl->server();
    const bool use_html = UseHTML(cntl->http_request());
    
    // NOTE: the plain output also fits format of public/configure so that user
    // can load values more easily.
    cntl->http_response().set_content_type(
        use_html ? "text/html" : "text/plain");
    butil::IOBufBuilder os;
    std::string str;
    if (use_html) {
        os << "<!DOCTYPE html><html><head>\n"
            "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\" />\n";
        bool expand = cntl->http_request().uri().GetQuery("expand");
        PutVarsHeading(os, expand);
        os << "</head><body>";
        server->PrintTabsBody(os, "status");
        os << "<div class=\"layer1\">\n";
    }
    os << "version: " << server->version() << '\n';

    // non_service_error
    if (use_html) {
        os << "<p class=\"variable\">";
    }
    os << "non_service_error: ";
    if (use_html) {
        os << "<span id=\"value-" << server->_nerror_bvar.name() << "\">";
    }
    os << server->_nerror_bvar.get_value();
    if (use_html) {
        os << "</span></p><div class=\"detail\"><div id=\"" << server->_nerror_bvar.name()
           << "\" class=\"flot-placeholder\"></div></div>";
    }
    os << '\n';
    
    // connection_count
    if (use_html) {
        os << "<p class=\"variable\">";
    }
    os << "connection_count: ";
    if (use_html) {
        os << "<span id=\"value-" << server->ServerPrefix()
           << "_connection_count\">";
    }
    ServerStatistics ss;
    server->GetStat(&ss);
    os << ss.connection_count;
    if (use_html) {
        os << "</span></p><div class=\"detail\"><div id=\""
           << server->ServerPrefix()
           << "_connection_count\" class=\"flot-placeholder\"></div></div>";
    }
    os << '\n';

    // max_concurrency
    os << "max_concurrency: ";
    const int mc = server->options().max_concurrency;
    if (mc <= 0) {
        os << "unlimited";
    } else {
        os << mc;
    }
    os << '\n';

    // concurrency
    if (use_html) {
        os << "<p class=\"variable\">";
    }
    os << "concurrency: ";
    if (use_html) {
        os << "<span id=\"value-" << server->ServerPrefix()
           << "_concurrency\">";
    }
    os << server->Concurrency();
    if (use_html) {
        os << "</span></p><div class=\"detail\"><div id=\""
           << server->ServerPrefix()
           << "_concurrency\" class=\"flot-placeholder\"></div></div>";
    }
    os << '\n';


    const Server::ServiceMap &services = server->_fullname_service_map;
    std::ostringstream desc;
    DescribeOptions desc_options;
    desc_options.verbose = true;
    desc_options.use_html = use_html;

    for (Server::ServiceMap::const_iterator 
            iter = services.begin(); iter != services.end(); ++iter) {
        const Server::ServiceProperty& sp = iter->second;
        if (!sp.is_user_service()) {
            continue;
        }
        CHECK(sp.service);
        if (dynamic_cast<Tabbed*>(sp.service)) {
            // Tabbed services are probably for monitoring, their own status
            // are not important.
            continue;
        }
        const google::protobuf::ServiceDescriptor* d =
            sp.service->GetDescriptor();
        os << (use_html ? "<h3>" : "[") << d->full_name()
           << (use_html ? "</h3>" : "]")
           << '\n';
        
        // Output customized status if the service implements Describable
        Describable* obj = dynamic_cast<Describable*>(sp.service);
        if (obj) {
            desc.str("");
            obj->Describe(desc, desc_options);
            const size_t len = desc.str().size();
            if (len) {
                os << desc.str();
                if (desc.str()[len-1] != '\n') {
                    os << '\n';
                }
            }
        }

        const int method_count = d->method_count();
        for (int j = 0; j < method_count; ++j) {
            const google::protobuf::MethodDescriptor* md = d->method(j);
            Server::MethodProperty* mp =
                server->_method_map.seek(d->method(j)->full_name());
            if (use_html) {
                os << "<h4>" << md->name()
                   << " (<a href=\"/protobufs/" << md->input_type()->full_name()
                   << "\">" << md->input_type()->name() << "</a>"
                   << ") returns (<a href=\"/protobufs/"
                   << md->output_type()->full_name()
                   << "\">" << md->output_type()->name() << "</a>)";
                if (mp) {
                    if (mp->http_url) {
                        os << " @" << *mp->http_url;
                    }
                }
                os << "</h4>\n";
            } else {
                os << "\n" << md->name() << " (" << md->input_type()->name()
                   << ") returns (" << md->output_type()->name() << ")";
                if (mp) {
                    if (mp->http_url) {
                        os << " @" << *mp->http_url;
                    }
                }
                os << '\n';
            }
            if (mp && mp->status) {
                mp->status->Describe(os, desc_options);
                os << '\n';
            }
        }
    }
    const NsheadService* nshead_svc = server->options().nshead_service;
    if (nshead_svc && nshead_svc->_status) {
        DescribeOptions options;
        options.verbose = false;
        options.use_html = use_html;
        os << (use_html ? "<h3>" : "[");
        nshead_svc->Describe(os, options);
        os << (use_html ? "</h3>\n" : "]\n");
        nshead_svc->_status->Describe(os, desc_options);
        os << '\n';
    }
#ifdef ENABLE_THRIFT_FRAMED_PROTOCOL
    const ThriftService* thrift_svc = server->options().thrift_service;
    if (thrift_svc && thrift_svc->_status) {
        DescribeOptions options;
        options.verbose = false;
        options.use_html = use_html;
        os << (use_html ? "<h3>" : "[");
        thrift_svc->Describe(os, options);
        os << (use_html ? "</h3>\n" : "]\n");
        thrift_svc->_status->Describe(os, desc_options);
        os << '\n';
    }
#endif
    if (policy::g_server_msg_status) {
        DescribeOptions options;
        options.verbose = false;
        options.use_html = use_html;
        os << (use_html ? "<h3>" : "[")
           << "RtmpServer Messages (in)"
           << (use_html ? "</h3>\n" : "]\n");
        policy::g_server_msg_status->Describe(os, desc_options);
        os << '\n';
    }
    if (policy::g_client_msg_status) {
        DescribeOptions options;
        options.verbose = false;
        options.use_html = use_html;
        os << (use_html ? "<h3>" : "[")
           << "RtmpClient Messages (in)"
           << (use_html ? "</h3>\n" : "]\n");
        policy::g_client_msg_status->Describe(os, desc_options);
        os << '\n';
    }

    if (use_html) {
        os << "</div></body></html>";
    }
    os.move_to(cntl->response_attachment());
    cntl->set_response_compress_type(COMPRESS_TYPE_GZIP);
}

void StatusService::GetTabInfo(TabInfoList* info_list) const {
    TabInfo* info = info_list->add();
    info->path = "/status";
    info->tab_name = "status";
}

} // namespace brpc
