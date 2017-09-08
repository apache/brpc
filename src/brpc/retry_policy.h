// Copyright (c) 2016 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_RETRY_POLICY_H
#define BRPC_RETRY_POLICY_H

#include "brpc/controller.h"


namespace brpc {

// Inherit this class to customize when the RPC should be retried.
class RetryPolicy {
public:
    virtual ~RetryPolicy();
    
    // Returns true if the RPC represented by `controller' should be retried.
    // [Example]
    // By default, HTTP errors are not retried, but you need to retry
    // HTTP_STATUS_FORBIDDEN in your app. You can implement the RetryPolicy
    // as follows:
    //
    //   class MyRetryPolicy : public brpc::RetryPolicy {
    //   public:
    //     bool DoRetry(const brpc::Controller* cntl) const {
    //       if (cntl->ErrorCode() == 0) { // don't retry successful RPC
    //         return false;
    //       }
    //       if (cntl->ErrorCode() == brpc::EHTTP && // http errors
    //           cntl->http_response().status_code() == brpc::HTTP_STATUS_FORBIDDEN) {
    //         return true;
    //       }
    //       // Leave other cases to default.
    //       return brpc::DefaultRetryPolicy()->DoRetry(cntl);
    //     }
    //   };
    // 
    // You can retry unqualified responses even if the RPC was successful
    //   class MyRetryPolicy : public brpc::RetryPolicy {
    //   public:
    //     bool DoRetry(const brpc::Controller* cntl) const {
    //       if (cntl->ErrorCode() == 0) { // successful RPC
    //         if (!is_qualified(cntl->response())) {
    //           cntl->response()->Clear();  // reset the response
    //           return true;
    //         }
    //         return false;
    //       }
    //       // Leave other cases to default.
    //       return brpc::DefaultRetryPolicy()->DoRetry(cntl);
    //     }
    //   };
    virtual bool DoRetry(const Controller* controller) const = 0;
    //                                                   ^
    //                                don't forget the const modifier
};

// Get the RetryPolicy used by brpc.
const RetryPolicy* DefaultRetryPolicy();

} // namespace brpc


#endif  // BRPC_RETRY_POLICY_H
