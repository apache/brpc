# MySQL auth handshake — end-to-end test plan

The server integration tests in `brpc_mysql_auth_handshake_unittest.cpp`
(`MysqlHandshakeServerTest.*`) run in one of two modes, selected by the
`-mysql_use_running_server` gflag.

There are four server tests:

| Test | What it checks |
|---|---|
| `ParsesRealServerGreeting` | HandshakeV10 parse of a real greeting |
| `GeneratesScramblesFromRealSalt` | scramble from a real salt, parameterized on password length (zero → empty response; non-zero → 20B native / 32B caching_sha2) |
| `PerformsFullAuthentication` | uncached login takes the **full-auth** path; asserts the response carries `AuthMoreData 0x04` (perform_full_authentication) and the RSA exchange yields `OK` |
| `CachesCredentialOnSecondLogin` | logs in twice; the **second** login must reuse the cache (fast-auth), never `0x04` |

## Mode 1 — self-spawned server (default; CI)

When `-mysql_use_running_server` is **not** set, the fixture brings up its
own throwaway `mysqld` (the `which`-then-spawn pattern from
`brpc_redis_unittest.cpp`) with an empty-password root, and tears it down
on exit. `caching_sha2_password` then completes via its empty-password
fast path. `PerformsFullAuthentication` skips here (an empty password
never triggers full auth); the other three run. Tests self-skip entirely
when `mysqld` is absent.

```sh
cd test && ./brpc_mysql_auth_handshake_unittest
```

## Mode 2 — already-running server (recommended for development & future CLs)

You start a `mysqld` yourself, with verbose logging so you can watch the
handshake, and point the tests at it with flags. The test neither starts
nor stops it. Reuse this workflow as more of the MySQL protocol lands
(text protocol, prepared statements, transactions).

### 1. Initialize a data directory (one time per fresh instance)

```sh
export MYSQL_DATA=/tmp/brpc_mysql_e2e
export MYSQL_PORT=13306
rm -rf "$MYSQL_DATA" && mkdir -p "$MYSQL_DATA"
mysqld --initialize-insecure --datadir="$MYSQL_DATA" --log-error="$MYSQL_DATA/init.err"
```

### 2. Start the server in your terminal (verbose, foreground)

```sh
mysqld --datadir="$MYSQL_DATA" --port="$MYSQL_PORT" \
       --socket="$MYSQL_DATA/mysqld.sock" --bind-address=127.0.0.1 \
       --mysqlx=OFF --log-error-verbosity=3 \
       --general-log=1 --general-log-file="$MYSQL_DATA/general.log"
```

### 3. Create the `root` / `root` account reachable over TCP

```sh
mysql --socket="$MYSQL_DATA/mysqld.sock" -u root <<'SQL'
ALTER USER 'root'@'localhost' IDENTIFIED WITH caching_sha2_password BY 'root';
CREATE USER IF NOT EXISTS 'root'@'%' IDENTIFIED WITH caching_sha2_password BY 'root';
GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' WITH GRANT OPTION;
DELETE FROM mysql.user WHERE user='';
FLUSH PRIVILEGES;
SQL
```

### 4. Run the tests against that server

```sh
cd test && ./brpc_mysql_auth_handshake_unittest \
    -mysql_use_running_server \
    -mysql_host=127.0.0.1 -mysql_port=13306 \
    -mysql_user=root -mysql_password=root
```

`PerformsFullAuthentication` requires a **cold** caching_sha2 cache — i.e.
a credential that has not authenticated since the server started. It is
the first authenticating test, so against a **freshly started** server it
sees the full-auth path. If you re-run without restarting the server, the
credential is already cached and that test will report fast-auth; restart
the server (or use a never-authenticated account) to exercise full auth
again.

## Flags

| Flag | Default | Meaning |
|---|---|---|
| `-mysql_use_running_server` | `false` | `true` → use an already-running server (no spawn/teardown); `false` → self-spawn |
| `-mysql_host` | `127.0.0.1` | running-server host |
| `-mysql_port` | `13306` | server TCP port (running server, and the port the spawned server binds) |
| `-mysql_user` | `root` | login user |
| `-mysql_password` | (empty) | login password |
