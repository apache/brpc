// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014/11/06 15:28:43

#include "base/time.h"
#include "base/logging.h"
#include "brpc/controller.h"           // Controller
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/builtin/threads_service.h"
#include "brpc/builtin/common.h"
#include "base/string_printf.h"

namespace brpc {

void ThreadsService::default_method(::google::protobuf::RpcController* cntl_base,
                                    const ::brpc::ThreadsRequest*,
                                    ::brpc::ThreadsResponse*,
                                    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    Controller *cntl = static_cast<Controller*>(cntl_base);
    cntl->http_response().set_content_type("text/plain");
    base::IOBuf& resp = cntl->response_attachment();

    base::IOPortal read_portal;
    std::string cmd = base::string_printf("pstack %lld", (long long)getpid());
    base::Timer tm;
    tm.start();
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == NULL) {
        LOG(FATAL) << "Fail to popen `" << cmd << "'";
        return;
    }
    read_portal.append_from_file_descriptor(fileno(pipe), MAX_READ);
    resp.swap(read_portal);
    
    // Call fread, otherwise following error will be reported:
    //   sed: couldn't flush stdout: Broken pipe
    // and pclose will fail:
    //   CHECK failed: 0 == pclose(pipe): Resource temporarily unavailable
    size_t fake_buf;
    base::ignore_result(fread(&fake_buf, sizeof(fake_buf), 1, pipe));
    CHECK_EQ(0, pclose(pipe)) << berror();
    tm.stop();
    resp.append(base::string_printf("\n\ntime=%lums", tm.m_elapsed()));
}

} // namespace brpc

