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
    const butil::IOBuf* buf;
    Controller* cntl;
};

struct ChecksumHandler {
    // checksum `buf'.
    // Returns checksum value
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
void ComputeDataChecksum(const ChecksumIn& in, ChecksumType checksum_type);

// Verify `data' checksum Returns true on success, false otherwise
bool VerifyDataChecksum(const ChecksumIn& in, ChecksumType checksum_type);

}  // namespace brpc

#endif  // BRPC_CHECKSUM_H
