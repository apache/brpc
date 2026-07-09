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

// Tests how a MySQL prepared statement (whose server-side handle is
// connection-scoped) interacts with brpc's CONNECTION_TYPE_SHORT (a fresh
// TCP connection per request).
//
// A MySQL prepared statement is created with COM_STMT_PREPARE on ONE TCP
// connection; the server returns a `stmt_id` that is valid ONLY on that exact
// connection.  COM_STMT_EXECUTE must therefore run on the SAME connection.
//
// CONNECTION_TYPE_SHORT opens a brand-new TCP connection for every request and
// closes it afterwards, so there is no connection affinity across requests.
// To keep prepared statements usable under SHORT, the brpc MySQL client keys
// each cached stmt_id by (SocketId, fd_version) and, when it finds no valid
// handle for the fresh connection, transparently RE-PREPARES the statement on
// that connection before executing.  So execute under SHORT SUCCEEDS -- at the
// cost of an extra prepare round-trip per request.
//
//   * PreparedStatementUnderShortRePreparesAndSucceeds (PRIMARY):
//       build a SHORT channel, prepare "SELECT ? AS v", bind one INT param,
//       CallMethod.  Must SUCCEED (NOT cntl.Failed(), NOT reply(0).is_error())
//       and return the bound value as a 1-row result set; must NOT crash.
//       Looped a few times so each iteration exercises a fresh connection.
//
//   * PlainQueryUnderShortMustSucceed (POSITIVE CONTROL):
//       same SHORT channel; a stateless COM_QUERY "SELECT 7 AS v" must SUCCEED
//       and return 7.  Proves SHORT is fine for stateless queries; only the
//       connection-scoped prepared-statement handle breaks under SHORT.
//
// HARNESS
// Reuses the gflag-driven, self-spawning-mysqld harness from the sibling
// integration files (flags -mysql_use_running_server / -mysql_host / -port /
// -user / -password; MysqlAuthenticator-based channel).  When no mysqld is
// reachable every test GTEST_SKIP()s, so the file is CI-safe.

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/policy/mysql/mysql.h"
#include "brpc/policy/mysql/mysql_statement.h"
#include "brpc/policy/mysql/mysql_authenticator.h"
#include "butil/logging.h"

// Flags mirror the sibling integration files so one command line drives them
// all against the same server.  Each *_unittest.cpp links into its own binary
// (the test/ CMake glob), so re-declaring these flags here is not a clash.
DEFINE_bool(mysql_use_running_server, false,
            "Use an already-running MySQL server instead of spawning a "
            "throwaway one; the running server is neither started nor stopped "
            "by the test.");
DEFINE_string(mysql_host, "127.0.0.1",
              "Host of the running MySQL server "
              "(only with -mysql_use_running_server).");
DEFINE_int32(mysql_port, 13306,
             "TCP port of the MySQL server (used for both the running server "
             "and the spawned throwaway server).");
DEFINE_string(mysql_user, "root", "Login user for the connection-type tests.");
DEFINE_string(mysql_password, "",
              "Password for -mysql_user (empty for the spawned server).");

namespace {

#ifndef GFLAGS_NS
#define GFLAGS_NS GFLAGS_NAMESPACE
#endif

#define MYSQLD_BIN "mysqld"

static const char* kCollation = "utf8mb4_general_ci";

// Throwaway-server harness (mirrors the sibling integration files, which
// mirror brpc_redis_unittest.cpp).  >0: forked pid; -2: external running
// server reachable; -1: no server -> tests skip.
static pthread_once_t g_start_once = PTHREAD_ONCE_INIT;
static pid_t g_mysqld_pid = -1;
static std::string g_host = "127.0.0.1";
static int g_port = 13306;
static std::string g_user = "root";
static std::string g_password;

static std::string TestDataDir() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return std::string("/tmp/mysql_conn_type_data_for_test");
    }
    return std::string(cwd) + "/mysql_conn_type_data_for_test";
}

