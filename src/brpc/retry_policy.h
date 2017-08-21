// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Feb 14 14:14:32 CST 2016

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

// Get the RetryPolicy used by baidu-rpc.
const RetryPolicy* DefaultRetryPolicy();

} // namespace brpc


#endif  // BRPC_RETRY_POLICY_H
