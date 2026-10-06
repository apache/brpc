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

// A minimal text-protocol column definition, parametrized by field type,
// flags and decimals for binary-protocol tests.
std::string MakeColumnDef(uint8_t type, uint16_t flags = 0, uint8_t decimals = 0) {
    std::string p;
    for (int i = 0; i < 6; ++i) {   // catalog/database/table/origin_table/name/origin_name
        p.push_back(0x00);          // length-encoded empty string
    }
    p.push_back(0x0c);              // length of the fixed-length fields
    p.push_back(0x21);              // charset (2 bytes)
    p.push_back(0x00);
    p.append(4, '\x00');            // column length (4 bytes)
    p.push_back((char)type);        // field type
    p.push_back((char)(flags & 0xFF));  // flag (2 bytes)
    p.push_back((char)(flags >> 8));
    p.push_back((char)decimals);    // decimals
    p.append(2, '\x00');            // filler
    return p;
}

std::string MakeColumnDef() {
    return MakeColumnDef(brpc::MYSQL_FIELD_TYPE_VAR_STRING);
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

// Build a binary-protocol (MYSQL_PREPARED_STATEMENT) result set with one
// column of |col_def| and a single raw binary row |row| (0x00 marker + NULL
// bitmap + field values). A trailing EOF packet is appended right after the
// row, so any test whose row is truncated also proves that decoding cannot
// borrow bytes from the coalesced next packet.
std::string MakeBinaryResultSet(const std::string& col_def,
                                 const std::string& row,
                                 bool with_trailing_eof = true) {
    std::string wire;
    AppendPacket(&wire, 1, std::string(1, '\x01'));  // 1-column result set
    AppendPacket(&wire, 2, col_def);
    AppendPacket(&wire, 3, MakeEof());               // EOF after column defs
    AppendPacket(&wire, 4, row);
    if (with_trailing_eof) {
        AppendPacket(&wire, 5, MakeEof());           // EOF after rows
    }
    return wire;
}

brpc::ParseError ParseWire(const std::string& wire,
                           brpc::MysqlReply* reply,
                           butil::Arena* arena,
                           brpc::MysqlStmtType stmt_type = brpc::MYSQL_NORMAL_STATEMENT,
                           bool is_auth = false,
                           bool protocol41 = true) {
    butil::IOBuf buf;
    buf.append(wire);
    bool more_results = false;
    return reply->ConsumePartialIOBuf(buf, arena, is_auth, stmt_type, &more_results, protocol41);
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

// Column count 251 needs the multi-byte 0xFC form on the wire (the single
// byte 0xFB is the length-encoded NULL marker, so a compliant server never
// emits it as a count); it must dispatch to the result-set branch even though
// 0xFB/0xFC collide with synthetic MysqlRspType values.
TEST(MysqlReplyParseTest, AcceptMultiByte251ColumnCount) {
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

// A handshake greeting that ends before the 4-byte thread id must be
// rejected; previously the unchecked cutn left tmp partially uninitialized
// and Auth::Parse continued successfully with garbage thread id.
TEST(MysqlReplyParseTest, RejectTruncatedGreeting) {
    std::string payload;
    payload.push_back('\x0a');           // protocol version 10
    payload.append("5.7.99-fake\x00", 12);  // NUL-terminated server version
    payload.append("\x01\x02", 2);     // only 2 of the 4 thread-id bytes

    std::string wire;
    AppendPacket(&wire, 0, payload);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, ParseWire(wire, &reply, &arena, brpc::MYSQL_NORMAL_STATEMENT, /*is_auth=*/true));
}

// A handshake greeting whose server version is not NUL-terminated (the
// delimiter was never reached) must be rejected.
TEST(MysqlReplyParseTest, RejectUnterminatedGreetingVersion) {
    std::string payload;
    payload.push_back('\x0a');          // protocol version 10
    payload.append("5.7.99-fake", 10);  // no NUL terminator

    std::string wire;
    AppendPacket(&wire, 0, payload);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, ParseWire(wire, &reply, &arena, brpc::MYSQL_NORMAL_STATEMENT, /*is_auth=*/true));
}

// A bare 0xFB (the length-encoded NULL marker, never a legal column count)
// must not be accepted as a zero-column result set even when followed by
// well-formed EOF packets.
TEST(MysqlReplyParseTest, RejectBareFbHeader) {
    std::string wire;
    AppendPacket(&wire, 1, std::string(1, '\xFB'));
    AppendPacket(&wire, 2, MakeEof());
    AppendPacket(&wire, 3, MakeEof());

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, ParseWire(wire, &reply, &arena));
}

