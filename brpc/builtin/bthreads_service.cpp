// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Tue Jun  2 16:28:03 CST 2015

#include <ostream>
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/controller.h"           // Controller
#include "brpc/builtin/common.h"
#include "brpc/builtin/bthreads_service.h"

namespace bthread {
void print_task(std::ostream& os, bthread_t tid);
}


namespace brpc {

void BthreadsService::default_method(::google::protobuf::RpcController* cntl_base,
                                     const ::brpc::BthreadsRequest*,
                                     ::brpc::BthreadsResponse*,
                                     ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    cntl->http_response().set_content_type("text/plain");
    base::IOBufBuilder os;
    const std::string& constraint = cntl->http_request().unresolved_path();
    
    if (constraint.empty()) {
        os << "Use /bthreads/<bthread_id>";
    } else {
        char* endptr = NULL;
        bthread_t tid = strtoull(constraint.c_str(), &endptr, 10);
        if (*endptr == '\0' || *endptr == '/') {
            ::bthread::print_task(os, tid);
        } else {
            cntl->http_response().set_status_code(HTTP_STATUS_NOT_FOUND);
            cntl->SetFailed(ENOMETHOD, "path=%s is not a bthread id",
                            constraint.c_str());
        }
    }
    os.move_to(cntl->response_attachment());
}

} // namespace brpc

