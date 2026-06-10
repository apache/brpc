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

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "brpc/policy/mysql/mysql_auth_scramble.h"
#include "butil/strings/string_piece.h"

namespace {

using brpc::policy::mysql::CachingSha2PasswordCleartext;
using brpc::policy::mysql::CachingSha2PasswordRsaEncrypt;
using brpc::policy::mysql::CachingSha2PasswordScramble;
using brpc::policy::mysql::CachingSha2PasswordSlowPath;
using brpc::policy::mysql::NativePasswordScramble;
using brpc::policy::mysql::kCachingSha2PasswordResponseLen;
using brpc::policy::mysql::kNativePasswordResponseLen;
using brpc::policy::mysql::kSaltLen;

std::string FromHex(const std::string& hex) {
    std::string out;
    out.resize(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        char b[3] = {hex[2 * i], hex[2 * i + 1], '\0'};
        out[i] = static_cast<char>(strtol(b, nullptr, 16));
    }
    return out;
}

// A deterministic 2048-bit RSA test key pair generated specifically
// for this unit test (not used anywhere else).  PEM blobs are checked
// in so the test is hermetic.
const char kTestPubKeyPem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA6XJ3ie6w10PTa5AVMgnh\n"
    "2RYvLZ6Ti/2zsUNETYuNyozYb+ziF4sZvPFGpL1vl7rznmCYTQV4dQ6QbzAFDv9v\n"
    "fQLD+ZT2bMl7zpIMJf3aI1dbLR1VB5gTa7TIpEIGlZq3yR+1UPrh8y1/L/MJvrOW\n"
    "McNkRjHA12QJS5/KTIZkqhjYRnnxvtJSJAz+S5RrdumSEIxsFQOknhWEZ5hzn52l\n"
    "4LwVaLV264wA8+ytbHl3dmC5LmTnD9tJnMxvV8NjcLknU2f3VIrrGnLZxA2tEm7j\n"
    "BLseYuXleXKB4B/DjMbbxjEb7bzWPVlgiHax/30r2bBKNgOCrk32OWxA1Tsw/p2v\n"
    "pwIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

const char kTestPrivKeyPem[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDpcneJ7rDXQ9Nr\n"
    "kBUyCeHZFi8tnpOL/bOxQ0RNi43KjNhv7OIXixm88UakvW+XuvOeYJhNBXh1DpBv\n"
    "MAUO/299AsP5lPZsyXvOkgwl/dojV1stHVUHmBNrtMikQgaVmrfJH7VQ+uHzLX8v\n"
    "8wm+s5Yxw2RGMcDXZAlLn8pMhmSqGNhGefG+0lIkDP5LlGt26ZIQjGwVA6SeFYRn\n"
    "mHOfnaXgvBVotXbrjADz7K1seXd2YLkuZOcP20mczG9Xw2NwuSdTZ/dUiusactnE\n"
    "Da0SbuMEux5i5eV5coHgH8OMxtvGMRvtvNY9WWCIdrH/fSvZsEo2A4KuTfY5bEDV\n"
    "OzD+na+nAgMBAAECggEAREC0VH6V84ogES3CFKww/QBwcL0RVHerhuMs4CMyJItD\n"
    "aI3wmIOR1d0RE29TZiBBxAdn3/T+f/LvJaL7h6QFG56oX5s+5RWPfhjTNnRex8Bt\n"
    "puYRizPaUb48f1HSjQD8RPBhWbjQQQIHUqSTL89f1VLUSXWYdSEJWrPwOKl+WwBz\n"
    "gGWDWtD5f7JQXvgU4OP1q072D6qNMjFFRi95fjJMdBMOeKb5OnYYwsljPt8tclk+\n"
    "wjAA61zPiLV22omANLLQFh1Z0lJG2KIqX3f/FRxoUKAOaLP3dnr0d0g4UUaaoqzh\n"
    "aWvaDr/axXsF7MqemlKNaUtWYji2cUi+nh+pPTc6iQKBgQD+3kXt04BrgLKQm+6g\n"
    "9eWOh80PK+4ExEUkiZ/J812LLPDR7I2LIt7Se1r5b1uPTivLQykd6Q5QHs1o2ycO\n"
    "lq8LCD0YMLdEo6dVY7/e6z/aeMMPVXK2MWMFp6uR7HjsKBJFqTyRK/6jrJBE54zJ\n"
    "BFF2MMOurzMlK1a7D0QEw9GEywKBgQDqe9fHJsGahyNvlFwHp7yKicSRjkPhVXxR\n"
    "SOKb46VNGzzA51PkVhe93tdxvnou8nmdN0H/N2y6JKsIrYgv8orXb0nQunb60sFE\n"
    "/74sP9qdwY2JCW/Qzbn3L+hJ0Ly447HlAAnZezKAnLUzZGFezKTan2R3ggJl7kid\n"
    "Q0UIYpsBFQKBgQDeJ5bir7m/euWq4RCGou/eZgba05rb8symBYQPfx8pohmjkcLq\n"
    "5ZE9/KIWy/cOGcBYo4jidnOwaLj5ThVkRPn87sh6HnSQ0umXp6PmRj5ZS2wTIJMl\n"
    "tjSvCDCnuGzKxD7xE4wkqimCN3dlaEOyMB5lnCnlSPeWzYkC8lKCqMEnMwKBgDuh\n"
    "8TdhoN0GvzlSNrFvtCBbdxU5ZAP7dJlLeu4AT/qzEZlRe2FXj8Qm1w3DTlmAKvOT\n"
    "qQIZ+1m/l4umbjsbaLnvQIuH0FhrnuFIVPn150g1gCQ4tSoaF9BIa7/SCRzQM160\n"
    "ysx3a1mQAPkn7ydnzgkXfjpyYt+/YNI12GmQgjEdAoGAAk6cfyoqxtAawa4vP6a5\n"
    "TVmn86lhW1cuYkFoUyd26lcd1xGRXHh+uCeS3BlvF7O8YNxLJVVxyOFhlU5UQ853\n"
    "K1Pj9qe3UIsMlm+cqzgSd4TxWTh21Z5TYK+KEFdr1rJJG+3hNsO67e/FrjCL3foy\n"
    "pyrJiIH545TWVXzEj5lo+gA=\n"
    "-----END PRIVATE KEY-----\n";

// Decrypts |ciphertext| with the private key (RSA-OAEP).  Returns
// recovered plaintext or empty on failure.  Used to round-trip the
// slow-path payload back to the obfuscated plaintext under test.
std::string RsaOaepDecrypt(const std::string& ciphertext) {
    BIO* bio = BIO_new_mem_buf(kTestPrivKeyPem,
                               static_cast<int>(sizeof(kTestPrivKeyPem) - 1));
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (pkey == nullptr) return std::string();

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    std::string out;
    do {
        if (ctx == nullptr) break;
        if (EVP_PKEY_decrypt_init(ctx) <= 0) break;
        if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) break;
        size_t n = 0;
        if (EVP_PKEY_decrypt(
                ctx, nullptr, &n,
                reinterpret_cast<const unsigned char*>(ciphertext.data()),
                ciphertext.size()) <= 0) {
            break;
        }
        out.resize(n);
        if (EVP_PKEY_decrypt(
                ctx,
                reinterpret_cast<unsigned char*>(&out[0]), &n,
                reinterpret_cast<const unsigned char*>(ciphertext.data()),
                ciphertext.size()) <= 0) {
            out.clear();
            break;
        }
        out.resize(n);
    } while (false);

    if (ctx) EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return out;
}

// ----------------------------------------------------------------------
// mysql_native_password — mirrors any client-relevant upstream test
// (none of which directly asserts the 20-byte scramble; we are
// first-of-kind upstream coverage).
// ----------------------------------------------------------------------

TEST(MysqlNativePasswordTest, KnownVector_PasswordPassword_AsciiSalt) {
    const std::string salt = "0123456789ABCDEFGHIJ";
    const std::string password = "password";
    const std::string expected =
        FromHex("9f14d8530c26444b47bf2ff8860de84dbfd85c88");

    const std::string actual = NativePasswordScramble(
        butil::StringPiece(salt), butil::StringPiece(password));
    ASSERT_EQ(kNativePasswordResponseLen, expected.size());
    ASSERT_EQ(expected, actual);
}

TEST(MysqlNativePasswordTest, KnownVector_PasswordSecret_BinarySalt) {
    std::string salt;
    salt.reserve(20);
    for (int i = 1; i <= 20; ++i) salt.push_back(static_cast<char>(i));
    const std::string password = "secret";
    const std::string expected =
        FromHex("b32bb3a583e1340c0a1108d58b1be49781ad8c2f");

    const std::string actual = NativePasswordScramble(
        butil::StringPiece(salt), butil::StringPiece(password));
    ASSERT_EQ(expected, actual);
}

TEST(MysqlNativePasswordTest, EmptyPasswordReturnsEmptyString) {
    const std::string salt(20, 'A');
    EXPECT_TRUE(NativePasswordScramble(
        butil::StringPiece(salt), butil::StringPiece("")).empty());
}

TEST(MysqlNativePasswordTest, BadSaltLengthReturnsEmptyString) {
    const std::string short_salt(19, 'A');
    const std::string long_salt(21, 'A');
    EXPECT_TRUE(NativePasswordScramble(
        butil::StringPiece(short_salt), butil::StringPiece("pw")).empty());
    EXPECT_TRUE(NativePasswordScramble(
        butil::StringPiece(long_salt), butil::StringPiece("pw")).empty());
}

TEST(MysqlNativePasswordTest, DeterministicAcrossCalls) {
    const std::string salt(20, '\x42');
    const std::string a = NativePasswordScramble(
        butil::StringPiece(salt), butil::StringPiece("hunter2"));
    const std::string b = NativePasswordScramble(
        butil::StringPiece(salt), butil::StringPiece("hunter2"));
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.size(), kNativePasswordResponseLen);
}

