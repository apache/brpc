// Copyright (c) 2017 Baidu, Inc.
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
// Author(s): Chengcheng Wu <wuchengcheng@qiyi.com>

#ifndef BRPC_POLICY_COUCHBASE_AUTHENTICATOR_H
#define BRPC_POLICY_COUCHBASE_AUTHENTICATOR_H

#include "brpc/authenticator.h"

namespace brpc {
namespace policy {

// Request to couchbase for authentication.
// Notice that authentication for couchbase in special SASLAuthProtocol.
// Couchbase Server 2.2 provide CRAM-MD5 support for SASL authentication,
// but Couchbase Server prior to 2.2 using PLAIN SASL authentication.
class CouchbaseAuthenticator : public Authenticator {
 public:
  CouchbaseAuthenticator(const std::string& bucket_name,
                         const std::string& bucket_password)
      : bucket_name_(bucket_name), bucket_password_(bucket_password) {}

  int GenerateCredential(std::string* auth_str) const;

  int VerifyCredential(const std::string&, const butil::EndPoint&,
                       brpc::AuthContext*) const {
    return 0;
  }

 private:
  const std::string bucket_name_;
  const std::string bucket_password_;
};

}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_COUCHBASE_AUTHENTICATOR_H
