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


#ifndef BRPC_AUTHENTICATOR_H
#define BRPC_AUTHENTICATOR_H

#include <ostream>
#include "butil/endpoint.h"                    // butil::EndPoint
#include "butil/macros.h"                      // BAIDU_CONCAT
#include "brpc/extension.h"              // Extension<T>


namespace brpc {

class AuthContext {
public:
    AuthContext() : _is_service(false) {}
    ~AuthContext() {}

    const std::string& user() const { return _user; }
    void set_user(const std::string& user) { _user = user; }
    
    const std::string& group() const { return _group; }
    void set_group(const std::string& group) { _group = group; }

    const std::string& roles() const { return _roles; }
    void set_roles(const std::string& roles) { _roles = roles; }

    const std::string& starter() const { return _starter; }
    void set_starter(const std::string& starter) { _starter = starter; }

    bool is_service() const { return _is_service; }
    void set_is_service(bool is_service) { _is_service = is_service; }

private:
    bool _is_service;
    std::string _user;
    std::string _group;
    std::string _roles;
    std::string _starter;    
};

class Authenticator {
public:
    virtual ~Authenticator() {}

    // Implement this method to generate credential information
    // into `auth_str' which will be sent to `VerifyCredential'
    // at server side. This method will be called on client side.
    // Returns 0 on success, error code otherwise
    virtual int GenerateCredential(std::string* auth_str) const = 0;

    // Implement this method to verify credential information
    // `auth_str' from `client_addr'. You can fill credential
    // context (result) into `*out_ctx' and later fetch this
    // pointer from `Controller'.
    // Returns 0 on success, error code otherwise
    virtual int VerifyCredential(const std::string& auth_str,
                                 const butil::EndPoint& client_addr,
                                 AuthContext* out_ctx) const = 0;

};

inline std::ostream& operator<<(std::ostream& os, const AuthContext& ctx) {
    return os << "[name=" << ctx.user() << " [This is a "
              << (ctx.is_service() ? "service" : "user")
              << "], group=" << ctx.group() << ", roles=" << ctx.roles()
              << ", starter=" << ctx.starter() << "]";
}


} // namespace brpc



#endif // BRPC_AUTHENTICATOR_H
