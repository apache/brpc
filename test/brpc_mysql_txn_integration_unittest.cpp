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

// MySQL client TRANSACTION integration tests, run through brpc's PUBLIC API
// against a REAL mysqld.
//
// Each TEST_F drives a transaction scenario end to end:
//   * brpc::Channel(protocol="mysql", connection_type="pooled",
//     auth=MysqlAuthenticator) -> a live connection to the server;
//   * brpc::NewMysqlTransaction(channel, opts) -> a connection-pinned
//     transaction handle (START TRANSACTION on a dedicated socket);
//   * MysqlRequest(tx).Query(...) + channel.CallMethod(...) -> statements
//     INSIDE the transaction (same pinned socket);
//   * MysqlRequest().Query(...) on the SAME channel -> a SECOND connection
//     from the pool, used as an independent observer to prove isolation
//     (uncommitted rows are invisible until commit());
//   * tx->commit() / tx->rollback() -> terminate the transaction.
//
// Because transactions, simple SELECTs and DML all flow through the same
// COM_QUERY text protocol, these tests also cover simple-query execution
// and text-result parsing (column metadata + row field decoding).
//
// Harness (server spawn / skip convention, -mysql_use_running_server and
// -mysql_host/-port/-user/-password gflags) follows
// test/mysql/brpc_mysql_auth_handshake_unittest.cpp and, transitively,
// test/brpc_redis_unittest.cpp's which-then-spawn pattern.  When mysqld is
// absent every test GTEST_SKIP()s, so the file is CI-safe with no server.

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <string>

#include "brpc/channel.h"
#include "brpc/policy/mysql/mysql.h"
#include "brpc/policy/mysql/mysql_transaction.h"
#include "brpc/policy/mysql/mysql_authenticator.h"
#include "butil/logging.h"

// These gflags are intentionally re-declared here (not shared with the auth
// unittest): the CMake glob at test/CMakeLists.txt builds each
// brpc_mysql_*_unittest.cpp into its OWN executable, so there is no symbol
// collision across test binaries.
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
DEFINE_string(mysql_user, "root", "Login user for the transaction tests.");
DEFINE_string(mysql_password, "",
              "Password for -mysql_user (empty for the spawned server).");
DEFINE_string(mysql_schema, "brpc_txn_test",
              "Schema (database) the transaction tests create and use.");

namespace {

// Throwaway-server harness (mirrors brpc_mysql_auth_handshake_unittest.cpp,
// which mirrors brpc_redis_unittest.cpp).  >0: forked pid; -2: external
// running server reachable; -1: no server -> tests skip.
#define MYSQLD_BIN "mysqld"

static pthread_once_t s_start_once = PTHREAD_ONCE_INIT;
static pid_t s_mysqld_pid = -1;
static std::string s_host = "127.0.0.1";
static int s_port = 13306;
static std::string s_user = "root";
static std::string s_password;

static std::string TestDataDir() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return std::string("/tmp/mysql_txn_data_for_test");
    }
    return std::string(cwd) + "/mysql_txn_data_for_test";
}

static void RemoveMysqlServer() {
    if (s_mysqld_pid > 0) {
        puts("[Stopping mysqld]");
        char cmd[1280];
        snprintf(cmd, sizeof(cmd), "kill %d", s_mysqld_pid);
        CHECK(0 == system(cmd));
        usleep(500000);
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", TestDataDir().c_str());
        CHECK(0 == system(cmd));
    }
}

