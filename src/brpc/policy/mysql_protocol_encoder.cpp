/*
  Copyright (c) 2017, 2018, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "brpc/policy/mysql_protocol.h"
#include "brpc/policy/mysql_protocol_encoder.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace brpc {
namespace policy {

::butil::IOBuf MySQLProtocolEncoder::EncodeOKMessage(
    uint8_t seq_no, uint64_t affected_rows, uint64_t last_insert_id,
    uint16_t status, uint16_t warnings) {

    ::brpc::policy::MysqlProtocolPacketBody body;
    body.AppendByte(0x0);
    body.AppendLenencInt(affected_rows);
    body.AppendLenencInt(last_insert_id);
    body.AppendInt(status);
    body.AppendInt(warnings);

    ::brpc::policy::MysqlProtocolPacketHeader header;
    header.SetPayloadLength(body.length())
        .SetSequenceId(seq_no);

    ::butil::IOBuf packet;
    header.AppendToIOBuf(packet);
    packet.append(body.buf().movable());

    return packet;
}

::butil::IOBuf MySQLProtocolEncoder::EncodeErrorMessage(
    uint8_t seq_no, uint16_t error_code, const std::string &sql_state,
    const std::string &error_msg) {

    ::brpc::policy::MysqlProtocolPacketBody body;
    body.AppendByte(0xff);
    body.AppendInt(error_code);
    body.AppendByte(0x23); // "#"
    if (sql_state.length() != 5) {
        LOG(INFO) << "sql state is invalid[sql=" << sql_state << "]. Use \"HY000\" instead.";
        body.AppendString(std::string("HY000"));
    } else {
        body.AppendString(sql_state);
    }
    body.AppendString(error_msg);

    ::brpc::policy::MysqlProtocolPacketHeader header;
    header.SetPayloadLength(body.length())
        .SetSequenceId(seq_no);

    ::butil::IOBuf packet;
    header.AppendToIOBuf(packet);
    packet.append(body.buf().movable());

    return packet;
}

MySQLProtocolEncoder::MsgBuffer MySQLProtocolEncoder::encode_auth_fast_message(
    uint8_t seq_no) {
  MsgBuffer out_buffer;

  encode_msg_begin(out_buffer);

  append_byte(out_buffer, 0x03);

  encode_msg_end(out_buffer, seq_no);

  return out_buffer;
}

butil::IOBuf MySQLProtocolEncoder::EncodeGreetingsMessage(
    uint8_t seq_no, const std::string &mysql_version, uint32_t connection_id,
    std::string auth_plugin_data /*= 20-byte str*/,
    ::brpc::policy::Capabilities::Flags capabilities /* =... */,
    const std::string &auth_plugin_name /*= ... */,
    uint8_t character_set /* = 0 */, uint16_t status_flags /* = 0 */) {
  ////////////////////////////////////////////////////////////////////////////////
  //
  // This is the layout of the Protocol::HandshakeV10 packet, according to:
  // https://dev.mysql.com/doc/internals/en/connection-phase-packets.html#packet-Protocol::Handshake
  //
  //   1              [0a] protocol version
  //   string[NUL]    server version
  //   4              connection id
  //   string[8]      auth-plugin-data-part-1
  //   1              [00] filler
  //   2              capability flags (lower 2 bytes)
  //   if more data in the packet {
  //     1              character set
  //     2              status flags
  //     2              capability flags (upper 2 bytes)
  //     if capabilities & CLIENT_PLUGIN_AUTH {
  //       1              length of auth-plugin-data
  //     } else {
  //       1              [00]
  //     }
  //     string[10]     reserved (all [00])
  //     if capabilities & CLIENT_SECURE_CONNECTION {
  //       string[$len]   auth-plugin-data-part-2 ($len=MAX(13, length of
  //       auth-plugin-data - 8))
  //     }
  //     if capabilities & CLIENT_PLUGIN_AUTH {
  //       string[NUL]    auth-plugin name
  //     }
  //   }
  //
  // NOTE: auth-plugin-data-part-2 must contain 0 as its last byte!
  //
  ////////////////////////////////////////////////////////////////////////////////

  // make sure the caller did not already add the final \0, which we are about
  // to do
//  assert(auth_plugin_data.back() !=
//         0);  // remove this assert if our auth-plugin-data needs
//              // to support 0's in its payload (so far we just use text)
//              // so the assertion works correctly)
  assert(auth_plugin_data.size() >
         8);  // our implementation might not work for len <= 8 bytes

  // add the required 0-terminator to auth-plugin-data
  auth_plugin_data.push_back(
      0);  // auth-plugin-data must have its last byte as 0

  ::brpc::policy::MysqlProtocolPacketBody body;

  // protocol version
  body.AppendByte(0x0a);

  // server version
  body.AppendString(mysql_version);
  body.AppendByte(0x0); // string terminator

  // connection id
  body.AppendInt(connection_id);

  // auth-plugin-data-part-1
  body.AppendString(auth_plugin_data.substr(0, 8));

  // [00] filler
  body.AppendByte(0x0);

  // capability flags (lower 2 bytes)
  body.AppendInt(capabilities.low_16_bits());

  // character set
  body.AppendByte(character_set);

  // status flags
  body.AppendInt(status_flags);

  // capability flags (upper 2 bytes)
  body.AppendInt(capabilities.high_16_bits());

  // if capabilities & CLIENT_PLUGIN_AUTH {
  //   1              length of auth-plugin-data
  // } else {
  //   1              [00]
  // }
  if (capabilities.test(::brpc::policy::Capabilities::PLUGIN_AUTH)) {
    body.AppendByte(auth_plugin_data.size());
  } else {
    body.AppendByte(0x0);
  }

  // 10 reserved zero bytes
  body.AppendString(std::string(10, '\0'));

  // if capabilities & CLIENT_SECURE_CONNECTION {
  //   string[$len]   auth-plugin-data-part-2 ($len=MAX(13, length of
  //   auth-plugin-data - 8))
  // }
  if (capabilities.test(::brpc::policy::Capabilities::SECURE_CONNECTION)) {
    body.AppendString(auth_plugin_data.substr(8));
  }

  // if capabilities & CLIENT_PLUGIN_AUTH {
  //   string[NUL]    auth-plugin name
  // }
  if (capabilities.test(::brpc::policy::Capabilities::PLUGIN_AUTH)) {
    body.AppendString(auth_plugin_name);
    body.AppendByte(0x0); // string-terminator
  }

  ::brpc::policy::MysqlProtocolPacketHeader header;
  header.SetPayloadLength(body.length())
      .SetSequenceId(seq_no);

  ::butil::IOBuf packet;
  header.AppendToIOBuf(packet);
  packet.append(body.buf().movable());

  return packet;
}

