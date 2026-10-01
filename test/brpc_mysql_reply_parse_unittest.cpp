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
#include <vector>

#include "butil/iobuf.h"
#include "butil/arena.h"
#include "brpc/policy/mysql/mysql_reply.h"
#include "brpc/policy/mysql/mysql_common.h"

namespace {

// Append a MySQL packet: 3-byte little-endian payload length + 1-byte
// sequence id, followed by the payload.
void AppendPacket(std::string* out, uint8_t seq, const std::string& payload) {
    const uint32_t len = (uint32_t)payload.size();
    out->push_back((char)(len & 0xFF));
    out->push_back((char)((len >> 8) & 0xFF));
    out->push_back((char)((len >> 16) & 0xFF));
    out->push_back((char)seq);
    out->append(payload);
}

// A minimal text-protocol column definition for a single VAR_STRING column.
std::string MakeColumnDef() {
    std::string p;
    for (int i = 0; i < 6; ++i) {   // catalog/database/table/origin_table/name/origin_name
        p.push_back(0x00);          // length-encoded empty string
    }
    p.push_back(0x0c);              // length of the fixed-length fields
    p.push_back(0x21);              // charset (2 bytes)
    p.push_back(0x00);
    p.append(4, '\x00');            // column length (4 bytes)
    p.push_back((char)brpc::MYSQL_FIELD_TYPE_VAR_STRING);   // field type
    p.push_back(0x00);              // flag (2 bytes): not-null and unsigned both off
    p.push_back(0x00);
    p.push_back(0x00);              // decimals
    p.append(2, '\x00');            // filler
    return p;
}

std::string MakeEof() {
    std::string p;
    p.push_back((char)0xFE);        // EOF header
    p.append(4, '\x00');            // warnings (2) + status flags (2)
    return p;
}

// A text result set carrying a single VAR_STRING column and one row whose only
// field is |field_payload| (the length-encoded field value as raw bytes).
std::string MakeTextResultSet(const std::string& field_payload,
                              bool with_trailing_eof) {
    std::string buf;
    AppendPacket(&buf, 1, std::string(1, '\x01'));  // result-set header, 1 column
    AppendPacket(&buf, 2, MakeColumnDef());
    AppendPacket(&buf, 3, MakeEof());               // EOF after column defs
    AppendPacket(&buf, 4, field_payload);           // the row
    if (with_trailing_eof) {
        AppendPacket(&buf, 5, MakeEof());           // EOF after rows
    }
    return buf;
}

// A row field whose length-encoded prefix claims far more bytes than the packet
// (0xFD + 3-byte length 0xFFFFFF) must be rejected rather than clamped. Before
// the fix cutn would clamp to what remained, allocate the full claimed length,
// and publish a StringPiece over uninitialized arena memory while desyncing the
// stream.
TEST(MysqlReplyParseTest, RejectOversizedTextFieldLength) {
    std::string field;
    field.push_back((char)0xFD);    // 3-byte length-encoded prefix
    field.push_back((char)0xFF);
    field.push_back((char)0xFF);
    field.push_back((char)0xFF);    // claims 0xFFFFFF bytes, none of which follow

    butil::IOBuf buf;
    buf.append(MakeTextResultSet(field, false));

    brpc::MysqlReply reply;
    butil::Arena arena;
    bool more_results = false;
    brpc::ParseError rc = reply.ConsumePartialIOBuf(
        buf, &arena, false, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, rc);
}

TEST(MysqlReplyParseTest, RejectZeroPayloadPacket) {
    butil::IOBuf buf;
    buf.append(std::string("\x00\x00\x00\x01", 4));

    brpc::MysqlReply reply;
    butil::Arena arena;
    bool more_results = false;
    brpc::ParseError rc = reply.ConsumePartialIOBuf(
        buf, &arena, false, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, rc);
}

TEST(MysqlReplyParseTest, RejectZeroPayloadPacketWithTrailingBytes) {
    std::string wire("\x00\x00\x00\x01", 4);
    AppendPacket(&wire, 2, std::string("\x00\x00\x00\x00\x00\x00\x00", 7));
    butil::IOBuf buf;
    buf.append(wire);

    brpc::MysqlReply reply;
    butil::Arena arena;
    bool more_results = false;
    brpc::ParseError rc = reply.ConsumePartialIOBuf(
        buf, &arena, false, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, rc);
}

TEST(MysqlReplyParseTest, RejectZeroPayloadPacketAfterFastAuthMarker) {
    std::string wire;
    AppendPacket(&wire, 2, std::string("\x01\x03", 2));
    wire.append(std::string("\x00\x00\x00\x03", 4));
    butil::IOBuf buf;
    buf.append(wire);

    brpc::MysqlReply reply;
    butil::Arena arena;
    bool more_results = false;
    brpc::ParseError rc = reply.ConsumePartialIOBuf(
        buf, &arena, true, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, rc);
}

// A well-formed field whose length matches the bytes present still parses, so
// the guard does not reject legitimate result sets.
TEST(MysqlReplyParseTest, AcceptWellFormedTextField) {
    std::string field;
    field.push_back((char)0x02);    // length-encoded length 2
    field.append("hi");

    butil::IOBuf buf;
    buf.append(MakeTextResultSet(field, true));

    brpc::MysqlReply reply;
    butil::Arena arena;
    bool more_results = false;
    brpc::ParseError rc = reply.ConsumePartialIOBuf(
        buf, &arena, false, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
    ASSERT_EQ(brpc::PARSE_OK, rc);
    ASSERT_TRUE(reply.is_resultset());
    ASSERT_EQ(1u, reply.column_count());
    ASSERT_EQ(1u, reply.row_count());
    ASSERT_EQ("hi", reply.next().field(0).string());
}

// A multi-byte (0xFC-prefixed) length-encoded integer whose value bytes are
// all present must still decode correctly.
TEST(MysqlReplyParseTest, AcceptMultiByteTextFieldLength) {
    std::string field;
    field.push_back((char)0xFC);    // 2-byte length-encoded prefix
    field.push_back((char)0x04);    // length 4
    field.push_back((char)0x00);
    field.append("abcd");

    butil::IOBuf buf;
    buf.append(MakeTextResultSet(field, true));

    brpc::MysqlReply reply;
    butil::Arena arena;
    bool more_results = false;
    brpc::ParseError rc = reply.ConsumePartialIOBuf(
        buf, &arena, false, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
    ASSERT_EQ(brpc::PARSE_OK, rc);
    ASSERT_TRUE(reply.is_resultset());
    ASSERT_EQ("abcd", reply.next().field(0).string());
}

// A column definition whose table-name length-encoded integer is truncated
// (0xFE prefix promises 8 value bytes but only 2 follow) must be rejected.
// Before the fix, the parser read uninitialized stack memory as the length.
TEST(MysqlReplyParseTest, RejectTruncatedLenEncInColumnDef) {
    // (prefix, number of value bytes actually appended)
    const struct {
        uint8_t prefix;
        size_t value_bytes;
    } cases[] = {
        {0xFC, 0},  // needs 2, has 0
        {0xFC, 1},  // needs 2, has 1
        {0xFD, 1},  // needs 3, has 1
        {0xFD, 2},  // needs 3, has 2
        {0xFE, 2},  // needs 8, has 2
        {0xFE, 7},  // needs 8, has 7
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        std::string col;
        col.push_back(3);
        col.append("def");           // catalog
        col.push_back(2);
        col.append("db");            // database
        col.push_back((char)cases[i].prefix);
        col.append(cases[i].value_bytes, '\xff');  // truncated table length

        std::string wire;
        AppendPacket(&wire, 1, std::string(1, '\x01'));  // 1-column result set
        AppendPacket(&wire, 2, col);

        butil::IOBuf buf;
        buf.append(wire);

        brpc::MysqlReply reply;
        butil::Arena arena;
        bool more_results = false;
        brpc::ParseError rc = reply.ConsumePartialIOBuf(
            buf, &arena, false, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
        ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, rc) << "case " << i;
    }
}

// A row field whose length-encoded prefix promises value bytes that are not
// in the packet must be rejected instead of reading uninitialized stack bytes.
// NOTE: a 0xFE-prefixed row field cannot be tested here because a text row
// whose first byte is 0xFE is indistinguishable from an EOF packet; that
// prefix is covered by RejectTruncatedLenEncInColumnDef above.
TEST(MysqlReplyParseTest, RejectTruncatedLenEncFieldLength) {
    const struct {
        uint8_t prefix;
        size_t value_bytes;
    } cases[] = {
        {0xFC, 0},  // needs 2, has 0
        {0xFD, 1},  // needs 3, has 1
        {0xFD, 2},  // needs 3, has 2
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        std::string field;
        field.push_back((char)cases[i].prefix);
        field.append(cases[i].value_bytes, '\xff');

        butil::IOBuf buf;
        buf.append(MakeTextResultSet(field, false));

        brpc::MysqlReply reply;
        butil::Arena arena;
        bool more_results = false;
        brpc::ParseError rc = reply.ConsumePartialIOBuf(
            buf, &arena, false, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
        ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, rc) << "case " << i;
    }
}

// An OK packet whose affected-rows length-encoded integer is truncated must
// be rejected (previously the uninitialized stack value was used directly).
TEST(MysqlReplyParseTest, RejectTruncatedOkPacket) {
    const std::string payloads[] = {
        std::string("\x00\xFC", 2),          // 0x00 marker + 0xFC, no value bytes
        std::string("\x00\xFE\xff\xff", 4),  // 0x00 marker + 0xFE, only 2 of 8 bytes
    };
    for (size_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); ++i) {
        std::string wire;
        AppendPacket(&wire, 0, payloads[i]);

        butil::IOBuf buf;
        buf.append(wire);

        brpc::MysqlReply reply;
        butil::Arena arena;
        bool more_results = false;
        brpc::ParseError rc = reply.ConsumePartialIOBuf(
            buf, &arena, false, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
        ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, rc) << "case " << i;
    }
}

}  // namespace
