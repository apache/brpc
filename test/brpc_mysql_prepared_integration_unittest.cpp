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

// Integration tests for the brpc MySQL client PREPARED-STATEMENT path,
// exercised end to end against a real mysqld through brpc's PUBLIC API
// (brpc::Channel + brpc::NewMysqlStatement + brpc::MysqlRequest /
// brpc::MysqlResponse).  This complements the low-level wire tests in
// brpc_mysql_auth_handshake_unittest.cpp, which speak the protocol over a
// raw socket; here we drive the actual client stack a user would use.
//
// Each fat test chains several prepared-statement behaviors (param counting,
// binding, typed fetch, re-execution, NULL handling, error paths) so the
// test boundaries reflect our own grouping of the client surface.
//
// HARNESS: Reuses the self-spawned / already-running mysqld pattern
// documented in test/README_mysql_auth.md and implemented in
// brpc_mysql_auth_handshake_unittest.cpp.  When -mysql_use_running_server
// is set the tests connect to a server the caller started (neither started
// nor stopped here); otherwise the fixture spawns a throwaway mysqld with
// an empty-password root.  Every test GTEST_SKIP()s when no mysqld is
// reachable, so the suite is safe to run in environments without MySQL.

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

#include <brpc/channel.h>
#include <brpc/controller.h>
#include "brpc/policy/mysql/mysql.h"
#include "brpc/policy/mysql/mysql_authenticator.h"
#include "butil/logging.h"

// These flags intentionally mirror the names used by
// brpc_mysql_auth_handshake_unittest.cpp so a single command line can drive
// both suites against the same running server.  gflags forbids registering
// the same flag twice in one binary, but each *_unittest.cpp is linked into
// its own executable (one gtest_main per glob entry), so there is no clash.
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
              "User for the prepared-statement tests against a running "
              "server.");
DEFINE_string(mysql_password, "",
              "Password for -mysql_user (empty for the spawned server).");
DEFINE_string(mysql_schema, "brpc_ps_test",
              "Schema/database the tests prepare statements against.");

namespace {

#define MYSQLD_BIN "mysqld"

// The schema the integration tests operate in.  On a spawned server we
// create it (and a seed table) ourselves over the unix socket; against a
// running server the caller must have granted -mysql_user access to it.
static const char* kCollation = "utf8mb4_general_ci";

static pthread_once_t g_start_once = PTHREAD_ONCE_INIT;
// >0  : we forked a throwaway mysqld with this pid.
// -2  : an already-running server is reachable.
// -1  : no server available; tests skip.
static pid_t g_mysqld_pid = -1;

static std::string g_host = "127.0.0.1";
static int g_port = 13306;
static std::string g_user = "root";
static std::string g_password;
static std::string g_schema;
// True once the seed schema/table is known to exist (created on spawn, or
// created best-effort against a running server via the channel itself).
static bool g_schema_ready = false;

static std::string TestDataDir() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return std::string("/tmp/mysql_ps_data_for_test");
    }
    return std::string(cwd) + "/mysql_ps_data_for_test";
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

// Opens a raw TCP connection to g_host:g_port purely as a readiness probe;
// returns the fd or -1.  (The tests themselves talk through brpc, not this.)
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
        g_schema = FLAGS_mysql_schema;
        printf("[Using running mysqld at %s:%d as user '%s', schema '%s']\n",
               g_host.c_str(), g_port, g_user.c_str(), g_schema.c_str());
        int fd = ProbeConnect();
        if (fd >= 0) {
            close(fd);
            g_mysqld_pid = -2;
            // We create the seed schema/table lazily through the channel in
            // the fixture (SetUp) so it works even without the mysql CLI.
        } else {
            printf("Cannot reach running mysqld at %s:%d, tests will skip\n",
                   g_host.c_str(), g_port);
        }
        return;
    }

    if (system("which " MYSQLD_BIN) != 0) {
        puts("Fail to find " MYSQLD_BIN ", tests will be skipped");
        return;
    }
    g_host = "127.0.0.1";
    g_port = FLAGS_mysql_port;
    g_user = "root";
    g_password.clear();
    g_schema = FLAGS_mysql_schema;
    const std::string datadir = TestDataDir();
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'",
             datadir.c_str(), datadir.c_str());
    if (system(cmd) != 0) {
        puts("Fail to create datadir, tests will be skipped");
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
        char* const argv[] = {(char*)MYSQLD_BIN,
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
            // Create the seed schema + table over the unix socket (root has
            // an empty password there).  Best-effort: if the mysql CLI is
            // missing we fall back to creating it through the channel in
            // SetUp (DDL also works over the prepared-statement channel via
            // a plain Query reply, but the CLI keeps the fixture simple).
            char create[2048];
            snprintf(create, sizeof(create),
                     "mysql --socket='%s/mysqld.sock' -u root -e \""
                     "CREATE DATABASE IF NOT EXISTS %s; \" 2>/dev/null",
                     datadir.c_str(), g_schema.c_str());
            (void)system(create);  // schema creation is also retried lazily
            return;
        }
        usleep(100000);
    }
    puts("mysqld did not become ready, tests will be skipped");
    g_mysqld_pid = -1;
}

