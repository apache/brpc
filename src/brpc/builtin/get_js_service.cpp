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

#include "butil/macros.h"                           // ARRAY_SIZE
#include "butil/iobuf.h"                            // butil::IOBuf
#include "brpc/controller.h"                  // Controller
#include "brpc/builtin/sorttable_js.h"
#include "brpc/builtin/jquery_min_js.h"
#include "brpc/builtin/flot_min_js.h"
#include "brpc/builtin/viz_min_js.h"
#include "brpc/builtin/get_js_service.h"
#include "brpc/builtin/common.h"


namespace brpc {

static const char* g_last_modified = "Wed, 16 Sep 2015 01:25:30 GMT";

static void SetExpires(HttpHeader* header, time_t seconds) {
    char buf[256];
    time_t now = time(0);
    Time2GMT(now, buf, sizeof(buf));
    header->SetHeader("Date", buf);
    Time2GMT(now + seconds, buf, sizeof(buf));
    header->SetHeader("Expires", buf);
}

void GetJsService::sorttable(
    ::google::protobuf::RpcController* controller,
    const GetJsRequest* /*request*/,
    GetJsResponse* /*response*/,
    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = (Controller*)controller;
    cntl->http_response().set_content_type("application/javascript");
    SetExpires(&cntl->http_response(), 80000);
    cntl->response_attachment().append(sorttable_js_iobuf());
}

void GetJsService::jquery_min(
    ::google::protobuf::RpcController* controller,
    const GetJsRequest* /*request*/,
    GetJsResponse* /*response*/,
    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = (Controller*)controller;
    cntl->http_response().set_content_type("application/javascript");
    SetExpires(&cntl->http_response(), 600);

    const std::string* ims =
        cntl->http_request().GetHeader("If-Modified-Since");
    if (ims != NULL && *ims == g_last_modified) {
        cntl->http_response().set_status_code(HTTP_STATUS_NOT_MODIFIED);
        return;
    }
    cntl->http_response().SetHeader("Last-Modified", g_last_modified);
    
    if (SupportGzip(cntl)) {
        cntl->http_response().SetHeader("Content-Encoding", "gzip");
        cntl->response_attachment().append(jquery_min_js_iobuf_gzip());
    } else {
        cntl->response_attachment().append(jquery_min_js_iobuf());
    }
}

void GetJsService::flot_min(
    ::google::protobuf::RpcController* controller,
    const GetJsRequest* /*request*/,
    GetJsResponse* /*response*/,
    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = (Controller*)controller;
    cntl->http_response().set_content_type("application/javascript");
    SetExpires(&cntl->http_response(), 80000);

    const std::string* ims =
        cntl->http_request().GetHeader("If-Modified-Since");
    if (ims != NULL && *ims == g_last_modified) {
        cntl->http_response().set_status_code(HTTP_STATUS_NOT_MODIFIED);
        return;
    }
    cntl->http_response().SetHeader("Last-Modified", g_last_modified);
    
    if (SupportGzip(cntl)) {
        cntl->http_response().SetHeader("Content-Encoding", "gzip");
        cntl->response_attachment().append(flot_min_js_iobuf_gzip());
    } else {
        cntl->response_attachment().append(flot_min_js_iobuf());
    }
}

void GetJsService::viz_min(
    ::google::protobuf::RpcController* controller,
    const GetJsRequest* /*request*/,
    GetJsResponse* /*response*/,
    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = (Controller*)controller;
    cntl->http_response().set_content_type("application/javascript");
    SetExpires(&cntl->http_response(), 80000);

    const std::string* ims =
        cntl->http_request().GetHeader("If-Modified-Since");
    if (ims != NULL && *ims == g_last_modified) {
        cntl->http_response().set_status_code(HTTP_STATUS_NOT_MODIFIED);
        return;
    }
    cntl->http_response().SetHeader("Last-Modified", g_last_modified);
    
    if (SupportGzip(cntl)) {
        cntl->http_response().SetHeader("Content-Encoding", "gzip");
        cntl->response_attachment().append(viz_min_js_iobuf_gzip());
    } else {
        cntl->response_attachment().append(viz_min_js_iobuf());
    }
}

} // namespace brpc
