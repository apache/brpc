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

// Much faster than snprintf(..., "%lu", d);
inline size_t AppendDecimal(char* outbuf, unsigned long d) {
    char buf[24];  // enough for decimal 64-bit integers
    size_t n = sizeof(buf);
    do {
        const unsigned long q = d / 10;
        buf[--n] = d - q * 10 + '0';
        d = q;
    } while (d);
    fast_memcpy(outbuf, buf + n, sizeof(buf) - n);
    return sizeof(buf) - n;
}

// This function is the hotspot of RedisCommandFormatV() when format is
// short or does not have many %. In a 100K-time call to formating of
// "GET key1", the time spent on RedisRequest.AddCommand() are ~700ns
// vs. ~400ns while using snprintf() vs. AppendDecimal() respectively.
inline void AppendHeader(std::string& buf, char fc, unsigned long value) {
    char header[32];
    header[0] = fc;
    size_t len = AppendDecimal(header + 1, value);
    header[len + 1] = '\r';
    header[len + 2] = '\n';
    buf.append(header, len + 3);
}
inline void AppendHeader(butil::IOBuf& buf, char fc, unsigned long value) {
    char header[32];
    header[0] = fc;
    size_t len = AppendDecimal(header + 1, value);
    header[len + 1] = '\r';
    header[len + 2] = '\n';
    buf.append(header, len + 3);
}


} // namespace brpc


#endif  // BRPC_REDIS_COMMAND_H
