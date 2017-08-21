// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Jun  2 16:28:03 CST 2015

#include <ostream>
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/controller.h"           // Controller
#include "brpc/builtin/common.h"
#include "brpc/builtin/sockets_service.h"
#include "brpc/socket.h"


namespace brpc {

void SocketsService::default_method(::google::protobuf::RpcController* cntl_base,
                                     const ::brpc::SocketsRequest*,
                                     ::brpc::SocketsResponse*,
                                     ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    cntl->http_response().set_content_type("text/plain");
    base::IOBufBuilder os;
    const std::string& constraint = cntl->http_request().unresolved_path();
    
    if (constraint.empty()) {
        os << "# Use /sockets/<SocketId>\n"
           << base::describe_resources<Socket>() << '\n';
    } else {
        char* endptr = NULL;
        SocketId sid = strtoull(constraint.c_str(), &endptr, 10);
        if (*endptr == '\0' || *endptr == '/') {
            Socket::DebugSocket(os, sid);
        } else {
            cntl->http_response().set_status_code(HTTP_STATUS_NOT_FOUND);
            cntl->SetFailed(ENOMETHOD, "path=%s is not a SocketId",
                            constraint.c_str());
        }
    }
    os.move_to(cntl->response_attachment());
}

} // namespace brpc