MySQLProtocolEncoder::MsgBuffer
MySQLProtocolEncoder::encode_auth_switch_message(
    uint8_t seq_no, const std::string &auth_plugin_name,
    const std::string &auth_plugin_data) {
  ////////////////////////////////////////////////////////////////////////////////
  //
  // This is the layout of the Protocol::AuthSwitchRequest
  //
  //   int<1>       0xfe (254)
  //   string[NUL]  auth-plugin-name
  //   string[EOF]  auth-plugin-data
  //
  // NOTE: auth-plugin-data must contain 0 as its last byte!
  //
  ////////////////////////////////////////////////////////////////////////////////

  // make sure the caller did not already add the final \0, we will do this
  // ourselves in this method
  assert(auth_plugin_data.back() !=
         0);  // remove this assert if our auth-plugin-data needs
              // to support 0's in its payload (so far we just use text,
              // so the assertion works correctly)

  MsgBuffer out_buffer;
  encode_msg_begin(out_buffer);

  append_byte(out_buffer, 0xfe);
  append_str(out_buffer, auth_plugin_name);
  append_byte(out_buffer, 0x0);
  append_str(out_buffer, auth_plugin_data);
  append_byte(out_buffer, 0x0);  // add the required 0 byte

  encode_msg_end(out_buffer, seq_no);
  return out_buffer;
}

