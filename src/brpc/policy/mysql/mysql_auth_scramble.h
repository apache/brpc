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

// Clean-room implementation of the three MySQL client authentication
// scrambles, written from MySQL's public protocol documentation and
// not derived from any GPL-licensed source.
//
// Specifications:
//   mysql_native_password:
//     https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase_authentication_methods_native_password_authentication.html
//   caching_sha2_password (fast path + RSA path):
//     https://dev.mysql.com/doc/dev/mysql-server/latest/page_caching_sha2_authentication_exchanges.html

#ifndef BRPC_POLICY_MYSQL_MYSQL_AUTH_SCRAMBLE_H
#define BRPC_POLICY_MYSQL_MYSQL_AUTH_SCRAMBLE_H

#include <string>

#include "butil/strings/string_piece.h"

namespace brpc {
namespace policy {
namespace mysql {

// Salt length in HandshakeV10's auth-plugin-data field.  Both
// mysql_native_password and caching_sha2_password use a 20-byte salt.
static const size_t kSaltLen = 20;

// mysql_native_password produces a 20-byte (SHA-1-sized) response.
static const size_t kNativePasswordResponseLen = 20;

// caching_sha2_password fast path produces a 32-byte (SHA-256-sized)
// response.
static const size_t kCachingSha2PasswordResponseLen = 32;

// Computes the mysql_native_password scramble.
//   scramble = SHA1(p) XOR SHA1( salt || SHA1( SHA1(p) ) )
//
// Returns 20 raw bytes on success.  Returns an empty string when the
// password is empty (per spec: zero-byte wire response) or when |salt|
// is not exactly kSaltLen bytes.
std::string NativePasswordScramble(const butil::StringPiece& salt,
                                   const butil::StringPiece& password);

// Computes the caching_sha2_password fast-path scramble.
//   scramble = SHA256(p) XOR SHA256( SHA256( SHA256(p) ) || salt )
//
// Returns 32 raw bytes on success.  Returns an empty string when the
// password is empty or when |salt| is not exactly kSaltLen bytes.
std::string CachingSha2PasswordScramble(const butil::StringPiece& salt,
                                        const butil::StringPiece& password);

// Computes the caching_sha2_password slow-path payload using RSA-OAEP
// encryption against the server's PEM-encoded RSA public key.
//
//   obfuscated = (password || '\0') XOR repeat(salt, len)
//   ciphertext = RSA-OAEP-SHA1-encrypt(obfuscated, server_pubkey)
//
// Returns the raw ciphertext (RSA modulus size in bytes) on success.
// Returns an empty string when |salt| is not kSaltLen, when the PEM
// blob does not parse as an RSA public key, or when the password plus
// terminator does not fit the OAEP plaintext budget for the key.
std::string CachingSha2PasswordRsaEncrypt(
        const butil::StringPiece& server_pubkey_pem,
        const butil::StringPiece& salt,
        const butil::StringPiece& password);

// Computes the caching_sha2_password "secure transport" payload: the
// raw password bytes followed by a single NUL terminator.  Safe to
// send only when the underlying channel is already protected
// (SSL-wrapped, unix domain socket, or shared memory) -- the bytes
// travel in the clear at this layer.
//
// Mirrors what the official mysql client sends from
//   sql-common/client_authentication.cc:871
// when is_secure_transport() returns true.
//
// Returns "<password>\0" on success.  Returns an empty string when
// |password| is empty (matches the wire convention for blank
// passwords).
std::string CachingSha2PasswordCleartext(const butil::StringPiece& password);

// Dispatches the caching_sha2_password slow-path response computation.
//
//   is_ssl=true  -> CachingSha2PasswordCleartext(password)
//                   |salt| and |server_pubkey_pem| are ignored.
//   is_ssl=false -> CachingSha2PasswordRsaEncrypt(
//                       server_pubkey_pem, salt, password)
//
// |is_ssl| is intentionally NOT defaulted: every caller must state
// whether the underlying channel is secure (SSL/unix-socket/shared-mem),
// making the cleartext-vs-RSA decision explicit at the call site.  Pass
// is_ssl=true on a secure channel to send the password in the clear (one
// round trip); pass is_ssl=false on plain TCP to use RSA-OAEP.
std::string CachingSha2PasswordSlowPath(
        const butil::StringPiece& password,
        const butil::StringPiece& salt,
        const butil::StringPiece& server_pubkey_pem,
        bool is_ssl);

}  // namespace mysql
}  // namespace policy
}  // namespace brpc

#endif  // BRPC_POLICY_MYSQL_MYSQL_AUTH_SCRAMBLE_H
