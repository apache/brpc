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

#include "brpc/retry_policy.h"


namespace brpc {

RetryPolicy::~RetryPolicy() {}

class RpcRetryPolicy : public RetryPolicy {
public:
    bool DoRetry(const Controller* controller) const {
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
};

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

} // namespace brpc