::butil::IOBuf MySQLProtocolEncoder::EncodeColumnsNumberMessage(
        uint8_t seq_no,
        uint64_t number) {
    ::brpc::policy::MysqlProtocolPacketBody body;
    body.AppendLenencInt(number);

    ::brpc::policy::MysqlProtocolPacketHeader header;
    header.SetPayloadLength(body.length())
        .SetSequenceId(seq_no);

    ::butil::IOBuf packet;
    header.AppendToIOBuf(packet);
    packet.append(body.buf().movable());

    return packet;
}

::butil::IOBuf MySQLProtocolEncoder::EncodeColumnMetaMessage(
        uint8_t seq_no, const ::brpc::policy::ResultMeta& meta) {
    ::brpc::policy::MysqlProtocolPacketBody body2;
    body2.AppendInt(static_cast<uint16_t>(meta.charset_set()));
    body2.AppendInt(static_cast<uint32_t>(meta.length()));
    body2.AppendInt(static_cast<uint8_t>(meta.type()));
    body2.AppendInt(static_cast<uint16_t>(meta.flags()));
    body2.AppendInt(static_cast<uint8_t>(meta.decimals()));
    body2.AppendInt(static_cast<uint16_t>(0U));

    ::brpc::policy::MysqlProtocolPacketBody body;
    body.AppendLenencStr(meta.catalog());
    body.AppendLenencStr(meta.schema());
    body.AppendLenencStr(meta.table());
    body.AppendLenencStr(meta.org_table());
    body.AppendLenencStr(meta.name());
    body.AppendLenencStr(meta.org_name());
    body.AppendLenencInt(static_cast<uint64_t>(body2.length()));

    ::brpc::policy::MysqlProtocolPacketHeader header;
    header.SetPayloadLength(body.length() + body2.length())
        .SetSequenceId(seq_no);

    ::butil::IOBuf packet;
    header.AppendToIOBuf(packet);
    packet.append(body.buf().movable());
    packet.append(body2.buf().movable());

    return packet;
}

::butil::IOBuf EncodeRowMessage(
        uint8_t seq_no, const ::brpc::policy::ResultRow& row) {
    ::brpc::policy::MysqlProtocolPacketBody body;

    for (auto i = 0; i < row.row_field_size(); ++i) {
        if (!row.row_field(i).is_null()) {
            body.AppendLenencStr(row.row_field(i).field_value());
        } else {
            body.AppendByte(0xfb); // NULL
        }
    }

    ::brpc::policy::MysqlProtocolPacketHeader header;
    header.SetPayloadLength(body.length())
        .SetSequenceId(seq_no);

    ::butil::IOBuf packet;
    header.AppendToIOBuf(packet);
    packet.append(body.buf().movable());

    return packet;
}

::butil::IOBuf MySQLProtocolEncoder::EncodeEOFMessage(
        uint8_t seq_no, uint16_t status, uint16_t warnings) {
    ::brpc::policy::MysqlProtocolPacketBody body;
    body.AppendByte(0xfe);
    body.AppendInt(status);
    body.AppendInt(warnings);

    ::brpc::policy::MysqlProtocolPacketHeader header;
    header.SetPayloadLength(body.length())
        .SetSequenceId(seq_no);

    ::butil::IOBuf packet;
    header.AppendToIOBuf(packet);
    packet.append(body.buf().movable());

    return packet;
}

void MySQLProtocolEncoder::encode_msg_begin(MsgBuffer &out_buffer) {
  // reserve space for header
  append_int(out_buffer, static_cast<uint32_t>(0x0));
}

