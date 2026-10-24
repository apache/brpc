# Standalone FlatBuffers service generator

For runtime enablement and a complete client/server check, see the
[English guide](../../docs/en/flatbuffers.md) or
[中文指南](../../docs/cn/flatbuffers.md).

`brpc_flatc` generates bRPC service bindings. The separate `fb_rpc` transport
implements their channel interface in `brpc::Channel`. The generator uses the installed, official
`flatbuffers/idl.h` Parser and `libflatbuffers`; it does not download or modify a
FlatBuffers fork. The official `flatc --cpp` remains responsible for table types.

## Build and generate

The tool needs a C++11 compiler, CMake, and official FlatBuffers **25.2.10**
headers/library. Use the matching `flatc` with default `--cpp` generation;
options that change C++ naming, such as `--scoped-enums`, are not modeled by
this generator. To build only the generator:

```sh
cmake -S tools/flatbuffers -B /tmp/brpc-codegen-build -DBUILD_TESTING=OFF
cmake --build /tmp/brpc-codegen-build -j2
mkdir -p /tmp/brpc-generated
flatc --cpp -o /tmp/brpc-generated test/flatbuffers_codegen/echo.fbs
/tmp/brpc-codegen-build/brpc_flatc -o /tmp/brpc-generated test/flatbuffers_codegen/echo.fbs
```

For a non-default installation, add `-DCMAKE_PREFIX_PATH=/path/to/flatbuffers`
and/or explicit `FLATBUFFERS_INCLUDE_DIR` and `FLATBUFFERS_LIBRARY` values.
Run official `flatc` from that matching installation, not an unrelated binary
found earlier on `PATH`. The runtime alone does not link `libflatbuffers`, but
this generator needs it.

The output directory must already exist. The command line is:

```text
brpc_flatc [-I include_dir]... [-o existing_output_dir] schema.fbs
```

For `echo.fbs`, the two tools produce:

- Official `flatc`: `echo_generated.h`.
- `brpc_flatc`: `echo.brpc.fb.h` and `echo.brpc.fb.cpp`, both Apache-licensed.

Compile the generated `.cpp` into the application with C++14 or newer and bRPC
configured with FlatBuffers support. When the detected `Protobuf_VERSION` is
greater than 4.21, use C++17 and the matching Protobuf/Abseil dependency set. Keep the generated header, official table header, and bRPC
headers on the include path. The generated service files do not pin a
FlatBuffers version; use an official `flatc` matching your installed table
headers, as required by official generated code.

Schema includes are resolved relative to the input file and through repeated
`-I` options (`-Idir` also works). Run official `flatc --cpp` for each included
schema to obtain its table header. Run `brpc_flatc` separately for each schema
that declares services; imported services are not emitted again. Pass the same
include directories to both tools. This tool processes one `.fbs` per invocation
and follows the official default `*_generated.h` naming convention.

## Explicit, stable IDs

```fbs
namespace example.api;

table Request { text:string; }
table Response { text:string; }

rpc_service Echo {
    Repeat(Request):Response (id: 41);
    Inspect(Request):Response (id: 7);
}
```

Every RPC method must have an explicit integer `(id: N)` in `[0, 2147483647]`.
IDs must be unique **within a service**. Missing, negative, duplicate, quoted,
or out-of-range IDs fail generation with a diagnostic and nonzero exit status.
Reordering declarations never changes their wire IDs. This does not require
explicit IDs on FlatBuffers table fields.

The generated names are `example::api::Echo`,
`example::api::Echo_Stub`, and the alias `example::api::Echo::Stub`.
They are **not** the old `EchoStub` spelling. Methods take protobuf
`RpcController`/`Closure` and `brpc::flatbuffers::Message` parameters. Derive from
`Echo` and override the schema methods; supply an implementation of
`brpc::flatbuffers::RpcChannel` to a stub, such as a `brpc::Channel` initialized
with `ChannelOptions::protocol = "fb_rpc"`. Register the implementation with
`Server::AddFlatBuffersService`. See [FlatBuffers RPC](../../docs/en/flatbuffers.md)
for ownership, protocol limitations, and response verification. Channel ownership
defaults to borrowed; `Service::STUB_OWNS_CHANNEL` transfers ownership to the stub.

