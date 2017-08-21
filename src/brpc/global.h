// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Oct 27 14:02:49 2014

#ifndef BRPC_GLOBAL_H
#define BRPC_GLOBAL_H


namespace brpc {

// Register all naming service, load balancers, compress handlers inside
// `brpc/policy/' directory
void GlobalInitializeOrDie();

} // namespace brpc


#endif // BRPC_GLOBAL_H
