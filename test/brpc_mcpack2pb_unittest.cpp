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
#include <pthread.h>
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
    for (; it1 != nullptr; ++it1) {
        if (it1->name == "content") {
            found_content = true;
            break;
        }
    }
    ASSERT_TRUE(found_content);
    ASSERT_EQ(mcpack2pb::FIELD_ARRAY, it1->value.type());

    mcpack2pb::ArrayIterator it2(it1->value);
    ASSERT_TRUE(it2 != nullptr);
    bool found_service_name = false;
    for (mcpack2pb::ObjectIterator it3(*it2); it3 != nullptr; ++it3) {
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
    ASSERT_TRUE(it != nullptr);
    EXPECT_EQ("msg", it->name.as_string());
    ASSERT_EQ(mcpack2pb::FIELD_STRING, it->value.type());
    std::string value;
    it->value.as_string(&value, "msg");
    EXPECT_EQ("abc", value);
    EXPECT_TRUE(stream.good());
}

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

// Builds the wire bytes of a recursive message `Node { repeated Node
// children = 1; }' nesting `depth' levels of { children: [ ... ] }, the way
// protoc-gen-mcpack serializes such a message.
static void AppendU32(std::string* out, uint32_t value) {
    char buf[4];
    buf[0] = (char)(value & 0xff);
    buf[1] = (char)((value >> 8) & 0xff);
    buf[2] = (char)((value >> 16) & 0xff);
    buf[3] = (char)((value >> 24) & 0xff);
    out->append(buf, 4);
}

static std::string BuildRecursivePayload(int depth) {
    // The innermost level is an empty object: an ItemsHead with no items.
    std::string body;
    AppendU32(&body, 0);
    const std::string name = std::string("children\0", 9);  // trailing '\0'
    // Reserve enough space for the final payload so that appending at each
    // nesting level does not reallocate (the size of each wrapping level is
    // 1 + 1 + 4 + body + 4 + item + 1 + 1 + 4 + name + arr).
    body.reserve((size_t)depth * 30);
    for (int i = 1; i < depth; ++i) {
        std::string item;  // a FIELD_OBJECT item wrapping the inner payload
        item.push_back(0x10);  // FIELD_OBJECT
        item.push_back(0x00);  // name_size = 0
        AppendU32(&item, (uint32_t)body.size());  // value_size
        item.append(body);

        std::string arr;  // an array holding a single item
        AppendU32(&arr, 1);
        arr.append(item);

        std::string child;  // FIELD_ARRAY "children"
        child.push_back(0x20);  // FIELD_ARRAY
        child.push_back((char)name.size());  // name_size
        AppendU32(&child, (uint32_t)arr.size());  // value_size
        child.append(name);
        child.append(arr);

        std::string new_body;  // an object holding a single field
        AppendU32(&new_body, 1);
        new_body.append(child);
        body.swap(new_body);
    }
    return body;
}

// Simulates the recursion pattern of the functions generated by
// protoc-gen-mcpack for a message with a repeated message field
// (e.g. Node.children): parse_<msg>_body_internal creates an ObjectIterator,
// set_<msg>_<field> creates an ArrayIterator and calls
// parse_<msg>_body_internal for each item.
static bool ParseNodeInternal(mcpack2pb::UnparsedValue& value) {
    mcpack2pb::ObjectIterator it(value);
    for (; it != nullptr; ++it) {
        if (it->name == "children") {
            if (it->value.type() != mcpack2pb::FIELD_ARRAY) {
                return false;
            }
            mcpack2pb::ArrayIterator it2(it->value);
            for (; it2 != nullptr; ++it2) {
                if (it2->type() != mcpack2pb::FIELD_OBJECT ||
                    !ParseNodeInternal(*it2)) {
                    return false;
                }
            }
        }
    }
    return value.stream()->good();
}

struct ParseArgs {
    const std::string* payload;
    bool parse_ok;
};

static void* ParseOnSmallStack(void* arg) {
    ParseArgs* args = static_cast<ParseArgs*>(arg);
    butil::IOBuf buf;
    buf.append(args->payload->data(), args->payload->size());
    butil::IOBufAsZeroCopyInputStream zc_stream(buf);
    mcpack2pb::InputStream stream(&zc_stream);
    mcpack2pb::UnparsedValue value(mcpack2pb::FIELD_OBJECT, &stream,
                                   buf.size());
    args->parse_ok = ParseNodeInternal(value);
    return nullptr;
}

// Parses `payload' on a 1 MB stack thread, the size of a NORMAL bthread
// stack in brpc, so that the test behaves like a request served by brpc.
static int ParseRecursivePayloadWith1MBStack(const std::string& payload,
                                             bool* ok) {
    ParseArgs args = { &payload, false };
    pthread_attr_t attr;
    int rc = pthread_attr_init(&attr);
    if (rc != 0) {
        return -1;
    }
    rc = pthread_attr_setstacksize(&attr, 1024 * 1024);
    if (rc != 0) {
        pthread_attr_destroy(&attr);
        return -1;
    }
    pthread_t tid;
    rc = pthread_create(&tid, &attr, ParseOnSmallStack, &args);
    // The attribute is not needed right after create, destroy it here so
    // that every path below shares one cleanup point.
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        return -1;
    }
    rc = pthread_join(tid, nullptr);
    if (rc != 0) {
        return -2;
    }
    *ok = args.parse_ok;
    return 0;
}

TEST(Mcpack2pbParserTest, DeeplyNestedPayloadIsRejectedWithoutStackOverflow) {
    // A few levels beyond MAX_DEPTH (128) suffice: such input must be
    // rejected by the depth limit. Before the fix the parse accepted it
    // (and far deeper input would overflow the 1 MB stack and crash the
    // process), so this fails without the guard. Keeping the depth small
    // keeps the test fast even under sanitizers.
    const std::string payload = BuildRecursivePayload(mcpack2pb::MAX_DEPTH + 2);
    bool ok = true;
    ASSERT_EQ(0, ParseRecursivePayloadWith1MBStack(payload, &ok));
    // The parse must fail cleanly instead of crashing the process.
    EXPECT_FALSE(ok);
}

TEST(Mcpack2pbParserTest, ModeratelyNestedPayloadParses) {
    // 32 levels are well within the depth limit and must parse cleanly.
    const std::string payload = BuildRecursivePayload(32);
    bool ok = false;
    ASSERT_EQ(0, ParseRecursivePayloadWith1MBStack(payload, &ok));
    EXPECT_TRUE(ok);
}

}  // namespace