// Builds a Channel configured for the prepared-statement protocol against
// the active server/schema.  Returns 0 on success.
static int InitChannel(brpc::Channel* channel) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MYSQL;
    options.connection_type = "pooled";
    options.timeout_ms = 5000;
    options.connect_timeout_ms = 5000;
    options.max_retry = 0;
    options.auth = new brpc::policy::MysqlAuthenticator(
        g_user, g_password, g_schema, "", kCollation);
    return channel->Init(g_host.c_str(), g_port, &options);
}

// Runs a single plain-text statement (DDL/DML) through |channel| and returns
// true when the server answered without an error reply.  Used by the fixture
// to set up seed tables; not itself one of the prepared-statement scenarios.
static bool RunPlainQuery(brpc::Channel* channel, const std::string& sql) {
    brpc::MysqlRequest request;
    if (!request.Query(sql)) {
        return false;
    }
    brpc::MysqlResponse response;
    brpc::Controller cntl;
    channel->CallMethod(NULL, &cntl, &request, &response, NULL);
    if (cntl.Failed()) {
        return false;
    }
    if (response.reply_size() < 1) {
        return false;
    }
    return !response.reply(0).is_error();
}

class MysqlPreparedTest : public testing::Test {
protected:
    void SetUp() override {
        pthread_once(&g_start_once, StartServerOnce);
        if (NoServer()) {
            return;
        }
        // Ensure the schema + the shared seed table exist exactly once.
        // (Idempotent CREATE IF NOT EXISTS, so re-running is harmless.)
        if (!g_schema_ready) {
            brpc::Channel setup;
            // Connect with an empty schema first so CREATE DATABASE works
            // even if g_schema does not yet exist on a running server.
            brpc::ChannelOptions options;
            options.protocol = brpc::PROTOCOL_MYSQL;
            options.connection_type = "pooled";
            options.timeout_ms = 5000;
            options.connect_timeout_ms = 5000;
            options.auth = new brpc::policy::MysqlAuthenticator(
                g_user, g_password, "", "", kCollation);
            if (setup.Init(g_host.c_str(), g_port, &options) == 0) {
                RunPlainQuery(&setup, "CREATE DATABASE IF NOT EXISTS " + g_schema);
            }
            // Now (re)connect bound to the schema and create the seed table.
            brpc::Channel ch;
            if (InitChannel(&ch) == 0) {
                RunPlainQuery(&ch, "DROP TABLE IF EXISTS ps_people");
                RunPlainQuery(&ch,
                    "CREATE TABLE ps_people("
                    "id INT, name VARCHAR(50), score BIGINT)");
                RunPlainQuery(&ch,
                    "INSERT INTO ps_people VALUES"
                    "(417,'maple',9100),(528,'cobalt',9200),(639,NULL,9300)");
                g_schema_ready = true;
            }
        }
        ASSERT_EQ(0, InitChannel(&channel_)) << "channel init failed";
    }

    static bool NoServer() { return g_mysqld_pid == -1; }

    brpc::Channel channel_;
};

#define SKIP_IF_NO_SERVER()                                  \
    do {                                                     \
        if (NoServer()) {                                    \
            GTEST_SKIP() << "no mysqld available";           \
        }                                                    \
    } while (0)

// Convenience: prepare |sql| against channel_, asserting success and
// returning the statement.  Returns nullptr on failure (caller asserts).
#define PREPARE_OR_FAIL(var, sql)                                          \
    auto var = brpc::NewMysqlStatement(channel_, (sql));                   \
    ASSERT_TRUE((var) != nullptr) << "prepare failed for: " << (sql)

