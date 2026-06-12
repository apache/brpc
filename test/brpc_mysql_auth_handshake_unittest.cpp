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

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "brpc/policy/mysql/mysql_auth_handshake.h"
#include "brpc/policy/mysql/mysql_auth_packet.h"
#include "brpc/policy/mysql/mysql_auth_scramble.h"
#include "butil/logging.h"
#include "butil/strings/string_piece.h"

// When true, the server-integration tests connect to an already-running
// MySQL server (on -mysql_host:-mysql_port, as -mysql_user/-mysql_password)
// that the test neither starts nor stops.  When false (the default), the
// fixture spawns and tears down its own throwaway server, exactly like
// test/brpc_redis_unittest.cpp.
DEFINE_bool(mysql_use_running_server, false,
            "Use an already-running MySQL server instead of spawning a "
            "throwaway one; the running server is neither started nor "
            "stopped by the test.");
DEFINE_string(mysql_host, "127.0.0.1",
              "Host of the running MySQL server "
              "(only with -mysql_use_running_server).");
DEFINE_int32(mysql_port, 13306,
             "TCP port of the MySQL server (used for both the running "
             "server and the spawned throwaway server).");
DEFINE_string(mysql_user, "root",
              "User for the authentication tests against a running server.");
DEFINE_string(mysql_password, "",
              "Password for -mysql_user (empty for the spawned server).");

namespace {

using brpc::policy::mysql::AuthMoreData;
using brpc::policy::mysql::AuthSwitchRequest;
using brpc::policy::mysql::BuildHandshakeResponse41;
using brpc::policy::mysql::DecodePacketHeader;
using brpc::policy::mysql::EncodePacketHeader;
using brpc::policy::mysql::HandshakeResponse41;
using brpc::policy::mysql::HandshakeV10;
using brpc::policy::mysql::PacketHeader;
using brpc::policy::mysql::ParseAuthMoreData;
using brpc::policy::mysql::ParseAuthSwitchRequest;
using brpc::policy::mysql::ParseHandshakeV10;
using brpc::policy::mysql::kAuthMoreDataTag;
using brpc::policy::mysql::kAuthSwitchRequestTag;
using brpc::policy::mysql::kErrPacketTag;
using brpc::policy::mysql::kHandshakeV10Tag;
using brpc::policy::mysql::kOkPacketTag;
using brpc::policy::mysql::kPacketHeaderLen;
using brpc::policy::mysql::kSaltLen;
using brpc::policy::mysql::CLIENT_CONNECT_WITH_DB;
using brpc::policy::mysql::CLIENT_PLUGIN_AUTH;
using brpc::policy::mysql::CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA;
using brpc::policy::mysql::CLIENT_PROTOCOL_41;
using brpc::policy::mysql::CLIENT_SECURE_CONNECTION;
using brpc::policy::mysql::NativePasswordScramble;
using brpc::policy::mysql::CachingSha2PasswordScramble;
using brpc::policy::mysql::CachingSha2PasswordRsaEncrypt;
using brpc::policy::mysql::CachingSha2PasswordCleartext;
using brpc::policy::mysql::CachingSha2PasswordSlowPath;
using brpc::policy::mysql::kNativePasswordResponseLen;
using brpc::policy::mysql::kCachingSha2PasswordResponseLen;

// Constructs a synthetic HandshakeV10 packet payload matching the wire
// format described at:
// https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase_packets_protocol_handshake_v10.html
std::string MakeHandshakeV10Payload(
        const std::string& server_version,
        uint32_t connection_id,
        const std::string& salt,
        uint32_t capability_flags,
        uint8_t character_set,
        uint16_t status_flags,
        const std::string& auth_plugin_name) {
    std::string out;
    out.push_back(static_cast<char>(kHandshakeV10Tag));
    out.append(server_version);
    out.push_back('\0');
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((connection_id >> (8 * i)) & 0xff));
    }
    // Salt part 1 (first 8 bytes).
    out.append(salt.data(), 8);
    // Filler.
    out.push_back('\0');
    // Capability flags low 16 bits.
    out.push_back(static_cast<char>(capability_flags & 0xff));
    out.push_back(static_cast<char>((capability_flags >> 8) & 0xff));
    // Character set.
    out.push_back(static_cast<char>(character_set));
    // Status flags.
    out.push_back(static_cast<char>(status_flags & 0xff));
    out.push_back(static_cast<char>((status_flags >> 8) & 0xff));
    // Capability flags high 16 bits.
    out.push_back(static_cast<char>((capability_flags >> 16) & 0xff));
    out.push_back(static_cast<char>((capability_flags >> 24) & 0xff));
    // Length of auth-plugin-data: 21 (8 + 12 + 1 NUL filler) when
    // CLIENT_PLUGIN_AUTH set, 0 otherwise.
    const uint8_t apd_total = (capability_flags & CLIENT_PLUGIN_AUTH) ? 21 : 0;
    out.push_back(static_cast<char>(apd_total));
    // 10 reserved zeros.
    out.append(10, '\0');
    if (capability_flags & CLIENT_SECURE_CONNECTION) {
        // Salt part 2: 12 bytes plus 1 NUL filler.
        out.append(salt.data() + 8, salt.size() - 8);
        out.push_back('\0');
    }
    if (capability_flags & CLIENT_PLUGIN_AUTH) {
        out.append(auth_plugin_name);
        out.push_back('\0');
    }
    return out;
}

// ----------------------------------------------------------------------
// HandshakeV10 parser
// ----------------------------------------------------------------------

TEST(HandshakeV10Test, HappyPath_Mysql8Style) {
    std::string salt;
    for (int i = 1; i <= 20; ++i) salt.push_back(static_cast<char>(i));
    const uint32_t caps =
        CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION | CLIENT_PLUGIN_AUTH;

    const std::string payload = MakeHandshakeV10Payload(
        "8.0.32", 42, salt, caps,
        /*character_set=*/0xff, /*status_flags=*/0x0002,
        "mysql_native_password");

    HandshakeV10 hs;
    ASSERT_TRUE(ParseHandshakeV10(payload, &hs));
    EXPECT_EQ(hs.protocol_version, kHandshakeV10Tag);
    EXPECT_EQ(hs.server_version, "8.0.32");
    EXPECT_EQ(hs.connection_id, 42u);
    EXPECT_EQ(hs.auth_plugin_data, salt);
    EXPECT_EQ(hs.auth_plugin_data.size(), kSaltLen);
    EXPECT_TRUE(hs.capability_flags & CLIENT_PLUGIN_AUTH);
    EXPECT_TRUE(hs.capability_flags & CLIENT_SECURE_CONNECTION);
    EXPECT_EQ(hs.character_set, 0xff);
    EXPECT_EQ(hs.status_flags, 0x0002);
    EXPECT_EQ(hs.auth_plugin_name, "mysql_native_password");
}

