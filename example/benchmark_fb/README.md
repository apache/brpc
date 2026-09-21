# FlatBuffers RPC example

[English enablement guide](../../docs/en/flatbuffers.md) |
[中文启用与验证指南](../../docs/cn/flatbuffers.md)

This directory contains independent client and server programs using the
`fb_rpc` protocol, generated FlatBuffers services, and
`brpc::flatbuffers::Message`. The directory name is retained for continuity;
this is a **bounded functional example**, not a performance comparison.

The schema declares `BenchmarkService.Echo` with stable wire method ID **7**.
The generated stub calls `Channel::FBCallMethod`; the server registers the
generated service with `Server::AddFlatBuffersService`.

## Prerequisites

Use an existing local C++ toolchain and matching development installations of
Protobuf, gflags, leveldb, OpenSSL, zlib, and FlatBuffers. The runtime is not
Protobuf-free: its controller/closure interfaces and other bRPC components still
need Protobuf. The projects require CMake 3.16 or newer; the CTest commands below
use CMake/CTest 3.17+ for `--no-tests=error`. With 3.16, run `smoke.py` directly.
Use a single-configuration generator (Unix Makefiles or Ninja) and Python 3 for
the smoke test. The commands below target
Linux and macOS and do not download dependencies.

The runtime needs FlatBuffers headers, but building `brpc_flatc` also needs the
official `libflatbuffers`. The official `flatc` generates table types; it cannot
replace `brpc_flatc`, which generates bRPC services. The repository's ON gate pins
FlatBuffers 25.2.10; use a matching complete installation to reproduce that gate.

Use the **same FlatBuffers release** for the runtime headers, official `flatc`,
and the headers/library used to build `brpc_flatc`. Use the same Protobuf
installation and compatible compiler/ABI for the runtime and this example.
When CMake detects `Protobuf_VERSION > 4.21`, the example requires C++17 and a
Protobuf CMake config package exporting its transitive Abseil dependencies.
An additional dependency prefix can be appended to `CMAKE_PREFIX_PATH` with a
semicolon.

## Build runtime, generator, then example

Run from a **writable** brpc checkout. Replace `DEPS`, `FB`, and `OPENSSL` with
local installation prefixes containing `include/` and `lib/` (or `lib64/`); they
may be the same directory. Set `CC`/`CXX` to a working compiler before configuring
any build. On macOS, use a consistent compiler, SDK and architecture, and pass
the actual OpenSSL prefix rather than assuming an Intel Homebrew path.

Example bindings and build products stay under `WORK`. However, the root brpc
configuration also generates `src/butil/config.h` in the checkout. A read-only
source mount will fail, and separate build directories alone do not isolate
concurrent ON/OFF configurations. Use separate writable checkouts for those.

The policy argument is an optional compatibility setting for older dependencies
that report unsupported policies with CMake 4. It does not replace the required
compiler/dependency versions or enable FlatBuffers by itself.

```sh
REPO="$PWD"
DEPS=/absolute/path/to/dependency-prefix
FB=/absolute/path/to/flatbuffers-prefix
OPENSSL=/absolute/path/to/openssl-prefix
WORK="$(mktemp -d "${TMPDIR:-/tmp}/brpc-benchmark-fb.XXXXXX")"
JOBS=2
printf 'WORK=%s\n' "$WORK"

cmake -S "$REPO" -B "$WORK/runtime" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_PREFIX_PATH="$DEPS;$FB;$OPENSSL" \
  -DOPENSSL_ROOT_DIR="$OPENSSL" \
  -DWITH_FLATBUFFERS=ON -DBUILD_SHARED_LIBS=OFF \
  -DFLATBUFFERS_INCLUDE_DIR="$FB/include" \
  -DBUILD_UNIT_TESTS=OFF -DDOWNLOAD_GTEST=OFF -DBUILD_BRPC_TOOLS=OFF
cmake --build "$WORK/runtime" --parallel "$JOBS"

cmake -S "$REPO/tools/flatbuffers" -B "$WORK/codegen" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH="$FB;$DEPS" \
  -DFLATBUFFERS_INCLUDE_DIR="$FB/include"
cmake --build "$WORK/codegen" --parallel "$JOBS"

cmake -S "$REPO/example/benchmark_fb" -B "$WORK/example" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$DEPS;$FB;$OPENSSL" \
  -DOPENSSL_ROOT_DIR="$OPENSSL" \
  -DBRPC_ROOT="$WORK/runtime/output" \
  -DFLATBUFFERS_INCLUDE_DIR="$FB/include" \
  -DFLATC_EXECUTABLE="$FB/bin/flatc" \
  -DBRPC_FLATC_EXECUTABLE="$WORK/codegen/brpc_flatc"
cmake --build "$WORK/example" --parallel "$JOBS"
(cd "$WORK/example" && ctest -V --no-tests=error --output-on-failure -R '^benchmark_fb_smoke$')
```

