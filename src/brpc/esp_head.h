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

#ifndef BRPC_ESP_HEAD_H
#define BRPC_ESP_HEAD_H

namespace brpc {

#pragma pack(push, r1, 1)
union EspAddress {
    uint64_t addr;
    struct {
        uint16_t stub;
        uint16_t port;
        uint32_t ip;
    };
};

struct EspHead {
    EspAddress from;
    EspAddress to;
    uint32_t msg;
    uint64_t msg_id;
    int body_len;
};
#pragma pack(pop, r1)

} // namespace brpc

#endif // BRPC_ESP_HEAD_H
