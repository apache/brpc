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

#include "butil/iobuf.h"
#include "brpc/redis.h"

namespace brpc {
namespace policy {

int RedisAuthenticator::GenerateCredential(std::string* auth_str) const {
    RedisRequest request;
    if (!passwd_.empty()) {
        const std::vector<butil::StringPiece> components = {"AUTH", passwd_};
        if (!request.AddCommandByComponents(components)) {
            return -1;
        }
    }
    if (db_ >= 0) {
        const std::string db = std::to_string(db_);
        const std::vector<butil::StringPiece> components = {"SELECT", db};
        if (!request.AddCommandByComponents(components)) {
            return -1;
        }
    }
    butil::IOBuf buf;
    if (!request.SerializeTo(&buf)) {
        return -1;
    }
    *auth_str = buf.to_string();
    return 0;
}

}  // namespace policy
}  // namespace brpc