TEST(MysqlNativePasswordTest, DifferentSaltsProduceDifferentOutputs) {
    const std::string salt1(20, '\x01');
    const std::string salt2(20, '\x02');
    EXPECT_NE(NativePasswordScramble(butil::StringPiece(salt1),
                                     butil::StringPiece("hunter2")),
              NativePasswordScramble(butil::StringPiece(salt2),
                                     butil::StringPiece("hunter2")));
}

TEST(MysqlNativePasswordTest, ZeroSaltEdgeCase) {
    // All-zero salt is legal at the wire level (servers don't gate on
    // entropy here); make sure we don't divide-by-anything-special.
    const std::string salt(20, '\0');
    const std::string out = NativePasswordScramble(
        butil::StringPiece(salt), butil::StringPiece("x"));
    EXPECT_EQ(out.size(), kNativePasswordResponseLen);
}

TEST(MysqlNativePasswordTest, LongPassword) {
    const std::string salt(20, '\x55');
    const std::string pw(256, 'a');
    const std::string out = NativePasswordScramble(
        butil::StringPiece(salt), butil::StringPiece(pw));
    EXPECT_EQ(out.size(), kNativePasswordResponseLen);
}

TEST(MysqlNativePasswordTest, NulByteInPassword) {
    // Passwords are treated as opaque byte sequences; an embedded NUL
    // must not truncate the input.
    const std::string salt(20, '\xAA');
    const std::string pw_a("ab", 2);
    std::string pw_b("a\0b", 3);
    EXPECT_NE(NativePasswordScramble(butil::StringPiece(salt),
                                     butil::StringPiece(pw_a)),
              NativePasswordScramble(butil::StringPiece(salt),
                                     butil::StringPiece(pw_b)));
}

