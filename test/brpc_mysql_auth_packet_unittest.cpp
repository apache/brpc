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

#include <gtest/gtest.h>

#include <string>

#include "brpc/policy/mysql/mysql_auth_packet.h"
#include "butil/strings/string_piece.h"

namespace {

using brpc::policy::mysql::DecodeLengthEncodedInt;
using brpc::policy::mysql::DecodeLengthEncodedString;
using brpc::policy::mysql::DecodeNullTerminatedString;
using brpc::policy::mysql::DecodePacketHeader;
using brpc::policy::mysql::EncodeLengthEncodedInt;
using brpc::policy::mysql::EncodeLengthEncodedString;
using brpc::policy::mysql::EncodePacketHeader;
using brpc::policy::mysql::PacketHeader;
using brpc::policy::mysql::kMaxPayloadLen;
using brpc::policy::mysql::kPacketHeaderLen;

// ----------------------------------------------------------------------
// length-encoded integer
// ----------------------------------------------------------------------

TEST(LenencIntTest, Decode_1Byte_Zero) {
    const char buf[] = {0x00};
    uint64_t v = 0xdead;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 1), &v), 1u);
    EXPECT_EQ(v, 0u);
}

TEST(LenencIntTest, Decode_1Byte_Max250) {
    const char buf[] = {static_cast<char>(0xfa)};
    uint64_t v = 0;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 1), &v), 1u);
    EXPECT_EQ(v, 0xfau);
}

TEST(LenencIntTest, Decode_2Byte_251) {
    const char buf[] = {static_cast<char>(0xfc), static_cast<char>(0xfb), 0x00};
    uint64_t v = 0;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 3), &v), 3u);
    EXPECT_EQ(v, 251u);
}

TEST(LenencIntTest, Decode_2Byte_Max65535) {
    const char buf[] = {static_cast<char>(0xfc),
                        static_cast<char>(0xff),
                        static_cast<char>(0xff)};
    uint64_t v = 0;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 3), &v), 3u);
    EXPECT_EQ(v, 0xffffu);
}

TEST(LenencIntTest, Decode_3Byte) {
    const char buf[] = {static_cast<char>(0xfd), 0x01, 0x02, 0x03};
    uint64_t v = 0;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 4), &v), 4u);
    EXPECT_EQ(v, 0x030201u);
}

TEST(LenencIntTest, Decode_8Byte) {
    const char buf[] = {static_cast<char>(0xfe),
                        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint64_t v = 0;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 9), &v), 9u);
    EXPECT_EQ(v, 0x0807060504030201ULL);
}

TEST(LenencIntTest, Decode_ReservedFF_ReturnsZero) {
    const char buf[] = {static_cast<char>(0xff)};
    uint64_t v = 0;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 1), &v), 0u);
}

TEST(LenencIntTest, Decode_Truncated_ReturnsZero) {
    const char buf[] = {static_cast<char>(0xfc), 0x01};  // missing 1 byte
    uint64_t v = 0;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 2), &v), 0u);
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 0), &v), 0u);
}

TEST(LenencIntTest, Decode_NullMarkerFB_ReportsNull) {
    const char buf[] = {static_cast<char>(0xfb)};
    uint64_t v = 0xdead;
    bool is_null = false;
    // 0xFB is the NULL marker: 1 byte consumed, value NULL, *out defined to 0.
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 1), &v, &is_null),
              1u);
    EXPECT_TRUE(is_null);
    EXPECT_EQ(v, 0u);
}

TEST(LenencIntTest, Decode_NonNull_SetsIsNullFalse) {
    const char buf[] = {0x05};
    uint64_t v = 0;
    bool is_null = true;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 1), &v, &is_null),
              1u);
    EXPECT_FALSE(is_null);
    EXPECT_EQ(v, 5u);
}

TEST(LenencIntTest, Decode_Failure_DefinesOutAndIsNull) {
    // Reserved 0xFF marker -> failure; *out reset to 0, *is_null to false even
    // though both held stale values, so a careless caller can't read garbage.
    const char buf[] = {static_cast<char>(0xff)};
    uint64_t v = 0xdead;
    bool is_null = true;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 1), &v, &is_null),
              0u);
    EXPECT_FALSE(is_null);
    EXPECT_EQ(v, 0u);
}

TEST(LenencIntTest, Decode_NullMarker_WithoutIsNullArg) {
    // |is_null| is optional; 0xFB without it must not crash and still
    // consumes the single marker byte.
    const char buf[] = {static_cast<char>(0xfb)};
    uint64_t v = 0xdead;
    EXPECT_EQ(DecodeLengthEncodedInt(butil::StringPiece(buf, 1), &v), 1u);
    EXPECT_EQ(v, 0u);
}

TEST(LenencIntTest, Encode_RoundTrip_AllRanges) {
    const uint64_t values[] = {
        0, 1, 250, 251, 0xffff, 0x10000, 0xffffff, 0x1000000, 0xffffffffULL
    };
    for (uint64_t v : values) {
        std::string buf;
        EncodeLengthEncodedInt(v, &buf);
        uint64_t decoded = 0;
        EXPECT_GT(DecodeLengthEncodedInt(buf, &decoded), 0u);
        EXPECT_EQ(decoded, v);
    }
}

// ----------------------------------------------------------------------
// length-encoded string
// ----------------------------------------------------------------------

