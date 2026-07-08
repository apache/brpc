# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Apache bRPC is an industrial-grade C++ RPC framework supporting multiple protocols (baidu_std, HTTP/H2, gRPC, thrift, redis, memcached, RTMP, RDMA) on the same port. Used in high-performance systems: search, storage, ML, ads, recommendations. Current version: 1.17.0.

## Build Commands

### Make (primary)

```bash
# Generate config (required before first build)
./config_brpc.sh --headers=/usr/include --libs=/usr/lib

# Build library (produces libbrpc.a and libbrpc.so/dylib)
make -j$(nproc)

# Build debug version (with UNIT_TEST flag, no NDEBUG)
make debug
```

Config options: `--with-glog`, `--with-thrift`, `--with-rdma`, `--with-asan`, `--werror`

### CMake

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Key CMake options: `-DWITH_GLOG=ON`, `-DWITH_THRIFT=ON`, `-DWITH_RDMA=ON`, `-DWITH_ASAN=ON`, `-DBUILD_UNIT_TESTS=ON`, `-DDOWNLOAD_GTEST=ON`

### Bazel

```bash
bazel build -- //... -//example/...
```

## Running Tests

Tests use Google Test. Build and run with Make:

```bash
cd test
make -j$(nproc)
./run_tests.sh          # runs all: test_butil, test_bvar, bthread_*unittest, brpc_*unittest
```

Run a single test binary:

```bash
cd test
./<test_binary>                    # e.g. ./test_butil
./<test_binary> --gtest_filter='TestSuite.TestName'   # single test case
```

With CMake:

```bash
cmake .. -DBUILD_UNIT_TESTS=ON -DDOWNLOAD_GTEST=ON
make -j$(nproc) && ctest
```

ASAN is used in CI: `ASAN_OPTIONS="detect_leaks=0:detect_stack_use_after_return=1" ./<test_binary>`

## Architecture

### Core Libraries (under `src/`)

- **brpc/** — The RPC framework. Three core abstractions:
  - `Server` — listens on a port, dispatches requests to registered services. Supports multiple protocols on the same port via protocol detection.
  - `Channel` — client-side stub for sending RPCs. Configured with naming service, load balancer, timeout, retry policies via `ChannelOptions`.
  - `Controller` — per-RPC context carrying request metadata, error state, timeout, attachments. Extends `google::protobuf::RpcController`.
  - `Socket` (`socket.h`) — low-level connection abstraction managing fd lifecycle, SSL, and write buffering. Uses `VersionedRefWithId` for safe concurrent access.

- **bthread/** — M:N user-level threading (the concurrency foundation of brpc)
  - `TaskControl` manages a pool of worker pthreads, each running a `TaskGroup`
  - `TaskGroup` owns a local run queue + work-stealing queue (`work_stealing_queue.h`, lock-free CAS-based)
  - `bthread_start_urgent()` runs task immediately on current worker; `bthread_start_background()` enqueues for later scheduling
  - Most brpc callbacks execute in bthreads, not pthreads

- **butil/** — Base utility library (originally forked from Chromium)
  - `IOBuf` (`iobuf.h`) — zero-copy buffer using reference-counted blocks with SmallView (2 inline BlockRefs) / BigView (heap) optimization. Core data structure for network I/O.
  - Also: FlatMap, logging, string utils, time, files, containers
  - `third_party/` — Bundled snappy, murmurhash3, symbolize, etc.

- **bvar/** — Multi-dimensional statistics variables
  - Thread-local aggregation via `AgentCombiner` for lock-free counters
  - Types: `Adder`, `Recorder`, `LatencyRecorder`, `PassiveStatus`
  - Composable windows: `PerSecond<Adder<>>`, `Window<>`
  - Auto-exposed via builtin `/vars` endpoint

- **json2pb/** — Bidirectional JSON <-> Protobuf conversion

- **mcpack2pb/** — MCPack format <-> Protobuf conversion (Baidu legacy)

### Key Design Patterns

- **Protocol plugins** (`src/brpc/policy/`): Each protocol implements the `Protocol` struct (function pointers for Parse/Serialize/Pack/Process/Verify in `protocol.h`), registered via `RegisterProtocol()`. 18 protocols implemented. Adding a new protocol does not touch core files — see `docs/en/new_protocol.md`.
- **Naming services / Load balancers**: Pluggable via `NamingService` and `LoadBalancer` interfaces (DNS, ZK, etcd, round-robin, consistent hashing, locality-aware, etc.)
- **Builtin services** (`src/brpc/builtin/`): Every brpc server auto-exposes debug endpoints — `/status`, `/vars`, `/flags`, `/rpcz`, `/hotspots` (cpu/heap/contention profilers).
- **Error model**: Functions return 0/-1 with errno; `Controller::SetFailed()` for RPC-level errors; custom error codes defined in `errno.proto`.
- **Memory patterns**: `ResourcePool` / `ObjectPool` for socket and bthread recycling; `butil::intrusive_ptr` for hot-path reference counting; IOBuf for zero-copy I/O.

## Code Style

- Google C++ Style Guide with **4-space indentation**
- Protocol-specific code goes in `src/brpc/policy/`, not in core files like `server.cpp` or `channel.cpp`
- General modifications should not be hidden inside protocol-specific files
- All changes require unit tests
- CI runs on GitHub Actions (Linux gcc/clang, macOS) — must pass before merge
- Uses C++11 minimum; some C++17 features with compiler guards. Legacy patterns (`DISALLOW_COPY_AND_ASSIGN` macro) still prevalent.

## Dependencies

Required: protobuf (3.x–21.x), gflags, leveldb, openssl. Optional: glog, thrift, gperftools (tcmalloc/profiler), gtest (for tests), libunwind, abseil-cpp, BoringSSL (alternative to openssl).

## Examples

30+ examples in `example/` covering echo, HTTP, gRPC, streaming, redis, memcache, thrift, RDMA, coroutine, etc. Each has its own Makefile/CMakeLists.txt/BUILD.
