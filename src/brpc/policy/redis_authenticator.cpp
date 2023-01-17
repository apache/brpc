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

#include "brpc/policy/redis_authenticator.h"

#include "butil/base64.h"
#include "butil/iobuf.h"
#include "butil/string_printf.h"
#include "butil/sys_byteorder.h"
#include "brpc/redis_command.h"

namespace brpc {
namespace policy {

int RedisAuthenticator::GenerateCredential(std::string* auth_str) const {
    butil::IOBuf buf;
    if (!passwd_.empty()) {
        brpc::RedisCommandFormat(&buf, "AUTH %s", passwd_.c_str());
    }
    if (db_ >= 0) {
        brpc::RedisCommandFormat(&buf, "SELECT %d", db_);
    }
    *auth_str = buf.to_string();
    return 0;
}

}  // namespace policy
}  // namespace brpc