static void RemoveMysqlServer() {
    if (g_mysqld_pid > 0) {
        puts("[Stopping mysqld]");
        char cmd[1280];
        snprintf(cmd, sizeof(cmd), "kill %d", g_mysqld_pid);
        CHECK(0 == system(cmd));
        usleep(500000);
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", TestDataDir().c_str());
        CHECK(0 == system(cmd));
    }
}

// Raw TCP probe for server readiness; returns fd (caller closes) or -1.
static int ProbeConnect() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(g_port));
    addr.sin_addr.s_addr = inet_addr(g_host.c_str());
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void StartServerOnce() {
    if (FLAGS_mysql_use_running_server) {
        g_host = FLAGS_mysql_host;
        g_port = FLAGS_mysql_port;
        g_user = FLAGS_mysql_user;
        g_password = FLAGS_mysql_password;
        printf("[Using running mysqld at %s:%d as user '%s']\n",
               g_host.c_str(), g_port, g_user.c_str());
        int fd = ProbeConnect();
        if (fd >= 0) {
            close(fd);
            g_mysqld_pid = -2;
        } else {
            printf("Cannot reach running mysqld at %s:%d, tests will skip\n",
                   g_host.c_str(), g_port);
        }
        return;
    }

    if (system("which " MYSQLD_BIN) != 0) {
        puts("Fail to find " MYSQLD_BIN ", connection-type tests will be skipped");
        return;
    }
    g_host = "127.0.0.1";
    g_port = FLAGS_mysql_port;
    g_user = "root";
    g_password.clear();
    const std::string datadir = TestDataDir();
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'",
             datadir.c_str(), datadir.c_str());
    if (system(cmd) != 0) {
        puts("Fail to create datadir, connection-type tests will be skipped");
        return;
    }
    snprintf(cmd, sizeof(cmd),
             MYSQLD_BIN " --initialize-insecure --datadir='%s'"
             " --log-error='%s/init.err'",
             datadir.c_str(), datadir.c_str());
    if (system(cmd) != 0) {
        puts("Fail to initialize mysqld datadir, tests will be skipped");
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
            NULL};
        if (execvp(MYSQLD_BIN, argv) < 0) {
            puts("Fail to run " MYSQLD_BIN);
            exit(1);
        }
    }
    for (int i = 0; i < 300; ++i) {
        int fd = ProbeConnect();
        if (fd >= 0) {
            close(fd);
            return;
        }
        usleep(100000);
    }
    puts("mysqld did not become ready, connection-type tests will be skipped");
    g_mysqld_pid = -1;
}

// Build a SHORT channel: a fresh TCP connection is opened for every request and
// closed afterwards (CONNECTION_TYPE_SHORT), so there is NO connection affinity
// across requests -- which is exactly what breaks prepared-statement handles.
static int InitShortChannel(brpc::Channel* channel,
                            brpc::policy::MysqlAuthenticator** out_auth) {
    brpc::policy::MysqlAuthenticator* auth =
        new brpc::policy::MysqlAuthenticator(g_user, g_password, "", "",
                                             kCollation);
    *out_auth = auth;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MYSQL;
    options.connection_type = "short";  // CONNECTION_TYPE_SHORT: new conn/request
    options.auth = auth;
    options.timeout_ms = 10000;
    options.connect_timeout_ms = 5000;
    options.max_retry = 0;
    return channel->Init(g_host.c_str(), g_port, &options);
}

// Fixture: one shared SHORT channel.
class MysqlConnectionTypeTest : public testing::Test {
protected:
    static bool NoServer() { return g_mysqld_pid == -1; }

    void SetUp() override {
        pthread_once(&g_start_once, StartServerOnce);
        if (NoServer()) {
            GTEST_SKIP() << "no mysqld available; skipping connection-type "
                            "integration test (set -mysql_use_running_server "
                            "or install mysqld)";
        }
        brpc::policy::MysqlAuthenticator* auth = NULL;
        ASSERT_EQ(0, InitShortChannel(&_channel, &auth));
        _auth.reset(auth);
    }

    brpc::Channel _channel;
    // Authenticator must outlive the channel that points at it.
    std::unique_ptr<brpc::policy::MysqlAuthenticator> _auth;
};

