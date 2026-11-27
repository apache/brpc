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

// End-to-end tests for the caching_sha2_password full-authentication
// exchange over a plain-TCP connection, driven through a real brpc
// Channel against a fake MySQL server.  The fake server sends the
// perform_full_authentication marker (0x04) and, when the client asks
// for it (0x02), an RSA public key.  The client must only encrypt the
// password with that in-band key when it matches a pinned key; the
// server decrypts what it receives to prove whether the password was
// released.

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <thread>

#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/policy/mysql/mysql.h"
#include "brpc/policy/mysql/mysql_auth_scramble.h"
#include "brpc/policy/mysql/mysql_authenticator.h"

namespace {

const char kPassword[] = "s3cret-pw";
const char kSalt[] = "0123456789abcdefghij";  // 20 bytes

// One leading byte and two length-encoded zero integers, status 0x0002
// (autocommit) and zero warnings: the minimal OK packet body.
std::string MakeOkPayload() {
    return std::string("\x00\x00\x00\x02\x00\x00\x00", 7);
}

// Builds a HandshakeV10 payload advertising caching_sha2_password.
std::string MakeGreeting(const std::string& salt) {
    std::string out;
    out.push_back('\x0a');  // protocol version
    out.append("8.0.0-fake");
    out.push_back('\0');
    out.append(4, '\0');  // connection id
    out.append(salt.data(), 8);
    out.push_back('\0');  // filler
    const uint32_t caps = 0xffffffff;
    out.push_back(static_cast<char>(caps & 0xff));
    out.push_back(static_cast<char>((caps >> 8) & 0xff));
    out.push_back('\x21');  // character set
    out.append(2, '\0');    // status flags
    out.push_back(static_cast<char>((caps >> 16) & 0xff));
    out.push_back(static_cast<char>((caps >> 24) & 0xff));
    out.push_back('\x15');  // auth-plugin-data length (21)
    out.append(10, '\0');
    out.append(salt.data() + 8, salt.size() - 8);
    out.push_back('\0');  // salt terminator
    out.append("caching_sha2_password");
    out.push_back('\0');
    return out;
}

bool WriteFull(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t nw = write(fd, data.data() + off, data.size() - off);
        if (nw > 0) {
            off += static_cast<size_t>(nw);
        } else if (nw < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool ReadFull(int fd, char* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        const ssize_t nr = read(fd, buf + off, n - off);
        if (nr > 0) {
            off += static_cast<size_t>(nr);
        } else if (nr < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

// Frames |payload| with the 4-byte MySQL packet header carrying |seq|.
bool WritePacket(int fd, const std::string& payload, uint8_t seq) {
    std::string out;
    const uint32_t len = static_cast<uint32_t>(payload.size());
    out.push_back(static_cast<char>(len & 0xff));
    out.push_back(static_cast<char>((len >> 8) & 0xff));
    out.push_back(static_cast<char>((len >> 16) & 0xff));
    out.push_back(static_cast<char>(seq));
    out.append(payload);
    return WriteFull(fd, out);
}

// Reads one MySQL packet.  Returns false on EOF or timeout, which is how
// the client signals that it refused to continue the handshake.
bool ReadPacket(int fd, std::string* payload, uint8_t* seq) {
    char header[4];
    if (!ReadFull(fd, header, sizeof(header))) {
        return false;
    }
    const uint32_t len = (static_cast<uint8_t>(header[0]))
                       | (static_cast<uint8_t>(header[1]) << 8)
                       | (static_cast<uint8_t>(header[2]) << 16);
    *seq = static_cast<uint8_t>(header[3]);
    payload->resize(len);
    return len == 0 || ReadFull(fd, &(*payload)[0], len);
}

void SetSocketTimeout(int fd) {
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

struct RsaKey {
    EVP_PKEY* pkey = nullptr;
    std::string public_pem;
    ~RsaKey() { EVP_PKEY_free(pkey); }
};

bool GenerateRsaKey(RsaKey* out) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (ctx == nullptr) {
        return false;
    }
    bool ok = EVP_PKEY_keygen_init(ctx) > 0 &&
              EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) > 0 &&
              EVP_PKEY_keygen(ctx, &out->pkey) > 0;
    EVP_PKEY_CTX_free(ctx);
    if (!ok) {
        return false;
    }
    BIO* bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
        return false;
    }
    ok = PEM_write_bio_PUBKEY(bio, out->pkey) == 1;
    if (ok) {
        char* data = nullptr;
        const long len = BIO_get_mem_data(bio, &data);
        out->public_pem.assign(data, len);
    }
    BIO_free(bio);
    return ok;
}

// Decrypts the RSA-OAEP blob the client sent and undoes the salt XOR.
bool RecoverPassword(EVP_PKEY* pkey, const std::string& ciphertext,
                     const std::string& salt, std::string* password) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (ctx == nullptr) {
        return false;
    }
    bool ok = false;
    std::string plaintext;
    do {
        if (EVP_PKEY_decrypt_init(ctx) <= 0) break;
        if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0) break;
        size_t out_len = 0;
        if (EVP_PKEY_decrypt(ctx, nullptr, &out_len,
                             reinterpret_cast<const unsigned char*>(ciphertext.data()),
                             ciphertext.size()) <= 0) {
            break;
        }
        plaintext.resize(out_len);
        if (EVP_PKEY_decrypt(ctx,
                             reinterpret_cast<unsigned char*>(&plaintext[0]), &out_len,
                             reinterpret_cast<const unsigned char*>(ciphertext.data()),
                             ciphertext.size()) <= 0) {
            break;
        }
        plaintext.resize(out_len);
        ok = true;
    } while (false);
    EVP_PKEY_CTX_free(ctx);
    if (!ok || plaintext.empty()) {
        return false;
    }
    for (size_t i = 0; i < plaintext.size(); ++i) {
        plaintext[i] ^= salt[i % salt.size()];
    }
    // The plaintext is (password || '\0') XOR salt.
    if (plaintext.back() != '\0') {
        return false;
    }
    password->assign(plaintext.data(), plaintext.size() - 1);
    return true;
}

// The two 2048-bit keys used by the tests: |kServerKey| is the key the
// fake server owns, |kAttackerKey| plays a man-in-the-middle's key.
RsaKey* GetServerKey() {
    static RsaKey key;
    static const bool ok = GenerateRsaKey(&key);
    return ok ? &key : nullptr;
}

RsaKey* GetAttackerKey() {
    static RsaKey key;
    static const bool ok = GenerateRsaKey(&key);
    return ok ? &key : nullptr;
}

// The behaviour the fake server drives after the client's fast scramble.
enum class FullAuthMode {
    // Send 0x04 and record whether the client asks for a public key.
    kNoPinnedKey,
    // Send 0x04, answer the public-key request with the server key, and
    // decrypt the ciphertext the client sends back.
    kPinnedKey,
    // Send 0x04, answer the public-key request with the attacker key; the
    // client must refuse to encrypt anything.
    kMismatchedPinnedKey,
};

// A fake MySQL server that stops after the full-auth exchange: it lets
// the client authenticate, answers its first query with an OK packet,
// and records what the client sent during the exchange.
class FakeMysqlServer {
public:
    explicit FakeMysqlServer(FullAuthMode mode) : _mode(mode) {}

