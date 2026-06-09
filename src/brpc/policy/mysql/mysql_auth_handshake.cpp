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

#include "brpc/policy/mysql/mysql_auth_handshake.h"

#include <cstring>

#include "brpc/policy/mysql/mysql_auth_packet.h"
#include "brpc/policy/mysql/mysql_auth_scramble.h"
#include "butil/logging.h"

namespace brpc {
namespace policy {
namespace mysql {

namespace {

// MySQL HandshakeV10 fixed-size pieces and constants.
const size_t kAuthPluginDataPart1Len = 8;
const size_t kReservedAfterCapsLen   = 10;
const size_t kFillerAfterPart1Len    = 1;
const size_t kReservedInResponseLen  = 23;

// Reads N little-endian bytes from |buf| at |off| into |out|.
template <typename T>
bool ReadLE(const butil::StringPiece& buf, size_t off, size_t n, T* out) {
    if (off + n > buf.size()) return false;
    T v = 0;
    for (size_t i = 0; i < n; ++i) {
        v |= static_cast<T>(static_cast<unsigned char>(buf[off + i])) << (8 * i);
    }
    *out = v;
    return true;
}

template <typename T>
void WriteLE(T value, size_t n, std::string* out) {
    for (size_t i = 0; i < n; ++i) {
        out->push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

}  // namespace

bool ParseHandshakeV10(const butil::StringPiece& payload, HandshakeV10* out) {
    if (payload.empty()) {
        LOG(ERROR) << "ParseHandshakeV10: empty payload";
        return false;
    }

    size_t off = 0;
    out->protocol_version = static_cast<uint8_t>(payload[off++]);
    if (out->protocol_version != kHandshakeV10Tag) {
        LOG(ERROR) << "ParseHandshakeV10: unexpected protocol_version="
                   << static_cast<int>(out->protocol_version) << ", expected "
                   << static_cast<int>(kHandshakeV10Tag);
        return false;
    }

    // server_version: NUL-terminated string
    std::string version;
    {
        const butil::StringPiece rest(payload.data() + off,
                                      payload.size() - off);
        const size_t consumed = DecodeNullTerminatedString(rest, &version);
        if (consumed == 0) {
            LOG(ERROR) << "ParseHandshakeV10: unterminated server_version string";
            return false;
        }
        off += consumed;
    }
    out->server_version = std::move(version);

    // connection_id: 4 LE bytes
    if (!ReadLE<uint32_t>(payload, off, 4, &out->connection_id)) {
        LOG(ERROR) << "ParseHandshakeV10: truncated before connection_id";
        return false;
    }
    off += 4;

    // auth-plugin-data-part-1: 8 bytes
    if (off + kAuthPluginDataPart1Len > payload.size()) {
        LOG(ERROR) << "ParseHandshakeV10: truncated before "
                      "auth-plugin-data-part-1";
        return false;
    }
    std::string salt(payload.data() + off, kAuthPluginDataPart1Len);
    off += kAuthPluginDataPart1Len;

    // filler 0x00
    if (off + kFillerAfterPart1Len > payload.size()) {
        LOG(ERROR) << "ParseHandshakeV10: truncated before filler after "
                      "auth-plugin-data-part-1";
        return false;
    }
    off += kFillerAfterPart1Len;

    // capability flags (lower 2 bytes)
    uint16_t caps_lo = 0;
    if (!ReadLE<uint16_t>(payload, off, 2, &caps_lo)) {
        LOG(ERROR) << "ParseHandshakeV10: truncated before capability flags "
                      "(lower 2 bytes)";
        return false;
    }
    off += 2;
    out->capability_flags = caps_lo;

    if (off == payload.size()) {
        // Pre-4.1 server.  We don't support these — bail.
        LOG(ERROR) << "ParseHandshakeV10: pre-4.1 server not supported";
        return false;
    }

    // character_set
    if (off >= payload.size()) {
        LOG(ERROR) << "ParseHandshakeV10: truncated before character_set";
        return false;
    }
    out->character_set = static_cast<uint8_t>(payload[off++]);

    // status_flags
    if (!ReadLE<uint16_t>(payload, off, 2, &out->status_flags)) {
        LOG(ERROR) << "ParseHandshakeV10: truncated before status_flags";
        return false;
    }
    off += 2;

    // capability flags upper 2 bytes
    uint16_t caps_hi = 0;
    if (!ReadLE<uint16_t>(payload, off, 2, &caps_hi)) {
        LOG(ERROR) << "ParseHandshakeV10: truncated before capability flags "
                      "(upper 2 bytes)";
        return false;
    }
    off += 2;
    out->capability_flags |= static_cast<uint32_t>(caps_hi) << 16;

    // length of auth-plugin-data (or 0x00 when CLIENT_PLUGIN_AUTH is absent)
    if (off >= payload.size()) {
        LOG(ERROR) << "ParseHandshakeV10: truncated before "
                      "auth-plugin-data length";
        return false;
    }
    const uint8_t apd_total_len = static_cast<uint8_t>(payload[off++]);

    // 10 reserved bytes (all 0x00)
    if (off + kReservedAfterCapsLen > payload.size()) {
        LOG(ERROR) << "ParseHandshakeV10: truncated before 10 reserved bytes";
        return false;
    }
    off += kReservedAfterCapsLen;

    if (out->capability_flags & CLIENT_SECURE_CONNECTION) {
        // auth-plugin-data-part-2: max(13, apd_total_len - 8) bytes.  Modern
        // servers send 13 (12 salt bytes + 1 NUL filler).
        const size_t part2_len = apd_total_len > kAuthPluginDataPart1Len
            ? static_cast<size_t>(apd_total_len) - kAuthPluginDataPart1Len
            : static_cast<size_t>(13);
        const size_t want = part2_len < 13 ? 13 : part2_len;
        if (off + want > payload.size()) {
            LOG(ERROR) << "ParseHandshakeV10: truncated auth-plugin-data-part-2,"
                          " want " << want << " bytes, have "
                       << (payload.size() - off);
            return false;
        }
        // Concat salt parts; trim trailing NUL filler so callers see the
        // raw 20-byte salt.
        salt.append(payload.data() + off, want);
        off += want;
        if (!salt.empty() && salt.back() == '\0') {
            salt.pop_back();
        }
    }
    if (salt.size() != kSaltLen) {
        LOG(ERROR) << "ParseHandshakeV10: auth-plugin-data length mismatch, got "
                   << salt.size() << " expected " << kSaltLen;
        return false;
    }
    out->auth_plugin_data = std::move(salt);

    if (out->capability_flags & CLIENT_PLUGIN_AUTH) {
        std::string name;
        const butil::StringPiece rest(payload.data() + off,
                                      payload.size() - off);
        const size_t consumed = DecodeNullTerminatedString(rest, &name);
        // Some servers omit the trailing NUL; tolerate by treating the
        // remainder of the payload as the plugin name.
        if (consumed == 0) {
            out->auth_plugin_name.assign(rest.data(), rest.size());
        } else {
            out->auth_plugin_name = std::move(name);
        }
    }

    return true;
}

bool BuildHandshakeResponse41(const HandshakeResponse41& req, std::string* out) {
    // The CLIENT_SECURE_CONNECTION encoding prefixes auth_response with a
    // single length byte, so it cannot represent a payload larger than 255
    // bytes.  Validate this FIRST and fail hard rather than silently
    // truncating: a truncated auth_response is invalid and would
    // desynchronize the packet stream.  Larger payloads (e.g. RSA
    // ciphertext) require the caller to negotiate
    // CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA, which has no such limit.
    const bool lenenc_client_data =
        req.capability_flags & CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA;
    if (!lenenc_client_data &&
        (req.capability_flags & CLIENT_SECURE_CONNECTION) &&
        req.auth_response.size() > 0xff) {
        LOG(ERROR) << "Cannot build HandshakeResponse41: auth_response is "
                   << req.auth_response.size() << " bytes, exceeding the "
                      "255-byte CLIENT_SECURE_CONNECTION length prefix; "
                      "negotiate CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA for "
                      "larger payloads";
        return false;
    }

    WriteLE<uint32_t>(req.capability_flags, 4, out);
    WriteLE<uint32_t>(req.max_packet_size, 4, out);
    out->push_back(static_cast<char>(req.character_set));
    out->append(kReservedInResponseLen, '\0');
    out->append(req.username);
    out->push_back('\0');

    if (lenenc_client_data) {
        EncodeLengthEncodedString(req.auth_response, out);
    } else if (req.capability_flags & CLIENT_SECURE_CONNECTION) {
        // Length validated above to fit in a single byte.
        const uint8_t len = static_cast<uint8_t>(req.auth_response.size());
        out->push_back(static_cast<char>(len));
        out->append(req.auth_response.data(), req.auth_response.size());
    } else {
        out->append(req.auth_response);
        out->push_back('\0');
    }

    if (req.capability_flags & CLIENT_CONNECT_WITH_DB) {
        out->append(req.database);
        out->push_back('\0');
    }

    if (req.capability_flags & CLIENT_PLUGIN_AUTH) {
        out->append(req.auth_plugin_name);
        out->push_back('\0');
    }
    return true;
}

bool ParseAuthSwitchRequest(const butil::StringPiece& payload,
                            AuthSwitchRequest* out) {
    if (payload.empty() ||
        static_cast<uint8_t>(payload[0]) != kAuthSwitchRequestTag) {
        LOG(ERROR) << "ParseAuthSwitchRequest: empty payload or missing "
                      "AuthSwitchRequest tag";
        return false;
    }
    size_t off = 1;
    std::string name;
    const butil::StringPiece rest(payload.data() + off, payload.size() - off);
    const size_t consumed = DecodeNullTerminatedString(rest, &name);
    if (consumed == 0) {
        LOG(ERROR) << "ParseAuthSwitchRequest: unterminated auth_plugin_name "
                      "string";
        return false;
    }
    off += consumed;
    out->auth_plugin_name = std::move(name);

    // Remainder is auth-plugin-data; trim a single trailing NUL filler.
    out->auth_plugin_data.assign(payload.data() + off, payload.size() - off);
    if (!out->auth_plugin_data.empty() && out->auth_plugin_data.back() == '\0') {
        out->auth_plugin_data.pop_back();
    }
    return true;
}

bool ParseAuthMoreData(const butil::StringPiece& payload, AuthMoreData* out) {
    if (payload.empty() ||
        static_cast<uint8_t>(payload[0]) != kAuthMoreDataTag) {
        LOG(ERROR) << "ParseAuthMoreData: empty payload or missing "
                      "AuthMoreData tag";
        return false;
    }
    out->data.assign(payload.data() + 1, payload.size() - 1);
    return true;
}

}  // namespace mysql
}  // namespace policy
}  // namespace brpc
