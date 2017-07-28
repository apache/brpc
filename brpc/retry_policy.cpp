// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2016 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Feb 14 14:14:32 CST 2016

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
                || EOVERCROWDED == error_code);
    }
};

// NOTE(gejun): g_default_policy can't be deleted on process's exit because
// concurrent processing of responses at client-side may trigger retry and
// use the policy.
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

