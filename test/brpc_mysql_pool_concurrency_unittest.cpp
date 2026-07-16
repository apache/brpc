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

// Concurrency stress test on a POOLED mysql channel: many bthreads hammering
// ONE pooled Channel, re-running concurrently the self-checking work units
// that the two sibling integration files cover serially:
//
//   * brpc_mysql_txn_integration_unittest.cpp       (transaction scenarios)
//   * brpc_mysql_prepared_integration_unittest.cpp  (prepared-statement
//                                                     scenarios)
//
// Only the literal data values differ from the siblings so that cross-talk
// between concurrent workers is detectable by value.  A POOLED Channel must
// check out / return pooled sockets without races, must PIN one socket per
// MysqlTransaction, and must keep concurrent transactions / prepared
// statements isolated.
//
// WHAT IT CHECKS
// ConnectionType = POOLED, pool capped at FIVE connections via the gflag
// `max_connection_pool_size` (DEFINE_int32 max_connection_pool_size in
// src/brpc/socket.cpp:99).  FIVE is deliberate: with more workers than
// pooled sockets we exercise BOTH pooled-socket reuse AND the create-a-NEW-
// connection-under-load path concurrently, surfacing checkout/return races,
// transaction connection-affinity/pinning under contention, and fd_version ABA.
//
//   * ManyWorkersMixedScenarios:
//       16-32 bthreads, each looping ~50x, each iteration picks ONE of five
//       representative self-checking work units (3 txn + 2 prepared) and
//       asserts its OWN correct, independent result.  Per-worker scratch tables
//       / per-iteration ids keep row-count assertions exact under concurrency.
//
//   * TwoTransactionsHoldDifferentPinnedSockets (focused check a):
//       two transactions in parallel must hold DIFFERENT pinned SocketIds
//       (GetSocketId()) and must not see each other's rows.
//
//   * TransactionPlusPreparedInParallel (focused check b):
//       one transaction + one prepared statement in parallel, each returns its
//       own correct independent result; the prepared path must not disturb the
//       transaction's pinned connection.
//
// The bar: NO concurrency bug across all iterations -- no shared-socket
// corruption, no interleaved/wrong replies, no crash.
//
// HARNESS
// Reuses the gflag-driven, self-spawning-mysqld harness from the two sibling
// integration files (flags -mysql_use_running_server / -mysql_host / -port /
// -user / -password / -schema; MysqlAuthenticator-based pooled Channel).  When
// no mysqld is reachable every test GTEST_SKIP()s, so the file is CI-safe.

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "brpc/channel.h"
#include "brpc/controller.h"
#include "brpc/policy/mysql/mysql.h"
#include "brpc/policy/mysql/mysql_transaction.h"
#include "brpc/policy/mysql/mysql_authenticator.h"
#include "bthread/bthread.h"
#include "butil/logging.h"
#include "butil/string_printf.h"
#include "butil/strings/string_piece.h"

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
DEFINE_string(mysql_user, "root", "Login user for the concurrency tests.");
DEFINE_string(mysql_password, "",
              "Password for -mysql_user (empty for the spawned server).");
DEFINE_string(mysql_schema, "brpc_pool_conc_test",
              "Schema (database) the concurrency tests create and use.");

namespace {

#ifndef GFLAGS_NS
#define GFLAGS_NS GFLAGS_NAMESPACE
#endif

#define MYSQLD_BIN "mysqld"

static const char* kCollation = "utf8mb4_general_ci";

// Concurrency knobs.  kWorkers is deliberately > the pool cap (5) so workers
// contend for pooled sockets AND force new-connection creation under load.
const int kWorkers = 24;
const int kIterationsPerWorker = 50;
const int kPoolCap = 5;

// Throwaway-server harness (mirrors the two sibling integration files, which
// mirror brpc_redis_unittest.cpp).  >0: forked pid; -2: external running
// server reachable; -1: no server -> tests skip.
static pthread_once_t g_start_once = PTHREAD_ONCE_INIT;
static pid_t g_mysqld_pid = -1;
static std::string g_host = "127.0.0.1";
static int g_port = 13306;
static std::string g_user = "root";
static std::string g_password;
static std::string g_schema;

static std::string TestDataDir() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return std::string("/tmp/mysql_pool_conc_data_for_test");
    }
    return std::string(cwd) + "/mysql_pool_conc_data_for_test";
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
        g_schema = FLAGS_mysql_schema;
        printf("[Using running mysqld at %s:%d as user '%s', schema '%s']\n",
               g_host.c_str(), g_port, g_user.c_str(), g_schema.c_str());
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
        puts("Fail to find " MYSQLD_BIN ", concurrency tests will be skipped");
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
        puts("Fail to create datadir, concurrency tests will be skipped");
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
    puts("mysqld did not become ready, concurrency tests will be skipped");
    g_mysqld_pid = -1;
}

// Small helpers over the brpc MySQL public API.

// Plain (no transaction / no statement) query on a fresh pooled connection.
static bool RunPlain(brpc::Channel& channel, const std::string& sql,
                     brpc::MysqlResponse* resp, std::string* err) {
    brpc::MysqlRequest req;
    if (!req.Query(sql)) {
        if (err) *err = "build query failed: " + sql;
        return false;
    }
    brpc::Controller cntl;
    channel.CallMethod(NULL, &cntl, &req, resp, NULL);
    if (cntl.Failed()) {
        if (err) *err = "rpc failed: " + cntl.ErrorText();
        return false;
    }
    if (resp->reply_size() < 1) {
        if (err) *err = "no reply for: " + sql;
        return false;
    }
    if (resp->reply(0).is_error()) {
        const brpc::MysqlReply& r = resp->reply(0);
        if (err) {
            *err = butil::string_printf("mysql error %u: %.*s (sql=%s)",
                r.error().errcode(), (int)r.error().msg().size(),
                r.error().msg().data(), sql.c_str());
        }
        return false;
    }
    return true;
}

