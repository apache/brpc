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

// Wire-format helpers for the MySQL client protocol (length-encoded
// integers, length-encoded strings, packet headers) used by the
// authentication-handshake layer.  Specification:
//   https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_basic_dt_integers.html
//   https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_basic_dt_strings.html
//   https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_basic_packets.html

#ifndef BRPC_POLICY_MYSQL_MYSQL_AUTH_PACKET_H
#define BRPC_POLICY_MYSQL_MYSQL_AUTH_PACKET_H

#include <stdint.h>

#include <string>

#include "butil/strings/string_piece.h"

namespace brpc {
namespace policy {
namespace mysql {

// MySQL packet header: 3-byte little-endian payload length + 1-byte
// sequence id.
struct PacketHeader {
    uint32_t payload_len;  // 0 .. (1 << 24) - 1
    uint8_t seq;
};
static const size_t kPacketHeaderLen = 4;

// Maximum payload length representable in a single MySQL packet
// (24-bit length field; larger payloads are split across packets).
static const uint32_t kMaxPayloadLen = (1u << 24) - 1;

// Decodes a length-encoded integer (lenenc-int) from |buf|.
// On success, stores the value in *out and returns the number of
// bytes consumed (1, 3, 4, or 9).  Returns 0 on truncation or on the
// reserved 0xff marker.
size_t DecodeLengthEncodedInt(const butil::StringPiece& buf, uint64_t* out);

// Appends a length-encoded integer encoding of |value| to |out|.
void EncodeLengthEncodedInt(uint64_t value, std::string* out);

// Decodes a length-encoded string into |out_value| and returns the
// number of bytes consumed.  Returns 0 if the leading lenenc-int is
// invalid or the declared payload is truncated.
size_t DecodeLengthEncodedString(const butil::StringPiece& buf,
                                 std::string* out_value);

// Appends a length-encoded string encoding of |value| to |out|.
void EncodeLengthEncodedString(const butil::StringPiece& value,
                               std::string* out);

// Decodes a packet header from the first kPacketHeaderLen bytes of
// |buf|.  Returns true on success.
bool DecodePacketHeader(const butil::StringPiece& buf, PacketHeader* out);

// Appends an encoded packet header to |out|.  Caller must guarantee
// header.payload_len <= kMaxPayloadLen.
void EncodePacketHeader(const PacketHeader& header, std::string* out);

// Decodes a NUL-terminated string starting at |buf[0]|.  Stores the
// string (without the NUL) in *out_value and returns bytes consumed
// (string length + 1).  Returns 0 if no NUL is found within |buf|.
size_t DecodeNullTerminatedString(const butil::StringPiece& buf,
                                  std::string* out_value);

}  // namespace mysql
}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_MYSQL_MYSQL_AUTH_PACKET_H
