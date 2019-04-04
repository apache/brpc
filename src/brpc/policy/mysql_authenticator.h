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
// pack mysql authentication_data
int MysqlPackAuthenticator(const MysqlReply::Auth& auth,
                           const std::string& user,
                           std::string* auth_str);
// Request to mysql for authentication.
class MysqlAuthenticator : public Authenticator {
public:
    MysqlAuthenticator(const std::string& user,
                       const std::string& passwd,
                       const std::string& schema = "",
                       const MysqlCollation collation = MYSQL_utf8_general_ci)
        : _user(user), _passwd(passwd), _schema(schema), _collation(collation) {}

    int GenerateCredential(std::string* auth_str) const;

    int VerifyCredential(const std::string&, const butil::EndPoint&, brpc::AuthContext*) const {
        return 0;
    }

    const std::string& user() const;
    const std::string& passwd() const;
    const std::string& schema() const;
    MysqlCollation collation() const;

private:
    DISALLOW_COPY_AND_ASSIGN(MysqlAuthenticator);

    const std::string _user;
    const std::string _passwd;
    const std::string _schema;
    const MysqlCollation _collation;
};

inline const std::string& MysqlAuthenticator::user() const {
    return _user;
}

inline const std::string& MysqlAuthenticator::schema() const {
    return _schema;
}

inline const std::string& MysqlAuthenticator::passwd() const {
    return _passwd;
}

inline MysqlCollation MysqlAuthenticator::collation() const {
    return _collation;
}

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_COUCHBASE_AUTHENTICATOR_H
