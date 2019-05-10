// Copyright (c) 2019 Baidu, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author(s): Yang,Liming <yangliming01@baidu.com>

#ifndef BRPC_POLICY_MYSQL_AUTHENTICATOR_H
#define BRPC_POLICY_MYSQL_AUTHENTICATOR_H

#include "butil/iobuf.h"
#include "brpc/authenticator.h"
#include "brpc/mysql_reply.h"

namespace brpc {
namespace policy {
// Request to mysql for authentication.
class MysqlAuthenticator : public Authenticator {
public:
    MysqlAuthenticator(const butil::StringPiece& user,
                       const butil::StringPiece& passwd,
                       const butil::StringPiece& schema,
                       const butil::StringPiece& params = "",
                       const butil::StringPiece& collation = MysqlDefaultCollation)
        : _user(user.data(), user.size()),
          _passwd(passwd.data(), passwd.size()),
          _schema(schema.data(), schema.size()),
          _params(params.data(), params.size()),
          _collation(collation.data(), collation.size()) {}

    int GenerateCredential(std::string* auth_str) const {
        return 0;
    }

    int VerifyCredential(const std::string&, const butil::EndPoint&, brpc::AuthContext*) const {
        return 0;
    }

    const butil::StringPiece user() const;
    const butil::StringPiece passwd() const;
    const butil::StringPiece schema() const;
    const butil::StringPiece params() const;
    const butil::StringPiece collation() const;
    bool SerializeToString(std::string* str) const;

private:
    DISALLOW_COPY_AND_ASSIGN(MysqlAuthenticator);

    const std::string _user;
    const std::string _passwd;
    const std::string _schema;
    const std::string _params;
    const std::string _collation;
};

inline const butil::StringPiece MysqlAuthenticator::user() const {
    return _user;
}

inline const butil::StringPiece MysqlAuthenticator::passwd() const {
    return _passwd;
}

inline const butil::StringPiece MysqlAuthenticator::schema() const {
    return _schema;
}

inline const butil::StringPiece MysqlAuthenticator::params() const {
    return _params;
}

inline const butil::StringPiece MysqlAuthenticator::collation() const {
    return _collation;
}

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_COUCHBASE_AUTHENTICATOR_H