TEST(HandshakeV10Test, HappyPath_CachingSha2Server) {
    std::string salt;
    for (int i = 0; i < 20; ++i) salt.push_back(static_cast<char>('A' + i));
    const uint32_t caps =
        CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION | CLIENT_PLUGIN_AUTH;

    const std::string payload = MakeHandshakeV10Payload(
        "8.0.32", 7, salt, caps, 0xff, 0x0002, "caching_sha2_password");

    HandshakeV10 hs;
    ASSERT_TRUE(ParseHandshakeV10(payload, &hs));
    EXPECT_EQ(hs.auth_plugin_name, "caching_sha2_password");
    EXPECT_EQ(hs.auth_plugin_data, salt);
}

TEST(HandshakeV10Test, RejectsBadProtocolVersion) {
    std::string payload(1, static_cast<char>(0x09));  // not 10
    payload.append("ignored");
    HandshakeV10 hs;
    EXPECT_FALSE(ParseHandshakeV10(payload, &hs));
}

TEST(HandshakeV10Test, RejectsTruncatedAtServerVersion) {
    // Tag, but no NUL anywhere -> server_version unterminated.
    std::string payload(1, static_cast<char>(kHandshakeV10Tag));
    payload.append(20, 'x');  // no NUL
    HandshakeV10 hs;
    EXPECT_FALSE(ParseHandshakeV10(payload, &hs));
}

TEST(HandshakeV10Test, RejectsEmptyPayload) {
    HandshakeV10 hs;
    EXPECT_FALSE(ParseHandshakeV10(butil::StringPiece(""), &hs));
}

TEST(HandshakeV10Test, RejectsTruncatedBeforeSalt) {
    // Build a payload then chop after capability_flags_lo.
    std::string salt(20, '\x01');
    const std::string full = MakeHandshakeV10Payload(
        "8.0.32", 1, salt, CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION,
        0xff, 0, "");
    // Chop early — keep only protocol+server_version+conn_id+part1+filler+caps_lo.
    const std::string truncated(full.data(), 6 + 1 + 4 + 8 + 1 + 2);
    HandshakeV10 hs;
    EXPECT_FALSE(ParseHandshakeV10(truncated, &hs));
}

TEST(HandshakeV10Test, ExtractsFull20ByteSalt) {
    std::string salt(20, 0);
    for (int i = 0; i < 20; ++i) salt[i] = static_cast<char>(0xA0 + i);
    const std::string payload = MakeHandshakeV10Payload(
        "8.0.32", 1, salt,
        CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION | CLIENT_PLUGIN_AUTH,
        0xff, 0, "mysql_native_password");
    HandshakeV10 hs;
    ASSERT_TRUE(ParseHandshakeV10(payload, &hs));
    EXPECT_EQ(hs.auth_plugin_data.size(), kSaltLen);
    EXPECT_EQ(hs.auth_plugin_data, salt);
}

// ----------------------------------------------------------------------
// HandshakeResponse41 builder
// ----------------------------------------------------------------------

TEST(HandshakeResponse41Test, BuildsExpectedLayout) {
    HandshakeResponse41 req;
    req.capability_flags = CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION
                         | CLIENT_PLUGIN_AUTH
                         | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA;
    req.max_packet_size = 1u << 24;
    req.character_set = 0x21;
    req.username = "root";
    req.auth_response = std::string(20, '\x42');  // canned scramble
    req.auth_plugin_name = "mysql_native_password";

    std::string payload;
    ASSERT_TRUE(BuildHandshakeResponse41(req, &payload));

    // 4 caps + 4 max_pkt + 1 charset + 23 reserved = 32 bytes fixed prefix
    ASSERT_GE(payload.size(), 32u);
    // Caps roundtrip
    uint32_t caps = static_cast<unsigned char>(payload[0])
        | (static_cast<uint32_t>(static_cast<unsigned char>(payload[1])) << 8)
        | (static_cast<uint32_t>(static_cast<unsigned char>(payload[2])) << 16)
        | (static_cast<uint32_t>(static_cast<unsigned char>(payload[3])) << 24);
    EXPECT_EQ(caps, req.capability_flags);
    // Username + NUL + lenenc(20) + 20 bytes + plugin + NUL
    const char* p = payload.data() + 32;
    EXPECT_EQ(std::string(p, 5), std::string("root\0", 5));
    p += 5;
    EXPECT_EQ(static_cast<unsigned char>(*p), 20u);  // lenenc(20) = 0x14
    ++p;
    EXPECT_EQ(std::string(p, 20), std::string(20, '\x42'));
    p += 20;
    const std::string plugin_nul("mysql_native_password\0", 22);
    EXPECT_EQ(std::string(p, plugin_nul.size()), plugin_nul);
}

TEST(HandshakeResponse41Test, OmitsDatabaseWhenFlagAbsent) {
    HandshakeResponse41 req;
    req.capability_flags = CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION
                         | CLIENT_PLUGIN_AUTH
                         | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA;
    req.max_packet_size = 1u << 24;
    req.character_set = 0x21;
    req.username = "u";
    req.auth_response = std::string(20, '\x01');
    req.database = "mydb";  // should be ignored
    req.auth_plugin_name = "mysql_native_password";

    std::string payload;
    ASSERT_TRUE(BuildHandshakeResponse41(req, &payload));
    EXPECT_EQ(payload.find("mydb"), std::string::npos);
}

TEST(HandshakeResponse41Test, IncludesDatabaseWhenFlagSet) {
    HandshakeResponse41 req;
    req.capability_flags = CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION
                         | CLIENT_PLUGIN_AUTH | CLIENT_CONNECT_WITH_DB
                         | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA;
    req.max_packet_size = 1u << 24;
    req.character_set = 0x21;
    req.username = "u";
    req.auth_response = std::string(20, '\x01');
    req.database = "mydb";
    req.auth_plugin_name = "mysql_native_password";

    std::string payload;
    ASSERT_TRUE(BuildHandshakeResponse41(req, &payload));
    EXPECT_NE(payload.find("mydb"), std::string::npos);
}

