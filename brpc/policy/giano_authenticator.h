// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
//
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Nov  2 00:30:49 2014

#ifndef BRPC_POLICY_GIANO_AUTHENTICATOR_H
#define BRPC_POLICY_GIANO_AUTHENTICATOR_H

#include <baas-lib-c/baas.h>                   // Giano stuff
#include "brpc/authenticator.h"

namespace brpc {
namespace policy {

class GianoAuthenticator: public Authenticator {
public:
    // Either `gen' or `ver' can be NULL (but not at the same time),
    // in which case it can only verify/generate credential data
    explicit GianoAuthenticator(const baas::CredentialGenerator* gen,
                                const baas::CredentialVerifier* ver);

    ~GianoAuthenticator();

    int GenerateCredential(std::string* auth_str) const;

    int VerifyCredential(const std::string& auth_str,
                         const base::EndPoint& client_addr,
                         AuthContext* out_ctx) const;

private:
    baas::CredentialGenerator* _generator;
    baas::CredentialVerifier* _verifier;
};


}  // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_GIANO_AUTHENTICATOR_H

