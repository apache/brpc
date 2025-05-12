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

#include "brpc/policy/crc32c_checksum.h"

#include "brpc/details/controller_private_accessor.h"
#include "brpc/log.h"
#include "butil/crc32c.h"

namespace brpc {
namespace policy {

void Crc32cCompute(const butil::IOBuf& buf, Controller* cntl) {
    butil::IOBufAsZeroCopyInputStream wrapper(buf);
    const void* data;
    int size;
    uint32_t crc = 0;
    while (wrapper.Next(&data, &size)) {
        crc = butil::crc32c::Extend(crc, static_cast<const char*>(data), size);
    }
    RPC_VLOG << "Crc32cCompute crc=" << crc;
    ControllerPrivateAccessor(cntl).set_checksum_value(crc);
}

bool Crc32cVerify(const butil::IOBuf& buf, const Controller& cntl) {
    butil::IOBufAsZeroCopyInputStream wrapper(buf);
    const void* data;
    int size;
    uint32_t crc = 0;
    while (wrapper.Next(&data, &size)) {
        crc = butil::crc32c::Extend(crc, static_cast<const char*>(data), size);
    }
    auto expected = ControllerPrivateAccessor(const_cast<Controller*>(&cntl))
                        .checksum_value();
    RPC_VLOG << "Crc32cVerify crc=" << crc << " expected=" << expected;
    return crc == expected;
}

}  // namespace policy
}  // namespace brpc
