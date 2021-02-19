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
// Author(s): Zhangke <zhangke960808@163.com>

#ifndef BRPC_POLICY_MONGO_AUTHENTICATOR_H_
#define BRPC_POLICY_MONGO_AUTHENTICATOR_H_

#include "brpc/authenticator.h"
namespace brpc {
namespace policy {

class MongoAuthenticator : public Authenticator {
 public:
  MongoAuthenticator(const butil::StringPiece& user,
                     const butil::StringPiece& passwd)
                     : _user(user.data(), user.size()),
                       _passwd(user.data(), user.size()) {}

  int GenerateCredential(std::string* auth_str) const {
    return 0;
  }

  int VerifyCredential(const std::string& auth_str,
                                 const butil::EndPoint& client_addr,
                                 AuthContext* out_ctx) const {
                                   return 0;
                                 }

  const butil::StringPiece user() const {
    return _user;
  }

  const butil::StringPiece passwd() const {
    return _passwd;
  }

 private:
  const std::string _user;
  const std::string _passwd;
};

}  // namespace policy
}  // namespace brpc

#endif // BRPC_POLICY_MONGO_AUTHENTICATOR_H_
