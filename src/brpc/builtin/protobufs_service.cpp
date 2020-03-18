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


#include <google/protobuf/descriptor.h>     // ServiceDescriptor
#include "brpc/controller.h"           // Controller
#include "brpc/server.h"               // Server
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/details/method_status.h"// MethodStatus
#include "brpc/builtin/protobufs_service.h"
#include "brpc/builtin/common.h"


namespace brpc {

ProtobufsService::ProtobufsService(Server* server) : _server(server) {
    CHECK_EQ(0, Init());
}

int ProtobufsService::Init() {
    Server::ServiceMap &services = _server->_fullname_service_map;
    std::vector<const google::protobuf::Descriptor*> stack;
    stack.reserve(services.size() * 3);
    for (Server::ServiceMap::iterator 
            iter = services.begin(); iter != services.end(); ++iter) {
        if (!iter->second.is_user_service()) {
            continue;
        }
        const google::protobuf::ServiceDescriptor* d =
            iter->second.service->GetDescriptor();
        _map[d->full_name()] = d->DebugString();
        const int method_count = d->method_count();
        for (int j = 0; j < method_count; ++j) {
            const google::protobuf::MethodDescriptor* md = d->method(j);
            stack.push_back(md->input_type());
            stack.push_back(md->output_type());
        }
    }
    while (!stack.empty()) {
        const google::protobuf::Descriptor* d = stack.back();
        stack.pop_back();
        _map[d->full_name()] = d->DebugString();
        for (int i = 0; i < d->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* f = d->field(i);
            if (f->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE ||
                f->type() == google::protobuf::FieldDescriptor::TYPE_GROUP) {
                const google::protobuf::Descriptor* sub_d = f->message_type();
                if (sub_d != d && _map.find(sub_d->full_name()) == _map.end()) {
                    stack.push_back(sub_d);
                }
            }
        }
    }
    return 0;
}

void ProtobufsService::default_method(::google::protobuf::RpcController* cntl_base,
                                   const ProtobufsRequest*,
                                   ProtobufsResponse*,
                                   ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    butil::IOBufBuilder os;
    const std::string& filter = cntl->http_request().unresolved_path();
    if (filter.empty()) {
        const bool use_html = UseHTML(cntl->http_request());
        cntl->http_response().set_content_type(
            use_html ? "text/html" : "text/plain");
        if (use_html) {
            os << "<!DOCTYPE html><html><head></head><body>\n";
        }    
        // list all structures.
        for (Map::iterator it = _map.begin(); it != _map.end(); ++it) {
            if (use_html) {
                os << "<p><a href=\"/protobufs/" << it->first << "\">";
            }
            os << it->first;
            if (use_html) {
                os << "</a></p>";
            }
            os << '\n';
        }
        if (use_html) {
            os << "</body></html>";
        }
    } else {
        // already text.
        cntl->http_response().set_content_type("text/plain");
        Map::iterator it = _map.find(filter);
        if (it == _map.end()) {
            cntl->SetFailed(ENOMETHOD,
                            "Fail to find any protobuf message by `%s'",
                            filter.c_str());
            return;
        }
        os << it->second;
    }
    os.move_to(cntl->response_attachment());
}

} // namespace brpc