    ~FakeMysqlServer() {
        Wait();
        if (_listen_fd >= 0) {
            close(_listen_fd);
        }
    }

    // Waits for the server thread to finish so that the recorded results
    // can be read without racing with it.
    void Wait() {
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    bool Start() {
        _listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (_listen_fd < 0) {
            return false;
        }
        int on = 1;
        setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        SetSocketTimeout(_listen_fd);
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            listen(_listen_fd, 1) != 0) {
            return false;
        }
        socklen_t addr_len = sizeof(addr);
        if (getsockname(_listen_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
            return false;
        }
        _port = ntohs(addr.sin_port);
        _thread = std::thread([this] { Serve(); });
        return true;
    }

    int port() const { return _port; }

    // Whether the client asked for the server's public key.
    bool pubkey_requested() const { return _pubkey_requested; }
    // Whether the client sent the RSA-encrypted password.
    bool password_sent() const { return _password_sent; }
    // The password recovered from the ciphertext, empty when none arrived.
    const std::string& recovered_password() const { return _recovered_password; }
    // Whether the whole exchange (including the query round trip) succeeded.
    bool completed() const { return _completed; }

private:
    void Serve() {
        const int fd = accept(_listen_fd, nullptr, nullptr);
        if (fd < 0) {
            return;
        }
        SetSocketTimeout(fd);
        std::string payload;
        uint8_t seq = 0;
        do {
            if (!WritePacket(fd, MakeGreeting(kSalt), 0)) break;
            // HandshakeResponse41 carrying the fast scramble.
            if (!ReadPacket(fd, &payload, &seq)) break;
            // perform_full_authentication.
            if (!WritePacket(fd, std::string("\x01\x04", 2), seq + 1)) break;
            if (_mode == FullAuthMode::kNoPinnedKey) {
                // The client must fail instead of asking for the key.
                _pubkey_requested = ReadPacket(fd, &payload, &seq);
                break;
            }
            if (!ReadPacket(fd, &payload, &seq) || payload != std::string("\x02", 1)) {
                break;
            }
            _pubkey_requested = true;
            RsaKey* key = (_mode == FullAuthMode::kMismatchedPinnedKey)
                              ? GetAttackerKey() : GetServerKey();
            if (key == nullptr ||
                !WritePacket(fd, std::string(1, '\x01') + key->public_pem, seq + 1)) {
                break;
            }
            if (_mode == FullAuthMode::kMismatchedPinnedKey) {
                // The client must not send the password under the wrong key.
                _password_sent = ReadPacket(fd, &payload, &seq);
                break;
            }
            if (!ReadPacket(fd, &payload, &seq)) {
                break;
            }
            _password_sent = true;
            if (!RecoverPassword(key->pkey, payload, kSalt, &_recovered_password)) {
                break;
            }
            if (!WritePacket(fd, MakeOkPayload(), seq + 1)) break;
            // The client now sends its first query; answer it so the RPC
            // completes and the connection phase is proven to be over.
            if (!ReadPacket(fd, &payload, &seq)) break;
            if (!WritePacket(fd, MakeOkPayload(), seq + 1)) break;
            _completed = true;
        } while (false);
        close(fd);
    }

    const FullAuthMode _mode;
    int _listen_fd = -1;
    int _port = -1;
    std::thread _thread;
    bool _pubkey_requested = false;
    bool _password_sent = false;
    bool _completed = false;
    std::string _recovered_password;
};

// Runs one RPC over a mysql Channel.  |pinned_pem| empty means no pin.
void RunClient(int port, const std::string& pinned_pem,
               bool* failed, std::string* error_text) {
    brpc::policy::MysqlAuthenticator auth("root", kPassword, "");
    if (!pinned_pem.empty()) {
        auth.set_server_public_key(pinned_pem);
    }
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MYSQL;
    options.timeout_ms = 5000;
    options.max_retry = 0;
    options.auth = &auth;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init("127.0.0.1", port, &options));
    brpc::MysqlRequest request;
    ASSERT_TRUE(request.Query("select 1"));
    brpc::MysqlResponse response;
    brpc::Controller cntl;
    channel.CallMethod(nullptr, &cntl, &request, &response, nullptr);
    *failed = cntl.Failed();
    *error_text = cntl.ErrorText();
}

TEST(MysqlFullAuthTest, PlainTcpWithoutPinnedKeyFails) {
    FakeMysqlServer server(FullAuthMode::kNoPinnedKey);
    ASSERT_TRUE(server.Start());
    bool failed = false;
    std::string error_text;
    RunClient(server.port(), "", &failed, &error_text);
    server.Wait();
    EXPECT_TRUE(failed) << "full auth without a pinned key must be refused";
    // The failure must tell the operator how to proceed.
    EXPECT_NE(std::string::npos, error_text.find("requires a pinned server public key"))
        << error_text;
    EXPECT_FALSE(server.pubkey_requested());
    EXPECT_FALSE(server.password_sent());
}

TEST(MysqlFullAuthTest, PlainTcpWithMismatchedPinnedKeyFails) {
    ASSERT_NE(nullptr, GetServerKey());
    ASSERT_NE(nullptr, GetAttackerKey());
    FakeMysqlServer server(FullAuthMode::kMismatchedPinnedKey);
    ASSERT_TRUE(server.Start());
    bool failed = false;
    std::string error_text;
    RunClient(server.port(), GetServerKey()->public_pem, &failed, &error_text);
    server.Wait();
    EXPECT_TRUE(failed) << "a server key that differs from the pinned key must be refused";
    EXPECT_NE(std::string::npos, error_text.find("does not match the pinned key")) << error_text;
    EXPECT_TRUE(server.pubkey_requested());
    EXPECT_FALSE(server.password_sent());
}

TEST(MysqlFullAuthTest, PlainTcpWithPinnedKeySucceeds) {
    ASSERT_NE(nullptr, GetServerKey());
    FakeMysqlServer server(FullAuthMode::kPinnedKey);
    ASSERT_TRUE(server.Start());
    bool failed = true;
    std::string error_text;
    RunClient(server.port(), GetServerKey()->public_pem, &failed, &error_text);
    server.Wait();
    EXPECT_FALSE(failed) << error_text;
    EXPECT_TRUE(server.pubkey_requested());
    ASSERT_TRUE(server.password_sent());
    EXPECT_EQ(kPassword, server.recovered_password());
    EXPECT_TRUE(server.completed());
}

// The secure-transport branch is untouched: on a TLS-protected connection
// the password travels in the clear (protected by TLS), whatever pinned key
// is configured, so full authentication there does not depend on a pin.
TEST(MysqlFullAuthTest, SecureTransportSendsCleartextRegardlessOfPin) {
    ASSERT_NE(nullptr, GetServerKey());
    const std::string pinned = GetServerKey()->public_pem;
    EXPECT_EQ(std::string(kPassword) + '\0',
              brpc::policy::mysql::CachingSha2PasswordSlowPath(
                  kPassword, kSalt, pinned, /*is_ssl=*/true));
    EXPECT_EQ(std::string(kPassword) + '\0',
              brpc::policy::mysql::CachingSha2PasswordSlowPath(
                  kPassword, kSalt, "" /* no pinned key */, /*is_ssl=*/true));
}

}  // namespace