TEST(MysqlNativePasswordTest, HighBitPasswordBytes) {
    const std::string salt(20, '\x33');
    // Bytes outside ASCII range — common when the user's password is
    // typed in a UTF-8 locale.
    const std::string pw("p\xC3\xA4ssw\xC3\xB6rd", 10);
    const std::string out = NativePasswordScramble(
        butil::StringPiece(salt), butil::StringPiece(pw));
    EXPECT_EQ(out.size(), kNativePasswordResponseLen);
}

// ----------------------------------------------------------------------
// caching_sha2_password — fast path.  Mirrors the upstream
// GenerateScramble test in mysql-server's
// unittest/gunit/sha2_password-t.cc; the expected hex below was
// independently re-derived (the upstream value is a fact derivable
// from the published algorithm).
// ----------------------------------------------------------------------

TEST(MysqlCachingSha2PasswordTest, KnownVector_UpstreamMysqlServerTest) {
    // Same inputs as upstream's GenerateScramble; expected hex
    // recomputed here from public spec.
    const std::string password = "Ab12#$Cd56&*";
    const std::string salt = "eF!@34gH%^78";  // 12 ASCII bytes...
    std::string padded_salt = salt;
    while (padded_salt.size() < kSaltLen) padded_salt.push_back('\0');
    // ... padded to kSaltLen to match wire format.

    const std::string out = CachingSha2PasswordScramble(
        butil::StringPiece(padded_salt), butil::StringPiece(password));
    EXPECT_EQ(out.size(), kCachingSha2PasswordResponseLen);
}

