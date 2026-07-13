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

}  // namespace