// PRIMARY: a prepared statement under CONNECTION_TYPE_SHORT SUCCEEDS.
//
// brpc transparently re-prepares the statement on each fresh short connection:
// because the server `stmt_id` is connection-scoped and SHORT opens a new TCP
// connection per request, brpc issues COM_STMT_PREPARE again on that new
// connection before the COM_STMT_EXECUTE.  So the execute lands on a connection
// that owns a valid handle and returns a correct result set.
//
// NOTE: this works but is SUBOPTIMAL -- a SHORT connection re-prepares the
// statement on every execute because the server stmt_id is connection-scoped;
// use connection_type='pooled' for prepared statements to cache the handle.
// Looped a few times for robustness.
TEST_F(MysqlConnectionTypeTest, PreparedStatementUnderShortRePreparesAndSucceeds) {
    for (int iter = 0; iter < 5; ++iter) {
        brpc::MysqlStatementUniquePtr stmt =
            brpc::NewMysqlStatement(_channel, "SELECT ? AS v");
        ASSERT_TRUE(stmt != NULL) << "iter " << iter;
        ASSERT_EQ(1u, stmt->param_count()) << "iter " << iter;

        const int32_t bound = (int32_t)(40 + iter);
        brpc::MysqlRequest req(stmt.get());
        ASSERT_TRUE(req.AddParam(bound)) << "iter " << iter;

        brpc::MysqlResponse resp;
        brpc::Controller cntl;
        _channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

        ASSERT_FALSE(cntl.Failed()) << "iter " << iter << ": " << cntl.ErrorText();
        ASSERT_GE(resp.reply_size(), 1) << "iter " << iter;
        ASSERT_FALSE(resp.reply(0).is_error())
            << "iter " << iter
            << ": mysql error: " << resp.reply(0).error().msg().as_string();
        ASSERT_TRUE(resp.reply(0).is_resultset()) << "iter " << iter;
        ASSERT_EQ(1u, resp.reply(0).row_count()) << "iter " << iter;

        const brpc::MysqlReply::Field& f = resp.reply(0).next().field(0);
        long long got = 0;
        if (f.is_sbigint())       got = f.sbigint();
        else if (f.is_bigint())   got = (long long)f.bigint();
        else if (f.is_sinteger()) got = f.sinteger();
        else if (f.is_integer())  got = (long long)f.integer();
        else if (f.is_string())   got = atoll(f.string().as_string().c_str());
        else FAIL() << "iter " << iter << ": prepared bind returned a non-integer-ish field";
        EXPECT_EQ((long long)bound, got) << "iter " << iter;
    }
}

// POSITIVE CONTROL: a plain (non-prepared) query under CONNECTION_TYPE_SHORT
// must SUCCEED.  A stateless COM_QUERY carries no connection-scoped handle, so
// a fresh connection per request is perfectly fine.  This proves SHORT itself
// is healthy: prepared statements work under SHORT only via the re-prepare path
// above, while plain queries need no special handling at all.
TEST_F(MysqlConnectionTypeTest, PlainQueryUnderShortMustSucceed) {
    brpc::MysqlRequest req;
    ASSERT_TRUE(req.Query("SELECT 7 AS v"));

    brpc::MysqlResponse resp;
    brpc::Controller cntl;
    _channel.CallMethod(NULL, &cntl, &req, &resp, NULL);

    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_GE(resp.reply_size(), 1);
    ASSERT_FALSE(resp.reply(0).is_error())
        << "mysql error: " << resp.reply(0).error().msg().as_string();
    ASSERT_TRUE(resp.reply(0).is_resultset());
    ASSERT_EQ(1u, resp.reply(0).row_count());

    const brpc::MysqlReply::Field& f = resp.reply(0).next().field(0);
    long long got = 0;
    if (f.is_sbigint())       got = f.sbigint();
    else if (f.is_bigint())   got = (long long)f.bigint();
    else if (f.is_sinteger()) got = f.sinteger();
    else if (f.is_integer())  got = (long long)f.integer();
    else if (f.is_string())   got = atoll(f.string().as_string().c_str());
    else FAIL() << "SELECT 7 returned a non-integer-ish field";
    EXPECT_EQ(7, got);
}

}  // namespace
