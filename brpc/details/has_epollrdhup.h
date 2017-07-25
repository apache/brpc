// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Sep 14 22:04:41 CST 2014

#ifndef BRPC_HAS_EPOLLRDHUP_H
#define BRPC_HAS_EPOLLRDHUP_H


namespace brpc {

// Check if the kernel supports EPOLLRDHUP which is added in Linux 2.6.17
// This flag is useful in Edge Triggered mode. Without the flag user has
// to call an additional read() even if return value(positive) is less
// than given `count', otherwise return value=0(indicating EOF) may be lost.
extern const unsigned int has_epollrdhup;

} // namespace brpc


#endif  // BRPC_HAS_EPOLLRDHUP_H
