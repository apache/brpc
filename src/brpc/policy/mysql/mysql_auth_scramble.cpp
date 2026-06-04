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

#include "brpc/policy/mysql/mysql_auth_scramble.h"

#include <cstring>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "butil/sha1.h"

namespace brpc {
namespace policy {
namespace mysql {

namespace {

bool Sha256Bytes(const unsigned char* data, size_t len, unsigned char out[32]) {
    unsigned int digest_len = 0;
    return EVP_Digest(data, len, out, &digest_len, EVP_sha256(), nullptr) == 1
        && digest_len == 32;
}

}  // namespace

std::string NativePasswordScramble(const butil::StringPiece& salt,
                                   const butil::StringPiece& password) {
    if (password.empty()) {
        return std::string();
    }
    if (salt.size() != kSaltLen) {
        return std::string();
    }

    const size_t kHashLen = butil::kSHA1Length;

    unsigned char sha_pw[kHashLen];
    butil::SHA1HashBytes(
        reinterpret_cast<const unsigned char*>(password.data()),
        password.size(), sha_pw);

    unsigned char sha_sha_pw[kHashLen];
    butil::SHA1HashBytes(sha_pw, kHashLen, sha_sha_pw);

    unsigned char joined[kHashLen * 2];
    memcpy(joined, salt.data(), kHashLen);
    memcpy(joined + kHashLen, sha_sha_pw, kHashLen);

    unsigned char salted_hash[kHashLen];
    butil::SHA1HashBytes(joined, sizeof(joined), salted_hash);

    std::string out(kHashLen, '\0');
    for (size_t i = 0; i < kHashLen; ++i) {
        out[i] = static_cast<char>(sha_pw[i] ^ salted_hash[i]);
    }
    return out;
}

std::string CachingSha2PasswordScramble(const butil::StringPiece& salt,
                                        const butil::StringPiece& password) {
    if (password.empty()) {
        return std::string();
    }
    if (salt.size() != kSaltLen) {
        return std::string();
    }

    const size_t kHashLen = 32;

    unsigned char sha_pw[kHashLen];
    if (!Sha256Bytes(reinterpret_cast<const unsigned char*>(password.data()),
                     password.size(), sha_pw)) {
        return std::string();
    }

    unsigned char sha_sha_pw[kHashLen];
    if (!Sha256Bytes(sha_pw, kHashLen, sha_sha_pw)) {
        return std::string();
    }

    unsigned char joined[kHashLen + kSaltLen];
    memcpy(joined, sha_sha_pw, kHashLen);
    memcpy(joined + kHashLen, salt.data(), kSaltLen);

    unsigned char salted_hash[kHashLen];
    if (!Sha256Bytes(joined, sizeof(joined), salted_hash)) {
        return std::string();
    }

    std::string out(kHashLen, '\0');
    for (size_t i = 0; i < kHashLen; ++i) {
        out[i] = static_cast<char>(sha_pw[i] ^ salted_hash[i]);
    }
    return out;
}

std::string CachingSha2PasswordRsaEncrypt(
        const butil::StringPiece& server_pubkey_pem,
        const butil::StringPiece& salt,
        const butil::StringPiece& password) {
    if (salt.size() != kSaltLen) {
        return std::string();
    }
    if (server_pubkey_pem.empty()) {
        return std::string();
    }

    std::string plaintext;
    plaintext.resize(password.size() + 1);
    for (size_t i = 0; i < password.size(); ++i) {
        plaintext[i] = static_cast<char>(
            password.data()[i] ^ salt.data()[i % kSaltLen]);
    }
    plaintext[password.size()] = static_cast<char>(
        '\0' ^ salt.data()[password.size() % kSaltLen]);

    BIO* bio = BIO_new_mem_buf(server_pubkey_pem.data(),
                               static_cast<int>(server_pubkey_pem.size()));
    if (bio == nullptr) {
        return std::string();
    }
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (pkey == nullptr) {
        return std::string();
    }

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (ctx == nullptr) {
        EVP_PKEY_free(pkey);
        return std::string();
    }

    std::string out;
    do {
        if (EVP_PKEY_encrypt_init(ctx) <= 0) break;
        if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) break;

        size_t out_len = 0;
        if (EVP_PKEY_encrypt(
                ctx, nullptr, &out_len,
                reinterpret_cast<const unsigned char*>(plaintext.data()),
                plaintext.size()) <= 0) {
            break;
        }
        out.resize(out_len);
        if (EVP_PKEY_encrypt(
                ctx,
                reinterpret_cast<unsigned char*>(&out[0]), &out_len,
                reinterpret_cast<const unsigned char*>(plaintext.data()),
                plaintext.size()) <= 0) {
            out.clear();
            break;
        }
        out.resize(out_len);
    } while (false);

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return out;
}

std::string CachingSha2PasswordCleartext(const butil::StringPiece& password) {
    if (password.empty()) {
        return std::string();
    }
    std::string out;
    out.reserve(password.size() + 1);
    out.append(password.data(), password.size());
    out.push_back('\0');
    return out;
}

std::string CachingSha2PasswordSlowPath(
        const butil::StringPiece& password,
        const butil::StringPiece& salt,
        const butil::StringPiece& server_pubkey_pem,
        bool is_ssl) {
    if (is_ssl) {
        return CachingSha2PasswordCleartext(password);
    }
    return CachingSha2PasswordRsaEncrypt(server_pubkey_pem, salt, password);
}

}  // namespace mysql
}  // namespace policy
}  // namespace brpc
