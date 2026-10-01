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

#include <limits>
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

// A truncated length prefix must not borrow bytes from a coalesced next
// packet: the parser only sees the current packet's payload, so even though
// the following packet's bytes would complete the 0xFC value, the OK packet
// is rejected instead of silently desyncing the stream.
TEST(MysqlReplyParseTest, RejectLenEncCrossingPacketBoundary) {
    std::string wire;
    AppendPacket(&wire, 0, std::string("\x00\xFC", 2));  // OK marker + 0xFC prefix
    AppendPacket(&wire, 1, std::string("\x02\x00", 2));  // coalesced next packet

    butil::IOBuf buf;
    buf.append(wire);

    brpc::MysqlReply reply;
    butil::Arena arena;
    bool more_results = false;
    brpc::ParseError rc = reply.ConsumePartialIOBuf(
        buf, &arena, false, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, rc);
}

// An affected-rows value of 2^64-1 (the largest encodable length-encoded
// integer) must round-trip through the uint64_t storage, not be rejected as
// a negative sentinel.
TEST(MysqlReplyParseTest, AcceptUint64MaxAffectedRows) {
    std::string payload;
    payload.push_back('\x00');                    // OK marker
    payload.push_back((char)0xFE);                // 8-byte length-encoded prefix
    payload.append(8, '\xff');                   // affected rows = 2^64 - 1
    payload.push_back('\x00');                    // last insert id = 0
    payload.append("\x02\x00", 2);              // status flags
    payload.append("\x00\x00", 2);              // warnings

    std::string wire;
    AppendPacket(&wire, 0, payload);

    butil::IOBuf buf;
    buf.append(wire);

    brpc::MysqlReply reply;
    butil::Arena arena;
    bool more_results = false;
    brpc::ParseError rc = reply.ConsumePartialIOBuf(
        buf, &arena, false, brpc::MYSQL_NORMAL_STATEMENT, &more_results);
    ASSERT_EQ(brpc::PARSE_OK, rc);
    ASSERT_TRUE(reply.is_ok());
    ASSERT_EQ(std::numeric_limits<uint64_t>::max(), reply.ok().affect_row());
}

// Build a text result set whose header uses |count_wire| as the raw
// length-encoded column count and carries |n_defs| column definitions,
// with no rows.
std::string MakeResultSetWithColumns(const std::string& count_wire, size_t n_defs) {
    std::string wire;
    AppendPacket(&wire, 1, count_wire);
    for (size_t i = 0; i < n_defs; ++i) {
        AppendPacket(&wire, 2, MakeColumnDef());
    }
    AppendPacket(&wire, 3, MakeEof());  // EOF after column defs
    AppendPacket(&wire, 4, MakeEof());  // EOF after (empty) rows
    return wire;
}

brpc::ParseError ParseWire(const std::string& wire,
                           brpc::MysqlReply* reply,
                           butil::Arena* arena,
                           brpc::MysqlStmtType stmt_type = brpc::MYSQL_NORMAL_STATEMENT,
                           bool is_auth = false) {
    butil::IOBuf buf;
    buf.append(wire);
    bool more_results = false;
    return reply->ConsumePartialIOBuf(buf, arena, is_auth, stmt_type, &more_results);
}

// A result set whose column count needs the multi-byte 0xFC form (252..65535
// columns) is a result set, not a prepare-ok: the dispatcher must not match
// the wire byte 0xFC against the synthetic MYSQL_RSP_PREPARE_OK value.
TEST(MysqlReplyParseTest, AcceptMultiByteColumnCount) {
    std::string count_wire;
    count_wire.push_back((char)0xFC);  // 2-byte length-encoded prefix
    count_wire.push_back((char)0x00);  // 256, little-endian
    count_wire.push_back((char)0x01);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_OK, ParseWire(MakeResultSetWithColumns(count_wire, 256), &reply, &arena));
    ASSERT_TRUE(reply.is_resultset());
    ASSERT_EQ(256u, reply.column_count());
}