// |sql| INSIDE transaction |tx| (its pinned connection).
static bool RunInTx(brpc::Channel& channel, const brpc::MysqlTransaction* tx,
                    const std::string& sql, brpc::MysqlResponse* resp,
                    std::string* err) {
    brpc::MysqlRequest req(tx);
    if (!req.Query(sql)) {
        if (err) *err = "build query failed: " + sql;
        return false;
    }
    brpc::Controller cntl;
    channel.CallMethod(NULL, &cntl, &req, resp, NULL);
    if (cntl.Failed()) {
        if (err) *err = "rpc failed: " + cntl.ErrorText();
        return false;
    }
    if (resp->reply_size() < 1) {
        if (err) *err = "no reply for: " + sql;
        return false;
    }
    return true;
}

// Row count of the first reply when it is a result set, else -1.
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

// Coerce an integer-ish field to long long (handles the various widths the
// server may choose for a column / expression).
static bool FieldToLongLong(const brpc::MysqlReply::Field& f, long long* out) {
    if (f.is_sbigint())       *out = f.sbigint();
    else if (f.is_bigint())   *out = (long long)f.bigint();
    else if (f.is_sinteger()) *out = f.sinteger();
    else if (f.is_integer())  *out = (long long)f.integer();
    else if (f.is_string())   *out = atoll(f.string().as_string().c_str());
    else return false;
    return true;
}

static int InitPooledChannel(brpc::Channel* channel,
                             brpc::policy::MysqlAuthenticator** out_auth,
                             const std::string& schema) {
    brpc::policy::MysqlAuthenticator* auth =
        new brpc::policy::MysqlAuthenticator(g_user, g_password, schema, "",
                                             kCollation);
    *out_auth = auth;
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MYSQL;
    options.connection_type = "pooled";  // CONNECTION_TYPE_POOLED
    options.auth = auth;
    options.timeout_ms = 10000;
    options.connect_timeout_ms = 5000;
    options.max_retry = 0;
    return channel->Init(g_host.c_str(), g_port, &options);
}

// Fixture: one shared pooled channel, pool capped at FIVE.  Per-test scratch
// tables are created by the tests themselves (per-worker, to keep row counts
// exact under concurrency).
class MysqlPoolConcurrencyTest : public testing::Test {
protected:
    static bool NoServer() { return g_mysqld_pid == -1; }

    void SetUp() override {
        pthread_once(&g_start_once, StartServerOnce);
        if (NoServer()) {
            GTEST_SKIP() << "no mysqld available; skipping pool-concurrency "
                            "integration test (set -mysql_use_running_server "
                            "or install mysqld)";
        }
        // Cap the pool at FIVE for the whole test.  Verified flag name:
        // src/brpc/socket.cpp:99 DEFINE_int32(max_connection_pool_size, ...).
        ASSERT_FALSE(GFLAGS_NS::SetCommandLineOption(
                         "max_connection_pool_size",
                         std::to_string(kPoolCap).c_str()).empty())
            << "failed to set gflag max_connection_pool_size";

        // Create the schema over a schema-less channel, then bind to it.
        brpc::policy::MysqlAuthenticator* setup_auth = NULL;
        ASSERT_EQ(0, InitPooledChannel(&_setup_channel, &setup_auth, ""));
        _setup_auth.reset(setup_auth);
        brpc::MysqlResponse resp;
        std::string err;
        ASSERT_TRUE(RunPlain(_setup_channel,
                             "CREATE DATABASE IF NOT EXISTS " + g_schema,
                             &resp, &err)) << err;

        brpc::policy::MysqlAuthenticator* auth = NULL;
        ASSERT_EQ(0, InitPooledChannel(&_channel, &auth, g_schema));
        _auth.reset(auth);
    }

    brpc::Channel _setup_channel;
    brpc::Channel _channel;
    // Authenticators must outlive the channels that point at them.
    std::unique_ptr<brpc::policy::MysqlAuthenticator> _setup_auth;
    std::unique_ptr<brpc::policy::MysqlAuthenticator> _auth;
};

// WORK UNITS
// Each work unit is a self-checking re-run of a sibling scenario, with its OWN
// independent expected result so cross-talk/corruption is detectable.  Each
// returns true on a correct result; on any failure it fills |err|.
//
// All work units use a PER-WORKER scratch table (passed in) so concurrent
// workers never share rows, keeping row-count assertions exact.

// WU1 -- txn commit makes rows visible.
// (Transaction-commit-visibility check; uses its own per-worker id 71xxx
//  'aria' so concurrent workers never collide on data.)
static bool WU_TxnCommitVisible(brpc::Channel& ch, const std::string& table,
                                int iter, std::string* err) {
    const int id = 71000 + iter;
    const char* name = "aria";
    brpc::MysqlResponse resp;
    brpc::MysqlTransactionUniquePtr tx =
        brpc::NewMysqlTransaction(ch, brpc::MysqlTransactionOptions());
    if (tx == NULL) { *err = "WU1: NewMysqlTransaction NULL"; return false; }
    if (!RunInTx(ch, tx.get(),
                 butil::string_printf("INSERT INTO %s VALUES (%d, '%s')",
                                      table.c_str(), id, name),
                 &resp, err)) return false;
    if (resp.reply(0).is_error()) {
        *err = "WU1 INSERT err: " + resp.reply(0).error().msg().as_string();
        return false;
    }
    if (!tx->commit()) { *err = "WU1: commit failed"; return false; }

    // A fresh pooled connection must now see exactly our committed row.
    if (!RunPlain(ch, butil::string_printf(
                          "SELECT id, name FROM %s WHERE id=%d",
                          table.c_str(), id), &resp, err)) return false;
    if (ResultRowCount(resp) != 1) {
        *err = butil::string_printf("WU1: expected 1 visible row, got %lld",
                                    (long long)ResultRowCount(resp));
        return false;
    }
    const brpc::MysqlReply::Row& row = resp.reply(0).next();
    long long got_id = 0;
    if (!FieldToLongLong(row.field(0), &got_id) || got_id != id) {
        *err = "WU1: wrong id read back"; return false;
    }
    if (row.field(1).string().as_string() != name) {
        *err = "WU1: wrong name read back"; return false;
    }
    return true;
}

