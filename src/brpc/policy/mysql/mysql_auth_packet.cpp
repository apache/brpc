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

#include "brpc/policy/mysql/mysql_auth_packet.h"

#include <cstring>

#include "butil/logging.h"

namespace brpc {
namespace policy {
namespace mysql {

size_t DecodeLengthEncodedInt(const butil::StringPiece& buf, uint64_t* out,
                              bool* is_null) {
    // Define *out and *is_null on every path so a caller that forgets to
    // check the return value can never read an uninitialized result.
    *out = 0;
    if (is_null != nullptr) {
        *is_null = false;
    }
    if (buf.empty()) {
        LOG(WARNING) << "DecodeLengthEncodedInt: empty buffer";
        return 0;
    }
    const unsigned char first = static_cast<unsigned char>(buf[0]);
    if (first < 0xfb) {
        *out = first;
        return 1;
    }
    if (first == 0xfb) {
        // 0xFB is the lenenc NULL marker, not a length prefix.  Report NULL
        // (one byte consumed) instead of folding it into the failure path.
        if (is_null != nullptr) {
            *is_null = true;
        }
        return 1;
    }
    if (first == 0xfc) {
        if (buf.size() < 3) {
            LOG(WARNING) << "DecodeLengthEncodedInt: truncated 0xFC value, need 3 bytes";
            return 0;
        }
        *out = static_cast<unsigned char>(buf[1])
             | (static_cast<uint64_t>(static_cast<unsigned char>(buf[2])) << 8);
        return 3;
    }
    if (first == 0xfd) {
        if (buf.size() < 4) {
            LOG(WARNING) << "DecodeLengthEncodedInt: truncated 0xFD value, need 4 bytes";
            return 0;
        }
        *out = static_cast<unsigned char>(buf[1])
             | (static_cast<uint64_t>(static_cast<unsigned char>(buf[2])) << 8)
             | (static_cast<uint64_t>(static_cast<unsigned char>(buf[3])) << 16);
        return 4;
    }
    if (first == 0xfe) {
        if (buf.size() < 9) {
            LOG(WARNING) << "DecodeLengthEncodedInt: truncated 0xFE value, need 9 bytes";
            return 0;
        }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(static_cast<unsigned char>(buf[1 + i]))
                 << (8 * i);
        }
        *out = v;
        return 9;
    }
    // 0xff is reserved for error packet marker; not a valid lenenc-int.
    LOG(WARNING) << "DecodeLengthEncodedInt: reserved 0xFF marker";
    return 0;
}

void EncodeLengthEncodedInt(uint64_t value, std::string* out) {
    if (value < 0xfb) {
        out->push_back(static_cast<char>(value));
        return;
    }
    if (value < 0x10000ULL) {
        out->push_back(static_cast<char>(0xfc));
        out->push_back(static_cast<char>(value & 0xff));
        out->push_back(static_cast<char>((value >> 8) & 0xff));
        return;
    }
    if (value < 0x1000000ULL) {
        out->push_back(static_cast<char>(0xfd));
        out->push_back(static_cast<char>(value & 0xff));
        out->push_back(static_cast<char>((value >> 8) & 0xff));
        out->push_back(static_cast<char>((value >> 16) & 0xff));
        return;
    }
    out->push_back(static_cast<char>(0xfe));
    for (int i = 0; i < 8; ++i) {
        out->push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
}

size_t DecodeLengthEncodedString(const butil::StringPiece& buf,
                                 std::string* out_value,
                                 bool* is_null) {
    out_value->clear();
    if (is_null != nullptr) {
        *is_null = false;
    }
    uint64_t len = 0;
    bool len_is_null = false;
    const size_t prefix = DecodeLengthEncodedInt(buf, &len, &len_is_null);
    if (prefix == 0) {
        LOG(WARNING) << "DecodeLengthEncodedString: failed to decode length prefix";
        return 0;
    }
    if (len_is_null) {
        // Leading 0xFB: the string itself is NULL.  Only the marker byte is
        // consumed; there is no payload to read.
        if (is_null != nullptr) {
            *is_null = true;
        }
        return prefix;
    }
    if (prefix > buf.size() || len > buf.size() - prefix) {
        LOG(WARNING) << "DecodeLengthEncodedString: declared length " << len
                   << " exceeds buffer";
        return 0;
    }
    out_value->assign(buf.data() + prefix, len);
    return prefix + len;
}

void EncodeLengthEncodedString(const butil::StringPiece& value,
                               std::string* out) {
    EncodeLengthEncodedInt(value.size(), out);
    out->append(value.data(), value.size());
}

bool DecodePacketHeader(const butil::StringPiece& buf, PacketHeader* out) {
    if (buf.size() < kPacketHeaderLen) {
        LOG(WARNING) << "DecodePacketHeader: buffer smaller than packet header";
        return false;
    }
    out->payload_len =
          static_cast<unsigned char>(buf[0])
        | (static_cast<uint32_t>(static_cast<unsigned char>(buf[1])) << 8)
        | (static_cast<uint32_t>(static_cast<unsigned char>(buf[2])) << 16);
    out->seq = static_cast<unsigned char>(buf[3]);
    return true;
}

void EncodePacketHeader(const PacketHeader& header, std::string* out) {
    out->push_back(static_cast<char>(header.payload_len & 0xff));
    out->push_back(static_cast<char>((header.payload_len >> 8) & 0xff));
    out->push_back(static_cast<char>((header.payload_len >> 16) & 0xff));
    out->push_back(static_cast<char>(header.seq));
}

size_t DecodeNullTerminatedString(const butil::StringPiece& buf,
                                  std::string* out_value) {
    const char* nul = static_cast<const char*>(
        memchr(buf.data(), '\0', buf.size()));
    if (nul == nullptr) {
        LOG(WARNING) << "DecodeNullTerminatedString: no NUL terminator found";
        return 0;
    }
    const size_t len = static_cast<size_t>(nul - buf.data());
    out_value->assign(buf.data(), len);
    return len + 1;
}

}  // namespace mysql
}  // namespace policy
}  // namespace brpc