TEST(HandshakeResponse41Test, HandlesLargeAuthResponseViaLenEncoding) {
    // 256-byte RSA ciphertext — exercises lenenc 0xfc 2-byte branch.
    HandshakeResponse41 req;
    req.capability_flags = CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION
                         | CLIENT_PLUGIN_AUTH
                         | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA;
    req.max_packet_size = 1u << 24;
    req.character_set = 0x21;
    req.username = "u";
    req.auth_response = std::string(256, '\xAA');
    req.auth_plugin_name = "caching_sha2_password";

    std::string payload;
    ASSERT_TRUE(BuildHandshakeResponse41(req, &payload));
    // lenenc 256 -> 0xfc 0x00 0x01
    const std::string lenenc("\xfc\x00\x01", 3);
    EXPECT_NE(payload.find(lenenc), std::string::npos);
}

TEST(HandshakeResponse41Test, RejectsOversizeAuthResponseWithoutLenEnc) {
    // CLIENT_SECURE_CONNECTION without the lenenc flag uses a 1-byte length
    // prefix, so a >255-byte auth_response cannot be represented.  The builder
    // must hard-fail (return false) and write nothing, rather than silently
    // truncating to 255 bytes.
    HandshakeResponse41 req;
    req.capability_flags = CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION
                         | CLIENT_PLUGIN_AUTH;  // deliberately no LENENC flag
    req.max_packet_size = 1u << 24;
    req.character_set = 0x21;
    req.username = "u";
    req.auth_response = std::string(256, '\xAA');  // 256 > 255
    req.auth_plugin_name = "caching_sha2_password";

    std::string payload;
    EXPECT_FALSE(BuildHandshakeResponse41(req, &payload));
    EXPECT_TRUE(payload.empty())
        << "no bytes must be written to out on failure";
}

// Exactly 255 bytes is the boundary that still fits the 1-byte length prefix.
TEST(HandshakeResponse41Test, AcceptsMaxSizeAuthResponseWithoutLenEnc) {
    HandshakeResponse41 req;
    req.capability_flags = CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION
                         | CLIENT_PLUGIN_AUTH;
    req.max_packet_size = 1u << 24;
    req.character_set = 0x21;
    req.username = "u";
    req.auth_response = std::string(255, '\xAA');  // fits in one byte
    req.auth_plugin_name = "caching_sha2_password";

    std::string payload;
    ASSERT_TRUE(BuildHandshakeResponse41(req, &payload));
    // After "u\0" we expect length byte 0xFF (255) then 255 payload bytes.
    const size_t u_end = payload.find('u') + 2;
    EXPECT_EQ(static_cast<unsigned char>(payload[u_end]), 255u);
}

TEST(HandshakeResponse41Test, UsesSingleByteLengthWithoutLenEncFlag) {
    HandshakeResponse41 req;
    req.capability_flags = CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION
                         | CLIENT_PLUGIN_AUTH;
    req.max_packet_size = 1u << 24;
    req.character_set = 0x21;
    req.username = "u";
    req.auth_response = std::string(20, '\x77');
    req.auth_plugin_name = "mysql_native_password";

    std::string payload;
    ASSERT_TRUE(BuildHandshakeResponse41(req, &payload));
    // After username "u\0", we expect 1-byte length 0x14 (20).
    const size_t u_end = payload.find('u') + 2;  // skip 'u' + NUL
    EXPECT_EQ(static_cast<unsigned char>(payload[u_end]), 20u);
}

// ----------------------------------------------------------------------
// AuthSwitchRequest parser
// ----------------------------------------------------------------------

TEST(AuthSwitchRequestTest, HappyPath) {
    std::string payload(1, static_cast<char>(kAuthSwitchRequestTag));
    payload.append("caching_sha2_password");
    payload.push_back('\0');
    payload.append(20, '\xAA');
    payload.push_back('\0');  // trailing NUL filler
    AuthSwitchRequest sw;
    ASSERT_TRUE(ParseAuthSwitchRequest(payload, &sw));
    EXPECT_EQ(sw.auth_plugin_name, "caching_sha2_password");
    EXPECT_EQ(sw.auth_plugin_data, std::string(20, '\xAA'));
}

TEST(AuthSwitchRequestTest, RejectsBadTag) {
    std::string payload(1, static_cast<char>(0x00));
    payload.append("x\0", 2);
    AuthSwitchRequest sw;
    EXPECT_FALSE(ParseAuthSwitchRequest(payload, &sw));
}

TEST(AuthSwitchRequestTest, RejectsMissingPluginNameNul) {
    std::string payload(1, static_cast<char>(kAuthSwitchRequestTag));
    payload.append("no_nul_here_at_all");
    AuthSwitchRequest sw;
    EXPECT_FALSE(ParseAuthSwitchRequest(payload, &sw));
}

// ----------------------------------------------------------------------
// AuthMoreData parser
// ----------------------------------------------------------------------

TEST(AuthMoreDataTest, FastAuthOkMarker) {
    const char data[] = {static_cast<char>(kAuthMoreDataTag), '\x03'};
    AuthMoreData mod;
    ASSERT_TRUE(ParseAuthMoreData(butil::StringPiece(data, sizeof(data)), &mod));
    EXPECT_EQ(mod.data, std::string("\x03", 1));
}

TEST(AuthMoreDataTest, RequestPubKeyMarker) {
    const char data[] = {static_cast<char>(kAuthMoreDataTag), '\x04'};
    AuthMoreData mod;
    ASSERT_TRUE(ParseAuthMoreData(butil::StringPiece(data, sizeof(data)), &mod));
    EXPECT_EQ(mod.data, std::string("\x04", 1));
}

TEST(AuthMoreDataTest, PubKeyPayload) {
    std::string payload(1, static_cast<char>(kAuthMoreDataTag));
    const std::string pem = "-----BEGIN PUBLIC KEY-----\nABC\n-----END PUBLIC KEY-----\n";
    payload.append(pem);
    AuthMoreData mod;
    ASSERT_TRUE(ParseAuthMoreData(payload, &mod));
    EXPECT_EQ(mod.data, pem);
}

TEST(AuthMoreDataTest, RejectsBadTag) {
    std::string payload(1, static_cast<char>(0x00));
    payload.append("\x03", 1);
    AuthMoreData mod;
    EXPECT_FALSE(ParseAuthMoreData(payload, &mod));
}