// Column count 251 needs the 0xFC multi-byte form on the wire (the single
// byte 0xFB is the length-encoded NULL marker, so a compliant server never
// emits it as a count); it must dispatch to the result-set branch even though
// 0xFB/0xFC collide with synthetic MysqlRspType values.
TEST(MysqlReplyParseTest, AcceptSingleByte251ColumnCount) {
    std::string count_wire;
    count_wire.push_back((char)0xFC);  // 2-byte length-encoded prefix
    count_wire.push_back((char)0xFB);  // 251, little-endian
    count_wire.push_back((char)0x00);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_OK, ParseWire(MakeResultSetWithColumns(count_wire, 251), &reply, &arena));
    ASSERT_TRUE(reply.is_resultset());
    ASSERT_EQ(251u, reply.column_count());
}

// A truncated multi-byte column count (0xFC with no value bytes) must be
// rejected by the result-set header parser instead of being misclassified as
// a prepare-ok and read with unchecked fixed-width cuts.
TEST(MysqlReplyParseTest, RejectTruncatedMultiByteColumnCount) {
    std::string wire;
    AppendPacket(&wire, 1, std::string(1, '\xFC'));

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, ParseWire(wire, &reply, &arena));
}

// A long 0xFE-leading packet is a length-encoded column count (>= 2^24), not
// an EOF; it must be rejected by the column-count cap instead of being
// parsed as an EOF packet.
TEST(MysqlReplyParseTest, RejectHugeColumnCountFePrefix) {
    std::string count_wire;
    count_wire.push_back((char)0xFE);  // 8-byte length-encoded prefix
    count_wire.append(8, '\xff');     // claims 2^64 - 1 columns

    std::string wire;
    AppendPacket(&wire, 1, count_wire);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, ParseWire(wire, &reply, &arena));
}

// A short 0xFE-leading packet (< 9 payload bytes) is still an EOF reply.
TEST(MysqlReplyParseTest, AcceptStandaloneEofReply) {
    std::string wire;
    AppendPacket(&wire, 0, MakeEof());

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_OK, ParseWire(wire, &reply, &arena));
    ASSERT_TRUE(reply.is_eof());
}

// A row packet starting with a long 0xFE length-encoded value is a row, not
// an EOF: within a result set, EOF is only a SHORT 0xFE-leading packet.
TEST(MysqlReplyParseTest, RejectRowStartingWithFeLenenc) {
    std::string field;
    field.push_back((char)0xFE);      // 8-byte length-encoded prefix
    field.append(8, '\xff');           // claims a huge value, 0 bytes present

    std::string wire;
    AppendPacket(&wire, 1, std::string(1, '\x01'));  // 1-column result set
    AppendPacket(&wire, 2, MakeColumnDef());
    AppendPacket(&wire, 3, MakeEof());               // EOF after column defs
    AppendPacket(&wire, 4, field);                   // the row
    AppendPacket(&wire, 5, MakeEof());               // real EOF after rows

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, ParseWire(wire, &reply, &arena));
}

// A well-formed prepare-ok reply (COM_STMT_PREPARE response) parses.
TEST(MysqlReplyParseTest, AcceptPrepareOk) {
    std::string payload;
    payload.push_back('\x00');                 // OK-like marker
    payload.append("\x01\x00\x00\x00", 4);  // statement id
    payload.append("\x00\x00", 2);           // column count
    payload.append("\x00\x00", 2);           // param count
    payload.push_back('\x00');                 // filler
    payload.append("\x00\x00", 2);           // warnings

    std::string wire;
    AppendPacket(&wire, 0, payload);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_OK,
              ParseWire(wire, &reply, &arena, brpc::MYSQL_NEED_PREPARE));
    ASSERT_TRUE(reply.is_prepare_ok());
}

// A truncated prepare-ok header must be rejected instead of reading
// uninitialized stack bytes as statement id / column / param counts.
TEST(MysqlReplyParseTest, RejectTruncatedPrepareOk) {
    std::string wire;
    AppendPacket(&wire, 0, std::string("\x00\x01\x00\x00", 4));

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG,
              ParseWire(wire, &reply, &arena, brpc::MYSQL_NEED_PREPARE));
}

}  // namespace