// WU2 -- txn rollback discards the insert.
// (Rollback-discards-insert check; uses per-worker id 82xxx 'cory'.)
static bool WU_TxnRollbackDiscards(brpc::Channel& ch, const std::string& table,
                                   int iter, std::string* err) {
    const int id = 82000 + iter;
    brpc::MysqlResponse resp;
    brpc::MysqlTransactionUniquePtr tx =
        brpc::NewMysqlTransaction(ch, brpc::MysqlTransactionOptions());
    if (tx == NULL) { *err = "WU2: NewMysqlTransaction NULL"; return false; }
    if (!RunInTx(ch, tx.get(),
                 butil::string_printf("INSERT INTO %s VALUES (%d, 'cory')",
                                      table.c_str(), id),
                 &resp, err)) return false;
    if (resp.reply(0).is_error()) {
        *err = "WU2 INSERT err: " + resp.reply(0).error().msg().as_string();
        return false;
    }
    if (!tx->rollback()) { *err = "WU2: rollback failed"; return false; }

    // The rolled-back row must be gone on a fresh connection.
    if (!RunPlain(ch, butil::string_printf("SELECT id FROM %s WHERE id=%d",
                                           table.c_str(), id),
                  &resp, err)) return false;
    if (ResultRowCount(resp) != 0) {
        *err = butil::string_printf(
            "WU2: rolled-back insert still visible (%lld rows)",
            (long long)ResultRowCount(resp));
        return false;
    }
    return true;
}

// WU3 -- a SELECT inside an open txn sees the txn's own uncommitted write.
// (Read-your-own-write check; uses per-worker id 93xxx 'echo'.)
static bool WU_TxnReadsOwnWrite(brpc::Channel& ch, const std::string& table,
                                int iter, std::string* err) {
    const int id = 93000 + iter;
    const char* name = "echo";
    brpc::MysqlResponse resp;
    brpc::MysqlTransactionUniquePtr tx =
        brpc::NewMysqlTransaction(ch, brpc::MysqlTransactionOptions());
    if (tx == NULL) { *err = "WU3: NewMysqlTransaction NULL"; return false; }
    if (!RunInTx(ch, tx.get(),
                 butil::string_printf("INSERT INTO %s VALUES (%d, '%s')",
                                      table.c_str(), id, name),
                 &resp, err)) return false;
    if (resp.reply(0).is_error()) {
        *err = "WU3 INSERT err: " + resp.reply(0).error().msg().as_string();
        return false;
    }
    // Same pinned connection: must see its own uncommitted row.
    if (!RunInTx(ch, tx.get(),
                 butil::string_printf("SELECT name FROM %s WHERE id=%d",
                                      table.c_str(), id),
                 &resp, err)) return false;
    bool ok = (ResultRowCount(resp) == 1) &&
              resp.reply(0).next().field(0).string().as_string() == name;
    // Discard the row so the per-worker table stays empty for the next pick.
    tx->rollback();
    if (!ok) { *err = "WU3: txn did not read its own uncommitted write"; }
    return ok;
}

// WU4 -- prepared: bind one INT param, fetch the matching row.
// (Prepared-bind-int check; each worker seeds its OWN (id 64xxx, 'lyra') row
//  and binds it back so the result is unique per worker.)
static bool WU_PreparedBindInt(brpc::Channel& ch, const std::string& table,
                               int iter, std::string* err) {
    const int id = 64000 + iter;
    const char* name = "lyra";
    brpc::MysqlResponse resp;
    // Seed our own row (autocommit) then read it back via a prepared SELECT.
    if (!RunPlain(ch, butil::string_printf("INSERT INTO %s VALUES (%d, '%s')",
                                           table.c_str(), id, name),
                  &resp, err)) return false;

    brpc::MysqlStatementUniquePtr stmt = brpc::NewMysqlStatement(
        ch, butil::string_printf("SELECT name FROM %s WHERE id=?",
                                 table.c_str()));
    if (stmt == NULL) { *err = "WU4: NewMysqlStatement NULL"; return false; }
    if (stmt->param_count() != 1u) { *err = "WU4: param_count != 1"; return false; }

    brpc::MysqlRequest req(stmt.get());
    if (!req.AddParam((int32_t)id)) { *err = "WU4: AddParam failed"; return false; }
    brpc::Controller cntl;
    ch.CallMethod(NULL, &cntl, &req, &resp, NULL);
    if (cntl.Failed()) { *err = "WU4 rpc: " + cntl.ErrorText(); return false; }
    if (resp.reply_size() < 1 || !resp.reply(0).is_resultset()) {
        *err = "WU4: not a resultset"; return false;
    }
    if (resp.reply(0).row_count() != 1u) {
        *err = "WU4: expected exactly 1 row"; return false;
    }
    bool ok = resp.reply(0).next().field(0).string().as_string() == name;
    // Clean our seeded row so subsequent picks on this table stay exact.
    RunPlain(ch, butil::string_printf("DELETE FROM %s WHERE id=%d",
                                      table.c_str(), id), &resp, err);
    if (!ok) { *err = "WU4: prepared bind returned wrong/cross-talked name"; }
    return ok;
}

// WU5 -- prepared multi-param arithmetic: SELECT ? + ? with two INT params.
// (Two-param arithmetic check; uses per-iteration operands so each worker
//  verifies its OWN sum.)
static bool WU_PreparedArithmetic(brpc::Channel& ch, int worker, int iter,
                                  std::string* err) {
    const int32_t a = 1000 + worker;
    const int32_t b = 7 + iter;
    const long long expect = (long long)a + b;
    brpc::MysqlStatementUniquePtr stmt =
        brpc::NewMysqlStatement(ch, "SELECT CAST(? AS SIGNED) + CAST(? AS SIGNED)");
    if (stmt == NULL) { *err = "WU5: NewMysqlStatement NULL"; return false; }
    if (stmt->param_count() != 2u) { *err = "WU5: param_count != 2"; return false; }

    brpc::MysqlRequest req(stmt.get());
    if (!req.AddParam(a) || !req.AddParam(b)) {
        *err = "WU5: AddParam failed"; return false;
    }
    brpc::MysqlResponse resp;
    brpc::Controller cntl;
    ch.CallMethod(NULL, &cntl, &req, &resp, NULL);
    if (cntl.Failed()) { *err = "WU5 rpc: " + cntl.ErrorText(); return false; }
    if (resp.reply_size() < 1 || !resp.reply(0).is_resultset() ||
        resp.reply(0).row_count() != 1u) {
        *err = "WU5: bad resultset"; return false;
    }
    long long got = 0;
    if (!FieldToLongLong(resp.reply(0).next().field(0), &got) || got != expect) {
        *err = butil::string_printf("WU5: ?+? wrong (got %lld want %lld)",
                                    got, expect);
        return false;
    }
    return true;
}

