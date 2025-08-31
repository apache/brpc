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
#include "butil/sys_byteorder.h"

namespace brpc {
namespace policy {

void Crc32cCompute(const ChecksumIn& in) {
    auto buf = in.buf;
    auto cntl = in.cntl;
    butil::IOBufAsZeroCopyInputStream wrapper(*buf);
    const void* data;
    int size;
    uint32_t crc = 0;
    while (wrapper.Next(&data, &size)) {
        crc = butil::crc32c::Extend(crc, static_cast<const char*>(data), size);
    }
    RPC_VLOG << "Crc32cCompute crc=" << crc;
    crc = butil::HostToNet32(butil::crc32c::Mask(crc));
    ControllerPrivateAccessor(cntl).set_checksum_value(
        reinterpret_cast<char*>(&crc), sizeof(crc));
}

bool Crc32cVerify(const ChecksumIn& in) {
    auto buf = in.buf;
    auto cntl = in.cntl;
    butil::IOBufAsZeroCopyInputStream wrapper(*buf);
    const void* data;
    int size;
    uint32_t crc = 0;
    while (wrapper.Next(&data, &size)) {
        crc = butil::crc32c::Extend(crc, static_cast<const char*>(data), size);
    }
    auto& val = ControllerPrivateAccessor(const_cast<Controller*>(cntl))
                    .checksum_value();
    CHECK_EQ(val.size(), sizeof(crc));
    auto expected = *reinterpret_cast<const uint32_t*>(val.data());
    expected = butil::crc32c::Unmask(butil::NetToHost32(expected));
    RPC_VLOG << "Crc32cVerify crc=" << crc << " expected=" << expected;
    return crc == expected;
}

}  // namespace policy
}  // namespace brpc
