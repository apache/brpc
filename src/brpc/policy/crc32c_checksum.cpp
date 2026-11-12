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

#include <cstring>

#include "brpc/details/controller_private_accessor.h"
#include "brpc/log.h"
#include "butil/crc32c.h"
#include "butil/sys_byteorder.h"

namespace brpc {
namespace policy {

namespace {

#if BRPC_WITH_GDR
// CRC32C is computed on the CPU, so GPU memory cannot be read through CPU
// pointers. Copy the block to a host buffer first and extend the crc over the
// copy; the data itself stays in GPU memory (the GPU -> GPU data path of GDR
// is untouched).
// Writes the extended crc to `*crc' and returns true on success; returns
// false without touching `*crc' when the host buffer cannot be allocated or
// the copy fails. Callers must abort (fail the RPC) on false instead of
// substituting a placeholder crc: any fixed value can collide with the real
// crc and would make Crc32cVerify() accept an unchecked attachment.
static bool ExtendCrc32cFromGpu(uint32_t* crc, const butil::IOBuf& buf) {
    const size_t size = buf.size();
    if (size == 0) {
        return true;
    }
    char* host = new (std::nothrow) char[size];
    if (host == nullptr) {
        LOG(ERROR) << "Fail to alloc host buffer to checksum gpu memory";
        return false;
    }
    if (buf.copy_from_gpu(host, size) != size) {
        LOG(ERROR) << "Fail to copy data from gpu memory to checksum";
        delete[] host;
        return false;
    }
    *crc = butil::crc32c::Extend(*crc, host, size);
    delete[] host;
    return true;
}
#endif  // BRPC_WITH_GDR

// Extends `*crc' over every block of `buf'. Shared by Crc32cCompute and
// Crc32cVerify to keep the byte range/order (`buf' then `attachment', see
// ChecksumIn::attachment) identical on both call sites.
// Returns false (with `*crc' left at the last consistent value) when a GPU
// block cannot be checksummed; see ExtendCrc32cFromGpu.
static bool ExtendCrc32c(uint32_t* crc, const butil::IOBuf& buf) {
    if (buf.size() == 0) {
        return true;
    }
#if BRPC_WITH_GDR
    if (buf.is_gpu_memory()) {
        return ExtendCrc32cFromGpu(crc, buf);
    }
#endif  // BRPC_WITH_GDR
    butil::IOBufAsZeroCopyInputStream wrapper(buf);
    const void* data;
    int size;
    while (wrapper.Next(&data, &size)) {
        *crc = butil::crc32c::Extend(*crc, static_cast<const char*>(data), size);
    }
    return true;
}

// Computes the crc32c over `in.buf', and over `in.attachment' as well when
// the caller opted in (ChecksumIn::attachment != nullptr).
// Returns false when any range cannot be checksummed (GPU read failure);
// `*crc' is unspecified in that case and must not be used.
static bool ComputeCrc32c(const ChecksumIn& in, uint32_t* crc) {
    *crc = 0;
    if (!ExtendCrc32c(crc, *in.buf)) {
        return false;
    }
    if (in.attachment != nullptr) {
        if (!ExtendCrc32c(crc, *in.attachment)) {
            return false;
        }
    }
    return true;
}

}  // namespace

// Returns false when the crc could not be computed (GPU memory read
// failure); `in.cntl' is left without a checksum value and the caller must
// fail the RPC instead of sending unchecked data.
bool Crc32cCompute(const ChecksumIn& in) {
    uint32_t crc;
    if (!ComputeCrc32c(in, &crc)) {
        return false;
    }
    RPC_VLOG << "Crc32cCompute crc=" << crc;
    crc = butil::HostToNet32(butil::crc32c::Mask(crc));
    ControllerPrivateAccessor(in.cntl).set_checksum_value(
        reinterpret_cast<char*>(&crc), sizeof(crc));
    return true;
}

// Returns false on mismatch or when the crc could not be computed.
bool Crc32cVerify(const ChecksumIn& in) {
    auto& val = ControllerPrivateAccessor(const_cast<Controller*>(in.cntl))
                    .checksum_value();
    uint32_t expected;
    if (val.size() != sizeof(expected)) {
        return false;
    }
    std::memcpy(&expected, val.data(), sizeof(expected));
    expected = butil::crc32c::Unmask(butil::NetToHost32(expected));
    uint32_t crc;
    if (!ComputeCrc32c(in, &crc)) {
        return false;
    }
    RPC_VLOG << "Crc32cVerify crc=" << crc << " expected=" << expected;
    return crc == expected;
}

}  // namespace policy
}  // namespace brpc
