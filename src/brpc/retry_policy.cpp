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


#include "brpc/retry_policy.h"
#include "butil/fast_rand.h"


namespace brpc {

bool RpcRetryPolicy::DoRetry(const Controller* controller) const {
    const int error_code = controller->ErrorCode();
    if (!error_code) {
        return false;
    }
    return (EFAILEDSOCKET == error_code
            || EEOF == error_code
            || EHOSTDOWN == error_code
            || ELOGOFF == error_code
            || ETIMEDOUT == error_code // This is not timeout of RPC.
            || ELIMIT == error_code
            || ENOENT == error_code
            || EPIPE == error_code
            || ECONNREFUSED == error_code
            || ECONNRESET == error_code
            || ENODATA == error_code
            || EOVERCROWDED == error_code
            || EH2RUNOUTSTREAMS == error_code);
}

// NOTE(gejun): g_default_policy can't be deleted on process's exit because
// client-side may still retry and use the policy at exit
static pthread_once_t g_default_policy_once = PTHREAD_ONCE_INIT;
static RpcRetryPolicy* g_default_policy = NULL;
static void init_default_policy() {
    g_default_policy = new RpcRetryPolicy;
}
const RetryPolicy* DefaultRetryPolicy() {
    pthread_once(&g_default_policy_once, init_default_policy);
    return g_default_policy;
}

int32_t RpcRetryPolicyWithFixedBackoff::GetBackoffTimeMs(
    const Controller* controller) const {
    int64_t remaining_rpc_time_ms =
        (controller->deadline_us() - butil::gettimeofday_us()) / 1000;
    if (remaining_rpc_time_ms < _no_backoff_remaining_rpc_time_ms) {
        return 0;
    }
    return _backoff_time_ms;
}

int32_t RpcRetryPolicyWithJitteredBackoff::GetBackoffTimeMs(
    const Controller* controller) const {
    int64_t remaining_rpc_time_ms =
        (controller->deadline_us() - butil::gettimeofday_us()) / 1000;
    if (remaining_rpc_time_ms < _no_backoff_remaining_rpc_time_ms) {
        return 0;
    }
    return butil::fast_rand_in(_min_backoff_time_ms,
                               _max_backoff_time_ms);
}

} // namespace brpc
