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

// Codec for the four MySQL connection-phase packets the client touches
// during authentication.  All functions operate on raw packet payloads
// (without the 4-byte packet header); the caller is responsible for
// framing.  Specifications:
//   HandshakeV10:
//     https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase_packets_protocol_handshake_v10.html
//   HandshakeResponse41:
//     https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase_packets_protocol_handshake_response.html
//   AuthSwitchRequest / AuthMoreData:
//     https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase_packets_protocol_auth_switch_request.html
//     https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase_packets_protocol_auth_more_data.html

#ifndef BRPC_POLICY_MYSQL_MYSQL_AUTH_HANDSHAKE_H
#define BRPC_POLICY_MYSQL_MYSQL_AUTH_HANDSHAKE_H

#include <stdint.h>

#include <string>

#include "butil/strings/string_piece.h"

namespace brpc {
namespace policy {
namespace mysql {

// Subset of MySQL capability flags we recognize.
enum CapabilityFlag : uint32_t {
    CLIENT_LONG_PASSWORD                  = 0x00000001,
    CLIENT_LONG_FLAG                      = 0x00000004,
    CLIENT_CONNECT_WITH_DB                = 0x00000008,
    CLIENT_PROTOCOL_41                    = 0x00000200,
    CLIENT_TRANSACTIONS                   = 0x00002000,
    CLIENT_SECURE_CONNECTION              = 0x00008000,
    CLIENT_PLUGIN_AUTH                    = 0x00080000,
    CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA = 0x00200000,
    CLIENT_DEPRECATE_EOF                  = 0x01000000,
};

// The leading status byte of an authentication-related packet.  Used
// by callers to dispatch a packet payload to the right parser before
// invoking any of the functions below.
enum PacketTag : uint8_t {
    kHandshakeV10Tag       = 0x0a,
    kAuthSwitchRequestTag  = 0xfe,
    kAuthMoreDataTag       = 0x01,
    kOkPacketTag           = 0x00,
    kErrPacketTag          = 0xff,
};

// Parsed HandshakeV10 (server greeting).
struct HandshakeV10 {
    uint8_t protocol_version;        // always 10
    std::string server_version;      // human-readable, NUL-terminated on wire
    uint32_t connection_id;
    std::string auth_plugin_data;    // 20-byte salt (parts 1 + 2 concatenated)
    uint32_t capability_flags;       // upper 16 bits OR'd in when present
    uint8_t character_set;
    uint16_t status_flags;
    std::string auth_plugin_name;    // e.g., "mysql_native_password"
};

// Parses |payload| (a packet body without the 4-byte header) as a
// HandshakeV10.  Returns true on success.  Rejects packets whose
// protocol_version is not 10 or whose salt is not 20 bytes long.
bool ParseHandshakeV10(const butil::StringPiece& payload, HandshakeV10* out);

// Inputs for building a HandshakeResponse41 payload.  The caller is
// expected to have already negotiated capability_flags against the
// server's advertised flags and computed the scrambled auth_response.
struct HandshakeResponse41 {
    uint32_t capability_flags;
    uint32_t max_packet_size;
    uint8_t character_set;
    std::string username;
    std::string auth_response;        // bytes from NativePasswordScramble,
                                      // CachingSha2PasswordScramble, etc.
    std::string database;             // omitted when CLIENT_CONNECT_WITH_DB
                                      // is not in capability_flags
    std::string auth_plugin_name;     // included when CLIENT_PLUGIN_AUTH
                                      // is in capability_flags
};

// Appends a HandshakeResponse41 payload (no header) to |out| and returns
// true.  auth_response encoding obeys capability_flags:
//   - CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA  -> length-encoded string
//   - CLIENT_SECURE_CONNECTION               -> 1-byte length + data
//   - neither                                -> NUL-terminated
// The 1-byte-length scheme cannot represent an auth_response longer than
// 255 bytes.  Rather than silently truncating it (which produces an
// invalid response and desynchronizes the packet stream), the function
// logs an error and returns false WITHOUT writing to |out|.  Callers with
// larger payloads (e.g. RSA ciphertext) must negotiate
// CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA.
bool BuildHandshakeResponse41(const HandshakeResponse41& req, std::string* out);

// Parsed AuthSwitchRequest (server asks client to switch plugins).
struct AuthSwitchRequest {
    std::string auth_plugin_name;
    std::string auth_plugin_data;   // 20-byte salt; trailing NUL stripped
};

// Parses an AuthSwitchRequest payload.  Returns true on success.  The
// caller must have already verified payload[0] == kAuthSwitchRequestTag.
bool ParseAuthSwitchRequest(const butil::StringPiece& payload,
                            AuthSwitchRequest* out);

// Parsed AuthMoreData (server sends RSA pubkey or fast-auth status).
struct AuthMoreData {
    std::string data;   // 0x03=fast-auth-ok, 0x04=request-pubkey, or PEM
};

// Parses an AuthMoreData payload.  Returns true on success.  The
// caller must have already verified payload[0] == kAuthMoreDataTag.
bool ParseAuthMoreData(const butil::StringPiece& payload, AuthMoreData* out);

}  // namespace mysql
}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_MYSQL_MYSQL_AUTH_HANDSHAKE_H