// ----------------------------------------------------------------------
// End-to-end handshake against a real mysqld.
//
// Two modes, selected by the -mysql_use_running_server flag:
//
//   * Self-spawned throwaway server (the DEFAULT, flag false).  The
//     fixture brings up its own mysqld and tears it down on exit,
//     exactly like test/brpc_redis_unittest.cpp; --initialize-insecure
//     leaves root with an empty password, so caching_sha2_password
//     completes via its fast path with no RSA round trip.  Keeps CI
//     self-contained.
//
//   * Already-running server (flag true).  The tests connect to a
//     server you started yourself on -mysql_host:-mysql_port and do
//     NOT start or stop it.  Run that server in a terminal with
//     --log-error-verbosity=3 to watch the handshake; see
//     test/README_mysql_auth.md for the bring-up commands.  With a real
//     -mysql_password, caching_sha2_password takes its RSA full-auth
//     path over plain TCP, exercising CachingSha2PasswordRsaEncrypt
//     against a real server.
//
// MySQL 8.4+/9.x ship without the mysql_native_password server plugin,
// so both modes authenticate with caching_sha2_password.
// ----------------------------------------------------------------------

#define MYSQLD_BIN "mysqld"

static pthread_once_t start_mysqld_once = PTHREAD_ONCE_INIT;
// >0  : we forked a throwaway mysqld with this pid.
// -2  : an already-running server (-mysql_use_running_server) is reachable.
// -1  : no server available; server tests skip.
static pid_t g_mysqld_pid = -1;

// Connection parameters, resolved once in RunMysqlServer().
static std::string g_mysql_host = "127.0.0.1";
static int g_mysql_port = 13306;
static std::string g_mysql_user = "root";
static std::string g_mysql_password;  // empty for the self-spawned server

// A (user, password) pair the auth tests exercise.  An empty password
// takes caching_sha2's fast path; a non-empty password against a cold
// cache takes the RSA full-auth path.  Populated once in
// RunMysqlServer(): the spawned server gets BOTH an empty-password and a
// non-empty-password account so it can exercise both paths; a running
// server contributes the single -mysql_user/-mysql_password credential.
struct AuthCase {
    std::string label;
    std::string user;
    std::string password;
    bool use_ssl;  // drive the login over a SSL connection (set at every init site)
};
static std::vector<AuthCase> g_auth_cases;

// Non-empty-password accounts created on the spawned server.  Two distinct
// accounts so the plaintext (RSA) and SSL (cleartext) full-auth tests each
// hit a COLD caching_sha2 cache deterministically (one login would
// otherwise warm the cache for the other).
static const char* const kSpawnPwUser = "brpc_test";
static const char* const kSpawnSslUser = "brpc_ssl";
static const char* const kSpawnPwPassword = "brpc_test_password";

// True when this process spawned its own throwaway mysqld (vs. a running
// server).  Spawned servers are brand-new, so credentials are cold.
static bool IsSpawnedServer() { return g_mysqld_pid > 0; }

// Returns the first non-empty-password credential matching |use_ssl|, or
// NULL when the active server exposes none (so the caller can skip).
static const AuthCase* FindNonEmptyCase(bool use_ssl) {
    for (size_t i = 0; i < g_auth_cases.size(); ++i) {
        if (!g_auth_cases[i].password.empty() &&
            g_auth_cases[i].use_ssl == use_ssl) {
            return &g_auth_cases[i];
        }
    }
    return NULL;
}

// Absolute path to the throwaway data directory.  mysqld resolves a
// relative --datadir against its basedir (not the current working
// directory), so the path handed to mysqld must be absolute.
static std::string TestDataDir() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return std::string("/tmp/mysql_data_for_test");
    }
    return std::string(cwd) + "/mysql_data_for_test";
}

static void RemoveMysqlServer() {
    if (g_mysqld_pid > 0) {
        puts("[Stopping mysqld]");
        char cmd[1280];
        snprintf(cmd, sizeof(cmd), "kill %d", g_mysqld_pid);
        CHECK(0 == system(cmd));
        // Wait for mysqld to flush and exit before removing its datadir.
        usleep(500000);
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", TestDataDir().c_str());
        CHECK(0 == system(cmd));
    }
}