// Parameter counting across statement shapes, plus executing a no-parameter
// SELECT that returns the full seed result set.
TEST_F(MysqlPreparedTest, ParamCountsAndNoParamSelect) {
    SKIP_IF_NO_SERVER();

    // param_count must reflect the placeholders in each shape.
    {
        PREPARE_OR_FAIL(s, "INSERT INTO ps_people VALUES(?, ?, ?)");
        EXPECT_EQ(3u, s->param_count());
    }
    {
        PREPARE_OR_FAIL(s, "SELECT * FROM ps_people WHERE id=? AND name=?");
        EXPECT_EQ(2u, s->param_count());
    }
    {
        PREPARE_OR_FAIL(s, "DELETE FROM ps_people WHERE id=417");
        EXPECT_EQ(0u, s->param_count());
    }
    {
        PREPARE_OR_FAIL(s, "DELETE FROM ps_people WHERE id=?");
        EXPECT_EQ(1u, s->param_count());
    }

    // A no-parameter SELECT returns a result set covering all three seed rows.
    PREPARE_OR_FAIL(s, "SELECT id, name, score FROM ps_people ORDER BY id");
    EXPECT_EQ(0u, s->param_count());
    brpc::MysqlRequest request(s.get());
    brpc::MysqlResponse response;
    brpc::Controller cntl;
    channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_GE(response.reply_size(), 1u);
    const brpc::MysqlReply& r = response.reply(0);
    ASSERT_TRUE(r.is_resultset()) << "expected a result set, got: " << r;
    EXPECT_EQ(3u, r.column_count());
    EXPECT_EQ(3u, r.row_count());
}

// Bind and execute, all parameter flavors in one place: a single INT bind, a
// single STRING bind, and a two-INT arithmetic expression each return their
// own correct result.
TEST_F(MysqlPreparedTest, BindAndExecuteIntStringAndArithmetic) {
    SKIP_IF_NO_SERVER();

    // (a) bind one INT param -> matching row's name.
    {
        PREPARE_OR_FAIL(s, "SELECT name FROM ps_people WHERE id=?");
        ASSERT_EQ(1u, s->param_count());
        brpc::MysqlRequest request(s.get());
        ASSERT_TRUE(request.AddParam((int32_t)417));
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_GE(response.reply_size(), 1u);
        const brpc::MysqlReply& r = response.reply(0);
        ASSERT_TRUE(r.is_resultset()) << r;
        ASSERT_EQ(1u, r.row_count());
        const brpc::MysqlReply::Field& f = r.next().field(0);
        ASSERT_TRUE(f.is_string());
        EXPECT_EQ("maple", f.string().as_string());
    }

    // (b) bind one STRING param -> matching row's id.
    {
        PREPARE_OR_FAIL(s, "SELECT id FROM ps_people WHERE name=?");
        ASSERT_EQ(1u, s->param_count());
        brpc::MysqlRequest request(s.get());
        ASSERT_TRUE(request.AddParam(butil::StringPiece("cobalt")));
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_GE(response.reply_size(), 1u);
        const brpc::MysqlReply& r = response.reply(0);
        ASSERT_TRUE(r.is_resultset()) << r;
        ASSERT_EQ(1u, r.row_count());
        const brpc::MysqlReply::Field& f = r.next().field(0);
        // id is INT; brpc surfaces a signed INT column as sinteger.
        ASSERT_TRUE(f.is_sinteger() || f.is_integer())
            << "expected an integer id field";
        if (f.is_sinteger()) {
            EXPECT_EQ(528, f.sinteger());
        } else {
            EXPECT_EQ(528u, f.integer());
        }
    }

    // (c) two INT params in an arithmetic expression -> their sum.
    {
        PREPARE_OR_FAIL(s, "SELECT CAST(? AS SIGNED) + CAST(? AS SIGNED)");
        ASSERT_EQ(2u, s->param_count());
        brpc::MysqlRequest request(s.get());
        ASSERT_TRUE(request.AddParam((int32_t)315));
        ASSERT_TRUE(request.AddParam((int32_t)28));
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_GE(response.reply_size(), 1u);
        const brpc::MysqlReply& r = response.reply(0);
        ASSERT_TRUE(r.is_resultset()) << r;
        ASSERT_EQ(1u, r.row_count());
        const brpc::MysqlReply::Field& f = r.next().field(0);
        // The sum comes back as a (possibly wide) integer; accept any width.
        ASSERT_FALSE(f.is_nil());
        long long got = 0;
        if (f.is_sbigint()) got = f.sbigint();
        else if (f.is_bigint()) got = (long long)f.bigint();
        else if (f.is_sinteger()) got = f.sinteger();
        else if (f.is_integer()) got = f.integer();
        else if (f.is_string()) got = atoll(f.string().as_string().c_str());
        else FAIL() << "unexpected field type for ?+?";
        EXPECT_EQ(343, got);
    }
}