// Worker driver for ManyWorkersMixedScenarios.
struct MixWorkerArgs {
    brpc::Channel* channel;
    int worker_id;
    std::string table;   // per-worker scratch table
    std::string error;   // first failure (empty == all good)
    int completed;       // iterations completed without error
};

void* MixWorker(void* p) {
    MixWorkerArgs* a = static_cast<MixWorkerArgs*>(p);
    a->error.clear();
    a->completed = 0;
    for (int iter = 0; iter < kIterationsPerWorker; ++iter) {
        // Rotate through the five work units; offset by worker so different
        // workers run different units at the same instant.
        const int pick = (a->worker_id + iter) % 5;
        std::string err;
        bool ok = false;
        switch (pick) {
            case 0: ok = WU_TxnCommitVisible(*a->channel, a->table, iter, &err); break;
            case 1: ok = WU_TxnRollbackDiscards(*a->channel, a->table, iter, &err); break;
            case 2: ok = WU_TxnReadsOwnWrite(*a->channel, a->table, iter, &err); break;
            case 3: ok = WU_PreparedBindInt(*a->channel, a->table, iter, &err); break;
            default: ok = WU_PreparedArithmetic(*a->channel, a->worker_id, iter, &err); break;
        }
        if (!ok) {
            a->error = butil::string_printf("worker %d iter %d pick %d: %s",
                                            a->worker_id, iter, pick, err.c_str());
            return NULL;
        }
        ++a->completed;
    }
    return NULL;
}

// TEST 1: many bthreads, each looping ~50x over a mix of the reused work
// units, on ONE pooled channel capped at 5 sockets.  Surfaces checkout/return
// races, affinity-under-contention bugs, fd_version ABA, and the new-connection
// creation path.
TEST_F(MysqlPoolConcurrencyTest, ManyWorkersMixedScenarios) {
    std::string err;
    std::vector<MixWorkerArgs> args(kWorkers);
    // One scratch table per worker so concurrent workers never share rows.
    for (int w = 0; w < kWorkers; ++w) {
        args[w].channel = &_channel;
        args[w].worker_id = w;
        args[w].table = butil::string_printf("pool_conc_w%d", w);
        brpc::MysqlResponse resp;
        ASSERT_TRUE(RunPlain(_channel, "DROP TABLE IF EXISTS " + args[w].table,
                             &resp, &err)) << err;
        ASSERT_TRUE(RunPlain(_channel,
                             "CREATE TABLE " + args[w].table +
                                 " (id INT PRIMARY KEY, name VARCHAR(32)) "
                                 "ENGINE=InnoDB",
                             &resp, &err)) << err;
    }

    std::vector<bthread_t> threads(kWorkers);
    for (int w = 0; w < kWorkers; ++w) {
        ASSERT_EQ(0, bthread_start_background(&threads[w], NULL, MixWorker,
                                              &args[w]));
    }
    for (int w = 0; w < kWorkers; ++w) {
        bthread_join(threads[w], NULL);
    }

    for (int w = 0; w < kWorkers; ++w) {
        EXPECT_TRUE(args[w].error.empty()) << args[w].error;
        EXPECT_EQ(kIterationsPerWorker, args[w].completed)
            << "worker " << w << " did not finish all iterations";
    }

    // Cleanup.
    for (int w = 0; w < kWorkers; ++w) {
        brpc::MysqlResponse resp;
        RunPlain(_channel, "DROP TABLE IF EXISTS " + args[w].table, &resp, &err);
    }
}

// Focused-check worker A: one transaction, per-worker table, INSERT + SELECT,
// records its pinned SocketId and the value it read.
struct AffinityWorkerArgs {
    brpc::Channel* channel;
    std::string table;
    int id;                    // value inserted (and expected back)
    brpc::SocketId socket_id;  // pinned socket for this txn
    int64_t row_count;
    int read_value;
    bool committed;
    std::string error;
};

void* AffinityWorker(void* p) {
    AffinityWorkerArgs* a = static_cast<AffinityWorkerArgs*>(p);
    a->error.clear();
    a->socket_id = 0;
    a->row_count = -1;
    a->read_value = -1;
    a->committed = false;

    brpc::MysqlTransactionUniquePtr tx =
        brpc::NewMysqlTransaction(*a->channel, brpc::MysqlTransactionOptions());
    if (tx == NULL) { a->error = "NewMysqlTransaction NULL"; return NULL; }
    a->socket_id = tx->GetSocketId();

    brpc::MysqlResponse resp;
    if (!RunInTx(*a->channel, tx.get(),
                 butil::string_printf("INSERT INTO %s VALUES (%d)",
                                      a->table.c_str(), a->id),
                 &resp, &a->error)) return NULL;
    if (resp.reply(0).is_error()) {
        a->error = "INSERT err: " + resp.reply(0).error().msg().as_string();
        return NULL;
    }
    // Read inside the txn: must see exactly our own row (per-worker table).
    if (!RunInTx(*a->channel, tx.get(),
                 butil::string_printf("SELECT v FROM %s", a->table.c_str()),
                 &resp, &a->error)) return NULL;
    a->row_count = ResultRowCount(resp);
    if (a->row_count == 1) {
        long long v = 0;
        if (FieldToLongLong(resp.reply(0).next().field(0), &v)) {
            a->read_value = (int)v;
        }
    }
    a->committed = tx->commit();
    if (!a->committed) a->error = "commit failed";
    return NULL;
}

