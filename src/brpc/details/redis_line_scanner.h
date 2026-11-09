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


#ifndef BRPC_DETAILS_REDIS_LINE_SCANNER_H
#define BRPC_DETAILS_REDIS_LINE_SCANNER_H

#include "butil/iobuf.h"

namespace brpc {
namespace detail {

// Input may be appended between calls, but its existing prefix must not change.
// Keep the backing-block position too: forwarding a fresh iterator to a saved
// byte offset would still repeatedly walk all earlier blocks.
struct RedisLineScanState {
    uint32_t block_index;
    uint32_t block_offset;
    uint32_t scanned;
    bool previous_cr;

    void Reset() {
        block_index = block_offset = scanned = 0;
        previous_cr = false;
    }

    size_t FindCRLF(const butil::IOBuf& buf, size_t limit) {
        if (scanned > buf.size()) {
            Reset();
        }
        while (block_index < buf.backing_block_num() && scanned < limit) {
            const butil::StringPiece block = buf.backing_block(block_index);
            while (block_offset < block.size() && scanned < limit) {
                const char c = block[block_offset++];
                ++scanned;
                if (previous_cr && c == '\n') {
                    return scanned - 2;
                }
                previous_cr = (c == '\r');
            }
            if (block_offset < block.size() ||
                block_index + 1 >= buf.backing_block_num()) {
                // The last block may grow on the next append.
                break;
            }
            ++block_index;
            block_offset = 0;
        }
        return static_cast<size_t>(-1);
    }
};

static_assert(sizeof(RedisLineScanState) <= 16,
              "Line scan state must fit in the RedisReply union");

}  // namespace detail
}  // namespace brpc
#endif  // BRPC_DETAILS_REDIS_LINE_SCANNER_H
