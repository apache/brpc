/*
  Copyright (c) 2016, 2018, Oracle and/or its affiliates. All rights reserved.

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

#ifndef BRPC_POLICY_MYSQL_CONSTANTS_H
#define BRPC_POLICY_MYSQL_CONSTANTS_H

#include <cstdint>

namespace brpc {
namespace policy {

namespace Capabilities {

/** Type used to pass capability bitset as one number */
typedef uint32_t AllFlags;

/** Type used to pass high/low half of capability bitset as one number */
typedef uint16_t HalfFlags;

class Flags {
 public:
  constexpr Flags() : flags_(0) {}
  explicit constexpr Flags(AllFlags flags) : flags_(flags) {}

// Developer Studio does not like this even if it's deprecated not to have it.
#ifndef __SUNPRO_CC
  constexpr Flags(const Flags &) = default;
#endif

  Flags &operator=(const Flags &other) = default;

  constexpr bool operator==(const Flags &other) const {
    return flags_ == other.flags_;
  }

  constexpr bool operator!=(const Flags &other) const {
    return flags_ != other.flags_;
  }

  constexpr Flags operator|(const Flags &other) const {
    return Flags(flags_ | other.flags_);
  }

  constexpr Flags operator&(const Flags &other) const {
    return Flags(flags_ & other.flags_);
  }

  bool test(const Flags &want) const {
    return (flags_ & want.flags_) == want.flags_;
  }
  Flags &set(const Flags &other) {
    flags_ |= other.flags_;
    return *this;
  }
  Flags &clear(const Flags &other) {
    flags_ &= ~other.flags_;
    return *this;
  }
  Flags &reset() {
    flags_ = 0;
    return *this;
  }

  constexpr AllFlags bits() const { return flags_; }
  constexpr HalfFlags high_16_bits() const {
    return static_cast<HalfFlags>(flags_ >> 16);
  }
  constexpr HalfFlags low_16_bits() const {
    return static_cast<HalfFlags>(flags_ & 0x0000ffff);
  }

  Flags &clear_high_16_bits() {
    flags_ &= 0x0000ffff;
    return *this;
  }
  Flags &clear_low_16_bits() {
    flags_ &= 0xffff0000;
    return *this;
  }

 private:
  AllFlags flags_;
};

/** @brief Capability flags passed in handshake packet.
 *
 * See https://dev.mysql.com/doc/internals/en/capability-flags.html
 * See also MySQL Server source include/mysql_com.h
 * To search for documentation of a particular flag, prepend CLIENT_ to its name
 * (it was removed on purpose to prevent name collisions when including mysql.h)
 **/
static constexpr Flags LONG_PASSWORD(1 << 0);
static constexpr Flags FOUND_ROWS(1 << 1);
static constexpr Flags LONG_FLAG(1 << 2);
static constexpr Flags CONNECT_WITH_DB(1 << 3);

static constexpr Flags NO_SCHEMA(1 << 4);
static constexpr Flags COMPRESS(1 << 5);
static constexpr Flags ODBC(1 << 6);
static constexpr Flags LOCAL_FILES(1 << 7);

static constexpr Flags IGNORE_SPACE(1 << 8);
static constexpr Flags PROTOCOL_41(
    1
    << 9);  // Server: supports the 4.1 protocol, Client: uses the 4.1 protocol
static constexpr Flags INTERACTIVE(1 << 10);
static constexpr Flags SSL(
    1 << 11);  // Server: supports SSL, Client: switch to SSL

static constexpr Flags SIG_PIPE(1 << 12);
static constexpr Flags TRANSACTIONS(1 << 13);
static constexpr Flags RESERVED_14(1 << 14);        // deprecated in 8.0.3
static constexpr Flags SECURE_CONNECTION(1 << 15);  // deprecated in 8.0.3

static constexpr Flags MULTI_STATEMENTS(1 << 16);
static constexpr Flags MULTI_RESULTS(1 << 17);
static constexpr Flags MULTI_PS_MULTO_RESULTS(1 << 18);
static constexpr Flags PLUGIN_AUTH(1 << 19);

static constexpr Flags CONNECT_ATTRS(1 << 20);
static constexpr Flags PLUGIN_AUTH_LENENC_CLIENT_DATA(1 << 21);
static constexpr Flags EXPIRED_PASSWORDS(1 << 22);
static constexpr Flags SESSION_TRACK(1 << 23);

static constexpr Flags DEPRECATE_EOF(1 << 24);
static constexpr Flags OPTIONAL_RESULTSET_METADATA(
    1 << 25);  // \  docs for 5.7 don't
static constexpr Flags SSL_VERIFY_SERVER_CERT(1UL << 30);  //  > mention these
static constexpr Flags REMEMBER_OPTIONS(1UL << 31);        // /

// other useful flags (our invention, mysql_com.h does not define them)
static constexpr Flags ALL_ZEROS(0U);
static constexpr Flags ALL_ONES(~0U);

}  // namespace Capabilities

/** @enum Command
 *
 * Types of the supported commands from the client.
 *
 **/
enum Command {
  SLEEP = 0x00,
  QUIT = 0x01,
  INIT_DB = 0x02,
  QUERY = 0x03,
  FIELD_LIST = 0x04,
  CREATE_DB = 0x05,
  DROP_DB = 0x06,
  REFRESH = 0x07,
  SHUTDOWN = 0x08,
  STATISTICS = 0x09,
  PROCESS_INFO = 0x0a,
  CONNECT = 0x0b,
  PROCESS_KILL = 0x0c,
  DEBUG = 0x0d,
  PING = 0x0e,
  TIME = 0x0f,
  DELAYED_INSERT = 0x10,
  CHANGE_USER = 0x11,
  BINLOG_DUMP = 0x12,
  TABLE_DUMP = 0x13,
  CONNECT_OUT = 0x14,
  REGISTER_SLAVE = 0x15,
  STMT_PREPARE = 0x16,
  STMT_EXECUTE = 0x17,
  STMT_SEND_LOG_DATA = 0x18,
  STMT_CLOSE = 0x19,
  STMT_RESET = 0x1a,
  SET_OPTION = 0x1b,
  STMT_FETCH = 0x1c,
  DAEMON = 0x1d,
  BINLOG_DUMP_GTID = 0x1e,
  RESET_CONNECTION = 0x1f,
};

} // namespace policy
}  // namespace brpc 

#endif  // BRPC_POLICY_MYSQL_CONSTANTS_H