void MySQLProtocolEncoder::encode_msg_end(MsgBuffer &out_buffer,
                                          uint8_t seq_no) {
  assert(out_buffer.size() >= 4);
  // fill the header
  uint32_t msg_len = static_cast<uint32_t>(out_buffer.size()) - 4;
  if (msg_len > 0xffffff) {
    throw std::runtime_error("Invalid message length: " +
                             std::to_string(msg_len));
  }
  uint32_t header = msg_len | static_cast<uint32_t>(seq_no << 24);

  auto len = sizeof(header);
  for (size_t i = 0; len > 0; ++i, --len) {
    out_buffer[i] = static_cast<byte>(header);
    header = static_cast<decltype(header)>(header >> 8);
  }
}

void MySQLProtocolEncoder::append_byte(MsgBuffer &buffer, byte value) {
  buffer.push_back(value);
}

void MySQLProtocolEncoder::append_str(MsgBuffer &buffer,
                                      const std::string &value) {
  buffer.insert(buffer.end(), value.begin(), value.end());
}

void MySQLProtocolEncoder::append_buffer(MsgBuffer &buffer,
                                         const MsgBuffer &value) {
  buffer.insert(buffer.end(), value.begin(), value.end());
}

void MySQLProtocolEncoder::append_lenenc_int(MsgBuffer &buffer, uint64_t val) {
  if (val < 251) {
    append_byte(buffer, static_cast<byte>(val));
  } else if (val < (1 << 16)) {
    append_byte(buffer, 0xfc);
    append_int(buffer, static_cast<uint16_t>(val));
  } else {
    append_byte(buffer, 0xfe);
    append_int(buffer, val);
  }
}

void MySQLProtocolEncoder::append_lenenc_str(MsgBuffer &buffer,
                                             const std::string &value) {
  append_lenenc_int(buffer, value.length());
  append_str(buffer, value);
}

MySQLColumnType column_type_from_string(const std::string &type) {
  int res = 0;

  try {
    res = std::stoi(type);
  } catch (const std::invalid_argument &) {
    if (type == "DECIMAL") return MySQLColumnType::DECIMAL;
    if (type == "TINY") return MySQLColumnType::TINY;
    if (type == "SHORT") return MySQLColumnType::SHORT;
    if (type == "LONG") return MySQLColumnType::LONG;
    if (type == "INT24") return MySQLColumnType::INT24;
    if (type == "LONGLONG") return MySQLColumnType::LONGLONG;
    if (type == "DECIMAL") return MySQLColumnType::DECIMAL;
    if (type == "NEWDECIMAL") return MySQLColumnType::NEWDECIMAL;
    if (type == "FLOAT") return MySQLColumnType::FLOAT;
    if (type == "DOUBLE") return MySQLColumnType::DOUBLE;
    if (type == "BIT") return MySQLColumnType::BIT;
    if (type == "TIMESTAMP") return MySQLColumnType::TIMESTAMP;
    if (type == "DATE") return MySQLColumnType::DATE;
    if (type == "TIME") return MySQLColumnType::TIME;
    if (type == "DATETIME") return MySQLColumnType::DATETIME;
    if (type == "YEAR") return MySQLColumnType::YEAR;
    if (type == "STRING") return MySQLColumnType::STRING;
    if (type == "VAR_STRING") return MySQLColumnType::VAR_STRING;
    if (type == "BLOB") return MySQLColumnType::BLOB;
    if (type == "SET") return MySQLColumnType::SET;
    if (type == "ENUM") return MySQLColumnType::ENUM;
    if (type == "GEOMETRY") return MySQLColumnType::GEOMETRY;
    if (type == "NULL") return MySQLColumnType::NULL_;
    if (type == "TINYBLOB") return MySQLColumnType::TINY_BLOB;
    if (type == "LONGBLOB") return MySQLColumnType::LONG_BLOB;
    if (type == "MEDIUMBLOB") return MySQLColumnType::MEDIUM_BLOB;

    throw std::invalid_argument("Unknown type: \"" + type + "\"");
  }

  return static_cast<MySQLColumnType>(res);
}

}  // namespace policy
} // namespace brpc