// Opens a TCP connection to g_mysql_host:g_mysql_port.  Returns the fd
// on success or -1 on failure (without logging, so callers can poll).
static int ConnectTestMysql() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(g_mysql_port));
    addr.sin_addr.s_addr = inet_addr(g_mysql_host.c_str());
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void RunMysqlServer() {
    // Mode 1 (flag true): connect to a server the caller started; do not
    // start or stop it.
    if (FLAGS_mysql_use_running_server) {
        g_mysql_host = FLAGS_mysql_host;
        g_mysql_port = FLAGS_mysql_port;
        g_mysql_user = FLAGS_mysql_user;
        g_mysql_password = FLAGS_mysql_password;
        printf("[Using running mysqld at %s:%d as user '%s']\n",
               g_mysql_host.c_str(), g_mysql_port, g_mysql_user.c_str());
        int fd = ConnectTestMysql();
        if (fd >= 0) {
            close(fd);
            g_mysqld_pid = -2;  // running server reachable
            g_auth_cases.push_back(
                {"flag-credential", g_mysql_user, g_mysql_password, false});
            g_auth_cases.push_back(
                {"flag-credential-ssl", g_mysql_user, g_mysql_password, true});
        } else {
            printf("Cannot reach running mysqld at %s:%d, "
                   "following tests will be skipped\n",
                   g_mysql_host.c_str(), g_mysql_port);
        }
        return;
    }

    // Mode 2 (default): spawn a throwaway server with an empty-password
    // root and tear it down on exit (the redis-unittest pattern).
    if (system("which " MYSQLD_BIN) != 0) {
        puts("Fail to find " MYSQLD_BIN ", following tests will be skipped");
        return;
    }
    g_mysql_host = "127.0.0.1";
    g_mysql_port = FLAGS_mysql_port;
    g_mysql_user = "root";
    g_mysql_password.clear();
    const std::string datadir = TestDataDir();
    char cmd[2048];
    // Start from a clean, empty data directory every run; mysqld
    // --initialize-insecure requires the directory to exist and be empty.
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'",
             datadir.c_str(), datadir.c_str());
    if (system(cmd) != 0) {
        puts("Fail to create datadir, following tests will be skipped");
        return;
    }
    // Initialize root with an empty password.  mysqld auto-detects its
    // basedir from the binary location, so no --basedir is needed.
    snprintf(cmd, sizeof(cmd),
             MYSQLD_BIN " --initialize-insecure --datadir='%s'"
             " --log-error='%s/init.err'",
             datadir.c_str(), datadir.c_str());
    if (system(cmd) != 0) {
        puts("Fail to initialize mysqld datadir, following tests will be skipped");
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", datadir.c_str());
        CHECK(0 == system(cmd));
        return;
    }
    atexit(RemoveMysqlServer);

    g_mysqld_pid = fork();
    if (g_mysqld_pid < 0) {
        puts("Fail to fork");
        exit(1);
    } else if (g_mysqld_pid == 0) {
        puts("[Starting mysqld]");
        char port_arg[32];
        snprintf(port_arg, sizeof(port_arg), "--port=%d", FLAGS_mysql_port);
        const std::string datadir_arg = "--datadir=" + datadir;
        const std::string socket_arg = "--socket=" + datadir + "/mysqld.sock";
        const std::string pidfile_arg = "--pid-file=" + datadir + "/mysqld.pid";
        const std::string logerr_arg = "--log-error=" + datadir + "/mysqld.err";
        char* const argv[] = {
            (char*)MYSQLD_BIN,
            (char*)datadir_arg.c_str(),
            (char*)port_arg,
            (char*)socket_arg.c_str(),
            (char*)pidfile_arg.c_str(),
            (char*)logerr_arg.c_str(),
            (char*)"--mysqlx=OFF",
            (char*)"--bind-address=127.0.0.1",
            NULL };
        if (execvp(MYSQLD_BIN, argv) < 0) {
            puts("Fail to run " MYSQLD_BIN);
            exit(1);
        }
    }
    // Poll until mysqld accepts TCP connections (it has to recover its
    // freshly created tablespace first), giving up after ~30s.
    for (int i = 0; i < 300; ++i) {
        int fd = ConnectTestMysql();
        if (fd >= 0) {
            close(fd);
            // The spawned server always tests the empty-password root.
            g_auth_cases.push_back(
                {"empty-password", "root", std::string(), false});
            // Additionally create two non-empty-password accounts (over the
            // unix socket, where root has an empty password): one for the
            // plaintext/RSA full-auth path and one for the SSL/cleartext
            // full-auth path, each cold so both are deterministic.
            // Best-effort: if the mysql client is missing both are skipped.
            char create[2048];
            snprintf(create, sizeof(create),
                     "mysql --socket='%s/mysqld.sock' -u root -e \""
                     "CREATE USER IF NOT EXISTS '%s'@'%%' IDENTIFIED WITH "
                     "caching_sha2_password BY '%s'; "
                     "GRANT ALL PRIVILEGES ON *.* TO '%s'@'%%'; "
                     "CREATE USER IF NOT EXISTS '%s'@'%%' IDENTIFIED WITH "
                     "caching_sha2_password BY '%s'; "
                     "GRANT ALL PRIVILEGES ON *.* TO '%s'@'%%';\" 2>/dev/null",
                     datadir.c_str(), kSpawnPwUser, kSpawnPwPassword,
                     kSpawnPwUser, kSpawnSslUser, kSpawnPwPassword,
                     kSpawnSslUser);
            if (system(create) == 0) {
                g_auth_cases.push_back(
                    {"nonempty-password", kSpawnPwUser, kSpawnPwPassword,
                     false});
                g_auth_cases.push_back(
                    {"nonempty-password-ssl", kSpawnSslUser, kSpawnPwPassword,
                     true});
            } else {
                puts("mysql client unavailable; spawned server will test "
                     "only the empty-password path");
            }
            return;
        }
        usleep(100000);
    }
    puts("mysqld did not become ready, following tests will be skipped");
    g_mysqld_pid = -1;
}

