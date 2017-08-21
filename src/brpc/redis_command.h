// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: Ge,Jun (gejun@baidu.com)
// Date: Fri Jun  5 18:25:40 CST 2015

#ifndef BRPC_REDIS_COMMAND_H
#define BRPC_REDIS_COMMAND_H

#include "base/iobuf.h"
#include "base/status.h"


namespace brpc {

// Format a redis command and append it to `buf'.
// Returns base:Status::OK() on success.
base::Status RedisCommandFormat(base::IOBuf* buf, const char* fmt, ...);
base::Status RedisCommandFormatV(base::IOBuf* buf, const char* fmt, va_list args);

// Just convert the command to the text format of redis without processing the
// specifiers(%) inside.
base::Status RedisCommandNoFormat(base::IOBuf* buf, const base::StringPiece& command);

// Concatenate components to form a redis command.
base::Status RedisCommandByComponents(base::IOBuf* buf,
                                      const base::StringPiece* components,
                                      size_t num_components);

} // namespace brpc


#endif  // BRPC_REDIS_COMMAND_H
