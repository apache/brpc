// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Nov  3 00:40:20 2014

#ifdef BAIDU_INTERNAL

#include "base/logging.h"
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
    if (_generator) {
        delete _generator;
        _generator = NULL;
    }
    if (_verifier) {
        delete _verifier;
        _verifier = NULL;
    }
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
        const base::EndPoint& client_addr,
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
