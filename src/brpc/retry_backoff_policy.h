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


#ifndef BRPC_RETRY_BACKOFF_POLICY_H
#define BRPC_RETRY_BACKOFF_POLICY_H

#include "brpc/controller.h"
#include "butil/fast_rand.h"

namespace brpc {

// Inherit this class to customize the RPC retry backoff policy.
class RetryBackoffPolicy {
public:
    virtual ~RetryBackoffPolicy() = default;

    // Returns the backoff time in milliseconds before every retry.
    virtual int32_t GetBackoffTimeMs(const Controller* controller, int nretry,
                                     int64_t remaining_rpc_time_ms) const = 0;
    //                                                                ^
    //                                                  don't forget the const modifier
};

class FixedRetryBackoffPolicy : public RetryBackoffPolicy {
public:
    FixedRetryBackoffPolicy(int32_t backoff_time_ms, int32_t no_backoff_remaining_rpc_time_ms)
        : _backoff_time_ms(backoff_time_ms)
        , _no_backoff_remaining_rpc_time_ms(no_backoff_remaining_rpc_time_ms) {}

    int32_t GetBackoffTimeMs(const Controller* controller, int nretry,
                             int64_t remaining_rpc_time_ms) const override;

private:
    int32_t _backoff_time_ms;
    // If remaining rpc time is less than `_no_backoff_remaining_rpc_time', no backoff.
    int32_t _no_backoff_remaining_rpc_time_ms;
};

class JitteredRetryBackoffPolicy : public RetryBackoffPolicy {
public:
    explicit JitteredRetryBackoffPolicy(int32_t min_backoff_time_ms,
                                        int32_t max_backoff_time_ms,
                                        int32_t no_backoff_remaining_rpc_time_ms)
        : _min_backoff_time_ms(min_backoff_time_ms)
        , _max_backoff_time_ms(max_backoff_time_ms)
        , _no_backoff_remaining_rpc_time_ms(no_backoff_remaining_rpc_time_ms) {}

    int32_t GetBackoffTimeMs(const Controller* controller, int nretry,
                             int64_t remaining_rpc_time_ms) const override;

private:
    // Generate jittered backoff time between [_min_backoff_ms, _max_backoff_ms].
    int32_t _min_backoff_time_ms;
    int32_t _max_backoff_time_ms;
    // If remaining rpc time is less than `_no_backoff_remaining_rpc_time', no backoff.
    int32_t _no_backoff_remaining_rpc_time_ms;
};


} // namespace brpc


#endif  // BRPC_RETRY_BACKOFF_POLICY_H