// Raw TCP probe to detect server readiness; returns fd (caller closes) or -1.
static int ProbeMysql() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(s_port));
    addr.sin_addr.s_addr = inet_addr(s_host.c_str());
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void RunMysqlServer() {
    if (FLAGS_mysql_use_running_server) {
        s_host = FLAGS_mysql_host;
        s_port = FLAGS_mysql_port;
        s_user = FLAGS_mysql_user;
        s_password = FLAGS_mysql_password;
        printf("[Using running mysqld at %s:%d as user '%s']\n",
               s_host.c_str(), s_port, s_user.c_str());
        int fd = ProbeMysql();
        if (fd >= 0) {
            close(fd);
            s_mysqld_pid = -2;
        } else {
            printf("Cannot reach running mysqld at %s:%d, tests will skip\n",
                   s_host.c_str(), s_port);
        }
        return;
    }

    if (system("which " MYSQLD_BIN) != 0) {
        puts("Fail to find " MYSQLD_BIN ", transaction tests will be skipped");
        return;
    }
    s_host = "127.0.0.1";
    s_port = FLAGS_mysql_port;
    s_user = "root";
    s_password.clear();
    const std::string datadir = TestDataDir();
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'",
             datadir.c_str(), datadir.c_str());
    if (system(cmd) != 0) {
        puts("Fail to create datadir, transaction tests will be skipped");
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

    s_mysqld_pid = fork();
    if (s_mysqld_pid < 0) {
        puts("Fail to fork");
        exit(1);
    } else if (s_mysqld_pid == 0) {
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
    // Wait for TCP readiness (fresh tablespace recovery), then create a
    // password account so the caching_sha2 client can authenticate over TCP
    // exactly like the running-server mode.  root keeps its empty password on
    // the unix socket; we exercise the spawned server as empty-password root.
    for (int i = 0; i < 300; ++i) {
        int fd = ProbeMysql();
        if (fd >= 0) {
            close(fd);
            return;
        }
        usleep(100000);
    }
    puts("mysqld did not become ready, transaction tests will be skipped");
    s_mysqld_pid = -1;
}

// Small helpers over the brpc MySQL public API.

// Runs |sql| outside any transaction on |channel| (a fresh pooled
// connection).  Returns false on transport failure.
static bool RunPlain(brpc::Channel& channel, const std::string& sql,
                     brpc::MysqlResponse* resp) {
    brpc::MysqlRequest req;
    if (!req.Query(sql)) {
        return false;
    }
    brpc::Controller cntl;
    channel.CallMethod(NULL, &cntl, &req, resp, NULL);
    return !cntl.Failed();
}

// Runs |sql| INSIDE transaction |tx| (its pinned connection).  Returns false
// on transport failure.
static bool RunInTx(brpc::Channel& channel, const brpc::MysqlTransaction* tx,
                    const std::string& sql, brpc::MysqlResponse* resp) {
    brpc::MysqlRequest req(tx);
    if (!req.Query(sql)) {
        return false;
    }
    brpc::Controller cntl;
    channel.CallMethod(NULL, &cntl, &req, resp, NULL);
    return !cntl.Failed();
}

// Convenience: expects a single OK reply for a DML/DDL statement.
static ::testing::AssertionResult ExpectOk(const brpc::MysqlResponse& resp) {
    if (resp.reply_size() < 1) {
        return ::testing::AssertionFailure() << "no reply";
    }
    const brpc::MysqlReply& r = resp.reply(0);
    if (r.is_error()) {
        return ::testing::AssertionFailure()
               << "ERR " << r.error().errcode() << ": "
               << r.error().msg().as_string();
    }
    if (!r.is_ok()) {
        return ::testing::AssertionFailure() << "reply is not OK, type=" << r.type();
    }
    return ::testing::AssertionSuccess();
}

// Returns the row count of the FIRST reply, asserting it is a result set.
// On any non-resultset reply returns -1 (so callers can fail clearly).
static int64_t ResultRowCount(const brpc::MysqlResponse& resp) {
    if (resp.reply_size() < 1) {
        return -1;
    }
    const brpc::MysqlReply& r = resp.reply(0);
    if (!r.is_resultset()) {
        return -1;
    }
    return static_cast<int64_t>(r.row_count());
}

// Fixture: one channel + a scratch table per test (built in SetUp, dropped in
// TearDown).  InnoDB so DML is transactional.
class MysqlTxnIntegrationTest : public testing::Test {
protected:
    static bool NoServer() { return s_mysqld_pid == -1; }

    void SetUp() override {
        pthread_once(&s_start_once, RunMysqlServer);
        if (NoServer()) {
            GTEST_SKIP() << "no mysqld available; skipping transaction tests";
        }
        // Authenticator carries user/password and the working schema.  An
        // empty schema is created first over a schema-less channel.
        ASSERT_TRUE(InitChannel(&_setup_channel, /*schema=*/""));
        brpc::MysqlResponse resp;
        ASSERT_TRUE(RunPlain(_setup_channel, "CREATE DATABASE IF NOT EXISTS " +
                                                 FLAGS_mysql_schema, &resp));

        ASSERT_TRUE(InitChannel(&_channel, FLAGS_mysql_schema));
        ASSERT_TRUE(RunPlain(_channel, "DROP TABLE IF EXISTS " + Table(), &resp));
        ASSERT_TRUE(ExpectOk(resp)) << "drop pre-existing scratch table";
        ASSERT_TRUE(RunPlain(_channel,
                             "CREATE TABLE " + Table() +
                                 " (id INT PRIMARY KEY, name VARCHAR(32)) "
                                 "ENGINE=InnoDB",
                             &resp));
        ASSERT_TRUE(ExpectOk(resp)) << "create scratch table";
    }

    void TearDown() override {
        if (NoServer()) {
            return;
        }
        brpc::MysqlResponse resp;
        RunPlain(_channel, "DROP TABLE IF EXISTS " + Table(), &resp);
    }

    // Pooled channel is required so a transaction can pin its own dedicated
    // connection while the test issues independent observer queries on others.
    bool InitChannel(brpc::Channel* channel, const std::string& schema) {
        _auth.reset(new brpc::policy::MysqlAuthenticator(s_user, s_password,
                                                         schema));
        brpc::ChannelOptions options;
        options.protocol = "mysql";
        options.connection_type = "pooled";
        options.auth = _auth.get();
        options.timeout_ms = 5000;
        options.max_retry = 0;
        char addr[128];
        snprintf(addr, sizeof(addr), "%s:%d", s_host.c_str(), s_port);
        return channel->Init(addr, &options) == 0;
    }

    std::string Table() const { return "txn_scratch"; }

    brpc::Channel _setup_channel;
    brpc::Channel _channel;
    // Authenticator must outlive the channels that point at it.
    std::unique_ptr<brpc::policy::MysqlAuthenticator> _auth;
};

// Test cases.  Each fat test chains several transactional behaviors so a single
// TEST_F validates a whole group of related transaction guarantees together.

// Transaction lifecycle: commit publishes a row to other connections, and a
// rolled-back insert as well as a rolled-back delete leave the table exactly as
// it was before the transaction started.
TEST_F(MysqlTxnIntegrationTest, CommitPublishesRollbackRestores) {
    brpc::MysqlResponse resp;

    // 1) committed INSERT must be visible on a fresh connection afterwards.
    {
        brpc::MysqlTransactionUniquePtr tx =
            brpc::NewMysqlTransaction(_channel, brpc::MysqlTransactionOptions());
        ASSERT_TRUE(tx != NULL) << "failed to start transaction";
        ASSERT_TRUE(RunInTx(_channel, tx.get(),
                            "INSERT INTO " + Table() + " VALUES (3107, 'quill')",
                            &resp));
        EXPECT_TRUE(ExpectOk(resp));
        EXPECT_EQ(resp.reply(0).ok().affect_row(), 1u);
        ASSERT_TRUE(tx->commit());
    }
    ASSERT_TRUE(RunPlain(_channel, "SELECT id, name FROM " + Table(), &resp));
    EXPECT_EQ(ResultRowCount(resp), 1);
    {
        const brpc::MysqlReply& r = resp.reply(0);
        ASSERT_TRUE(r.is_resultset());
        ASSERT_EQ(r.row_count(), 1u);
        const brpc::MysqlReply::Row& row = r.next();
        ASSERT_EQ(row.field_count(), 2u);
        EXPECT_EQ(row.field(0).sinteger(), 3107);
        EXPECT_EQ(row.field(1).string().as_string(), "quill");
    }

    // 2) a rolled-back INSERT must leave no trace (still exactly one row).
    {
        brpc::MysqlTransactionUniquePtr tx =
            brpc::NewMysqlTransaction(_channel, brpc::MysqlTransactionOptions());
        ASSERT_TRUE(tx != NULL);
        ASSERT_TRUE(RunInTx(_channel, tx.get(),
                            "INSERT INTO " + Table() + " VALUES (5288, 'brindle')",
                            &resp));
        EXPECT_TRUE(ExpectOk(resp));
        ASSERT_TRUE(tx->rollback());
    }
    ASSERT_TRUE(RunPlain(_channel, "SELECT id FROM " + Table(), &resp));
    EXPECT_EQ(ResultRowCount(resp), 1) << "rolled-back insert must vanish";

    // 3) a rolled-back DELETE of the committed row must restore it.
    {
        brpc::MysqlTransactionUniquePtr tx =
            brpc::NewMysqlTransaction(_channel, brpc::MysqlTransactionOptions());
        ASSERT_TRUE(tx != NULL);
        ASSERT_TRUE(RunInTx(_channel, tx.get(),
                            "DELETE FROM " + Table() + " WHERE id = 3107", &resp));
        EXPECT_TRUE(ExpectOk(resp));
        EXPECT_EQ(resp.reply(0).ok().affect_row(), 1u);
        ASSERT_TRUE(tx->rollback());
    }
    ASSERT_TRUE(RunPlain(_channel, "SELECT id FROM " + Table(), &resp));
    EXPECT_EQ(ResultRowCount(resp), 1) << "rolled-back delete must restore row";
    {
        const brpc::MysqlReply& r = resp.reply(0);
        ASSERT_TRUE(r.is_resultset());
        ASSERT_EQ(r.row_count(), 1u);
        EXPECT_EQ(r.next().field(0).sinteger(), 3107);
    }
}

// Isolation in both directions on the same open transaction: the transaction
// reads its own not-yet-committed write on its pinned connection, while an
// independent pooled connection sees nothing until the rollback.
TEST_F(MysqlTxnIntegrationTest, OwnWriteVisibleOthersIsolated) {
    brpc::MysqlTransactionUniquePtr tx =
        brpc::NewMysqlTransaction(_channel, brpc::MysqlTransactionOptions());
    ASSERT_TRUE(tx != NULL);

    brpc::MysqlResponse resp;
    ASSERT_TRUE(RunInTx(_channel, tx.get(),
                        "INSERT INTO " + Table() + " VALUES (6741, 'tangle')",
                        &resp));
    EXPECT_TRUE(ExpectOk(resp));

    // Same pinned connection: must read its own uncommitted row.
    ASSERT_TRUE(RunInTx(_channel, tx.get(),
                        "SELECT name FROM " + Table() + " WHERE id = 6741", &resp));
    EXPECT_EQ(ResultRowCount(resp), 1);
    {
        const brpc::MysqlReply& r = resp.reply(0);
        ASSERT_TRUE(r.is_resultset());
        ASSERT_EQ(r.row_count(), 1u);
        EXPECT_EQ(r.next().field(0).string().as_string(), "tangle");
    }

    // A different pooled connection must NOT see the uncommitted write.
    ASSERT_TRUE(RunPlain(_channel, "SELECT id FROM " + Table(), &resp));
    EXPECT_EQ(ResultRowCount(resp), 0)
        << "uncommitted write leaked to another connection";

    ASSERT_TRUE(tx->rollback());

    // After rollback nothing remains anywhere.
    ASSERT_TRUE(RunPlain(_channel, "SELECT id FROM " + Table(), &resp));
    EXPECT_EQ(ResultRowCount(resp), 0);
}

// Autocommit behavior, both states in one test: with autocommit on (the
// default) a bare INSERT is immediately durable on a new connection; toggling
// autocommit off on a pinned connection turns a later INSERT into pending work
// that ROLLBACK discards.
TEST_F(MysqlTxnIntegrationTest, AutocommitOnDurableOffRollbackable) {
    brpc::MysqlResponse resp;

    // autocommit ON: immediate durability.
    ASSERT_TRUE(RunPlain(_channel,
                         "INSERT INTO " + Table() + " VALUES (4419, 'amber')",
                         &resp));
    EXPECT_TRUE(ExpectOk(resp));
    ASSERT_TRUE(RunPlain(_channel, "SELECT id FROM " + Table(), &resp));
    EXPECT_EQ(ResultRowCount(resp), 1);

    // autocommit OFF on a pinned connection: a new INSERT is pending and a
    // ROLLBACK drops only it, leaving the earlier durable row in place.
    {
        brpc::MysqlTransactionUniquePtr tx =
            brpc::NewMysqlTransaction(_channel, brpc::MysqlTransactionOptions());
        ASSERT_TRUE(tx != NULL);
        ASSERT_TRUE(RunInTx(_channel, tx.get(), "SET autocommit = 0", &resp));
        EXPECT_TRUE(ExpectOk(resp));
        ASSERT_TRUE(RunInTx(_channel, tx.get(),
                            "INSERT INTO " + Table() + " VALUES (8053, 'frost')",
                            &resp));
        EXPECT_TRUE(ExpectOk(resp));
        ASSERT_TRUE(tx->rollback());
    }
    ASSERT_TRUE(RunPlain(_channel, "SELECT id FROM " + Table(), &resp));
    EXPECT_EQ(ResultRowCount(resp), 1)
        << "autocommit=0 + rollback should drop only the pending insert";
    EXPECT_EQ(resp.reply(0).next().field(0).sinteger(), 4419);
}

// Multi-statement transactional grouping plus partial undo: two inserts grouped
// under one transaction become visible together only after commit, and within a
// second transaction a SAVEPOINT lets a later insert be peeled back while the
// pre-savepoint work survives the final commit.
TEST_F(MysqlTxnIntegrationTest, GroupedInsertsThenSavepointPartialUndo) {
    brpc::MysqlResponse resp;

    // Two inserts under one transaction: invisible until commit, then both show.
    {
        brpc::MysqlTransactionUniquePtr tx =
            brpc::NewMysqlTransaction(_channel, brpc::MysqlTransactionOptions());
        ASSERT_TRUE(tx != NULL);
        ASSERT_TRUE(RunInTx(_channel, tx.get(),
                            "INSERT INTO " + Table() + " VALUES (211, 'one')",
                            &resp));
        EXPECT_TRUE(ExpectOk(resp));
        ASSERT_TRUE(RunInTx(_channel, tx.get(),
                            "INSERT INTO " + Table() + " VALUES (733, 'two')",
                            &resp));
        EXPECT_TRUE(ExpectOk(resp));

        // Not yet visible to a separate connection.
        ASSERT_TRUE(RunPlain(_channel, "SELECT id FROM " + Table(), &resp));
        EXPECT_EQ(ResultRowCount(resp), 0);

        ASSERT_TRUE(tx->commit());
    }
    ASSERT_TRUE(RunPlain(_channel, "SELECT id FROM " + Table(), &resp));
    EXPECT_EQ(ResultRowCount(resp), 2) << "both grouped inserts visible";

    // Start fresh for the savepoint half.
    ASSERT_TRUE(RunPlain(_channel, "DELETE FROM " + Table(), &resp));
    ASSERT_TRUE(ExpectOk(resp));

    // SAVEPOINT then ROLLBACK TO it: pre-savepoint row kept, post dropped.
    {
        brpc::MysqlTransactionUniquePtr tx =
            brpc::NewMysqlTransaction(_channel, brpc::MysqlTransactionOptions());
        ASSERT_TRUE(tx != NULL);
        ASSERT_TRUE(RunInTx(_channel, tx.get(),
                            "INSERT INTO " + Table() + " VALUES (901, 'kept')",
                            &resp));
        EXPECT_TRUE(ExpectOk(resp));
        ASSERT_TRUE(RunInTx(_channel, tx.get(), "SAVEPOINT mark1", &resp));
        EXPECT_TRUE(ExpectOk(resp));
        ASSERT_TRUE(RunInTx(_channel, tx.get(),
                            "INSERT INTO " + Table() + " VALUES (902, 'gone')",
                            &resp));
        EXPECT_TRUE(ExpectOk(resp));
        ASSERT_TRUE(RunInTx(_channel, tx.get(), "ROLLBACK TO SAVEPOINT mark1",
                            &resp));
        EXPECT_TRUE(ExpectOk(resp));

        // Inside the txn only the pre-savepoint row remains.
        ASSERT_TRUE(RunInTx(_channel, tx.get(), "SELECT id FROM " + Table(),
                            &resp));
        EXPECT_EQ(ResultRowCount(resp), 1);

        ASSERT_TRUE(tx->commit());
    }
    ASSERT_TRUE(RunPlain(_channel, "SELECT id FROM " + Table(), &resp));
    EXPECT_EQ(ResultRowCount(resp), 1) << "only the kept row should persist";
    {
        const brpc::MysqlReply& r = resp.reply(0);
        ASSERT_TRUE(r.is_resultset());
        ASSERT_EQ(r.row_count(), 1u);
        EXPECT_EQ(r.next().field(0).sinteger(), 901);
    }
}

// Error surfaces from within a transaction: a duplicate-primary-key insert
// returns an ERR reply (and the transaction still rolls back cleanly), and a
// write attempted in a read-only transaction is likewise rejected with ERR.
TEST_F(MysqlTxnIntegrationTest, DuplicateKeyAndReadOnlyWriteReportErr) {
    brpc::MysqlResponse resp;

    // Seed a committed row so the in-txn insert collides on the primary key.
    ASSERT_TRUE(RunPlain(_channel,
                         "INSERT INTO " + Table() + " VALUES (1505, 'seed')",
                         &resp));
    ASSERT_TRUE(ExpectOk(resp));

    {
        brpc::MysqlTransactionUniquePtr tx =
            brpc::NewMysqlTransaction(_channel, brpc::MysqlTransactionOptions());
        ASSERT_TRUE(tx != NULL);
        // Duplicate-key insert -> ERR packet (errno 1062, ER_DUP_ENTRY).
        ASSERT_TRUE(RunInTx(_channel, tx.get(),
                            "INSERT INTO " + Table() + " VALUES (1505, 'clash')",
                            &resp));
        ASSERT_GE(resp.reply_size(), 1u);
        const brpc::MysqlReply& r = resp.reply(0);
        EXPECT_TRUE(r.is_error()) << "duplicate key should yield an ERR reply";
        if (r.is_error()) {
            EXPECT_EQ(r.error().errcode(), 1062) << "expected ER_DUP_ENTRY (1062)";
        }
        ASSERT_TRUE(tx->rollback());
    }

    // A read-only transaction must reject a write.
    {
        brpc::MysqlTransactionOptions opts;
        opts.readonly = true;
        brpc::MysqlTransactionUniquePtr tx =
            brpc::NewMysqlTransaction(_channel, opts);
        ASSERT_TRUE(tx != NULL) << "failed to start read-only transaction";
        ASSERT_TRUE(RunInTx(_channel, tx.get(),
                            "INSERT INTO " + Table() + " VALUES (1777, 'nope')",
                            &resp));
        ASSERT_GE(resp.reply_size(), 1u);
        const brpc::MysqlReply& r = resp.reply(0);
        EXPECT_TRUE(r.is_error())
            << "write in a read-only transaction should be rejected with ERR";
        if (r.is_error()) {
            // ER_CANT_EXECUTE_IN_READ_ONLY_TRANSACTION == 1792.
            EXPECT_EQ(r.error().errcode(), 1792)
                << "expected read-only-transaction error (1792)";
        }
        ASSERT_TRUE(tx->rollback());
    }
}

}  // namespace
