// Copyright (c) 2015 Baidu, Inc.
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

#ifndef BRPC_REDIS_COMMAND_H
#define BRPC_REDIS_COMMAND_H

#include "butil/iobuf.h"
#include "butil/status.h"


namespace brpc {

// Format a redis command and append it to `buf'.
// Returns butil::Status::OK() on success.
butil::Status RedisCommandFormat(butil::IOBuf* buf, const char* fmt, ...);
butil::Status RedisCommandFormatV(butil::IOBuf* buf, const char* fmt, va_list args);

// Just convert the command to the text format of redis without processing the
// specifiers(%) inside.
butil::Status RedisCommandNoFormat(butil::IOBuf* buf, const butil::StringPiece& command);

// Concatenate components to form a redis command.
butil::Status RedisCommandByComponents(butil::IOBuf* buf,
                                      const butil::StringPiece* components,
                                      size_t num_components);

} // namespace brpc


#endif  // BRPC_REDIS_COMMAND_H