TEST(LenencStringTest, Empty) {
    std::string buf;
    EncodeLengthEncodedString(butil::StringPiece(""), &buf);
    EXPECT_EQ(buf, std::string("\0", 1));
    std::string out;
    EXPECT_EQ(DecodeLengthEncodedString(buf, &out), 1u);
    EXPECT_TRUE(out.empty());
}

TEST(LenencStringTest, ShortString_RoundTrip) {
    std::string buf;
    EncodeLengthEncodedString(butil::StringPiece("hello"), &buf);
    EXPECT_EQ(buf.size(), 6u);
    std::string out;
    EXPECT_EQ(DecodeLengthEncodedString(buf, &out), 6u);
    EXPECT_EQ(out, "hello");
}

TEST(LenencStringTest, ContainsNul_RoundTrip) {
    std::string buf;
    const std::string value("a\0b\0c", 5);
    EncodeLengthEncodedString(butil::StringPiece(value), &buf);
    std::string out;
    EXPECT_EQ(DecodeLengthEncodedString(buf, &out), 6u);
    EXPECT_EQ(out, value);
}

TEST(LenencStringTest, TruncatedPayload_ReturnsZero) {
    // Encoded length says 10 but only 3 bytes available.
    std::string buf;
    buf.push_back(0x0a);
    buf.append("abc");
    std::string out;
    EXPECT_EQ(DecodeLengthEncodedString(buf, &out), 0u);
}

TEST(LenencStringTest, NullMarkerFB_ReportsNull) {
    // A length-encoded string whose leading lenenc-int is 0xFB is NULL,
    // distinct from the empty string (lenenc 0x00).  Only the marker byte is
    // consumed and out_value is cleared.
    const char buf[] = {static_cast<char>(0xfb), 'x', 'y'};
    std::string out = "stale";
    bool is_null = false;
    EXPECT_EQ(DecodeLengthEncodedString(butil::StringPiece(buf, 3), &out,
                                        &is_null),
              1u);
    EXPECT_TRUE(is_null);
    EXPECT_TRUE(out.empty());
}

TEST(LenencStringTest, NonNull_SetsIsNullFalse) {
    std::string buf;
    EncodeLengthEncodedString(butil::StringPiece("hi"), &buf);
    std::string out;
    bool is_null = true;
    EXPECT_EQ(DecodeLengthEncodedString(buf, &out, &is_null), 3u);
    EXPECT_FALSE(is_null);
    EXPECT_EQ(out, "hi");
}

TEST(LenencStringTest, EmptyIsNotNull) {
    // Empty string (lenenc 0x00) must NOT be reported as NULL.
    std::string buf;
    EncodeLengthEncodedString(butil::StringPiece(""), &buf);
    std::string out = "stale";
    bool is_null = true;
    EXPECT_EQ(DecodeLengthEncodedString(buf, &out, &is_null), 1u);
    EXPECT_FALSE(is_null);
    EXPECT_TRUE(out.empty());
}

// ----------------------------------------------------------------------
// packet header
// ----------------------------------------------------------------------

TEST(PacketHeaderTest, RoundTrip_TypicalSizes) {
    const uint32_t sizes[] = {0u, 1u, 0xffu, 0x100u, 0xffffu, 0x10000u, 0x123456u};
    for (uint32_t s : sizes) {
        PacketHeader in = {s, 7};
        std::string buf;
        EncodePacketHeader(in, &buf);
        ASSERT_EQ(buf.size(), kPacketHeaderLen);
        PacketHeader out;
        ASSERT_TRUE(DecodePacketHeader(buf, &out));
        EXPECT_EQ(out.payload_len, s);
        EXPECT_EQ(out.seq, 7u);
    }
}

TEST(PacketHeaderTest, MaxPayloadLength) {
    PacketHeader in = {kMaxPayloadLen, 0};
    std::string buf;
    EncodePacketHeader(in, &buf);
    PacketHeader out;
    ASSERT_TRUE(DecodePacketHeader(buf, &out));
    EXPECT_EQ(out.payload_len, kMaxPayloadLen);
}

TEST(PacketHeaderTest, SequenceWraparound) {
    PacketHeader in = {0, 255};
    std::string buf;
    EncodePacketHeader(in, &buf);
    PacketHeader out;
    ASSERT_TRUE(DecodePacketHeader(buf, &out));
    EXPECT_EQ(out.seq, 255u);
}

TEST(PacketHeaderTest, Decode_TruncatedReturnsFalse) {
    PacketHeader out;
    EXPECT_FALSE(DecodePacketHeader(butil::StringPiece("\x00\x00\x00", 3), &out));
    EXPECT_FALSE(DecodePacketHeader(butil::StringPiece("", 0), &out));
}

// ----------------------------------------------------------------------
// NUL-terminated string
// ----------------------------------------------------------------------

TEST(NullTermStringTest, HappyPath) {
    const char buf[] = "hello\0extra";
    std::string out;
    EXPECT_EQ(DecodeNullTerminatedString(
                  butil::StringPiece(buf, sizeof(buf) - 1), &out),
              6u);
    EXPECT_EQ(out, "hello");
}

TEST(NullTermStringTest, EmptyString) {
    const char buf[] = "\0rest";
    std::string out;
    EXPECT_EQ(DecodeNullTerminatedString(
                  butil::StringPiece(buf, sizeof(buf) - 1), &out),
              1u);
    EXPECT_TRUE(out.empty());
}

TEST(NullTermStringTest, NoNul_ReturnsZero) {
    std::string out;
    EXPECT_EQ(DecodeNullTerminatedString(butil::StringPiece("abc"), &out), 0u);
}

}  // namespace
