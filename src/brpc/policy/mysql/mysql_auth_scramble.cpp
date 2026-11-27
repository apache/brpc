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
#include <openssl/x509.h>

#include "butil/base64.h"
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

// Appends |len| in DER length encoding.
void AppendDerLength(size_t len, std::string* out) {
    if (len < 0x80) {
        out->push_back(static_cast<char>(len));
        return;
    }
    char bytes[sizeof(size_t)];
    size_t n = 0;
    while (len > 0) {
        bytes[n++] = static_cast<char>(len & 0xff);
        len >>= 8;
    }
    out->push_back(static_cast<char>(0x80 | n));
    while (n > 0) {
        out->push_back(bytes[--n]);
    }
}

// Decodes the base64 body of the PEM block delimited by |label|.
bool DecodePemBody(const butil::StringPiece& pem, const char* label,
                   std::string* der) {
    const std::string begin = std::string("-----BEGIN ") + label + "-----";
    const std::string end = std::string("-----END ") + label + "-----";
    const size_t b = pem.find(begin);
    const size_t e = pem.find(end);
    if (b == butil::StringPiece::npos || e == butil::StringPiece::npos ||
        e < b + begin.size()) {
        return false;
    }
    std::string body;
    for (size_t i = b + begin.size(); i < e; ++i) {
        const char c = pem[i];
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t') {
            body.push_back(c);
        }
    }
    return butil::Base64Decode(body, der);
}

// Wraps a PKCS#1 RSAPublicKey DER in a SubjectPublicKeyInfo DER:
//   SEQUENCE {
//     SEQUENCE { OID rsaEncryption, NULL },
//     BIT STRING { unused-bits=0, RSAPublicKey }
//   }
std::string WrapRsaPublicKeyAsSubjectPublicKeyInfo(const std::string& pkcs1) {
    static const char kRsaAlgorithmIdentifier[] =
        "\x30\x0d"                                    // SEQUENCE, 13 bytes
        "\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01"  // OID rsaEncryption
        "\x05\x00";                                   // NULL
    std::string bit_string;
    bit_string.push_back('\0');  // no unused bits
    bit_string.append(pkcs1);
    std::string bit_string_tlv;
    bit_string_tlv.push_back('\x03');
    AppendDerLength(bit_string.size(), &bit_string_tlv);
    bit_string_tlv.append(bit_string);

    std::string spki;
    spki.push_back('\x30');
    AppendDerLength(sizeof(kRsaAlgorithmIdentifier) - 1 + bit_string_tlv.size(), &spki);
    spki.append(kRsaAlgorithmIdentifier, sizeof(kRsaAlgorithmIdentifier) - 1);
    spki.append(bit_string_tlv);
    return spki;
}

// Parses a PEM-encoded public key and returns its SubjectPublicKeyInfo DER
// encoding, or an empty string when |pem| is not a parseable public key.
// Both "BEGIN PUBLIC KEY" (SubjectPublicKeyInfo) and "BEGIN RSA PUBLIC KEY"
// (PKCS#1) are accepted, and both are re-encoded as SubjectPublicKeyInfo so
// that they compare equal.
std::string PublicKeyDer(const butil::StringPiece& pem) {
    if (pem.empty()) {
        return std::string();
    }
    // PEM_read_bio_PUBKEY() does not read the PKCS#1 form on every supported
    // OpenSSL version, so handle that form explicitly.
    if (pem.find("-----BEGIN RSA PUBLIC KEY-----") != butil::StringPiece::npos) {
        std::string pkcs1;
        if (!DecodePemBody(pem, "RSA PUBLIC KEY", &pkcs1)) {
            return std::string();
        }
        return WrapRsaPublicKeyAsSubjectPublicKeyInfo(pkcs1);
    }
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (bio == nullptr) {
        return std::string();
    }
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (pkey == nullptr) {
        return std::string();
    }
    unsigned char* der = nullptr;
    const int len = i2d_PUBKEY(pkey, &der);
    EVP_PKEY_free(pkey);
    if (len <= 0 || der == nullptr) {
        OPENSSL_free(der);  // no-op when |der| was never set
        return std::string();
    }
    std::string out(reinterpret_cast<const char*>(der), static_cast<size_t>(len));
    OPENSSL_free(der);
    return out;
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

bool ServerPublicKeyMatches(const butil::StringPiece& server_pubkey_pem,
                            const butil::StringPiece& pinned_pubkey_pem) {
    const std::string server_der = PublicKeyDer(server_pubkey_pem);
    return !server_der.empty() && server_der == PublicKeyDer(pinned_pubkey_pem);
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
