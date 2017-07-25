// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Aug 31 16:27:49 CST 2014

#ifndef BRPC_TRACEPRINTF_H
#define BRPC_TRACEPRINTF_H

#include "base/macros.h"

// To baidu-rpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.


namespace brpc {

bool CanAnnotateSpan();
void AnnotateSpan(const char* fmt, ...);

} // namespace brpc


// Use this macro to print log to /rpcz and tracing system.
// If rpcz is not enabled, arguments to this macro is NOT evaluated, don't
// have (critical) side effects in arguments.
#define TRACEPRINTF(fmt, args...)                                       \
    do {                                                                \
        if (::brpc::CanAnnotateSpan()) {                          \
            ::brpc::AnnotateSpan("[" __FILE__ ":" BAIDU_SYMBOLSTR(__LINE__) "] " fmt, ##args);           \
        }                                                               \
    } while (0)

#endif  // BRPC_TRACEPRINTF_H
