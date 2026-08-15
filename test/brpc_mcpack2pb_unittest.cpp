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

TEST(Mcpack2pbParserTest, StringFieldWithZeroValueSize) {
    // A 51-byte mcpack2 frame whose `service_name' string field has
    // value_size == 0. This used to underflow in UnparsedValue::as_string()
    // (resize(_size - 1) with _size == 0) and throw std::length_error, which
    // is not caught on the request path and therefore crashes the server.
    const unsigned char data[] = {
        0x10, 0x00, 0x2d, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0xa0, 0x08, 0x1e, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x10, 0x00, 0x14, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0xd0, 0x0d, 0x00, 0x73, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65,
        0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x00,
    };
    butil::IOBuf body;
    body.append(data, sizeof(data));

    butil::IOBufAsZeroCopyInputStream zc_stream(body);
    mcpack2pb::InputStream stream(&zc_stream);
    ASSERT_NE(0u, mcpack2pb::unbox(&stream));

    mcpack2pb::ObjectIterator it1(&stream, body.size() - stream.popped_bytes());
    bool found_content = false;
    for (; it1 != NULL; ++it1) {
        if (it1->name == "content") {
            found_content = true;
            break;
        }
    }
    ASSERT_TRUE(found_content);
    ASSERT_EQ(mcpack2pb::FIELD_ARRAY, it1->value.type());

    mcpack2pb::ArrayIterator it2(it1->value);
    ASSERT_TRUE(it2 != NULL);
    bool found_service_name = false;
    for (mcpack2pb::ObjectIterator it3(*it2); it3 != NULL; ++it3) {
        if (it3->name == "service_name") {
            found_service_name = true;
            ASSERT_EQ(mcpack2pb::FIELD_STRING, it3->value.type());
            std::string service_name = "stale";
            it3->value.as_string(&service_name, "service_name");
            // A zero-sized string field must be rejected gracefully instead of
            // throwing (resize(SIZE_MAX)) and crashing the process.
            EXPECT_FALSE(it3->value.stream()->good());
            // The output string must be cleared so callers that reuse the
            // string do not keep a stale value.
            EXPECT_TRUE(service_name.empty());
            break;
        }
    }
    ASSERT_TRUE(found_service_name);
}

TEST(Mcpack2pbParserTest, ParseStringField) {
    // A valid object {"msg":"abc"}.
    const unsigned char data[] = {
        0x10, 0x00, 0x0f, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0xd0, 0x04, 0x04,
        0x6d, 0x73, 0x67, 0x00,
        0x61, 0x62, 0x63, 0x00,
    };
    butil::IOBuf body;
    body.append(data, sizeof(data));

    butil::IOBufAsZeroCopyInputStream zc_stream(body);
    mcpack2pb::InputStream stream(&zc_stream);
    ASSERT_NE(0u, mcpack2pb::unbox(&stream));

    mcpack2pb::ObjectIterator it(&stream, body.size() - stream.popped_bytes());
    ASSERT_TRUE(it != NULL);
    EXPECT_EQ("msg", it->name.as_string());
    ASSERT_EQ(mcpack2pb::FIELD_STRING, it->value.type());
    std::string value;
    it->value.as_string(&value, "msg");
    EXPECT_EQ("abc", value);
    EXPECT_TRUE(stream.good());
}

}  // namespace
