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

// Unit tests for the mcpack2pb parser.

#include <gtest/gtest.h>
#include "butil/iobuf.h"
#include "mcpack2pb/parser.h"

namespace {

TEST(Mcpack2pbParserTest, ArrayItemCountIsCappedToRemainingBytes) {
    // An mcpack array whose header claims item_count = 0x7fffffff (INT32_MAX)
    // but contains no actual items. The raw item_count is fed by the
    // generated code into Reserve() of a repeated protobuf field, which used
    // to preallocate ~16GB from a tiny request. The item count must be capped
    // by the bytes actually available in the array.
    const unsigned char data[] = {
        0xff, 0xff, 0xff, 0x7f,  // item_count = 0x7fffffff, no items
    };
    butil::IOBuf body;
    body.append(data, sizeof(data));

    butil::IOBufAsZeroCopyInputStream zc_stream(body);
    mcpack2pb::InputStream stream(&zc_stream);
    mcpack2pb::ArrayIterator it(&stream, sizeof(data));
    // No item can fit in an empty payload.
    EXPECT_EQ(0u, it.item_count());
}

TEST(Mcpack2pbParserTest, ArrayItemCountIsCappedToAvailableBytes) {
    // item_count = 1000 but the payload only holds one int32 item (6 bytes).
    // Each item occupies at least one byte, so the count must not exceed the
    // remaining bytes (6).
    const unsigned char data[] = {
        0xe8, 0x03, 0x00, 0x00,  // item_count = 1000
        0x14, 0x00, 0x00, 0x00, 0x00, 0x00,  // one int32 item
    };
    butil::IOBuf body;
    body.append(data, sizeof(data));

    butil::IOBufAsZeroCopyInputStream zc_stream(body);
    mcpack2pb::InputStream stream(&zc_stream);
    mcpack2pb::ArrayIterator it(&stream, sizeof(data));
    EXPECT_LE(it.item_count(), sizeof(data) - sizeof(uint32_t));
}

TEST(Mcpack2pbParserTest, ArrayItemCountIsZeroWhenPayloadSmallerThanHeader) {
    // The declared array payload (3 bytes) is smaller than the 4-byte
    // ItemsHead, but the stream still contains data. The parser must not read
    // past the declared boundary and trust the extra bytes as item_count
    // (which would feed a huge value into Reserve() again).
    const unsigned char data[] = {
        0xff, 0xff, 0xff, 0x7f,  // would be read as item_count = 0x7fffffff
    };
    butil::IOBuf body;
    body.append(data, sizeof(data));

    butil::IOBufAsZeroCopyInputStream zc_stream(body);
    mcpack2pb::InputStream stream(&zc_stream);
    mcpack2pb::ArrayIterator it(&stream, 3);  // size = 3 < sizeof(ItemsHead)
    EXPECT_EQ(0u, it.item_count());
}

}  // namespace
