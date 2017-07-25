#include "base/logging.h"
#include "base/memory/singleton_on_pthread_once.h"
#include "brpc/policy/esp_authenticator.h"


namespace brpc {
namespace policy {

const char* MAGICNUM = "\0ESP\x01\x02";
const int MAGICNUM_LEN = 6;

int EspAuthenticator::GenerateCredential(std::string* auth_str) const {
    auth_str->assign(MAGICNUM, MAGICNUM_LEN);
    uint16_t local_port = 0;
    auth_str->append((char *)&local_port, sizeof(local_port));
    return 0;
}

int EspAuthenticator::VerifyCredential(
        const std::string& /*auth_str*/,
        const base::EndPoint& /*client_addr*/,
        AuthContext* /*out_ctx*/) const {
    //nothing to do
    return 0;
}

const Authenticator* global_esp_authenticator() {
    return base::get_leaky_singleton<EspAuthenticator>();
}

}  // namespace policy
} // namespace brpc