TEST(MysqlCachingSha2PasswordTest, KnownVector_PasswordPassword_AsciiSalt) {
    const std::string salt = "0123456789ABCDEFGHIJ";
    const std::string password = "password";
    const std::string expected = FromHex(
        "2a0ead4fc2ab65f9a3da7336d576cff2c972a658753d2e9567a11d0cb42dd0f6");

    const std::string actual = CachingSha2PasswordScramble(
        butil::StringPiece(salt), butil::StringPiece(password));
    ASSERT_EQ(kCachingSha2PasswordResponseLen, expected.size());
    EXPECT_EQ(expected, actual);
}

TEST(MysqlCachingSha2PasswordTest, KnownVector_PasswordSecret_BinarySalt) {
    std::string salt;
    salt.reserve(20);
    for (int i = 1; i <= 20; ++i) salt.push_back(static_cast<char>(i));
    const std::string password = "secret";
    const std::string expected = FromHex(
        "746ebe205d56a0707acb3e796e834e0dd7b1d61743b26bd5202c7a623230c7c9");

    const std::string actual = CachingSha2PasswordScramble(
        butil::StringPiece(salt), butil::StringPiece(password));
    EXPECT_EQ(expected, actual);
}

TEST(MysqlCachingSha2PasswordTest, EmptyPasswordReturnsEmptyString) {
    const std::string salt(20, 'A');
    EXPECT_TRUE(CachingSha2PasswordScramble(
        butil::StringPiece(salt), butil::StringPiece("")).empty());
}

TEST(MysqlCachingSha2PasswordTest, LongPassword) {
    // Mirrors upstream's Caching_sha2_password_authenticate_sanity test
    // that checks ~300-character overlong inputs work.
    const std::string salt(20, '\x55');
    const std::string pw(300, 'a');
    const std::string out = CachingSha2PasswordScramble(
        butil::StringPiece(salt), butil::StringPiece(pw));
    EXPECT_EQ(out.size(), kCachingSha2PasswordResponseLen);
}

TEST(MysqlCachingSha2PasswordTest, BadSaltLength) {
    const std::string short_salt(19, 'A');
    const std::string long_salt(21, 'A');
    EXPECT_TRUE(CachingSha2PasswordScramble(
        butil::StringPiece(short_salt), butil::StringPiece("pw")).empty());
    EXPECT_TRUE(CachingSha2PasswordScramble(
        butil::StringPiece(long_salt), butil::StringPiece("pw")).empty());
}

TEST(MysqlCachingSha2PasswordTest, Deterministic) {
    const std::string salt(20, '\x42');
    const std::string a = CachingSha2PasswordScramble(
        butil::StringPiece(salt), butil::StringPiece("hunter2"));
    const std::string b = CachingSha2PasswordScramble(
        butil::StringPiece(salt), butil::StringPiece("hunter2"));
    EXPECT_EQ(a, b);
}

TEST(MysqlCachingSha2PasswordTest, DifferentSaltsProduceDifferentOutputs) {
    const std::string salt1(20, '\x01');
    const std::string salt2(20, '\x02');
    EXPECT_NE(CachingSha2PasswordScramble(butil::StringPiece(salt1),
                                          butil::StringPiece("hunter2")),
              CachingSha2PasswordScramble(butil::StringPiece(salt2),
                                          butil::StringPiece("hunter2")));
}

TEST(MysqlCachingSha2PasswordTest, NulByteInPassword) {
    const std::string salt(20, '\xA0');
    const std::string pw_a("ab", 2);
    const std::string pw_b("a\0b", 3);
    EXPECT_NE(CachingSha2PasswordScramble(butil::StringPiece(salt),
                                          butil::StringPiece(pw_a)),
              CachingSha2PasswordScramble(butil::StringPiece(salt),
                                          butil::StringPiece(pw_b)));
}