// Re-execute one statement with new parameters, and fetch every column type
// (INT, VARCHAR, BIGINT) of a single matched row through its typed accessor.
TEST_F(MysqlPreparedTest, ReExecuteAndTypedColumnFetch) {
    SKIP_IF_NO_SERVER();

    // Re-execute the SAME statement twice with different bound ids.
    {
        PREPARE_OR_FAIL(s, "SELECT name FROM ps_people WHERE id=?");
        struct Case { int32_t id; const char* name; };
        const Case cases[] = {{417, "maple"}, {528, "cobalt"}};
        for (const Case& c : cases) {
            SCOPED_TRACE(c.id);
            brpc::MysqlRequest request(s.get());
            ASSERT_TRUE(request.AddParam(c.id));
            brpc::MysqlResponse response;
            brpc::Controller cntl;
            channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            ASSERT_GE(response.reply_size(), 1u);
            const brpc::MysqlReply& r = response.reply(0);
            ASSERT_TRUE(r.is_resultset()) << r;
            ASSERT_EQ(1u, r.row_count());
            const brpc::MysqlReply::Field& f = r.next().field(0);
            ASSERT_TRUE(f.is_string());
            EXPECT_EQ(c.name, f.string().as_string());
        }
    }

    // Typed fetch: read INT id, VARCHAR name and BIGINT score off one row.
    {
        PREPARE_OR_FAIL(s, "SELECT id, name, score FROM ps_people WHERE id=?");
        brpc::MysqlRequest request(s.get());
        ASSERT_TRUE(request.AddParam((int32_t)528));
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_GE(response.reply_size(), 1u);
        const brpc::MysqlReply& r = response.reply(0);
        ASSERT_TRUE(r.is_resultset()) << r;
        ASSERT_EQ(3u, r.column_count());
        ASSERT_EQ(1u, r.row_count());
        const brpc::MysqlReply::Row& row = r.next();

        // Column 0: INT id == 528.
        const brpc::MysqlReply::Field& id = row.field(0);
        ASSERT_TRUE(id.is_sinteger() || id.is_integer());
        EXPECT_EQ(528, id.is_sinteger() ? id.sinteger() : (int)id.integer());

        // Column 1: VARCHAR name == "cobalt".
        const brpc::MysqlReply::Field& name = row.field(1);
        ASSERT_TRUE(name.is_string());
        EXPECT_EQ("cobalt", name.string().as_string());

        // Column 2: BIGINT score == 9200.
        const brpc::MysqlReply::Field& score = row.field(2);
        ASSERT_TRUE(score.is_sbigint() || score.is_bigint() ||
                    score.is_sinteger() || score.is_integer())
            << "expected an integer score field";
        long long sc = 0;
        if (score.is_sbigint()) sc = score.sbigint();
        else if (score.is_bigint()) sc = (long long)score.bigint();
        else if (score.is_sinteger()) sc = score.sinteger();
        else sc = score.integer();
        EXPECT_EQ(9200, sc);
    }
}

// NULL handling both ways: a column whose value is SQL NULL (the seed row with
// a NULL name) and a literal NULL in the SELECT list both surface as nil.
TEST_F(MysqlPreparedTest, NullColumnAndLiteralNullAreNil) {
    SKIP_IF_NO_SERVER();

    // A row with a NULL name column surfaces field(0) as nil.
    {
        PREPARE_OR_FAIL(s, "SELECT name FROM ps_people WHERE id=?");
        brpc::MysqlRequest request(s.get());
        ASSERT_TRUE(request.AddParam((int32_t)639));
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_GE(response.reply_size(), 1u);
        const brpc::MysqlReply& r = response.reply(0);
        ASSERT_TRUE(r.is_resultset()) << r;
        ASSERT_EQ(1u, r.row_count());
        EXPECT_TRUE(r.next().field(0).is_nil());
    }

    // A literal NULL in the SELECT list also comes back nil.
    {
        PREPARE_OR_FAIL(s, "SELECT NULL");
        EXPECT_EQ(0u, s->param_count());
        brpc::MysqlRequest request(s.get());
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_GE(response.reply_size(), 1u);
        const brpc::MysqlReply& r = response.reply(0);
        ASSERT_TRUE(r.is_resultset()) << r;
        ASSERT_EQ(1u, r.row_count());
        EXPECT_TRUE(r.next().field(0).is_nil());
    }
}

