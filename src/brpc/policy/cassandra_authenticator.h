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
// Author(s): Daojin, Cai <caidaojin@qiyi.com>

#ifndef BRPC_POLICY_CASSANDRA_AUTHENTICATOR_H
#define BRPC_POLICY_CASSANDRA_AUTHENTICATOR_H

#include "brpc/authenticator.h"

namespace brpc {
namespace policy {

class CassandraAuthenticator : public Authenticator {
public:
    CassandraAuthenticator(const std::string& username,
                           const std::string& password,
                           const std::string& keyspace) 
        : _user_name(username), _password(password), _key_space(keyspace) {}

    CassandraAuthenticator(const std::string& username,
                           const std::string& password) 
        : _user_name(username), _password(password) {}

    CassandraAuthenticator(const std::string& keyspace)
        : _key_space(keyspace){}

    void set_cql_protocol_version(uint8_t version) {
        _cql_protocol_version = version;
    }

    bool has_set_cql_protocol_version() const {
        return _cql_protocol_version != 0x00;
    }

    uint8_t cql_protocol_version() const {
        return _cql_protocol_version;
    }

    const std::string& user_name() const {
        return _user_name;
    }

    const std::string& password() const {
        return _password;
    }

    const std::string& key_space() const {
        return _key_space;
    }

private:
    // CQL version.
    uint8_t _cql_protocol_version = 0x00;

    std::string _user_name;
    std::string _password;
    std::string _key_space;
};

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_CASSANDRA_AUTHENTICATOR_H