// TEST 2 (focused check a): two transactions in parallel must hold DIFFERENT
// pinned SocketIds and must not see each other's rows.
TEST_F(MysqlPoolConcurrencyTest, TwoTransactionsHoldDifferentPinnedSockets) {
    const std::string t0 = "pool_conc_affinity_a";
    const std::string t1 = "pool_conc_affinity_b";
    std::string err;
    brpc::MysqlResponse resp;
    for (const std::string& t : {t0, t1}) {
        ASSERT_TRUE(RunPlain(_channel, "DROP TABLE IF EXISTS " + t, &resp, &err)) << err;
        ASSERT_TRUE(RunPlain(_channel,
                             "CREATE TABLE " + t + " (v INT) ENGINE=InnoDB",
                             &resp, &err)) << err;
    }

    for (int iter = 0; iter < kIterationsPerWorker; ++iter) {
        ASSERT_TRUE(RunPlain(_channel, "TRUNCATE TABLE " + t0, &resp, &err)) << err;
        ASSERT_TRUE(RunPlain(_channel, "TRUNCATE TABLE " + t1, &resp, &err)) << err;

        AffinityWorkerArgs a0{&_channel, t0, 30100 + iter, 0, -1, -1, false, ""};
        AffinityWorkerArgs a1{&_channel, t1, 30900 + iter, 0, -1, -1, false, ""};

        bthread_t b0, b1;
        ASSERT_EQ(0, bthread_start_background(&b0, NULL, AffinityWorker, &a0));
        ASSERT_EQ(0, bthread_start_background(&b1, NULL, AffinityWorker, &a1));
        bthread_join(b0, NULL);
        bthread_join(b1, NULL);

        ASSERT_TRUE(a0.error.empty()) << "iter " << iter << " txn0: " << a0.error;
        ASSERT_TRUE(a1.error.empty()) << "iter " << iter << " txn1: " << a1.error;

        // Each saw exactly its own single row -- no cross-talk.
        EXPECT_EQ(1, a0.row_count) << "iter " << iter;
        EXPECT_EQ(1, a1.row_count) << "iter " << iter;
        EXPECT_EQ(a0.id, a0.read_value) << "iter " << iter;
        EXPECT_EQ(a1.id, a1.read_value) << "iter " << iter;
        EXPECT_TRUE(a0.committed) << "iter " << iter;
        EXPECT_TRUE(a1.committed) << "iter " << iter;

        // Connection affinity: two concurrent txns hold DIFFERENT pinned sockets.
        EXPECT_NE(0u, a0.socket_id) << "iter " << iter;
        EXPECT_NE(0u, a1.socket_id) << "iter " << iter;
        EXPECT_NE(a0.socket_id, a1.socket_id)
            << "iter " << iter
            << ": two concurrent transactions shared a pooled socket! sid0="
            << a0.socket_id << " sid1=" << a1.socket_id;
    }

    for (const std::string& t : {t0, t1}) {
        RunPlain(_channel, "DROP TABLE IF EXISTS " + t, &resp, &err);
    }
}

// Focused-check worker B: a prepared statement (SELECT ? + ?) run repeatedly
// in parallel with a transaction; must return its own correct sum each time.
struct PreparedWorkerArgs {
    brpc::Channel* channel;
    int base;              // operand seed
    bool ok;
    std::string error;
};

void* PreparedWorker(void* p) {
    PreparedWorkerArgs* a = static_cast<PreparedWorkerArgs*>(p);
    a->ok = true;
    a->error.clear();
    for (int k = 0; k < 4; ++k) {
        std::string err;
        if (!WU_PreparedArithmetic(*a->channel, a->base, k, &err)) {
            a->ok = false;
            a->error = err;
            return NULL;
        }
    }
    return NULL;
}

// TEST 3 (focused check b): one transaction + one prepared statement in
// parallel each return correct independent results; the prepared path must not
// disturb the transaction's pinned connection.
TEST_F(MysqlPoolConcurrencyTest, TransactionPlusPreparedInParallel) {
    const std::string t = "pool_conc_txn_stmt";
    std::string err;
    brpc::MysqlResponse resp;
    ASSERT_TRUE(RunPlain(_channel, "DROP TABLE IF EXISTS " + t, &resp, &err)) << err;
    ASSERT_TRUE(RunPlain(_channel, "CREATE TABLE " + t + " (v INT) ENGINE=InnoDB",
                         &resp, &err)) << err;

    for (int iter = 0; iter < kIterationsPerWorker; ++iter) {
        ASSERT_TRUE(RunPlain(_channel, "TRUNCATE TABLE " + t, &resp, &err)) << err;

        AffinityWorkerArgs ta{&_channel, t, 50500 + iter, 0, -1, -1, false, ""};
        PreparedWorkerArgs pa{&_channel, 200 + iter, true, ""};

        bthread_t bt, bp;
        ASSERT_EQ(0, bthread_start_background(&bt, NULL, AffinityWorker, &ta));
        ASSERT_EQ(0, bthread_start_background(&bp, NULL, PreparedWorker, &pa));
        bthread_join(bt, NULL);
        bthread_join(bp, NULL);

        ASSERT_TRUE(ta.error.empty()) << "iter " << iter << " txn: " << ta.error;
        ASSERT_TRUE(pa.ok) << "iter " << iter << " prepared: " << pa.error;

        // Transaction result intact and isolated.
        EXPECT_EQ(1, ta.row_count) << "iter " << iter;
        EXPECT_EQ(ta.id, ta.read_value) << "iter " << iter;
        EXPECT_TRUE(ta.committed) << "iter " << iter;
        EXPECT_NE(0u, ta.socket_id) << "iter " << iter;
    }

    RunPlain(_channel, "DROP TABLE IF EXISTS " + t, &resp, &err);
}

// OWNER-PRIORITY CONCURRENCY CHECKS (TEST A / B / C)
//
// These three tests intentionally cap the pool SMALL relative to the number of
// concurrent workers (TEST A/B: 4 sockets vs 8 workers; TEST C: 2 sockets) so
// that workers both CONTEND for pooled sockets and FORCE new-connection
// creation, while transactions RESERVE (pull out) pooled sockets.  They use
// their OWN data namespace (ids in the 120xxx/130xxx/140xxx ranges, names
// "nova"/"zephyr"/"quill") that is distinct from the reused work units above
// (71xxx/82xxx/93xxx/64xxx; aria/cory/echo/lyra) and from the sibling files.
//
// Pool size is set PER-TEST via SetCommandLineOption at the top of each test
// body (tests may share a process, so the previous test's value must not leak
// in).  No ASSERT_* runs inside a bthread; every worker records into its args
// struct and the main thread asserts after join.

static void SetPoolCap(int cap) {
    GFLAGS_NS::SetCommandLineOption("max_connection_pool_size",
                                    std::to_string(cap).c_str());
}