// Error paths must not crash the client: a malformed statement and a
// parameter-count mismatch each surface either a failed RPC or an error reply,
// never a silent success or a crash.
TEST_F(MysqlPreparedTest, MalformedAndParamMismatchSurfaceErrors) {
    SKIP_IF_NO_SERVER();

    // Malformed SQL: dangling WHERE with no predicate.
    {
        auto s = brpc::NewMysqlStatement(
            channel_, "SELECT id FROM ps_people WHERE id=? AND WHERE");
        // Acceptable: prepare returns null, OR the first execute reports an
        // error reply.  A crash or a silent success is not.
        if (s == nullptr) {
            SUCCEED() << "prepare of malformed SQL returned null as expected";
        } else {
            brpc::MysqlRequest request(s.get());
            brpc::MysqlResponse response;
            brpc::Controller cntl;
            channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
            if (cntl.Failed()) {
                SUCCEED() << "execute of malformed statement failed as expected: "
                          << cntl.ErrorText();
            } else {
                ASSERT_GE(response.reply_size(), 1u);
                EXPECT_TRUE(response.reply(0).is_error())
                    << "malformed prepared statement unexpectedly succeeded";
            }
        }
    }

    // Bind too few params: one-? statement executed with zero params.
    {
        PREPARE_OR_FAIL(s, "SELECT name FROM ps_people WHERE id=?");
        ASSERT_EQ(1u, s->param_count());
        brpc::MysqlRequest request(s.get());
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
        if (cntl.Failed()) {
            SUCCEED() << "mismatched param count failed the RPC as expected: "
                      << cntl.ErrorText();
        } else {
            ASSERT_GE(response.reply_size(), 1u);
            EXPECT_TRUE(response.reply(0).is_error())
                << "execute with too few params unexpectedly produced a "
                   "non-error reply";
        }
    }
}

// One statement re-used across executes agrees with itself, and a second,
// independent statement on the same channel still works afterward.
TEST_F(MysqlPreparedTest, StatementReuseAndIndependentStatement) {
    SKIP_IF_NO_SERVER();
    PREPARE_OR_FAIL(s1, "SELECT COUNT(*) FROM ps_people");

    // Execute s1 twice; both must agree.
    long long first_count = -1;
    for (int iter = 0; iter < 2; ++iter) {
        SCOPED_TRACE(iter);
        brpc::MysqlRequest request(s1.get());
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_GE(response.reply_size(), 1u);
        const brpc::MysqlReply& r = response.reply(0);
        ASSERT_TRUE(r.is_resultset()) << r;
        ASSERT_EQ(1u, r.row_count());
        const brpc::MysqlReply::Field& f = r.next().field(0);
        long long c = 0;
        if (f.is_sbigint()) c = f.sbigint();
        else if (f.is_bigint()) c = (long long)f.bigint();
        else if (f.is_sinteger()) c = f.sinteger();
        else if (f.is_integer()) c = f.integer();
        else if (f.is_string()) c = atoll(f.string().as_string().c_str());
        else FAIL() << "unexpected COUNT(*) field type";
        if (first_count < 0) first_count = c;
        EXPECT_EQ(first_count, c);
    }
    EXPECT_EQ(3, first_count) << "seed table should hold 3 rows";

    // A second, independent statement on the same channel still works after
    // s1 has been used -- exercises concurrent statement objects / reuse.
    PREPARE_OR_FAIL(s2, "SELECT id FROM ps_people WHERE id=?");
    brpc::MysqlRequest request(s2.get());
    ASSERT_TRUE(request.AddParam((int32_t)417));
    brpc::MysqlResponse response;
    brpc::Controller cntl;
    channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
    ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
    ASSERT_GE(response.reply_size(), 1u);
    ASSERT_TRUE(response.reply(0).is_resultset());
    EXPECT_EQ(1u, response.reply(0).row_count());
}