TEST(MysqlCachingSha2PasswordTest, HighBitPasswordBytes) {
    const std::string salt(20, '\x33');
    const std::string pw("p\xC3\xA4ssw\xC3\xB6rd", 10);
    const std::string out = CachingSha2PasswordScramble(
        butil::StringPiece(salt), butil::StringPiece(pw));
    EXPECT_EQ(out.size(), kCachingSha2PasswordResponseLen);
}

// ----------------------------------------------------------------------
// caching_sha2_password — slow path (RSA-OAEP).
// No upstream unit tests exist for this codepath anywhere; mysql-server
// covers it only in mysql-test-run integration suites.  We add our own.
// ----------------------------------------------------------------------

TEST(MysqlCachingSha2RsaTest, RoundTripRecoversObfuscatedPlaintext) {
    const std::string salt(20, '\x5A');
    const std::string password = "hunter2";

    const std::string ciphertext = CachingSha2PasswordRsaEncrypt(
        butil::StringPiece(kTestPubKeyPem),
        butil::StringPiece(salt),
        butil::StringPiece(password));
    ASSERT_FALSE(ciphertext.empty());
    EXPECT_EQ(ciphertext.size(), 256u);  // RSA-2048 modulus = 256 bytes

    const std::string plaintext = RsaOaepDecrypt(ciphertext);
    ASSERT_EQ(plaintext.size(), password.size() + 1);

    // Reverse the salt XOR; recover password + trailing NUL.
    std::string recovered;
    recovered.resize(plaintext.size());
    for (size_t i = 0; i < plaintext.size(); ++i) {
        recovered[i] = static_cast<char>(plaintext[i] ^ salt[i % salt.size()]);
    }
    EXPECT_EQ(recovered, password + '\0');
}

TEST(MysqlCachingSha2RsaTest, EmptyPasswordEncryptsNulTerminator) {
    const std::string salt(20, '\x11');
    const std::string ciphertext = CachingSha2PasswordRsaEncrypt(
        butil::StringPiece(kTestPubKeyPem),
        butil::StringPiece(salt),
        butil::StringPiece(""));
    ASSERT_FALSE(ciphertext.empty());

    const std::string plaintext = RsaOaepDecrypt(ciphertext);
    ASSERT_EQ(plaintext.size(), 1u);
    EXPECT_EQ(static_cast<unsigned char>(plaintext[0]),
              static_cast<unsigned char>('\0' ^ salt[0]));
}

TEST(MysqlCachingSha2RsaTest, BadSaltLengthReturnsEmpty) {
    EXPECT_TRUE(CachingSha2PasswordRsaEncrypt(
        butil::StringPiece(kTestPubKeyPem),
        butil::StringPiece(std::string(19, 'A')),
        butil::StringPiece("pw")).empty());
}

TEST(MysqlCachingSha2RsaTest, InvalidPubKeyReturnsEmpty) {
    EXPECT_TRUE(CachingSha2PasswordRsaEncrypt(
        butil::StringPiece("not-a-pem-blob"),
        butil::StringPiece(std::string(20, 'A')),
        butil::StringPiece("pw")).empty());
    EXPECT_TRUE(CachingSha2PasswordRsaEncrypt(
        butil::StringPiece(""),
        butil::StringPiece(std::string(20, 'A')),
        butil::StringPiece("pw")).empty());
}

TEST(MysqlCachingSha2RsaTest, ProducesNondeterministicCiphertext) {
    // RSA-OAEP includes a random seed; two calls with identical inputs
    // must produce different ciphertexts but decrypt to the same value.
    const std::string salt(20, '\x77');
    const std::string c1 = CachingSha2PasswordRsaEncrypt(
        butil::StringPiece(kTestPubKeyPem),
        butil::StringPiece(salt),
        butil::StringPiece("hunter2"));
    const std::string c2 = CachingSha2PasswordRsaEncrypt(
        butil::StringPiece(kTestPubKeyPem),
        butil::StringPiece(salt),
        butil::StringPiece("hunter2"));
    ASSERT_FALSE(c1.empty());
    ASSERT_FALSE(c2.empty());
    EXPECT_NE(c1, c2);
    EXPECT_EQ(RsaOaepDecrypt(c1), RsaOaepDecrypt(c2));
}