// TEST A worker: one transaction running SEVERAL statements into its OWN
// per-worker scratch table, recording the pinned SocketId seen BEFORE every
// statement so the main thread can check intra-txn pinning and inter-txn
// isolation.
struct PinnedTxnWorkerArgs {
    brpc::Channel* channel;
    std::string table;             // per-worker scratch table
    int id;                        // per-worker row id (unique)
    std::vector<brpc::SocketId> socket_ids;  // GetSocketId() before each stmt
    std::string error;             // empty == ok
};

void* PinnedTxnWorker(void* p) {
    PinnedTxnWorkerArgs* a = static_cast<PinnedTxnWorkerArgs*>(p);
    a->error.clear();
    a->socket_ids.clear();

    brpc::MysqlTransactionUniquePtr tx =
        brpc::NewMysqlTransaction(*a->channel, brpc::MysqlTransactionOptions());
    if (tx == NULL) { a->error = "NewMysqlTransaction NULL"; return NULL; }

    brpc::MysqlResponse resp;

    // Statement 1: INSERT our own row.
    a->socket_ids.push_back(tx->GetSocketId());
    if (!RunInTx(*a->channel, tx.get(),
                 butil::string_printf("INSERT INTO %s VALUES (%d, 'nova')",
                                      a->table.c_str(), a->id),
                 &resp, &a->error)) return NULL;
    if (resp.reply(0).is_error()) {
        a->error = "INSERT err: " + resp.reply(0).error().msg().as_string();
        return NULL;
    }

    // Statement 2: SELECT our own (uncommitted) row back.
    a->socket_ids.push_back(tx->GetSocketId());
    if (!RunInTx(*a->channel, tx.get(),
                 butil::string_printf("SELECT name FROM %s WHERE id=%d",
                                      a->table.c_str(), a->id),
                 &resp, &a->error)) return NULL;
    if (ResultRowCount(resp) != 1 ||
        resp.reply(0).next().field(0).string().as_string() != "nova") {
        a->error = "SELECT-own-row did not read back its own write";
        return NULL;
    }

    // Statement 3: UPDATE our own row.
    a->socket_ids.push_back(tx->GetSocketId());
    if (!RunInTx(*a->channel, tx.get(),
                 butil::string_printf("UPDATE %s SET name='zephyr' WHERE id=%d",
                                      a->table.c_str(), a->id),
                 &resp, &a->error)) return NULL;
    if (resp.reply(0).is_error()) {
        a->error = "UPDATE err: " + resp.reply(0).error().msg().as_string();
        return NULL;
    }

    // Statement 4: SELECT the updated value back.
    a->socket_ids.push_back(tx->GetSocketId());
    if (!RunInTx(*a->channel, tx.get(),
                 butil::string_printf("SELECT name FROM %s WHERE id=%d",
                                      a->table.c_str(), a->id),
                 &resp, &a->error)) return NULL;
    if (ResultRowCount(resp) != 1 ||
        resp.reply(0).next().field(0).string().as_string() != "zephyr") {
        a->error = "SELECT after UPDATE saw wrong value";
        return NULL;
    }

    // Discard so the per-worker table is empty for the next outer-loop pass.
    if (!tx->rollback()) { a->error = "rollback failed"; return NULL; }
    return NULL;
}

// TEST A: ConcurrentTxnsStayPinned  (the most important check)
//
// kTxns overlapping transactions, each on its own bthread, on the POOLED
// channel with the pool capped SMALL (4) relative to kTxns (8) so they contend
// AND force new-connection creation.  Each txn runs 4 statements into its OWN
// scratch table and snapshots tx->GetSocketId() before every statement.
// Asserts (on the main thread, after join):
//   * INTRA-txn: the SocketId is CONSTANT across a transaction's statements
//     (the txn is pinned to one connection);
//   * INTER-txn: the live SocketIds are DISTINCT across the concurrent txns
//     (no two simultaneously-open transactions share a pooled connection).
// The whole thing loops a few times to shake scheduling.
TEST_F(MysqlPoolConcurrencyTest, ConcurrentTxnsStayPinned) {
    const int kTxns = 8;
    SetPoolCap(4);  // SMALL vs kTxns=8: contend + force new connections.

    std::string err;
    brpc::MysqlResponse resp;
    std::vector<std::string> tables(kTxns);
    for (int w = 0; w < kTxns; ++w) {
        tables[w] = butil::string_printf("pool_conc_pin_w%d", w);
        ASSERT_TRUE(RunPlain(_channel, "DROP TABLE IF EXISTS " + tables[w],
                             &resp, &err)) << err;
        ASSERT_TRUE(RunPlain(_channel,
                             "CREATE TABLE " + tables[w] +
                                 " (id INT PRIMARY KEY, name VARCHAR(32)) "
                                 "ENGINE=InnoDB",
                             &resp, &err)) << err;
    }

    for (int loop = 0; loop < 10; ++loop) {
        std::vector<PinnedTxnWorkerArgs> args(kTxns);
        for (int w = 0; w < kTxns; ++w) {
            args[w].channel = &_channel;
            args[w].table = tables[w];
            // Unique per worker AND per loop so rows never collide.
            args[w].id = 120000 + loop * 100 + w;
        }

        std::vector<bthread_t> threads(kTxns);
        for (int w = 0; w < kTxns; ++w) {
            ASSERT_EQ(0, bthread_start_background(&threads[w], NULL,
                                                  PinnedTxnWorker, &args[w]));
        }
        for (int w = 0; w < kTxns; ++w) {
            bthread_join(threads[w], NULL);
        }

        // No worker errored.
        for (int w = 0; w < kTxns; ++w) {
            ASSERT_TRUE(args[w].error.empty())
                << "loop " << loop << " txn " << w << ": " << args[w].error;
            ASSERT_EQ(4u, args[w].socket_ids.size()) << "loop " << loop;
        }

        // INTRA-txn pinning: one constant non-zero SocketId per transaction.
        std::vector<brpc::SocketId> live(kTxns);
        for (int w = 0; w < kTxns; ++w) {
            const brpc::SocketId sid = args[w].socket_ids[0];
            EXPECT_NE(0u, sid) << "loop " << loop << " txn " << w;
            for (size_t s = 1; s < args[w].socket_ids.size(); ++s) {
                EXPECT_EQ(sid, args[w].socket_ids[s])
                    << "loop " << loop << " txn " << w << " stmt " << s
                    << ": transaction was NOT pinned to one connection";
            }
            live[w] = sid;
        }

        // INTER-txn isolation: all live (simultaneously-open) SocketIds distinct.
        for (int i = 0; i < kTxns; ++i) {
            for (int j = i + 1; j < kTxns; ++j) {
                EXPECT_NE(live[i], live[j])
                    << "loop " << loop << ": txns " << i << " and " << j
                    << " shared a pooled connection (sid=" << live[i] << ")";
            }
        }
    }

    for (int w = 0; w < kTxns; ++w) {
        RunPlain(_channel, "DROP TABLE IF EXISTS " + tables[w], &resp, &err);
    }
}

