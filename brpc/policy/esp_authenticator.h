#ifndef BRPC_POLICY_ESP_AUTHENTICATOR_H
#define BRPC_POLICY_ESP_AUTHENTICATOR_H

#include "brpc/authenticator.h"


namespace brpc {
namespace policy {

class EspAuthenticator: public Authenticator {
public:
    int GenerateCredential(std::string* auth_str) const;

    int VerifyCredential(const std::string& auth_str,
                         const base::EndPoint& client_addr,
                         AuthContext* out_ctx) const;
};

const Authenticator* global_esp_authenticator();

}  // namespace policy
} // namespace brpc


#endif // BRPC_POLICY_GIANO_AUTHENTICATOR_H