// An explicitly encoded zero column count (0xFC 0x00 0x00) is not a legal
// result-set header either; a result set always carries at least one column.
TEST(MysqlReplyParseTest, RejectZeroColumnCount) {
    std::string count_wire;
    count_wire.push_back((char)0xFC);
    count_wire.push_back((char)0x00);
    count_wire.push_back((char)0x00);

    std::string wire;
    AppendPacket(&wire, 1, count_wire);
    AppendPacket(&wire, 2, MakeEof());
    AppendPacket(&wire, 3, MakeEof());

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, ParseWire(wire, &reply, &arena));
}

// A column definition truncated in its fixed tail (missing the filler byte
// after origin_name, or missing the final 2 reserved bytes after decimal)
// must be rejected instead of being accepted via an unchecked pop_front.
TEST(MysqlReplyParseTest, RejectTruncatedColumnDefTail) {
    // |drop_tail| bytes removed from a complete column definition.
    for (size_t drop_tail = 1; drop_tail <= 3; ++drop_tail) {
        std::string col = MakeColumnDef();
        col.resize(col.size() - drop_tail);

        std::string wire;
        AppendPacket(&wire, 1, std::string(1, '\x01'));  // 1-column result set
        AppendPacket(&wire, 2, col);

        brpc::MysqlReply reply;
        butil::Arena arena;
        ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, ParseWire(wire, &reply, &arena))
            << "drop_tail " << drop_tail;
    }
}

// An initial-handshake ERR packet (sent before CLIENT_PROTOCOL_41 is
// negotiated, e.g. "Too many connections") carries no '#' and sql_state;
// it must parse with the whole tail as the message instead of being
// rejected as malformed.
TEST(MysqlReplyParseTest, AcceptInitialHandshakeErr) {
    std::string payload;
    payload.push_back((char)0xFF);               // ERR marker
    payload.append("\x10\x04", 2);            // error code 1040, little-endian
    payload.append("Too many connections");    // message, no '#' + sql_state

    std::string wire;
    AppendPacket(&wire, 0, payload);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_OK,
              ParseWire(wire, &reply, &arena, brpc::MYSQL_NORMAL_STATEMENT,
                        /*is_auth=*/true, /*protocol41=*/false));
    ASSERT_TRUE(reply.is_error());
    ASSERT_EQ(1040u, reply.error().errcode());
    ASSERT_EQ(butil::StringPiece("Too many connections"), reply.error().msg());
}