// TEST B workers: an aborting-transaction mix.  Mode 0 explicitly rollback()s
// after an INSERT; mode 1 simply drops the MysqlTransactionUniquePtr WITHOUT
// commit, so its destructor auto-rollbacks (see MysqlTransaction::~ in
// mysql_transaction.h).  Either way the insert must NOT survive.
struct AbortWorkerArgs {
    brpc::Channel* channel;
    std::string table;   // shared abort table
    int id;              // unique id this worker inserts then aborts
    int mode;            // 0 == explicit rollback, 1 == drop -> dtor rollback
    std::string error;
};

void* AbortWorker(void* p) {
    AbortWorkerArgs* a = static_cast<AbortWorkerArgs*>(p);
    a->error.clear();
    {
        brpc::MysqlTransactionUniquePtr tx = brpc::NewMysqlTransaction(
            *a->channel, brpc::MysqlTransactionOptions());
        if (tx == NULL) { a->error = "NewMysqlTransaction NULL"; return NULL; }

        brpc::MysqlResponse resp;
        if (!RunInTx(*a->channel, tx.get(),
                     butil::string_printf("INSERT INTO %s VALUES (%d, 'quill')",
                                          a->table.c_str(), a->id),
                     &resp, &a->error)) return NULL;
        if (resp.reply(0).is_error()) {
            a->error = "INSERT err: " + resp.reply(0).error().msg().as_string();
            return NULL;
        }
        if (a->mode == 0) {
            if (!tx->rollback()) { a->error = "explicit rollback failed"; return NULL; }
        }
        // mode 1: fall off the end of this scope -> tx dtor auto-rollbacks.
    }
    return NULL;
}

// TEST B: ConcurrentTxnAbortAndAutoRollback
//
// Under the same small-pool contended setup, run a concurrent MIX of
// explicitly-rolled-back transactions and dropped-without-commit transactions
// (whose dtor auto-rollbacks).  Exercises the reserve -> return / auto-rollback
// path under contention (the UAF/leak-prone path).  After join, from a fresh
// non-tx connection, assert NONE of the aborted inserts are visible, workers
// saw no errors, and the channel is still healthy (a final pooled SELECT
// succeeds -> reserved connections were returned to the pool cleanly).
TEST_F(MysqlPoolConcurrencyTest, ConcurrentTxnAbortAndAutoRollback) {
    const int kWorkersB = 8;
    SetPoolCap(4);  // SMALL vs 8 workers: contend + force new connections.

    const std::string t = "pool_conc_abort";
    std::string err;
    brpc::MysqlResponse resp;
    ASSERT_TRUE(RunPlain(_channel, "DROP TABLE IF EXISTS " + t, &resp, &err)) << err;
    ASSERT_TRUE(RunPlain(_channel,
                         "CREATE TABLE " + t +
                             " (id INT PRIMARY KEY, name VARCHAR(32)) "
                             "ENGINE=InnoDB",
                         &resp, &err)) << err;

    for (int loop = 0; loop < 6; ++loop) {
        std::vector<AbortWorkerArgs> args(kWorkersB);
        for (int w = 0; w < kWorkersB; ++w) {
            args[w].channel = &_channel;
            args[w].table = t;
            args[w].id = 130000 + loop * 100 + w;  // unique per loop+worker
            args[w].mode = w % 2;  // half explicit rollback, half dtor rollback
        }

        std::vector<bthread_t> threads(kWorkersB);
        for (int w = 0; w < kWorkersB; ++w) {
            ASSERT_EQ(0, bthread_start_background(&threads[w], NULL,
                                                  AbortWorker, &args[w]));
        }
        for (int w = 0; w < kWorkersB; ++w) {
            bthread_join(threads[w], NULL);
        }

        for (int w = 0; w < kWorkersB; ++w) {
            EXPECT_TRUE(args[w].error.empty())
                << "loop " << loop << " worker " << w << " (mode "
                << args[w].mode << "): " << args[w].error;
        }

        // Effects discarded: not a single aborted insert is visible from a
        // fresh (non-tx) pooled connection.
        ASSERT_TRUE(RunPlain(_channel,
                             "SELECT COUNT(*) FROM " + t, &resp, &err)) << err;
        long long visible = -1;
        ASSERT_EQ(1, ResultRowCount(resp)) << "loop " << loop;
        ASSERT_TRUE(FieldToLongLong(resp.reply(0).next().field(0), &visible));
        EXPECT_EQ(0, visible)
            << "loop " << loop << ": " << visible
            << " aborted/dropped insert(s) leaked into the table";
    }

    // Channel still healthy: a final simple SELECT on a fresh pooled request
    // succeeds -> the reserved connections were returned to the pool cleanly.
    ASSERT_TRUE(RunPlain(_channel, "SELECT 1", &resp, &err)) << err;
    EXPECT_EQ(1, ResultRowCount(resp));

    RunPlain(_channel, "DROP TABLE IF EXISTS " + t, &resp, &err);
}

// TEST C support: a transaction that RESERVES a pooled connection (pulls it out
// of the pool) and holds it for the lifetime of the worker, so that the
// prepared statement S is forced onto connections that may not have its
// server-side stmt_id cached.
struct ReserveWorkerArgs {
    brpc::Channel* channel;
    std::string error;
};

