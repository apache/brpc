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


#ifndef BRPC_REDIS_COMMAND_H
#define BRPC_REDIS_COMMAND_H

#include <limits>
#include <memory>           // std::unique_ptr
#include <vector>
#include "butil/iobuf.h"
#include "butil/status.h"
#include "butil/arena.h"
#include "brpc/parse_result.h"

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

// A parser used to parse redis raw command.
class RedisCommandParser {
public:
    RedisCommandParser();

    // Parse raw message from `buf'. Return PARSE_OK and set the parsed command
    // to `args' and length to `len' if successful. Memory of args are allocated 
    // in `arena'.
    ParseError Consume(butil::IOBuf& buf, std::vector<butil::StringPiece>* args,
                       butil::Arena* arena);

private:
    // Reset parser to the initial state.
    void Reset();

    bool _parsing_array;            // if the parser has met array indicator '*'
    int _length;                    // array length
    int _index;                     // current parsing array index
    std::vector<butil::StringPiece> _args;  // parsed command string
};

} // namespace brpc


#endif  // BRPC_REDIS_COMMAND_H