Success must include the named `benchmark_fb_smoke` test, exit status 0, and:

```text
benchmark_fb smoke passed: 13 verified replies, 2 schema rejections, clean shutdown
```

`No tests were found` is not a successful verification. Configure with
`-DBUILD_TESTING=ON`, build the example, and run CTest from the example build
folder, not the brpc runtime folder.

`BRPC_ROOT` can instead point to an already-built FB ON `output/` directory or
an installed prefix containing `include/` and `lib/` (or `lib64/`). The example
reads that prefix's real `include/butil/config.h` and rejects an FB OFF build.
It never generates a substitute configuration header or defines
`BRPC_WITH_FLATBUFFERS` to pretend the library supports this feature.

The official `flatc` version must match `FLATBUFFERS_INCLUDE_DIR`; configuration
fails early on a mismatch. `BRPC_FLATC_EXECUTABLE` is the brpc generator, **not**
the official `flatc`. Both generators run during the example build and write
only to `build-directory/generated/`. Do not commit `*_generated.h` or
`*.brpc.fb.h` / `*.brpc.fb.cpp`.

The default links `libbrpc.a`. `-DLINK_SO=ON` selects an **already built** shared
library; it does not create one. After the commands above, a shared-library
variant can be built and checked with:

```sh
cmake -S "$REPO" -B "$WORK/runtime" -DBUILD_SHARED_LIBS=ON
cmake --build "$WORK/runtime" --target brpc-shared --parallel "$JOBS"
cmake -S "$REPO/example/benchmark_fb" -B "$WORK/example" -DLINK_SO=ON
cmake --build "$WORK/example" --parallel "$JOBS"
(cd "$WORK/example" && ctest -V --no-tests=error --output-on-failure -R '^benchmark_fb_smoke$')
```

These reconfigure commands reuse the previously populated CMake caches. For a
fresh build directory, pass the complete configuration options again. Optional
runtime features may require extra dependencies via
`-DBRPC_EXTRA_LIBRARIES='library1;library2'`. Use `CMAKE_CXX_COMPILER` consistently
across all three builds when a non-default compiler is required. An FB ON header
and an FB OFF binary mixed into one prefix are not a supported installation;
rebuild or reinstall that prefix rather than overriding feature macros.

## Reuse an existing FB ON runtime

Skip the runtime build when a compatible library already exists. Set `REPO`,
`DEPS`, `FB`, `OPENSSL`, `WORK`, and `JOBS` as above; build `brpc_flatc` if needed.
In the example configure command, replace `-DBRPC_ROOT="$WORK/runtime/output"`
with your existing output/install prefix and set `BRPC_FLATC_EXECUTABLE` to the
matching generator. All remaining configure, build and smoke steps are the same.
A Make build exports `output/`; a raw Bazel build directory is not an installed
prefix with the layout expected by `BRPC_ROOT`.

## Run separately

Run the server and client on the **same host/container**. This example rejects
non-loopback addresses. In the build terminal:

```sh
"$WORK/example/benchmark_fb_server" --listen_addr=127.0.0.1:0 --duration_s=300
```

It prints a flushed `BRPC_FB_READY 127.0.0.1:<selected-port>` line only after
`Server::Start` succeeds and termination handlers are installed. Use that actual
endpoint in another terminal. Shell variables are not shared between terminals:
set `WORK` to the absolute path printed by the build and replace `PORT_FROM_READY`
with the numeric port. The server exits after 300 seconds, or earlier on Ctrl-C.

