#ifndef BRPC_POLICY_MYSQL_AUTHENTICATOR_H
#define BRPC_POLICY_MYSQL_AUTHENTICATOR_H

#include <brpc/authenticator.h>

namespace brpc {
namespace policy {

class MysqlAuthenticator : public ::brpc::Authenticator {
public:
    MysqlAuthenticator() { }
    virtual ~MysqlAuthenticator() { }

    int GenerateCredential(std::string* auth_str) const override {
        CHECK(false) << "No implementation";
        return -1;
    }

    int VerifyCredential(const std::string& auth_str,
                         const ::butil::EndPoint& client_addr,
                         ::brpc::AuthContext* out_ctx) const override {
        CHECK(false) << "No implementation";
        return -1;
    }

    virtual int VerifyCredential(
        uint16_t client_flags,
        const std::string& username,
        const std::string& database,
        const std::string& auth_response,
        const std::string& scramble,
        const ::butil::EndPoint& client_addr,
        ::brpc::AuthContext* out_ctx) const = 0;
};

} // policy
} // tamdb

#endif // BRPC_POLICY_MYSQL_AUTHENTICATOR_H
