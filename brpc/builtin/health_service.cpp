// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/11/06 15:28:43

#include <vector>                           // std::vector
#include "brpc/controller.h"           // Controller
#include "brpc/server.h"               // Server
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/builtin/health_service.h"
#include "brpc/builtin/common.h"


namespace brpc {

void HealthService::default_method(::google::protobuf::RpcController* cntl_base,
                                   const ::brpc::HealthRequest*,
                                   ::brpc::HealthResponse*,
                                   ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    const Server* server = cntl->server();
    if (server->options().health_reporter) {
        server->options().health_reporter->GenerateReport(
            cntl, done_guard.release());
    } else {
        cntl->http_response().set_content_type("text/plain");
        cntl->response_attachment().append("OK");
    }
}

} // namespace brpc

