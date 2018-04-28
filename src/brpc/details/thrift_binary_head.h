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

#ifndef BRPC_THRIFT_HEADER_H
#define BRPC_THRIFT_HEADER_H


namespace brpc {

static const int32_t VERSION_MASK = ((int32_t)0xffffff00);
static const int32_t VERSION_1 = ((int32_t)0x80010000);
struct thrift_binary_head_t {
    int32_t  body_len;
};

} // namespace brpc


#endif // BRPC_THRIFT_HEADER_H