// A legacy (pre-4.1) ERR message that itself starts with '#' must be kept
// intact; the first message byte cannot be used to sniff the layout.
TEST(MysqlReplyParseTest, AcceptLegacyErrMessageStartingWithHash) {
    std::string payload;
    payload.push_back((char)0xFF);               // ERR marker
    payload.append("\x60\x04", 2);            // error code 1120, little-endian
    payload.append("#quota exceeded");          // message starting with '#'

    std::string wire;
    AppendPacket(&wire, 0, payload);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_OK,
              ParseWire(wire, &reply, &arena, brpc::MYSQL_NORMAL_STATEMENT,
                        /*is_auth=*/true, /*protocol41=*/false));
    ASSERT_TRUE(reply.is_error());
    ASSERT_EQ(1120u, reply.error().errcode());
    ASSERT_EQ(butil::StringPiece("#quota exceeded"), reply.error().msg());
    ASSERT_TRUE(reply.error().status().empty());

    // The same short message must not be rejected as a truncated sql_state.
    std::string short_payload;
    short_payload.push_back((char)0xFF);
    short_payload.append("\x50\x04", 2);
    short_payload.append("#bad");
    std::string short_wire;
    AppendPacket(&short_wire, 0, short_payload);

    brpc::MysqlReply short_reply;
    butil::Arena short_arena;
    ASSERT_EQ(brpc::PARSE_OK,
              ParseWire(short_wire, &short_reply, &short_arena, brpc::MYSQL_NORMAL_STATEMENT,
                        /*is_auth=*/true, /*protocol41=*/false));
    ASSERT_EQ(butil::StringPiece("#bad"), short_reply.error().msg());
}

// A protocol-4.1 ERR whose message itself starts with '#': the marker and
// sql_state still come from the wire layout, and the message keeps its '#'.
TEST(MysqlReplyParseTest, AcceptProtocol41ErrMessageStartingWithHash) {
    std::string payload;
    payload.push_back((char)0xFF);               // ERR marker
    payload.append("\x1f\x04", 2);            // error code 1055, little-endian
    payload.push_back('#');
    payload.append("42000", 5);                 // sql state
    payload.append("#boom", 5);                 // message starting with '#'

    std::string wire;
    AppendPacket(&wire, 0, payload);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_OK, ParseWire(wire, &reply, &arena));
    ASSERT_TRUE(reply.is_error());
    ASSERT_EQ(1055u, reply.error().errcode());
    ASSERT_EQ(butil::StringPiece("42000"), reply.error().status());
    ASSERT_EQ(butil::StringPiece("#boom"), reply.error().msg());
}

// A protocol-4.1 ERR packet whose '#' marker is present but whose sql_state
// is truncated must still be rejected.
TEST(MysqlReplyParseTest, RejectTruncatedSqlState) {
    std::string payload;
    payload.push_back((char)0xFF);               // ERR marker
    payload.append("\x1f\x04", 2);            // error code 1055, little-endian
    payload.append("#AB", 3);                  // '#' but only 2 of 5 state bytes

    std::string wire;
    AppendPacket(&wire, 0, payload);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG, ParseWire(wire, &reply, &arena));
}

// A well-formed protocol-4.1 ERR packet ('#' + 5-byte sql_state + message)
// still parses with its sql_state populated.
TEST(MysqlReplyParseTest, AcceptProtocol41Err) {
    std::string payload;
    payload.push_back((char)0xFF);               // ERR marker
    payload.append("\x1f\x04", 2);            // error code 1055, little-endian
    payload.push_back('#');
    payload.append("42000", 5);                 // sql state
    payload.append("boom", 4);                  // message

    std::string wire;
    AppendPacket(&wire, 0, payload);

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_OK, ParseWire(wire, &reply, &arena));
    ASSERT_TRUE(reply.is_error());
    ASSERT_EQ(1055u, reply.error().errcode());
    ASSERT_EQ(butil::StringPiece("42000"), reply.error().status());
    ASSERT_EQ(butil::StringPiece("boom"), reply.error().msg());
}

// A well-formed binary-protocol row parses and round-trips its fixed-width
// value.
TEST(MysqlReplyParseTest, AcceptBinaryRow) {
    std::string row;
    row.push_back('\x00');            // binary row marker
    row.push_back('\x00');            // NULL bitmap (1 byte for 1 column): no NULLs
    row.append(8, '\x2a');            // 8-byte unsigned LONGLONG value

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_OK,
              ParseWire(MakeBinaryResultSet(
                            MakeColumnDef(brpc::MYSQL_FIELD_TYPE_LONGLONG,
                                          brpc::MYSQL_UNSIGNED_FLAG),
                            row),
                        &reply, &arena, brpc::MYSQL_PREPARED_STATEMENT));
    ASSERT_TRUE(reply.is_resultset());
    ASSERT_EQ(1u, reply.row_count());
    ASSERT_EQ(0x2a2a2a2a2a2a2a2aULL, reply.next().field(0).bigint());
}

