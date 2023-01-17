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