This intentionally bounded generator supports unary table RPCs, multiple
services/methods, namespaces, included request/response tables, and absent
optional strings. Streaming and C++ keyword names in service/type/namespace
positions are rejected rather than silently misgenerated. Method names that
collide with generated service APIs are also rejected. Services cannot be named
`Stub`, `descriptor`, `GetDescriptor`, or `FBCallMethod`, because these names
are also members of the generated service class. Generated service and stub
class names must be distinct from other services, schema types, table builder
classes, unscoped enum values, union traits, table creation functions, and
namespace prefixes, including those in imported schemas. C++ reserved
identifiers are rejected in the scopes where they are reserved. A service name
ending in `_` is also rejected because its `_Stub` suffix would produce `__`.
Global service names also cannot shadow
namespaces introduced by the generated header (`brpc`, `butil`, `flatbuffers`,
`google`, or `std`). Schema file basenames may contain ASCII letters, digits,
underscores, dots, and hyphens. The header and source are staged under an
output-directory lock before publication. A failure restores the previous pair
when possible; if rollback also fails, the generator
attempts to remove both final outputs and reports that manual cleanup is needed
when the filesystem refuses. The standalone generator currently requires POSIX
file-locking semantics, including on Linux and macOS.
Service header guards preserve the exact filename and first exported service's
fully-qualified identity. They distinguish punctuation/case and same basenames
in different namespaces without depending on checkout or output paths. Schemas
included together must still define distinct fully-qualified C++ services.

## Descriptor and completion contracts

- `Echo::descriptor()` owns a `ServiceDescriptor` in a function-local static
  RAII holder. C++11 initialization is thread-safe; the holder and its owned
  method descriptors are destroyed normally at process exit. There is no
  leaked singleton allocation or unsynchronized lazy initialization.
- The descriptor table contains the namespace prefix, service name, ordered
  method names, and explicit IDs. Stubs use `method(position)`; service dispatch
  switches on the stable `method->index()`.
- Service dispatch checks descriptor ownership and identity, non-null request
  and response, and `request->Verify<RequestType>()` before calling user code.
- Invalid calls, null stub channels, and default unimplemented methods call
  `controller->SetFailed()` when a controller exists, then run non-null `done`
  exactly once. Null controllers and callbacks are tolerated on failure.
- Successful dispatch transfers completion responsibility to the user method.
  The generated dispatcher does not run `done` again, so asynchronous service
  implementations remain possible. User methods and channel implementations
  must themselves honor the exactly-once completion contract.

## Using another FlatBuffers version

The runtime uses FlatBuffers builder internals that may change between upstream
releases, so the source accepts only 25.2.10. To use another version, build it
into a private prefix, update the version checks, and run the full
verification against that prefix. The steps below use 23.5.26 and were tested
on macOS ARM64 (AppleClang 15) and Ubuntu 22.04 (GCC 11.4, Clang 14). They
cover CMake only; the 23.5.26 Bazel files load `@aspect_rules_js` and `@npm`,
which bRPC does not configure.

Build FlatBuffers. `CMAKE_INSTALL_LIBDIR=lib` keeps the library where
`verify.sh` looks for it:

```sh
git clone --depth 1 --branch v23.5.26 https://github.com/google/flatbuffers.git /tmp/flatbuffers-23.5.26
cmake -S /tmp/flatbuffers-23.5.26 -B /tmp/flatbuffers-23.5.26/build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/tmp/flatbuffers-23.5.26-install \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DFLATBUFFERS_BUILD_TESTS=OFF
cmake --build /tmp/flatbuffers-23.5.26/build --target install -j4
```

In the bRPC source tree, update the version checks in
`src/brpc/flatbuffers/message.h`, `tools/flatbuffers/brpc_flatc.cpp` and
`test/flatbuffers_codegen/CMakeLists.txt`:

```sh
perl -pi -e 's/25\.2\.10/23.5.26/g;
  s/(FLATBUFFERS_VERSION_MAJOR != )25\b/${1}23/;
  s/(FLATBUFFERS_VERSION_MINOR != )2\b/${1}5/;
  s/(FLATBUFFERS_VERSION_REVISION != )10\b/${1}26/' \
  src/brpc/flatbuffers/message.h tools/flatbuffers/brpc_flatc.cpp \
  test/flatbuffers_codegen/CMakeLists.txt
git diff --stat
```

The diff should touch 13 lines in these three files. If it does not, the source
has changed since this example was written; review the remaining version
checks by hand.

Then run the verification with the new prefix:

```sh
FLATBUFFERS_PREFIX=/tmp/flatbuffers-23.5.26-install \
  bash tools/flatbuffers/verify.sh /tmp/brpc-fb23-verify
```

Without `FLATBUFFERS_PREFIX`, the script downloads 25.2.10, which the updated
checks reject. Test your own schemas as well. Other versions may need more than
new version numbers, for example changes for Builder internals or the
generator's naming rules.

## Pre-submit verification

Local runs and the Linux/macOS codegen CI jobs use the same entry point:

```sh
CXX=clang++ bash tools/flatbuffers/verify.sh /tmp/brpc-codegen-verify
```

