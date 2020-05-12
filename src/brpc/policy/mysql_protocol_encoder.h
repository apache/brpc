/*
  Copyright (c) 2017, 2019, Oracle and/or its affiliates. All rights reserved.

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

#ifndef BRPC_POLICY_MYSQL_PROTOCOL_ENCODER_H
#define BRPC_POLICY_MYSQL_PROTOCOL_ENCODER_H

#include <stdint.h>
#include <string>
#include <vector>

#include "butil/iobuf.h"
#include "brpc/policy/mysql_constants.h"
#include "brpc/policy/mysql_protocol_common.h"
#include "brpc/policy/mysql_meta.pb.h"

namespace brpc { namespace policy {

const uint16_t MYSQL_PARSE_ERROR = 1064;

struct GreetingsMessage {
  GreetingsMessage() :
      seq_no(0U),
      mysql_version("8.0.5"),
      connection_id(1U),
      auth_plugin_data("123456789|123456789|"),
      capabilities(
          ::brpc::policy::Capabilities::CONNECT_WITH_DB |
          ::brpc::policy::Capabilities::PROTOCOL_41 |
          ::brpc::policy::Capabilities::SECURE_CONNECTION),
      auth_plugin_name("mysql_native_password"),
      character_set(0U),
      status_flags(0U) { }

  uint8_t seq_no;
  std::string mysql_version;
  uint32_t connection_id;
  std::string auth_plugin_data;
  ::brpc::policy::Capabilities::Flags capabilities;
  std::string auth_plugin_name;
  uint8_t character_set;
  uint16_t status_flags;
};

struct ErrorMessage {
    ErrorMessage() :
        seq_no(0U),
        error_code(0U),
        sql_state("HY000") { }

    uint8_t seq_no;
    uint16_t error_code;
    std::string sql_state;
    std::string error_msg;
};

class MySQLProtocolEncoder {
 public:
  using MsgBuffer = std::vector<byte>;

  /** @brief Encodes MySQL OK message
   *
   * @param seq_no          protocol packet sequence number to use
   * @param affected_rows   number of the rows affected by the statment
   *                        this OK replies to
   * @param last_insert_id  id of the last row inserted by the statement
   *                        this OK replies to (if any)
   * @param status          status of the statement this OK replies to
   * @param warnings        number of the warning for the statement this OK
   *replies to
   *
   * @returns buffer with the encoded message
   **/
  ::butil::IOBuf EncodeOKMessage(uint8_t seq_no, uint64_t affected_rows = 0,
                              uint64_t last_insert_id = 0, uint16_t status = 0,
                              uint16_t warnings = 0);

  ::butil::IOBuf EncodeErrorMessage(const ErrorMessage& msg) {
      return EncodeErrorMessage(
          msg.seq_no,
          msg.error_code,
          msg.sql_state,
          msg.error_msg);
  }
  /** @brief Encodes MySQL error message
   *
   * @param seq_no      protocol packet sequence number to use
   * @param error_code  code of the reported error
   * @param sql_state   SQL state to report
   * @param error_msg   error message
   *
   * @returns buffer with the encoded message
   **/
  ::butil::IOBuf EncodeErrorMessage(uint8_t seq_no, uint16_t error_code,
                                 const std::string &sql_state,
                                 const std::string &error_msg);

  /** @brief Encodes MySQL greetings message sent from the server when
   *         the client connects.
   *
   * @param seq_no          protocol packet sequence number to use
   * @param mysql_version   MySQL server version string
   * @param connection_id   is of the client connection
   * @param auth_plugin_data authentication plugin data (nonce)
   * @param capabilities    bitmask with MySQL Server capabilities
   * @param auth_plugin_name auth-plugin name, written only if PLUGIN_AUTH
   *cap.flag is set
   * @param character_set   id of the connection character set
   * @param status_flags    bitmask with MySQL Server status flags
   *
   * @returns buffer with the encoded message
   **/

  ::butil::IOBuf EncodeGreetingsMessage(const GreetingsMessage& g) {
      return EncodeGreetingsMessage(
          g.seq_no,
          g.mysql_version,
          g.connection_id,
          g.auth_plugin_data,
          g.capabilities,
          g.auth_plugin_name,
          g.character_set,
          g.status_flags);
  }

  ::butil::IOBuf EncodeGreetingsMessage(
      uint8_t seq_no, const std::string &mysql_version = "8.0.5",
      uint32_t connection_id = 1,
      std::string auth_plugin_data = "123456789|123456789|",
      ::brpc::policy::Capabilities::Flags capabilities =
          ::brpc::policy::Capabilities::PROTOCOL_41 |
          ::brpc::policy::Capabilities::SECURE_CONNECTION,
      const std::string &auth_plugin_name = "mysql_native_password",
      uint8_t character_set = 0, uint16_t status_flags = 0);

  /** @brief Encodes MySQL auth-switch message sent from the server when
   *         the client connects.
   *
   * @param seq_no          protocol packet sequence number to use
   * @param auth_plugin_name auth-plugin name, written only if PLUGIN_AUTH
   *cap.flag is set
   * @param auth_plugin_data authentication plugin data (nonce)
   *
   * @note auth_plugin_data should contain the 8/20/32 nonce bytes, WITHOUT the
   *       final \0 at the end (it will be added automatically by this method)
   *
   * @returns buffer with the encoded message
   **/
  MsgBuffer encode_auth_switch_message(uint8_t seq_no,
                                       const std::string &auth_plugin_name,
                                       const std::string &auth_plugin_data);

  /** @brief Encodes message containing number of the columns
   *        (used while sending resultset for the QUERY).
   *
   * @param seq_no  protocol packet sequence number to use
   * @param number  number of the columns to encode
   *
   * @returns buffer with the encoded message
   **/
  ::butil::IOBuf EncodeColumnsNumberMessage(uint8_t seq_no, uint64_t number);

  /** @brief Encodes message containing single column metadata.
   *
   * @param seq_no       protocol packet sequence number to use
   * @param column_info  map containing parameters names and values pairs for
   *the column
   *
   * @returns buffer with the encoded message
   **/
  ::butil::IOBuf EncodeColumnMetaMessage(
      uint8_t seq_no, const ::brpc::policy::ResultMeta& meta);

  /** @brief Encodes message containing single row in the resultset.
   *
   * @param seq_no        protocol packet sequence number to use
   * @param columns_info  vector with column metadata for consecutive row fields
   * @param row_values    vector with values (as string) for the consecutive row
   *fields
   *
   * @returns buffer with the encoded message
   **/
  ::butil::IOBuf EncodeRowMessage(
      uint8_t seq_no, const ::brpc::policy::ResultRow& row);

  /** @brief Encodes EOF message used to mark the end of columns metadata and
   *rows when sending the resultset to the client.
   *
   * @param seq_no    protocol packet sequence number to use
   * @param status    status mask for the ongoing operation
   * @param warnings  number of the warnings for ongoing operation
   *
   * @returns buffer with the encoded message
   **/
    ::butil::IOBuf EncodeEOFMessage(uint8_t seq_no, uint16_t status = 0,
                                    uint16_t warnings = 0);

  /**
   * encode a AuthFast message.
   *
   * used by cached_sha256_password
   *
   * @param seq_no    protocol packet sequence number to use
   * @returns buffer with the encoded message
   */
  MsgBuffer encode_auth_fast_message(uint8_t seq_no);

 protected:
  void encode_msg_begin(MsgBuffer &out_buffer);
  void encode_msg_end(MsgBuffer &out_buffer, uint8_t seq_no);
  void append_byte(MsgBuffer &buffer, byte value);

  template <class T, typename = std::enable_if<std::is_integral<T>::value>>
  void append_int(MsgBuffer &buffer, T value, size_t len = sizeof(T)) {
    buffer.reserve(buffer.size() + len);
    while (len-- > 0) {
      byte b = static_cast<byte>(value);
      buffer.push_back(b);
      value = static_cast<T>(value >> 8);
    }
  }

  void append_str(MsgBuffer &buffer, const std::string &value);
  void append_buffer(MsgBuffer &buffer, const MsgBuffer &value);
  void append_lenenc_int(MsgBuffer &buffer, uint64_t val);
  void append_lenenc_str(MsgBuffer &buffer, const std::string &value);
};

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_MYSQL_PROTOCOL_ENCODER_H