void* ReserveWorker(void* p) {
    ReserveWorkerArgs* a = static_cast<ReserveWorkerArgs*>(p);
    a->error.clear();
    // Open a few short transactions in series; each pulls a pooled connection
    // out (reserve) and returns it (rollback), churning which sockets are in
    // the pool while S is being executed concurrently.
    for (int k = 0; k < 8; ++k) {
        brpc::MysqlTransactionUniquePtr tx = brpc::NewMysqlTransaction(
            *a->channel, brpc::MysqlTransactionOptions());
        if (tx == NULL) { a->error = "reserve: NewMysqlTransaction NULL"; return NULL; }
        brpc::MysqlResponse resp;
        if (!RunInTx(*a->channel, tx.get(), "SELECT 1", &resp, &a->error)) return NULL;
        if (!tx->rollback()) { a->error = "reserve: rollback failed"; return NULL; }
    }
    return NULL;
}

// Execute a shared prepared statement S with a fresh INT param and verify the
// bound value comes back correctly.  S is shared across bthreads, so it lands
// on whatever pooled connection is currently free.
struct StmtExecWorkerArgs {
    brpc::Channel* channel;
    brpc::MysqlStatement* stmt;  // shared prepared statement S
    int base;                    // param seed (unique per worker)
    std::string error;
};

void* StmtExecWorker(void* p) {
    StmtExecWorkerArgs* a = static_cast<StmtExecWorkerArgs*>(p);
    a->error.clear();
    for (int k = 0; k < 12; ++k) {
        const int32_t v = a->base + k;
        brpc::MysqlRequest req(a->stmt);
        if (!req.AddParam(v)) { a->error = "AddParam failed"; return NULL; }
        brpc::MysqlResponse resp;
        brpc::Controller cntl;
        a->channel->CallMethod(NULL, &cntl, &req, &resp, NULL);
        if (cntl.Failed()) { a->error = "rpc: " + cntl.ErrorText(); return NULL; }
        if (resp.reply_size() < 1 || !resp.reply(0).is_resultset() ||
            resp.reply(0).row_count() != 1u) {
            a->error = "bad resultset for S"; return NULL;
        }
        long long got = 0;
        if (!FieldToLongLong(resp.reply(0).next().field(0), &got) || got != v) {
            a->error = butil::string_printf(
                "S returned wrong value (got %lld want %d)", got, v);
            return NULL;
        }
    }
    return NULL;
}

// TEST C: PreparedRePreparesWhenConnectionStolen
//
// MECHANISM: a server-side prepared statement id is per-(SocketId, fd_version)
// -- it is meaningful only on the exact connection that ran COM_STMT_PREPARE
// (see MysqlStatement::StatementId(SocketId)/SetStatementId(SocketId,...) in
// mysql_statement.h, and the fd_version/SocketId ABA discussion in this file's
// header).  When a shared MysqlStatement S lands on a pooled connection that
// does NOT have S's stmt_id cached, brpc must transparently issue a fresh
// COM_STMT_PREPARE on that connection before COM_STMT_EXECUTE, and the bound
// result must still be correct.
//
// SETUP: pool capped at 2.  S = "SELECT CAST(? AS SIGNED) AS v" is created and
// executed once (caching its stmt_id on whatever connection it first landed
// on).  Then background bthreads open transactions that RESERVE the two pooled
// connections (pulling S's original connection out), while other bthreads keep
// executing S with fresh params.  Looped with churn so S repeatedly hits
// connections it was not prepared on.  Every execute must still return the
// correct bound value; per-worker errors are recorded and asserted empty after
// join.
TEST_F(MysqlPoolConcurrencyTest, PreparedRePreparesWhenConnectionStolen) {
    SetPoolCap(2);  // tiny pool: 2 sockets, easy to "steal" via reserving txns.

    std::string err;
    brpc::MysqlResponse resp;

    brpc::MysqlStatementUniquePtr S =
        brpc::NewMysqlStatement(_channel, "SELECT CAST(? AS SIGNED) AS v");
    ASSERT_TRUE(S != NULL);
    ASSERT_EQ(1u, S->param_count());

    // Execute S once to cache its stmt_id on whatever connection it lands on.
    {
        brpc::MysqlRequest req(S.get());
        ASSERT_TRUE(req.AddParam((int32_t)140000));
        brpc::Controller cntl;
        _channel.CallMethod(NULL, &cntl, &req, &resp, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_TRUE(resp.reply_size() >= 1 && resp.reply(0).is_resultset());
        long long got = 0;
        ASSERT_TRUE(FieldToLongLong(resp.reply(0).next().field(0), &got));
        ASSERT_EQ(140000, got);
    }

    const int kExecutors = 4;
    const int kReservers = 3;
    for (int loop = 0; loop < 30; ++loop) {
        std::vector<StmtExecWorkerArgs> exec_args(kExecutors);
        std::vector<ReserveWorkerArgs> res_args(kReservers);
        std::vector<bthread_t> exec_threads(kExecutors);
        std::vector<bthread_t> res_threads(kReservers);

        for (int w = 0; w < kReservers; ++w) {
            res_args[w].channel = &_channel;
            ASSERT_EQ(0, bthread_start_background(&res_threads[w], NULL,
                                                  ReserveWorker, &res_args[w]));
        }
        for (int w = 0; w < kExecutors; ++w) {
            exec_args[w].channel = &_channel;
            exec_args[w].stmt = S.get();
            // Unique param ranges per worker+loop so a wrong value is unambiguous.
            exec_args[w].base = 141000 + loop * 1000 + w * 100;
            ASSERT_EQ(0, bthread_start_background(&exec_threads[w], NULL,
                                                  StmtExecWorker, &exec_args[w]));
        }
        for (int w = 0; w < kReservers; ++w) {
            bthread_join(res_threads[w], NULL);
        }
        for (int w = 0; w < kExecutors; ++w) {
            bthread_join(exec_threads[w], NULL);
        }

        for (int w = 0; w < kReservers; ++w) {
            EXPECT_TRUE(res_args[w].error.empty())
                << "loop " << loop << " reserver " << w << ": " << res_args[w].error;
        }
        for (int w = 0; w < kExecutors; ++w) {
            EXPECT_TRUE(exec_args[w].error.empty())
                << "loop " << loop << " executor " << w << ": " << exec_args[w].error;
        }
    }
}

}  // namespace