Install CMake, a C++ compiler, protobuf, gflags, LevelDB, OpenSSL, zlib, and
Gperftools first. The root unit-test configuration requires Gperftools even
when only the FlatBuffers target is built.
Use `CMAKE_PREFIX_PATH` for non-system dependencies (on macOS, select the desired
Homebrew protobuf prefix). The script fetches SHA-256-pinned official FlatBuffers
25.2.10 and GTest 1.12.0 archives (URLs and hashes are in `verify.sh`). It enables
`BUILD_UNIT_TESTS`, selects the matching `flatc`, and supplies GTest source through
`BRPC_SYSTEM_GTEST_SOURCE_DIR` with `DOWNLOAD_GTEST=OFF`; no system GTest package
is needed. It builds and runs `brpc_flatbuffers_unittest`, rejecting zero-case,
disabled, or skipped runs, then builds the generator and runs both codegen
acceptance and runtime tests. Missing tests or failures stop the script.
`FLATBUFFERS_PREFIX` may reuse an existing matching installation.
`CODEGEN_RUNTIME=OFF` only builds the generator and runs compile-only acceptance;
it skips GTest, the core bRPC build/unit tests, and runtime tests, even when reusing
a build directory. This is not the full CI check.

For generator changes, add both rejected and accepted schemas, compile the
actual output of both generators, and demonstrate that a new regression fails
before the fix. Check generated symbol families and global/nested/imported
scopes rather than only the reported spelling. Record compiler, dependency,
SDK, flags, and source hashes; the script prints tool versions, build directories,
archive hashes, verbose CTest results, and the core GTest XML report path.
The same script does not make different runner/compiler versions equivalent.
Ordinary FlatBuffers-OFF jobs are not codegen evidence.

## Independent acceptance tests

No root build file needs modification. The default standalone build enables
CTest and additionally needs matching official `flatc`, protobuf, and gflags
development headers. The commands below use CMake/CTest 3.17+ for
`--no-tests=error` (the standalone generator itself has a CMake 3.10 minimum):

```sh
cmake -S tools/flatbuffers -B /tmp/brpc-codegen-build -DBUILD_TESTING=ON
cmake --build /tmp/brpc-codegen-build -j2
(cd /tmp/brpc-codegen-build && ctest --no-tests=error --output-on-failure)
```

The build compiles the generated service against the real bRPC headers. CTest
rejects missing/duplicate/negative/overflow/string IDs, streaming RPCs, C++20
keywords, and service/stub names that collide with generated C++ types.
Test-only fault injection covers staging, backup, publication, and rollback
failures; concurrent publishers must also leave a consistent pair. The suite
then compiles the generated service and self-contained header for C++14-or-newer
compatibility, sparse IDs, reordered methods, global/nested
namespaces, zero/max IDs, and included table types. Test configuration headers
and generated artifacts stay in the standalone build directory, not in the
source tree. The name-shadowing cases are compile-only by default; pass
`RUNTIME_LIBRARIES` when invoking `acceptance.cmake` directly to exercise their
runtime checks as well.

Expect **one** CTest named `flatbuffers_codegen_acceptance` in this mode. Its
inner checks also compile colliding-basename headers alone and together in both
orders, and compare regenerated bytes after relocating the schema. An empty
CTest selection is not acceptance.

To enable additional behavior tests against an **already built**
FlatBuffers-enabled bRPC library, use matching runtime headers/dependencies and
an existing library. A shared library avoids having to reconstruct all static
platform link dependencies. Build it with root `BUILD_SHARED_LIBS=ON` and target
`brpc-shared` first; use `.dylib` on macOS instead of `.so`:

```sh
BRPC_LIBRARY=/absolute/path/to/FB-on/output/lib/libbrpc.so
cmake -S tools/flatbuffers -B /tmp/brpc-codegen-build \
  -DBUILD_TESTING=ON \
  -DBRPC_CODEGEN_BRPC_LIBRARY="$BRPC_LIBRARY"
cmake --build /tmp/brpc-codegen-build -j2
(cd /tmp/brpc-codegen-build && ctest --no-tests=error --output-on-failure)
```

Expect **two** CTests in this mode: `flatbuffers_codegen_acceptance` and
`flatbuffers_codegen_runtime`. For non-default dependency installations, pass
their prefixes through `CMAKE_PREFIX_PATH`; modern Protobuf needs its CMake
config package and Abseil. A static `libbrpc.a` is also accepted, but the caller
must supply its complete transitive/platform link requirements (including macOS
frameworks where needed) via `BRPC_CODEGEN_EXTRA_LIBRARIES`.

The runtime test covers concurrent first descriptor access, sparse
stub/dispatch mapping, absent and present strings, invalid/foreign/forged
methods, failed request verification, null arguments, unimplemented methods,
exactly-once error callbacks (including self-deleting closures), deferred
completion, and channel ownership. It uses a local abstract channel; no network
RPC compatibility is claimed.
