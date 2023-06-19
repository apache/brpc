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


#ifndef BRPC_RETRY_POLICY_H
#define BRPC_RETRY_POLICY_H

#include "brpc/controller.h"


namespace brpc {

// Inherit this class to customize when the RPC should be retried.
class RetryPolicy {
public:
    virtual ~RetryPolicy() = default;
    
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

    // Returns the backoff time in milliseconds before every retry.
    virtual int32_t GetBackoffTimeMs(const Controller* controller) const { return 0; }
    //                                                               ^
    //                                             don't forget the const modifier

    // Returns true if enable retry backoff in pthread, otherwise returns false.
    virtual bool CanRetryBackoffInPthread() const { return false; }
    //                                        ^
    //                     don't forget the const modifier
};

// Get the RetryPolicy used by brpc.
const RetryPolicy* DefaultRetryPolicy();

class RpcRetryPolicy : public RetryPolicy {
public:
    bool DoRetry(const Controller* controller) const override;
};

class RpcRetryPolicyWithFixedBackoff : public RpcRetryPolicy {
public:
    RpcRetryPolicyWithFixedBackoff(int32_t backoff_time_ms,
                                   int32_t no_backoff_remaining_rpc_time_ms,
                                   bool retry_backoff_in_pthread)
    : _backoff_time_ms(backoff_time_ms)
    , _no_backoff_remaining_rpc_time_ms(no_backoff_remaining_rpc_time_ms)
    , _retry_backoff_in_pthread(retry_backoff_in_pthread) {}

    int32_t GetBackoffTimeMs(const Controller* controller) const override;

    bool CanRetryBackoffInPthread() const override { return _retry_backoff_in_pthread; }


private:
    int32_t _backoff_time_ms;
    // If remaining rpc time is less than `_no_backoff_remaining_rpc_time', no backoff.
    int32_t _no_backoff_remaining_rpc_time_ms;
    bool _retry_backoff_in_pthread;
};

class RpcRetryPolicyWithJitteredBackoff : public RpcRetryPolicy {
public:
    RpcRetryPolicyWithJitteredBackoff(int32_t min_backoff_time_ms,
                                      int32_t max_backoff_time_ms,
                                      int32_t no_backoff_remaining_rpc_time_ms,
                                      bool retry_backoff_in_pthread)
            : _min_backoff_time_ms(min_backoff_time_ms)
            , _max_backoff_time_ms(max_backoff_time_ms)
            , _no_backoff_remaining_rpc_time_ms(no_backoff_remaining_rpc_time_ms)
            , _retry_backoff_in_pthread(retry_backoff_in_pthread) {}

    int32_t GetBackoffTimeMs(const Controller* controller) const override;

    bool CanRetryBackoffInPthread() const override { return _retry_backoff_in_pthread; }

private:
    // Generate jittered backoff time between [_min_backoff_ms, _max_backoff_ms].
    int32_t _min_backoff_time_ms;
    int32_t _max_backoff_time_ms;
    // If remaining rpc time is less than `_no_backoff_remaining_rpc_time', no backoff.
    int32_t _no_backoff_remaining_rpc_time_ms;
    bool _retry_backoff_in_pthread;
};

} // namespace brpc


#endif  // BRPC_RETRY_POLICY_H
