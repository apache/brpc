// Copyright (c) 2019 Baidu, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author(s): Yang,Liming <yangliming01@baidu.com>

#include <vector>
#include "brpc/policy/mysql_authenticator.h"
#include "brpc/policy/mysql_auth_hash.h"
#include "brpc/mysql_command.h"
#include "brpc/mysql_reply.h"
#include "brpc/mysql_common.h"
#include "butil/base64.h"
#include "butil/iobuf.h"
#include "butil/logging.h"  // LOG()
#include "butil/sys_byteorder.h"

namespace brpc {
namespace policy {

namespace {
const butil::StringPiece mysql_native_password("mysql_native_password");
const char* auth_param_delim = "\t";
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
    *str = ss.str();
    return true;
}

void MysqlParseAuthenticator(const butil::StringPiece& raw,
                             std::string* user,
                             std::string* password,
                             std::string* schema,
                             std::string* collation) {
    std::vector<size_t> idx;
    idx.reserve(4);
    for (size_t p = raw.find(auth_param_delim); p != butil::StringPiece::npos;
         p = raw.find(auth_param_delim, p + 1)) {
        idx.push_back(p);
    }
    user->assign(raw.data(), 0, idx[0]);
    password->assign(raw.data(), idx[0] + 1, idx[1] - idx[0] - 1);
    schema->assign(raw.data(), idx[1] + 1, idx[2] - idx[1] - 1);
    collation->assign(raw.data(), idx[2] + 1, idx[3] - idx[2] - 1);
}

void MysqlParseParams(const butil::StringPiece& raw, std::string* params) {
    size_t idx = raw.rfind(auth_param_delim);
    params->assign(raw.data(), idx + 1, raw.size() - idx - 1);
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
        salt = mysql_build_mysql41_authentication_response(salt.to_string(), password.data());
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
    auto iter = MysqlCollations.find(collation.data());
    if (iter == MysqlCollations.end()) {
        LOG(ERROR) << "wrong collation [" << collation << "]";
        return 1;
    }
    payload.append(&iter->second, 1);
    const std::string stuff(23, '\0');
    payload.append(stuff);
    payload.append(user.data());
    payload.push_back('\0');
    payload.append(pack_encode_length(salt.size()));
    payload.append(salt);
    if (schema != "") {
        payload.append(schema.data());
        payload.push_back('\0');
    }
    if (auth.auth_plugin() == mysql_native_password) {
        payload.append(mysql_native_password.data(), mysql_native_password.size());
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
