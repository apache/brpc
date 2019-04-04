// Copyright (c) 2019 Baidu, Inc.
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

// Authors: Yang,Liming (yangliming01@baidu.com)

#ifndef BRPC_MYSQL_COMMON_H
#define BRPC_MYSQL_COMMON_H

#include <sstream>

namespace brpc {
const int mysql_header_size = 4;
const uint32_t mysql_max_package_size = 0xFFFFFF;

inline std::string pack_encode_length(const uint64_t value) {
    std::stringstream ss;
    if (value <= 250) {
        ss.put((char)value);
    } else if (value <= 0xffff) {
        ss.put((char)0xfc).put((char)value).put((char)(value >> 8));
    } else if (value <= 0xffffff) {
        ss.put((char)0xfd).put((char)value).put((char)(value >> 8)).put((char)(value >> 16));
    } else {
        ss.put((char)0xfd)
            .put((char)value)
            .put((char)(value >> 8))
            .put((char)(value >> 16))
            .put((char)(value >> 24))
            .put((char)(value >> 32))
            .put((char)(value >> 40))
            .put((char)(value >> 48))
            .put((char)(value >> 56));
    }
    return ss.str();
}

// little endian order to host order
inline uint16_t mysql_uint2korr(const uint8_t* A) {
    return (uint16_t)(((uint16_t)(A[0])) + ((uint16_t)(A[1]) << 8));
}
inline uint32_t mysql_uint3korr(const uint8_t* A) {
    return (uint32_t)(((uint32_t)(A[0])) + (((uint32_t)(A[1])) << 8) + (((uint32_t)(A[2])) << 16));
}
inline uint32_t mysql_uint4korr(const uint8_t* A) {
    return (uint32_t)(((uint32_t)(A[0])) + (((uint32_t)(A[1])) << 8) + (((uint32_t)(A[2])) << 16) +
                      (((uint32_t)(A[3])) << 24));
}
inline uint64_t mysql_uint8korr(const uint8_t* A) {
    return (uint64_t)(((uint64_t)(A[0])) + (((uint64_t)(A[1])) << 8) + (((uint64_t)(A[2])) << 16) +
                      (((uint64_t)(A[3])) << 24) + (((uint64_t)(A[4])) << 32) +
                      (((uint64_t)(A[5])) << 40) + (((uint64_t)(A[6])) << 48) +
                      (((uint64_t)(A[7])) << 56));
}
}  // namespace brpc
#endif