// Reads exactly |n| bytes into |buf|.  When |ssl| is non-null the bytes
// come from the SSL session; otherwise from the raw fd.  Returns true on
// success.
static bool ReadFull(int fd, char* buf, size_t n, SSL* ssl = NULL) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = ssl ? SSL_read(ssl, buf + off, static_cast<int>(n - off))
                        : read(fd, buf + off, n - off);
        if (r > 0) {
            off += static_cast<size_t>(r);
        } else if (!ssl && r < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

// Writes all of |data| (over SSL when |ssl| is non-null).  Returns true
// on success.
static bool WriteFull(int fd, const std::string& data, SSL* ssl = NULL) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t w = ssl ? SSL_write(ssl, data.data() + off,
                                    static_cast<int>(data.size() - off))
                        : write(fd, data.data() + off, data.size() - off);
        if (w > 0) {
            off += static_cast<size_t>(w);
        } else if (!ssl && w < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

// Reads one MySQL packet (4-byte header + payload).  On success stores
// the payload in *payload, the sequence id in *seq, and returns true.
static bool ReadPacket(int fd, std::string* payload, uint8_t* seq,
                       SSL* ssl = NULL) {
    char hdr[kPacketHeaderLen];
    if (!ReadFull(fd, hdr, sizeof(hdr), ssl)) {
        return false;
    }
    PacketHeader header;
    if (!DecodePacketHeader(butil::StringPiece(hdr, sizeof(hdr)), &header)) {
        return false;
    }
    *seq = header.seq;
    payload->resize(header.payload_len);
    if (header.payload_len > 0 &&
        !ReadFull(fd, &(*payload)[0], header.payload_len, ssl)) {
        return false;
    }
    return true;
}

// Frames |payload| with a packet header carrying |seq| and writes it.
static bool WritePacket(int fd, const std::string& payload, uint8_t seq,
                        SSL* ssl = NULL) {
    std::string out;
    PacketHeader header;
    header.payload_len = static_cast<uint32_t>(payload.size());
    header.seq = seq;
    EncodePacketHeader(header, &out);
    out.append(payload);
    return WriteFull(fd, out, ssl);
}

// CLIENT_SSL capability flag (0x00000800) -- not part of the codec's
// CapabilityFlag enum; defined here for the test's SSL upgrade.
static const uint32_t kClientSSL = 0x00000800;

// Sends the MySQL SSLRequest packet (the 32-byte HandshakeResponse41
// fixed prefix with CLIENT_SSL set, no username) at sequence |seq|, then
// performs a SSL client handshake on |fd|.  Returns the SSL* on success
// (caller owns it) or NULL on failure.
static SSL* UpgradeToSSL(int fd, uint32_t capability_flags, uint8_t seq) {
    // SSLRequest payload: 4B caps + 4B max_packet_size + 1B charset + 23B
    // reserved = 32 bytes, with CLIENT_SSL set.
    const uint32_t caps = capability_flags | kClientSSL;
    std::string payload;
    for (int i = 0; i < 4; ++i)
        payload.push_back(static_cast<char>((caps >> (8 * i)) & 0xff));
    const uint32_t max_packet = 1u << 24;
    for (int i = 0; i < 4; ++i)
        payload.push_back(static_cast<char>((max_packet >> (8 * i)) & 0xff));
    payload.push_back(static_cast<char>(0x21));  // charset utf8_general_ci
    payload.append(23, '\0');
    if (!WritePacket(fd, payload, seq)) {
        return NULL;
    }
    // One client SSL_CTX for the whole process; certificate not verified
    // (mysqld's auto-generated cert is self-signed).
    static SSL_CTX* ctx = NULL;
    if (ctx == NULL) {
        ctx = SSL_CTX_new(TLS_client_method());
        if (ctx == NULL) {
            return NULL;
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }
    SSL* ssl = SSL_new(ctx);
    if (ssl == NULL) {
        return NULL;
    }
    SSL_set_fd(ssl, fd);
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

// Outcome of an SHA2-password client handshake, recording which
// authentication path the server drove so tests can assert on it.
struct LoginTrace {
    bool ok = false;            // server answered with an OK packet
    bool full_auth = false;     // server sent AuthMoreData 0x04
                                // (perform_full_authentication)
    bool fast_auth = false;     // server sent AuthMoreData 0x03
                                // (fast_auth_success; credential was cached)
    bool auth_switched = false; // server sent an AuthSwitchRequest
    bool used_ssl = false;      // handshake ran over a SSL connection
    bool used_cleartext = false;// full-auth sent the cleartext password
                                // (the is_ssl secure-transport branch)
    std::string switched_plugin;// plugin the server switched us to
    std::string err;            // human-readable reason when !ok

    // Convenience: which authentication path this login took.
    const char* path() const {
        if (full_auth) {
            return used_cleartext ? "full-authentication (cleartext over SSL)"
                                  : "full-authentication (RSA)";
        }
        if (fast_auth) return "cached fast-authentication";
        return "direct OK (empty password / immediate)";
    }
};

// Performs a complete SHA2-password client handshake against an
// already-greeted connection.  Drives every branch the codec implements:
//
//   1. initial scramble in HandshakeResponse41, using |initial_plugin| if
//      given (e.g. "mysql_native_password" to provoke an auth switch),
//      otherwise the plugin the server advertised in its greeting;
//   2. AuthSwitchRequest (server asks for a different plugin / new salt) ->
//      LoginTrace::auth_switched is set;
//   3. AuthMoreData fast-auth-success (0x03) -> cached path -> wait for OK;
//   4. AuthMoreData full-auth-required (0x04) -> full-auth path: request the
//      RSA public key (send 0x02), receive the PEM, send the RSA-OAEP
//      ciphertext.
//
// When |use_ssl| is true the client upgrades the connection to SSL
// (MySQL SSLRequest + SSL_connect) before sending HandshakeResponse41,
// and on full authentication routes through CachingSha2PasswordSlowPath
// with is_ssl=true -- i.e. the password is sent in the clear, protected
// by SSL, with no RSA exchange.  When false, full auth takes the RSA
// public-key path (CachingSha2PasswordSlowPath with is_ssl=false).
//
// The returned LoginTrace records success, which path the server took,
// whether SSL was used, and (verified by inspecting the slow-path output)
// whether the cleartext or RSA branch was taken.
static LoginTrace PerformSha2Login(int fd, const std::string& user,
                                   const std::string& password, bool use_ssl,
                                   const std::string& initial_plugin =
                                       std::string()) {
    LoginTrace t;
    SSL* ssl = NULL;
    std::string payload;
    uint8_t seq = 0;
    if (!ReadPacket(fd, &payload, &seq)) {  // greeting is always plaintext
        t.err = "failed to read greeting";
        goto done;
    }
    {
        HandshakeV10 hs;
        if (!ParseHandshakeV10(payload, &hs)) {
            t.err = "failed to parse greeting";
            goto done;
        }
        // The nonce used for both the fast scramble and the RSA-path XOR.
        std::string salt = hs.auth_plugin_data;
        // Initial client plugin: a caller-forced one (to provoke an auth
        // switch) if given, else the plugin the server advertised.
        std::string plugin =
            !initial_plugin.empty()
                ? initial_plugin
                : (hs.auth_plugin_name.empty() ? "caching_sha2_password"
                                               : hs.auth_plugin_name);

        HandshakeResponse41 resp;
        resp.capability_flags = CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION
                              | CLIENT_PLUGIN_AUTH
                              | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA;
        resp.max_packet_size = 1u << 24;
        resp.character_set = 0x21;  // utf8_general_ci

        // Greeting is seq 0; the client's next packet is seq 1.  A SSL
        // upgrade inserts the SSLRequest at seq 1, pushing the
        // HandshakeResponse41 to seq 2.
        uint8_t next_seq = static_cast<uint8_t>(seq + 1);
        if (use_ssl) {
            ssl = UpgradeToSSL(fd, resp.capability_flags, next_seq);
            if (ssl == NULL) {
                t.err = "SSL upgrade (SSLRequest + SSL_connect) failed";
                goto done;
            }
            t.used_ssl = true;
            resp.capability_flags |= kClientSSL;
            next_seq = static_cast<uint8_t>(next_seq + 1);
        }
        resp.username = user;
        resp.auth_plugin_name = plugin;
        if (plugin == "caching_sha2_password") {
            resp.auth_response = CachingSha2PasswordScramble(salt, password);
        } else {
            resp.auth_response = NativePasswordScramble(salt, password);
        }

        std::string resp_payload;
        if (!BuildHandshakeResponse41(resp, &resp_payload)) {
            t.err = "failed to build HandshakeResponse41";
            goto done;
        }
        if (!WritePacket(fd, resp_payload, next_seq, ssl)) {
            t.err = "failed to write HandshakeResponse41";
            goto done;
        }

        // Continuation loop: follow the server through any auth-switch /
        // more-data exchange to the terminal OK or ERR packet.
        for (int guard = 0; guard < 8; ++guard) {
            std::string pkt;
            uint8_t pkt_seq = 0;
            if (!ReadPacket(fd, &pkt, &pkt_seq, ssl)) {
                t.err = "failed to read server reply";
                goto done;
            }
            if (pkt.empty()) {
                t.err = "empty server reply";
                goto done;
            }
            const uint8_t tag = static_cast<uint8_t>(pkt[0]);
            if (tag == kOkPacketTag) {
                t.ok = true;
                goto done;
            }
            if (tag == kErrPacketTag) {
                t.err = "ERR packet: " + (pkt.size() > 9 ? pkt.substr(9)
                                          : std::string("(no message)"));
                goto done;
            }
            if (tag == kAuthSwitchRequestTag) {
                t.auth_switched = true;
                AuthSwitchRequest sw;
                if (!ParseAuthSwitchRequest(pkt, &sw)) {
                    t.err = "failed to parse AuthSwitchRequest";
                    goto done;
                }
                plugin = sw.auth_plugin_name;
                salt = sw.auth_plugin_data;
                t.switched_plugin = sw.auth_plugin_name;
                std::string scramble =
                    (plugin == "caching_sha2_password")
                        ? CachingSha2PasswordScramble(salt, password)
                        : NativePasswordScramble(salt, password);
                if (!WritePacket(fd, scramble,
                                 static_cast<uint8_t>(pkt_seq + 1), ssl)) {
                    t.err = "failed to write auth-switch response";
                    goto done;
                }
                continue;
            }
            if (tag == kAuthMoreDataTag) {
                AuthMoreData mod;
                if (!ParseAuthMoreData(pkt, &mod) || mod.data.empty()) {
                    t.err = "failed to parse AuthMoreData";
                    goto done;
                }
                const uint8_t marker = static_cast<uint8_t>(mod.data[0]);
                if (marker == 0x03) {
                    t.fast_auth = true;  // cached credential; OK packet follows
                    continue;
                }
                if (marker == 0x04) {
                    t.full_auth = true;  // perform_full_authentication
                    // On a secure channel the slow path ignores the pubkey
                    // and salt and sends the cleartext password, so we don't
                    // even request the RSA key.  On plain TCP we must fetch
                    // the server's RSA public key first.
                    std::string pubkey;
                    uint8_t resp_after = static_cast<uint8_t>(pkt_seq + 1);
                    if (!use_ssl) {
                        if (!WritePacket(fd, std::string("\x02", 1),
                                         static_cast<uint8_t>(pkt_seq + 1),
                                         ssl)) {
                            t.err = "failed to request public key";
                            goto done;
                        }
                        std::string key_pkt;
                        uint8_t key_seq = 0;
                        if (!ReadPacket(fd, &key_pkt, &key_seq, ssl)) {
                            t.err = "failed to read public key";
                            goto done;
                        }
                        AuthMoreData key_mod;
                        if (!ParseAuthMoreData(key_pkt, &key_mod)) {
                            t.err = "failed to parse public-key AuthMoreData";
                            goto done;
                        }
                        pubkey = key_mod.data;
                        resp_after = static_cast<uint8_t>(key_seq + 1);
                    }
                    // Route through the dispatcher so the test exercises the
                    // is_ssl decision end to end.
                    const std::string slow =
                        CachingSha2PasswordSlowPath(password, salt, pubkey,
                                                    use_ssl);
                    if (slow.empty()) {
                        t.err = "slow-path produced empty payload";
                        goto done;
                    }
                    // Verify which branch the dispatcher actually took by
                    // comparing its output to the cleartext form.
                    t.used_cleartext =
                        (slow == CachingSha2PasswordCleartext(password));
                    if (!WritePacket(fd, slow, resp_after, ssl)) {
                        t.err = "failed to write slow-path response";
                        goto done;
                    }
                    continue;
                }
                t.err = "unexpected AuthMoreData marker";
                goto done;
            }
            t.err = "unexpected packet tag";
            goto done;
        }
        t.err = "handshake did not terminate";
        goto done;
    }
done:
    if (ssl != NULL) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    return t;
}

class MysqlHandshakeServerTest : public testing::Test {
protected:
    void SetUp() override {
        pthread_once(&start_mysqld_once, RunMysqlServer);
    }
    // True when no server (spawned or external) is available.
    static bool NoServer() { return g_mysqld_pid == -1; }
};

// Parses the greeting packet that a real mysqld sends on connect.
TEST_F(MysqlHandshakeServerTest, ParsesRealServerGreeting) {
    if (NoServer()) {
        puts("Skipped due to absence of mysqld");
        return;
    }
    int fd = ConnectTestMysql();
    ASSERT_GE(fd, 0);

    std::string payload;
    uint8_t seq = 0xff;
    ASSERT_TRUE(ReadPacket(fd, &payload, &seq));
    EXPECT_EQ(seq, 0u);  // greeting is always sequence 0

    HandshakeV10 hs;
    ASSERT_TRUE(ParseHandshakeV10(payload, &hs));
    EXPECT_EQ(hs.protocol_version, kHandshakeV10Tag);
    EXPECT_FALSE(hs.server_version.empty());
    EXPECT_EQ(hs.auth_plugin_data.size(), kSaltLen);
    EXPECT_TRUE(hs.capability_flags & CLIENT_PROTOCOL_41);
    EXPECT_TRUE(hs.capability_flags & CLIENT_PLUGIN_AUTH);
    EXPECT_FALSE(hs.auth_plugin_name.empty());
    close(fd);
}

// Generates both scrambles (mysql_native_password and
// caching_sha2_password) -- the "intermediate" auth response -- from the
// salt in a real server greeting, parameterized on password length.  An
// empty (zero-length) password must yield an empty wire response for
// both plugins per spec; a non-empty password must yield the fixed-width
// digests (20 bytes for native, 32 for caching_sha2).  Confirms a wire
// salt from a live server is usable as scramble input.
TEST_F(MysqlHandshakeServerTest, GeneratesScramblesFromRealSalt) {
    if (NoServer()) {
        puts("Skipped due to absence of mysqld");
        return;
    }
    int fd = ConnectTestMysql();
    ASSERT_GE(fd, 0);
    std::string payload;
    uint8_t seq = 0;
    ASSERT_TRUE(ReadPacket(fd, &payload, &seq));
    HandshakeV10 hs;
    ASSERT_TRUE(ParseHandshakeV10(payload, &hs));
    close(fd);
    ASSERT_EQ(hs.auth_plugin_data.size(), kSaltLen);

    // Parameterized on password length: zero-length and non-empty.
    const std::string passwords[] = {std::string(),
                                     std::string("some_password")};
    for (const std::string& password : passwords) {
        SCOPED_TRACE(password.empty() ? "zero-length-password"
                                      : "nonzero-length-password");
        const std::string native =
            NativePasswordScramble(hs.auth_plugin_data, password);
        const std::string sha2 =
            CachingSha2PasswordScramble(hs.auth_plugin_data, password);
        if (password.empty()) {
            EXPECT_TRUE(native.empty());
            EXPECT_TRUE(sha2.empty());
        } else {
            EXPECT_EQ(native.size(), kNativePasswordResponseLen);
            EXPECT_EQ(sha2.size(), kCachingSha2PasswordResponseLen);
        }
    }
}

// Empty-password login takes caching_sha2's fast path and never triggers
// perform_full_authentication (0x04).  Uses the spawned server's
// empty-password root; skipped when no empty-password credential exists.
TEST_F(MysqlHandshakeServerTest, AuthenticatesEmptyPasswordFastPath) {
    if (NoServer()) {
        puts("Skipped due to absence of mysqld");
        return;
    }
    const AuthCase* empty = NULL;
    for (size_t i = 0; i < g_auth_cases.size(); ++i) {
        if (g_auth_cases[i].password.empty() && !g_auth_cases[i].use_ssl) {
            empty = &g_auth_cases[i];
            break;
        }
    }
    if (empty == NULL) {
        puts("Skipped: no empty-password credential on this server");
        return;
    }
    int fd = ConnectTestMysql();
    ASSERT_GE(fd, 0);
    const LoginTrace t =
        PerformSha2Login(fd, empty->user, empty->password, /*use_ssl=*/false);
    close(fd);
    EXPECT_TRUE(t.ok) << "login failed: " << t.err;
    EXPECT_FALSE(t.full_auth)
        << "empty-password login unexpectedly took the full-auth path";
}

// Full authentication over PLAIN TCP (is_ssl=false): a non-empty password
// against a cold caching_sha2 cache must take the full-auth path and route
// CachingSha2PasswordSlowPath down the RSA branch (NOT cleartext).
TEST_F(MysqlHandshakeServerTest, FullAuthenticationNotSSL) {
    if (NoServer()) {
        puts("Skipped due to absence of mysqld");
        return;
    }
    const AuthCase* c = FindNonEmptyCase(/*use_ssl=*/false);
    if (c == NULL) {
        puts("Skipped: no non-empty-password credential for plaintext "
             "full-auth (need a running server with -mysql_password, or the "
             "mysql client for the spawned account)");
        return;
    }
    int fd = ConnectTestMysql();
    ASSERT_GE(fd, 0);
    const LoginTrace t =
        PerformSha2Login(fd, c->user, c->password, /*use_ssl=*/false);
    close(fd);

    EXPECT_TRUE(t.ok) << "login as '" << c->user << "' failed: " << t.err;
    EXPECT_FALSE(t.used_ssl) << "this login must not be SSL-wrapped";
    if (IsSpawnedServer()) {
        // The spawned account is brand-new -> guaranteed cold cache.
        EXPECT_TRUE(t.full_auth)
            << "cold account should require full authentication (0x04)";
    }
    if (t.full_auth) {
        EXPECT_FALSE(t.used_cleartext)
            << "plain-TCP full-auth must use the RSA branch, not cleartext";
    }
}

// Full authentication over SSL (is_ssl=true): the client upgrades the
// connection to SSL, and on a cold cache the full-auth path routes
// CachingSha2PasswordSlowPath down the CLEARTEXT branch (no RSA) -- the
// secure channel protects the password.
TEST_F(MysqlHandshakeServerTest, FullAuthenticationSSL) {
    if (NoServer()) {
        puts("Skipped due to absence of mysqld");
        return;
    }
    const AuthCase* c = FindNonEmptyCase(/*use_ssl=*/true);
    if (c == NULL) {
        puts("Skipped: no non-empty-password credential for SSL full-auth");
        return;
    }
    int fd = ConnectTestMysql();
    ASSERT_GE(fd, 0);
    const LoginTrace t =
        PerformSha2Login(fd, c->user, c->password, /*use_ssl=*/true);
    close(fd);

    EXPECT_TRUE(t.ok) << "SSL login as '" << c->user << "' failed: " << t.err;
    EXPECT_TRUE(t.used_ssl) << "login should have upgraded the connection to SSL";
    if (IsSpawnedServer()) {
        EXPECT_TRUE(t.full_auth)
            << "cold account should require full authentication (0x04)";
    }
    if (t.full_auth) {
        EXPECT_TRUE(t.used_cleartext)
            << "SSL full-auth must use the cleartext branch, not RSA";
    }
}

// Caching behavior, parameterized over every credential.  caching_sha2
// caches a credential after the first successful authentication, so a
// second login reuses the cache (fast-auth) instead of repeating the full
// RSA exchange.  For each credential we log in twice on fresh
// connections: the first populates the cache, the second must NOT take
// the full-auth path.  Runs in both modes (with the spawned empty-password
// account both logins are trivially fast).
TEST_F(MysqlHandshakeServerTest, CachesCredentialOnSecondLogin) {
    if (NoServer()) {
        puts("Skipped due to absence of mysqld");
        return;
    }
    ASSERT_FALSE(g_auth_cases.empty());
    for (const AuthCase& c : g_auth_cases) {
        SCOPED_TRACE(c.label);
        // First login: establishes the credential in the server's cache.
        int fd1 = ConnectTestMysql();
        ASSERT_GE(fd1, 0);
        const LoginTrace first =
            PerformSha2Login(fd1, c.user, c.password, c.use_ssl);
        close(fd1);
        ASSERT_TRUE(first.ok) << "first login failed: " << first.err;

        // Second login: the credential is now cached, so the server must
        // take the fast-auth path, never perform_full_authentication.
        int fd2 = ConnectTestMysql();
        ASSERT_GE(fd2, 0);
        const LoginTrace second =
            PerformSha2Login(fd2, c.user, c.password, c.use_ssl);
        close(fd2);
        EXPECT_TRUE(second.ok) << "second login failed: " << second.err;
        EXPECT_FALSE(second.full_auth)
            << "second login unexpectedly took the full-auth (0x04) path; the "
               "credential should have been cached by the first login";
    }
}

// Auth-switch path.  The client advertises mysql_native_password in its
// HandshakeResponse41, but the account uses caching_sha2_password, so the
// server replies with an AuthSwitchRequest telling the client to switch.
// PerformSha2Login follows the switch (recomputing the scramble with the
// server-provided plugin and salt) and the login still reaches OK.
TEST_F(MysqlHandshakeServerTest, SwitchesFromNativePasswordToServerPlugin) {
    if (NoServer()) {
        puts("Skipped due to absence of mysqld");
        return;
    }
    ASSERT_FALSE(g_auth_cases.empty());
    const AuthCase& c = g_auth_cases.front();
    int fd = ConnectTestMysql();
    ASSERT_GE(fd, 0);
    const LoginTrace t =
        PerformSha2Login(fd, c.user, c.password, /*use_ssl=*/false,
                         "mysql_native_password");
    close(fd);

    EXPECT_TRUE(t.ok) << "login as '" << c.user << "' failed: " << t.err;
    EXPECT_TRUE(t.auth_switched)
        << "server did not send an AuthSwitchRequest after the client "
           "advertised mysql_native_password";
    EXPECT_EQ(t.switched_plugin, "caching_sha2_password")
        << "server switched to an unexpected plugin: " << t.switched_plugin;
}

}  // namespace

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);
    return RUN_ALL_TESTS();
}
