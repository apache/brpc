// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Sep 14 22:53:17 CST 2014

#ifndef BAIDU_RPC_LOG_H
#define BAIDU_RPC_LOG_H

#include <bthread/errno.h>
#include <inttypes.h>  // PRId64 PRIu64

#define RPC_VLOG_LEVEL     99
#define RPC_VLOG_IS_ON     VLOG_IS_ON(RPC_VLOG_LEVEL)
#define RPC_VLOG           VLOG(RPC_VLOG_LEVEL)
#define RPC_VPLOG          VPLOG(RPC_VLOG_LEVEL)
#define RPC_VLOG_IF(cond)  VLOG_IF(RPC_VLOG_LEVEL, (cond))
#define RPC_VPLOG_IF(cond) VPLOG_IF(RPC_VLOG_LEVEL, (cond))

#endif  // BAIDU_RPC_LOG_H