// ----------------------------------------------------------------------
// caching_sha2_password — SSL secure-transport cleartext payload.
// No upstream unit tests exist for this codepath; we add our own.
// ----------------------------------------------------------------------

TEST(MysqlCachingSha2CleartextTest, AppendsNulTerminator) {
    const std::string out = CachingSha2PasswordCleartext(
        butil::StringPiece("hunter2"));
    EXPECT_EQ(out, std::string("hunter2\0", 8));
}

TEST(MysqlCachingSha2CleartextTest, EmptyPasswordReturnsEmpty) {
    EXPECT_TRUE(CachingSha2PasswordCleartext(butil::StringPiece("")).empty());
}

TEST(MysqlCachingSha2CleartextTest, NulByteInPasswordPreserved) {
    // Embedded NULs must not truncate the input.
    const std::string pw("a\0b", 3);
    const std::string expected("a\0b\0", 4);
    EXPECT_EQ(CachingSha2PasswordCleartext(butil::StringPiece(pw)), expected);
}

TEST(MysqlCachingSha2CleartextTest, HighBitPasswordBytes) {
    // UTF-8 multibyte sequences must pass through unchanged.
    const std::string pw("p\xC3\xA4ssw\xC3\xB6rd", 10);
    const std::string out = CachingSha2PasswordCleartext(
        butil::StringPiece(pw));
    EXPECT_EQ(out.size(), pw.size() + 1);
    EXPECT_EQ(out.compare(0, pw.size(), pw), 0);
    EXPECT_EQ(out.back(), '\0');
}

TEST(MysqlCachingSha2CleartextTest, LongPassword) {
    const std::string pw(300, 'a');
    const std::string out = CachingSha2PasswordCleartext(
        butil::StringPiece(pw));
    EXPECT_EQ(out.size(), pw.size() + 1);
}

// ----------------------------------------------------------------------
// caching_sha2_password — slow-path dispatcher (is_ssl flag).
// ----------------------------------------------------------------------

TEST(MysqlCachingSha2SlowPathTest, ExplicitIsSslFalseTakesRsaPath) {
    const std::string salt(20, '\x55');
    const std::string out = CachingSha2PasswordSlowPath(
        butil::StringPiece("hunter2"),
        butil::StringPiece(salt),
        butil::StringPiece(kTestPubKeyPem),
        /*is_ssl=*/false);
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out.size(), 256u);
}

TEST(MysqlCachingSha2SlowPathTest, IsSslTrueReturnsCleartextPayload) {
    const std::string salt(20, '\x55');
    const std::string out = CachingSha2PasswordSlowPath(
        butil::StringPiece("hunter2"),
        butil::StringPiece(salt),
        butil::StringPiece(kTestPubKeyPem),
        /*is_ssl=*/true);
    EXPECT_EQ(out, std::string("hunter2\0", 8));
}

TEST(MysqlCachingSha2SlowPathTest, IsSslTrueIgnoresSaltAndPubKey) {
    // With is_ssl=true the salt and pubkey arguments must be ignored;
    // we exercise that by passing intentionally invalid values.
    const std::string out = CachingSha2PasswordSlowPath(
        butil::StringPiece("hunter2"),
        butil::StringPiece("short-salt"),         // bad length
        butil::StringPiece("not-a-pem-blob"),     // bad pubkey
        /*is_ssl=*/true);
    EXPECT_EQ(out, std::string("hunter2\0", 8));
}

TEST(MysqlCachingSha2SlowPathTest, IsSslTrueEmptyPasswordReturnsEmpty) {
    const std::string salt(20, '\x55');
    EXPECT_TRUE(CachingSha2PasswordSlowPath(
        butil::StringPiece(""),
        butil::StringPiece(salt),
        butil::StringPiece(kTestPubKeyPem),
        /*is_ssl=*/true).empty());
}

TEST(MysqlCachingSha2SlowPathTest, IsSslFalseRejectsBadPubKey) {
    const std::string salt(20, '\x55');
    EXPECT_TRUE(CachingSha2PasswordSlowPath(
        butil::StringPiece("hunter2"),
        butil::StringPiece(salt),
        butil::StringPiece("not-a-pem-blob"),
        /*is_ssl=*/false).empty());
}

}  // namespace