// A binary row truncated inside its NULL bitmap (or missing it entirely)
// must be rejected, without borrowing the coalesced following EOF packet.
TEST(MysqlReplyParseTest, RejectTruncatedBinaryNullBitmap) {
    std::string row;
    row.push_back('\x00');            // marker only, NULL bitmap missing

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG,
              ParseWire(MakeBinaryResultSet(
                            MakeColumnDef(brpc::MYSQL_FIELD_TYPE_LONGLONG,
                                          brpc::MYSQL_UNSIGNED_FLAG),
                            row),
                        &reply, &arena, brpc::MYSQL_PREPARED_STATEMENT));
}

// A binary row whose fixed-width numeric value ends early must be rejected;
// the missing bytes cannot be taken from the coalesced EOF packet.
TEST(MysqlReplyParseTest, RejectTruncatedBinaryFixedValue) {
    std::string row;
    row.push_back('\x00');            // binary row marker
    row.push_back('\x00');            // NULL bitmap
    row.append(4, '\x2a');            // only 4 of the 8 LONGLONG bytes

    brpc::MysqlReply reply;
    butil::Arena arena;
    ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG,
              ParseWire(MakeBinaryResultSet(
                            MakeColumnDef(brpc::MYSQL_FIELD_TYPE_LONGLONG,
                                          brpc::MYSQL_UNSIGNED_FLAG),
                            row),
                        &reply, &arena, brpc::MYSQL_PREPARED_STATEMENT));
}

// A binary string field whose length-encoded prefix or value is truncated
// must be rejected.
TEST(MysqlReplyParseTest, RejectTruncatedBinaryStringField) {
    const std::string tails[] = {
        std::string("\xFC", 1),                    // prefix, 0 of 2 length bytes
        std::string("\xFC\x05\x00"
                    "ab",
                    5),                             // length 5, only 2 bytes follow
    };
    for (size_t i = 0; i < sizeof(tails) / sizeof(tails[0]); ++i) {
        std::string row;
        row.push_back('\x00');        // binary row marker
        row.push_back('\x00');        // NULL bitmap
        row.append(tails[i]);

        brpc::MysqlReply reply;
        butil::Arena arena;
        ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG,
                  ParseWire(MakeBinaryResultSet(MakeColumnDef(), row), &reply, &arena,
                            brpc::MYSQL_PREPARED_STATEMENT))
            << "case " << i;
    }
}

// Binary TIME/DATETIME values whose length-encoded length promises more
// bytes than the row carries must be rejected.
TEST(MysqlReplyParseTest, RejectTruncatedBinaryTimeAndDatetime) {
    const struct {
        uint8_t type;
        uint8_t len;
        size_t value_bytes;
    } cases[] = {
        {brpc::MYSQL_FIELD_TYPE_TIME, 8, 3},       // TIME claims 8, has 3
        {brpc::MYSQL_FIELD_TYPE_DATETIME, 4, 1},   // DATETIME claims 4, has 1
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        std::string row;
        row.push_back('\x00');        // binary row marker
        row.push_back('\x00');        // NULL bitmap
        row.push_back((char)cases[i].len);
        row.append(cases[i].value_bytes, '\x11');

        brpc::MysqlReply reply;
        butil::Arena arena;
        ASSERT_EQ(brpc::PARSE_ERROR_ABSOLUTELY_WRONG,
                  ParseWire(MakeBinaryResultSet(MakeColumnDef(cases[i].type), row),
                            &reply, &arena, brpc::MYSQL_PREPARED_STATEMENT))
            << "case " << i;
    }
}

}  // namespace
