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

#ifndef BRPC_POLICY_CRC32C_CHECKSUM_H
#define BRPC_POLICY_CRC32C_CHECKSUM_H

#include "brpc/checksum.h"
#include "brpc/controller.h"
#include "butil/iobuf.h"  // butil::IOBuf

namespace brpc {
namespace policy {

// Compute checksum and store it in the controller. Returns false when the
// checksum could not be computed (e.g. GPU memory read failure); the
// controller is left without a checksum value and the caller must fail the
// RPC instead of sending data whose checksum was never calculated.
bool Crc32cCompute(const ChecksumIn& in);

// Verify checksum. Returns false on mismatch or when the checksum could
// not be computed.
bool Crc32cVerify(const ChecksumIn& in);

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_CRC32C_CHECKSUM_H
