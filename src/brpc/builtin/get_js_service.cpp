// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/10/30 15:57:02

#include "base/macros.h"                           // ARRAY_SIZE
#include "base/iobuf.h"                            // base::IOBuf
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

