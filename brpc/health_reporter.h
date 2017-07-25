// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Fri Mar 11 14:57:24 CST 2016

#ifndef BRPC_HEALTH_REPORTER_H
#define BRPC_HEALTH_REPORTER_H

#include "brpc/controller.h"


namespace brpc {

// For customizing /health page.
// Inherit this class and assign an instance to ServerOptions.health_reporter.
class HealthReporter {
public:
    virtual ~HealthReporter() {}
    
    // Get the http request from cntl->http_request() / cntl->request_attachment()
    // and put the response in cntl->http_response() / cntl->response_attachment()
    // Don't forget to call done->Run() at the end.
    virtual void GenerateReport(Controller* cntl, google::protobuf::Closure* done) = 0;
};

} // namespace brpc


#endif  // BRPC_HEALTH_REPORTER_H
