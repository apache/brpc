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

#ifndef BRPC_CHECKSUM_H
#define BRPC_CHECKSUM_H

#include "brpc/controller.h"
#include "brpc/options.pb.h"  // ChecksumType
#include "butil/iobuf.h"      // butil::IOBuf

namespace brpc {

struct ChecksumIn {
    ChecksumIn(const butil::IOBuf* buf, Controller* cntl,
               const butil::IOBuf* attachment = nullptr)
        : buf(buf), cntl(cntl), attachment(attachment) {}

    const butil::IOBuf* buf;
    Controller* cntl;
    // Attachment to fold into the checksum in addition to `buf', or nullptr if
    // the checksum should cover `buf' only (the default, pre-existing
    // behavior). When non-nullptr, implementations must extend the checksum
    // with `buf' first and `attachment' second, so that both sides of the
    // RPC (which independently decide whether to pass an attachment here
    // based on Controller::request/response_checksum_attachment()) compute
    // the same value over the same byte ranges in the same order.
    const butil::IOBuf* attachment;
};

struct ChecksumHandler {
    // checksum `buf'.
    // Returns checksum value
    //
    // NOTE: this signature is part of the public API (brpc/checksum.h is
    // installed and RegisterChecksumHandler() is exposed for out-of-tree
    // implementations), so it must stay `void'. Implementations that can
    // fail (e.g. GPU memory read failure with GDR) must report the failure
    // through the controller instead: call in.cntl->SetFailed() and leave
    // the checksum value unset. Callers check cntl.Failed() afterwards and
    // fail the RPC instead of sending unchecked data.
    void (*Compute)(const ChecksumIn& in);

    // verify buf checksum
    // Rerturn true on success, false otherwise
    bool (*Verify)(const ChecksumIn& in);

    // Name of the checksum algorithm, must be string constant.
    const char* name;
};

// [NOT thread-safe] Register `handler' using key=`type'
// Returns 0 on success, -1 otherwise
int RegisterChecksumHandler(ChecksumType type, ChecksumHandler handler);

// Returns the `name' of the checksumType if registered
const char* ChecksumTypeToCStr(ChecksumType type);

// Put all registered handlers into `vec'.
void ListChecksumHandler(std::vector<ChecksumHandler>* vec);

// Compute `data' checksum and set to controller
//
// NOTE: kept `void' for API compatibility. A handler that cannot compute the
// checksum (e.g. GPU memory read failure with GDR) reports it by calling
// in.cntl->SetFailed(); callers must check cntl.Failed() afterwards and
// abort the RPC instead of sending data whose checksum was never computed.
void ComputeDataChecksum(const ChecksumIn& in, ChecksumType checksum_type);

// Verify `data' checksum. Returns true on success, false on mismatch or when
// the checksum could not be computed.
bool VerifyDataChecksum(const ChecksumIn& in, ChecksumType checksum_type);

}  // namespace brpc

#endif  // BRPC_CHECKSUM_H