```sh
WORK=/absolute/path/printed/by/the/build
SERVER=127.0.0.1:PORT_FROM_READY
"$WORK/example/benchmark_fb_client" --server="$SERVER" \
  --request_count=16 --thread_num=2 --request_size=8193 --attachment_size=257
"$WORK/example/benchmark_fb_client" --server="$SERVER" \
  --request_count=2 --thread_num=1 --request_size=0 --omit_message=true
"$WORK/example/benchmark_fb_client" --server="$SERVER" \
  --request_count=2 --thread_num=1 --corrupt_request=true
"$WORK/example/benchmark_fb_client" --server="$SERVER" \
  --request_count=2 --thread_num=1
```

The first command must exit 0 and print:

```json
{"completed":16,"successes":16,"expected_rejections":0,"failures":0}
```

The corrupt-request command must also exit 0, but with a different result:

```json
{"completed":2,"successes":0,"expected_rejections":2,"failures":0}
```

The final normal command verifies recovery against the same server and must
report `completed=2`, `successes=2`, `expected_rejections=0`, `failures=0`.

The client always verifies the response schema **before** reading it, then
compares the request ID, every string byte, optional-string presence, attachment
size, and attachment bytes. Both empty and absent strings are supported.
`request_size` means string bytes, not total serialized frame size. Payloads and
attachments contain deterministic binary bytes, including zero bytes.

`--corrupt_request=true` damages the FlatBuffers root offset but leaves FRPC
framing valid. Exit status 0 in this mode means every request received the
expected generated-service schema rejection (`RpcController::SetFailed(string)`,
error code -1). A connection error, timeout, accepted request, or unexpected
response is a failure, not a successful negative test. JSON output distinguishes
`successes` from `expected_rejections`; `failures` is a 0/1 failure flag, not a
failed-RPC count. It contains no throughput estimate.

### Bounded controls

| Client option | Default | Allowed |
| --- | --- | --- |
| `server` | required | `127.0.0.1:<nonzero-port>` |
| `request_count` | 16 | 1..1000 total, not per thread |
| `thread_num` | 2 | 1..16 synchronous callers |
| `request_size` | 64 | 0..1048576 string bytes |
| `attachment_size` | 0 | 0..1048576 bytes |
| `timeout_ms` | 1500 | 1..5000 per RPC |
| `deadline_ms` | 30000 | 1..120000 for the client run |
| `connection_type` | `single` | `single`, `pooled`, `short` |
| `omit_message` | false | Preserve absent rather than empty string |
| `corrupt_request` | false | Require schema rejection |

The server defaults to `127.0.0.1:0`, `duration_s=60` (1..3600), and
`max_concurrency=32` (1..64). It stops earlier on SIGINT/SIGTERM and calls
`Stop`/`Join`. Both programs deliberately accept only IPv4 loopback endpoints.
Builtin services are disabled. FRPC does not provide authentication,
compression, checksums, or streaming; none is enabled by this example. It is not
a production/public-network deployment configuration.

## Smoke test behavior

CTest invokes the standard-library-only `smoke.py`. It:

1. Starts its own server on `127.0.0.1:0` and reads the readiness endpoint.
2. Verifies finite RPCs with binary attachments, empty and absent strings, and
   single/pooled/short connections.
3. Requires two malformed-schema requests to be rejected.
4. Verifies subsequent normal requests against the same server.
5. Terminates and reaps only the server process it started.

There are 13 verified replies and 2 expected rejections. Readiness and client
subprocesses have deadlines, the orchestration deadline is 35 seconds, and
CTest has a 50-second outer timeout. Shutdown allows 3 seconds before killing
and reaping a stuck server; requiring that fallback fails the smoke. No fixed
port or external server is needed, and the owned server is reaped before return.
Generated code remains only in the build directory; the smoke does not modify
source files.

For OFF-prefix rejection, version mismatch, missing libraries, empty CTest
runs, or connection failures, see the enablement guide's
[troubleshooting table](../../docs/en/flatbuffers.md#troubleshooting).

The same test can be invoked without CTest:

```sh
python3 "$REPO/example/benchmark_fb/smoke.py" \
  --server "$WORK/example/benchmark_fb_server" \
  --client "$WORK/example/benchmark_fb_client"
```
