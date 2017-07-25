// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/11/06 15:28:43

#include "brpc/controller.h"           // Controller
#include "brpc/server.h"               // Server
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/builtin/version_service.h"


namespace brpc {

void VersionService::default_method(::google::protobuf::RpcController* controller,
                                    const ::brpc::VersionRequest*,
                                    ::brpc::VersionResponse*,
                                    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = (Controller *)controller;
    cntl->http_response().set_content_type("text/plain");
    if (_server->version().empty()) {
        cntl->response_attachment().append("unknown");
    } else {
        cntl->response_attachment().append(_server->version());
    }
}

} // namespace brpc