// BINARY-protocol TIME and DATETIME column parsing
// (MysqlReply::Field::ParseBinaryTime / ParseBinaryDataTime).
//
// These code paths are ONLY reached over the prepared-statement (binary)
// result protocol -- a plain text Query would return the value pre-formatted
// by the server and never touch ParseBinaryTime/ParseBinaryDataTime.  So every
// case here PREPAREs a SELECT and executes it, forcing brpc to decode the
// packed wire bytes itself.
//
// TIME and DATETIME columns are surfaced as STRINGS: MysqlReply::Field's only
// text accessor is string() (returning a butil::StringPiece), and
// is_string() returns true for MYSQL_FIELD_TYPE_TIME and
// MYSQL_FIELD_TYPE_DATETIME (see mysql_reply.h).  The parser writes the
// formatted text into _data.str via str.set(ptr, len) with an explicitly
// computed length, so comparing the FULL string (length included) against the
// exact expected text catches any trailing-garbage / wrong-length bug -- in
// particular the variable-width TIME path (optional sign, 2- vs 3+-digit
// hour) that has historically mis-sized its output.
//
// We use CAST(literal AS TIME/DATETIME[(N)]) so the exact value (and the
// column's declared fractional-second precision, which drives the wire
// length) is fully under our control.
TEST_F(MysqlPreparedTest, BinaryTimeAndDateTimeParsing) {
    SKIP_IF_NO_SERVER();

    struct Case {
        const char* sql;       // prepared SELECT producing one TIME/DATETIME field
        const char* expected;  // exact string the field must equal
    };
    const Case cases[] = {
        // TIME, ordinary 2-digit hour.
        {"SELECT CAST('12:34:56' AS TIME)", "12:34:56"},
        // TIME, 3-digit hour: the variable-width hour path (total_hour >= 100).
        {"SELECT CAST('300:00:00' AS TIME)", "300:00:00"},
        // TIME, the documented maximum magnitude.
        {"SELECT CAST('838:59:59' AS TIME)", "838:59:59"},
        // TIME, negative: leading '-' sign byte on the wire.
        {"SELECT CAST('-12:30:45' AS TIME)", "-12:30:45"},
        // TIME with fractional seconds (decimal=3 -> 12-byte wire packet).
        {"SELECT CAST('01:02:03.456' AS TIME(3))", "01:02:03.456"},
        // DATETIME with no sub-second part (7-byte wire packet).
        {"SELECT CAST('2021-03-04 05:06:07' AS DATETIME)", "2021-03-04 05:06:07"},
        // DATETIME with microseconds (decimal=6 -> 11-byte wire packet).
        {"SELECT CAST('2021-03-04 05:06:07.123456' AS DATETIME(6))",
         "2021-03-04 05:06:07.123456"},
        // DATETIME at exact midnight: MySQL omits the time-of-day part, so this
        // arrives as a 4-byte (len==4) wire packet. The parser must emit the
        // full "YYYY-MM-DD 00:00:00" form and report exactly 19 bytes.
        {"SELECT CAST('2021-03-04 00:00:00' AS DATETIME)",
         "2021-03-04 00:00:00"},
        // DATE column: only the date part on the wire (len==4) -> "YYYY-MM-DD".
        {"SELECT CAST('2021-03-04' AS DATE)", "2021-03-04"},
        // TIME zero value: encoded with len==0 (no field bytes on the wire).
        // This must surface as the zero string "00:00:00", NOT as NULL.
        {"SELECT CAST('00:00:00' AS TIME)", "00:00:00"},
    };

    for (const Case& c : cases) {
        SCOPED_TRACE(c.sql);
        PREPARE_OR_FAIL(s, c.sql);
        EXPECT_EQ(0u, s->param_count());
        brpc::MysqlRequest request(s.get());
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        channel_.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_GE(response.reply_size(), 1u);
        const brpc::MysqlReply& r = response.reply(0);
        ASSERT_TRUE(r.is_resultset()) << "expected a result set, got: " << r;
        ASSERT_EQ(1u, r.column_count());
        ASSERT_EQ(1u, r.row_count());
        const brpc::MysqlReply::Field& f = r.next().field(0);
        // The binary TIME/DATETIME value must be surfaced as a string (this is
        // the ParseBinaryTime / ParseBinaryDataTime output).
        ASSERT_TRUE(f.is_string())
            << "TIME/DATETIME field should be exposed as a string";
        // Compare the FULL string, including its length: a trailing-garbage or
        // off-by-one length bug in the parser would make this exact compare
        // fail even if the visible prefix looks right.
        const std::string got = f.string().as_string();
        EXPECT_EQ(c.expected, got)
            << "binary-parsed value mismatch (got length " << got.size()
            << ", expected length " << strlen(c.expected) << ")";
        EXPECT_EQ(strlen(c.expected), got.size())
            << "binary-parsed value has wrong length";
    }
}

}  // namespace
