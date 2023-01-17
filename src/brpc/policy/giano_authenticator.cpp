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

#ifdef BAIDU_INTERNAL


#include "butil/logging.h"
#include "brpc/policy/giano_authenticator.h"

namespace brpc {
namespace policy {

GianoAuthenticator::GianoAuthenticator(const baas::CredentialGenerator* gen,
                                       const baas::CredentialVerifier* ver) {
    if (gen) {
        _generator = new(std::nothrow) baas::CredentialGenerator(*gen);
        CHECK(_generator);
    } else {
        _generator = NULL;
    }
    if (ver) {
        _verifier = new(std::nothrow) baas::CredentialVerifier(*ver);
        CHECK(_verifier);
    } else {
        _verifier = NULL;
    }        
}

GianoAuthenticator::~GianoAuthenticator() {
    delete _generator;
    _generator = NULL;

    delete _verifier;
    _verifier = NULL;
}

int GianoAuthenticator::GenerateCredential(std::string* auth_str) const {
    if (NULL == _generator) {
        LOG(FATAL) << "CredentialGenerator is NULL";
        return -1;
    }

    return (baas::sdk::BAAS_OK == 
            _generator->GenerateCredential(auth_str) ? 0 : -1);            
}

int GianoAuthenticator::VerifyCredential(
        const std::string& auth_str,
        const butil::EndPoint& client_addr,
        AuthContext* out_ctx) const {
    if (NULL == _verifier) {
        LOG(FATAL) << "CredentialVerifier is NULL";
        return -1;
    }

    baas::CredentialContext ctx;
    int rc = _verifier->Verify(
            auth_str, endpoint2str(client_addr).c_str(), &ctx);
    if (rc != baas::sdk::BAAS_OK) {
        LOG(WARNING) << "Giano fails to verify credentical, "
                     << baas::sdk::GetReturnCodeMessage(rc);
        return -1;        
    }
    if (out_ctx != NULL) {
        out_ctx->set_user(ctx.user());
        out_ctx->set_group(ctx.group());
        out_ctx->set_roles(ctx.roles());
        out_ctx->set_starter(ctx.starter());
        out_ctx->set_is_service(ctx.IsService());
    }
    return 0;
}

}  // namespace policy
}  // namespace brpc
#endif // BAIDU_INTERNAL
