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
//
// Author(s): Yang,Liming <yangliming01@baidu.com>

#include <vector>
#include "brpc/policy/mysql/mysql_authenticator.h"
#include "brpc/policy/mysql/mysql_auth_scramble.h"
#include "brpc/policy/mysql/mysql_command.h"
#include "brpc/policy/mysql/mysql_reply.h"
#include "brpc/policy/mysql/mysql_common.h"
#include "butil/base64.h"
#include "butil/iobuf.h"
#include "butil/logging.h"  // LOG()
#include "butil/sys_byteorder.h"

namespace brpc {
namespace policy {

namespace {
const butil::StringPiece mysql_native_password("mysql_native_password");
const butil::StringPiece caching_sha2_password("caching_sha2_password");
const char* auth_param_delim = "\t";

// Splits a serialized mysql auth string into its '\t'-separated fields.
void MysqlSplitAuthFields(const butil::StringPiece& raw,
                          std::vector<butil::StringPiece>* fields) {
    fields->clear();
    size_t pos = 0;
    while (true) {
        const size_t next = raw.find(auth_param_delim, pos);
        if (next == butil::StringPiece::npos) {
            fields->push_back(raw.substr(pos));
            return;
        }
        fields->push_back(raw.substr(pos, next - pos));
        pos = next + 1;
    }
}

bool MysqlHandleParams(const butil::StringPiece& params, std::string* param_cmd) {
    if (params.empty()) {
        return true;
    }
    const char* delim1 = "&";
    std::vector<size_t> idx;
    for (size_t p = params.find(delim1); p != butil::StringPiece::npos;
         p = params.find(delim1, p + 1)) {
        idx.push_back(p);
    }

    const char* delim2 = "=";
    std::stringstream ss;
    for (size_t i = 0; i < idx.size() + 1; ++i) {
        size_t pos = (i > 0) ? idx[i - 1] + 1 : 0;
        size_t len = (i < idx.size()) ? idx[i] - pos : params.size() - pos;
        butil::StringPiece raw(params.data() + pos, len);
        const size_t p = raw.find(delim2);
        if (p != butil::StringPiece::npos) {
            butil::StringPiece k(raw.data(), p);
            butil::StringPiece v(raw.data() + p + 1, raw.size() - p - 1);
            if (k == "charset") {
                ss << "SET NAMES " << v << ";";
            } else {
                ss << "SET " << k << "=" << v << ";";
            }
        }
    }
    *param_cmd = ss.str();
    return true;
}
};  // namespace

// user + "\t" + password + "\t" + schema + "\t" + collation + "\t" + param
// + "\t" + base64(pinned server public key)
//
// The trailing pinned key is empty when it is not configured.  It is base64
// encoded so that the PEM (multi-line, but also free of tabs in practice)
// can never collide with the field delimiter.
bool MysqlAuthenticator::SerializeToString(std::string* str) const {
    std::stringstream ss;
    ss << _user << auth_param_delim;
    ss << _passwd << auth_param_delim;
    ss << _schema << auth_param_delim;
    ss << _collation << auth_param_delim;
    std::string param_cmd;
    if (MysqlHandleParams(_params, &param_cmd)) {
        ss << param_cmd;
    } else {
        LOG(ERROR) << "handle mysql authentication params failed, ignore it";
        return false;
    }
    std::string encoded_key;
    butil::Base64Encode(_server_public_key, &encoded_key);
    ss << auth_param_delim << encoded_key;
    *str = ss.str();
    return true;
}

void MysqlParseAuthenticator(const butil::StringPiece& raw,
                             std::string* user,
                             std::string* password,
                             std::string* schema,
                             std::string* collation,
                             std::string* server_public_key) {
    std::vector<butil::StringPiece> fields;
    MysqlSplitAuthFields(raw, &fields);
    user->clear();
    password->clear();
    schema->clear();
    collation->clear();
    server_public_key->clear();
    if (fields.size() < 4) {
        LOG(ERROR) << "malformed mysql authentication string, expected at least 4 '\\t' "
                      "delimiters but found " << (fields.size() - 1);
        return;
    }
    user->assign(fields[0].data(), fields[0].size());
    password->assign(fields[1].data(), fields[1].size());
    schema->assign(fields[2].data(), fields[2].size());
    collation->assign(fields[3].data(), fields[3].size());
    if (fields.size() >= 6 && !fields[5].empty() &&
        !butil::Base64Decode(fields[5], server_public_key)) {
        LOG(ERROR) << "malformed base64 server public key in mysql authentication string";
        server_public_key->clear();
    }
}

void MysqlParseParams(const butil::StringPiece& raw, std::string* params) {
    std::vector<butil::StringPiece> fields;
    MysqlSplitAuthFields(raw, &fields);
    params->clear();
    if (fields.size() >= 5) {
        params->assign(fields[4].data(), fields[4].size());
    }
}

int MysqlPackAuthenticator(const MysqlReply::Auth& auth,
                           const butil::StringPiece& user,
                           const butil::StringPiece& password,
                           const butil::StringPiece& schema,
                           const butil::StringPiece& collation,
                           std::string* auth_cmd) {
    const uint16_t capability =
        butil::ByteSwapToLE16((schema == "" ? 0x8285 : 0x828d) & auth.capability());
    const uint16_t extended_capability = butil::ByteSwapToLE16(0x000b & auth.extended_capability());
    butil::IOBuf salt;
    salt.append(auth.salt().data(), auth.salt().size());
    salt.append(auth.salt2().data(), auth.salt2().size());
    if (auth.auth_plugin() == mysql_native_password) {
        // Clean-room mysql_native_password scramble:
        //   SHA1(p) XOR SHA1( salt || SHA1(SHA1(p)) )
        // Produces the same 20 wire bytes as the original GPL helper, but is
        // derived from MySQL's public protocol docs.  Returns empty for a
        // blank password (the wire convention) and empty on a bad salt length.
        const std::string scramble =
            mysql::NativePasswordScramble(salt.to_string(), password);
        if (!password.empty() && scramble.empty()) {
            LOG(ERROR) << "failed to build mysql_native_password scramble, salt size="
                       << salt.size() << " (expected " << mysql::kSaltLen << ")";
            return 1;
        }
        salt.clear();
        salt.append(scramble);
    } else if (auth.auth_plugin() == caching_sha2_password) {
        // Clean-room caching_sha2_password fast-path scramble (32 bytes):
        //   SHA256(p) XOR SHA256( SHA256( SHA256(p) ) || salt )
        // The server replies with an AuthMoreData status byte after this;
        // mysql_protocol.cpp's HandleAuthentication drives the follow-up
        // (fast-auth-success / full-auth RSA exchange).  Returns empty for a
        // blank password (the wire convention) and empty on a bad salt length.
        const std::string scramble =
            mysql::CachingSha2PasswordScramble(salt.to_string(), password);
        if (!password.empty() && scramble.empty()) {
            LOG(ERROR) << "failed to build caching_sha2_password scramble, salt size="
                       << salt.size() << " (expected " << mysql::kSaltLen << ")";
            return 1;
        }
        salt.clear();
        salt.append(scramble);
    } else {
        LOG(ERROR) << "no support auth plugin [" << auth.auth_plugin() << "]";
        return 1;
    }

    butil::IOBuf payload;
    payload.append(&capability, 2);
    payload.append(&extended_capability, 2);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    payload.push_back(0x00);
    auto iter = MysqlCollations.find(std::string(collation.data(), collation.size()));
    if (iter == MysqlCollations.end()) {
        LOG(ERROR) << "wrong collation [" << collation << "]";
        return 1;
    }
    payload.append(&iter->second, 1);
    const std::string stuff(23, '\0');
    payload.append(stuff);
    payload.append(user.data(), user.size());
    payload.push_back('\0');
    payload.append(pack_encode_length(salt.size()));
    payload.append(salt);
    if (schema != "") {
        payload.append(schema.data(), schema.size());
        payload.push_back('\0');
    }
    if (auth.auth_plugin() == mysql_native_password) {
        payload.append(mysql_native_password.data(), mysql_native_password.size());
        payload.push_back('\0');
    } else if (auth.auth_plugin() == caching_sha2_password) {
        payload.append(caching_sha2_password.data(), caching_sha2_password.size());
        payload.push_back('\0');
    }
    butil::IOBuf message;
    const uint32_t payload_size = butil::ByteSwapToLE32(payload.size());
    // header
    message.append(&payload_size, 3);
    message.push_back(0x01);
    // payload
    message.append(payload);
    *auth_cmd = message.to_string();
    return 0;
}

int MysqlPackParams(const butil::StringPiece& params, std::string* param_cmd) {
    if (!params.empty()) {
        butil::IOBuf buf;
        MysqlMakeCommand(&buf, MYSQL_COM_QUERY, params);
        buf.copy_to(param_cmd);
        return 0;
    }
    LOG(ERROR) << "empty connection params";
    return 1;
}

}  // namespace policy
}  // namespace brpc
