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

#include "brpc/policy/mysql_authenticator.h"
#include "brpc/policy/mysql_auth_hash.h"
#include "brpc/mysql_reply.h"
#include "butil/base64.h"
#include "butil/iobuf.h"
#include "butil/logging.h"  // LOG()
#include "butil/sys_byteorder.h"

namespace brpc {
namespace policy {

namespace {
const butil::StringPiece mysql_native_password("mysql_native_password");
std::string PackEncodeLength(const uint64_t value) {
    std::stringstream ss;
    if (value <= 250) {
        ss.put((char)value);
    } else if (value <= 0xffff) {
        ss.put((char)0xfc).put((char)value).put((char)(value >> 8));
    } else if (value <= 0xffffff) {
        ss.put((char)0xfd).put((char)value).put((char)(value >> 8)).put((char)(value >> 16));
    } else {
        ss.put((char)0xfd)
            .put((char)value)
            .put((char)(value >> 8))
            .put((char)(value >> 16))
            .put((char)(value >> 24))
            .put((char)(value >> 32))
            .put((char)(value >> 40))
            .put((char)(value >> 48))
            .put((char)(value >> 56));
    }
    return ss.str();
}
};  // namespace

int MysqlPackAuthenticator(const MysqlReply::Auth* auth,
                           const std::string* raw,
                           std::string* auth_str) {
    const size_t pos1 = raw->find(':', 0);
    const size_t pos2 = raw->find(':', pos1 + 1);
    const size_t pos3 = raw->find(':', pos2 + 1);
    const std::string user = std::string(*raw, 0, pos1);
    const std::string passwd = std::string(*raw, pos1 + 1, pos2 - pos1 - 1);
    const std::string schema = std::string(*raw, pos2 + 1, pos3 - pos2 - 1);
    const MysqlCollation collation = (MysqlCollation)atoi(std::string(*raw, pos3 + 1).c_str());

    const uint16_t capability =
        butil::ByteSwapToLE16((schema == "" ? 0x8285 : 0x828d) & auth->capability());
    const uint16_t extended_capability =
        butil::ByteSwapToLE16(0x000b & auth->extended_capability());
    const uint32_t max_package_length = butil::ByteSwapToLE32(16777216UL);
    butil::IOBuf salt;
    salt.append(auth->salt().data(), auth->salt().size());
    salt.append(auth->salt2().data(), auth->salt2().size());
    if (auth->auth_plugin() == mysql_native_password) {
        salt = mysql_build_mysql41_authentication_response(salt.to_string(), passwd);
    } else {
        LOG(ERROR) << "no support auth plugin " << auth->auth_plugin();
        return 1;
    }

    butil::IOBuf payload;
    payload.append(&capability, 2);
    payload.append(&extended_capability, 2);
    payload.append(&max_package_length, 4);
    payload.append(&collation, 1);
    const std::string stuff(23, '\0');
    payload.append(stuff);
    payload.append(user);
    payload.push_back('\0');
    payload.append(PackEncodeLength(salt.size()));
    payload.append(salt);
    if (schema != "") {
        payload.append(schema);
        payload.push_back('\0');
    }
    if (auth->auth_plugin() == mysql_native_password) {
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
    *auth_str = message.to_string();
    return 0;
}

int MysqlAuthenticator::GenerateCredential(std::string* auth_str) const {
    // do nothing
    return 0;
}

}  // namespace policy
}  // namespace brpc
