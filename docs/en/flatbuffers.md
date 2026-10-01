# FlatBuffers messages and RPC

[中文版](../cn/flatbuffers.md)

bRPC provides optional IOBuf-backed FlatBuffers messages, builders, service
descriptors, and the `fb_rpc` transport. The implementation builds on
[apache/brpc#3196](https://github.com/apache/brpc/pull/3196) and
[apache/brpc#3197](https://github.com/apache/brpc/pull/3197), while preserving
Protocol's existing protobuf callback signatures.

## Enable FlatBuffers

FlatBuffers is **OFF by default**. Enable it when building bRPC, then build the
client/server against that same library and generated configuration header.
Adding `-DBRPC_WITH_FLATBUFFERS=1` to an application is not a substitute: the
feature changes the Channel, Controller and Server ABI.

| Build system | Enable option | Exported runtime prefix |
| --- | --- | --- |
| CMake | `-DWITH_FLATBUFFERS=ON` | `<build>/output` |
| Make | `config_brpc.sh --with-flatbuffers` | `<checkout>/output` |
| Bazel | `--define=BRPC_WITH_FLATBUFFERS=true` on build/test commands | Bazel outputs, not an install prefix for the standalone example |

### Dependencies and versions

Prepare the normal bRPC dependencies described in [Getting started](getting_started.md):
a C++ toolchain, Protobuf compiler/development libraries, gflags, LevelDB,
OpenSSL and zlib. FlatBuffers RPC does **not** remove the Protobuf dependency.

| Component | Additional requirement |
| --- | --- |
| bRPC message/RPC runtime | FlatBuffers headers; no `libflatbuffers` linkage |
| Official schema generation | `flatc` matching the runtime headers |
| bRPC service generation | `brpc_flatc`, built with matching official headers and `libflatbuffers` |
| Example smoke | Python 3; no GoogleTest requirement |
| Library unit tests | GoogleTest and the project's test dependencies |

The repository's Bazel/ON gate pins FlatBuffers **25.2.10**. Use a complete,
matching installation to reproduce it; never remove the generated header's
version assertions. CMake runtime tests use `FLATBUFFERS_FLATC_EXECUTABLE`,
whereas generator acceptance and the example use `FLATC_EXECUTABLE`.
`BRPC_FLATC_EXECUTABLE` always means the bRPC generator, not official `flatc`.

Use the same Protobuf installation across all builds. When CMake detects
`Protobuf_VERSION > 4.21`, C++17 and the corresponding Abseil dependencies are
required; generator/example builds need Protobuf's CMake config package to
export those dependencies. The runtime/example path requires CMake 3.16+; the CTest commands
below use CMake/CTest 3.17+ for `--no-tests=error`. With 3.16, run the Python smoke
directly. Use a single-configuration generator such as Unix Makefiles or Ninja.

### CMake runtime build

Run from a **writable checkout**, with a working compiler selected through
`CC`/`CXX` if necessary. Example-generated bindings and binaries stay in `WORK`,
but root bRPC configuration also writes **`src/butil/config.h` in the checkout**.
Do not configure ON/OFF or different build systems concurrently in the same
checkout, even with separate build directories; use independent writable copies.

Replace the prefixes below with your installations (`include/`, `lib/` or
`lib64/`). A prefix may be reused for several dependencies. On macOS, select a
consistent compiler/SDK/architecture and the actual OpenSSL prefix; do not assume
that `/usr/local/opt/openssl` exists on Apple Silicon.

```sh
REPO="$PWD"
DEPS=/absolute/path/to/dependency-prefix
FB=/absolute/path/to/flatbuffers-prefix
OPENSSL=/absolute/path/to/openssl-prefix
WORK="$(mktemp -d "${TMPDIR:-/tmp}/brpc-benchmark-fb.XXXXXX")"
JOBS=2
printf 'WORK=%s\n' "$WORK"
```

If FlatBuffers is not installed, build a matching official source checkout first
(skip this block when `FB` already contains the required headers, compiler and
library). `FB` must be a writable installation prefix, not the source directory:

```sh
FB_SOURCE=/absolute/path/to/flatbuffers-25.2.10-source
cmake -S "$FB_SOURCE" -B "$WORK/flatbuffers" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX="$FB" -DCMAKE_INSTALL_LIBDIR=lib \
  -DFLATBUFFERS_BUILD_TESTS=OFF -DFLATBUFFERS_BUILD_FLATC=ON \
  -DFLATBUFFERS_BUILD_FLATLIB=ON -DFLATBUFFERS_BUILD_SHAREDLIB=OFF \
  -DFLATBUFFERS_INSTALL=ON -DFLATBUFFERS_LIBCXX_WITH_CLANG=OFF
cmake --build "$WORK/flatbuffers" --parallel "$JOBS"
cmake --install "$WORK/flatbuffers"
"$FB/bin/flatc" --version
```

Build the runtime without the unit-test dependencies:

```sh
cmake -S "$REPO" -B "$WORK/runtime" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_PREFIX_PATH="$DEPS;$FB;$OPENSSL" \
  -DOPENSSL_ROOT_DIR="$OPENSSL" \
  -DWITH_FLATBUFFERS=ON -DBUILD_SHARED_LIBS=OFF \
  -DFLATBUFFERS_INCLUDE_DIR="$FB/include" \
  -DBUILD_UNIT_TESTS=OFF -DDOWNLOAD_GTEST=OFF -DBUILD_BRPC_TOOLS=OFF
cmake --build "$WORK/runtime" --parallel "$JOBS"
grep -E '^#define BRPC_WITH_FLATBUFFERS +1$' "$WORK/runtime/output/include/butil/config.h"
```

The final check must print `#define BRPC_WITH_FLATBUFFERS 1`. Public headers live
in `brpc/flatbuffers/`: `message.h` for messages/builders and `service.h` for
services/descriptors. The configuration macro is always 0 or 1; use `#if`, not
`#ifdef`. The policy argument above is an optional compatibility setting for
older dependencies under CMake 4, not a FlatBuffers switch or a replacement for
required dependency versions.

### Make and Bazel alternatives

In a separate writable checkout, using the same dependency prefixes:

```sh
sh config_brpc.sh --with-flatbuffers \
  --headers="$FB/include $DEPS/include $OPENSSL/include" \
  --libs="$DEPS/lib $OPENSSL/lib" --cc="${CC:-cc}" --cxx="${CXX:-c++}"
make -j"$JOBS"
```

Use `lib64` or the platform's library directory where appropriate. The resulting
`output/` can replace `WORK/runtime/output` in the example command below. Make
unit tests take `FLATC="$FB/bin/flatc"` and require installed GoogleTest libraries
and gperftools, not just GoogleTest sources; their `test/libbrpc.dbg.*` must also
be loadable. The ON runner below builds GoogleTest, sets the test library path,
and validates reports; system test dependencies such as gperftools must already
be installed.

Bazel supplies its pinned dependencies; pass the feature flag to both commands:

```sh
bazel build --define=BRPC_WITH_FLATBUFFERS=true //:brpc
bazel test --define=BRPC_WITH_FLATBUFFERS=true --cache_test_results=no \
  //test:brpc_flatbuffers_unittest //test:brpc_flatbuffers_protocol_unittest
```

These Bazel targets verify the library, not the standalone example. Do not use a
raw `bazel-bin` directory as `BRPC_ROOT`; the example requires the include/lib
layout of a CMake/Make output or installed prefix.

## Verify with the client/server example

[example/benchmark_fb](../../example/benchmark_fb/README.md) is a **bounded
functional example**, not a performance benchmark. Its [schema](../../example/benchmark_fb/echo.fbs)
uses `BenchmarkService.Echo` with explicit wire ID 7. The
[server](../../example/benchmark_fb/server.cpp) calls `AddFlatBuffersService`;
the [client](../../example/benchmark_fb/client.cpp) uses the generated
`BenchmarkService::Stub` and `fb_rpc` channel.

### Generate, build and smoke-test

Continue in the same shell with the variables and runtime from above. An
existing compatible FB ON runtime may be used instead by changing `BRPC_ROOT`.
Build the bRPC generator, then the example:

```sh
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

The example build runs **both** generators. `flatc` produces `echo_generated.h`;
`brpc_flatc` produces `echo.brpc.fb.h/.cpp`, all under `WORK/example/generated/`.
Do not commit them or reuse stale generated headers after changing versions.
If library discovery is ambiguous, pass `FLATBUFFERS_LIBRARY` explicitly when
configuring `brpc_flatc`. See the [generator guide](../../tools/flatbuffers/README.md)
for included schemas, explicit IDs and generator acceptance tests.

Success means **one named CTest passed**, exit status 0, and this output:

```text
benchmark_fb smoke passed: 13 verified replies, 2 schema rejections, clean shutdown
```

That is 15 RPCs, not 15 CTests. The smoke verifies binary bytes, empty/absent
strings, attachments, concurrency, single/pooled/short connections, schema
rejection and recovery on the same server. It starts its own loopback server on
an ephemeral port and reaps it; requiring SIGKILL is a failure. The orchestration
has a 35-second deadline and CTest a 50-second timeout. `No tests were found`
is not a pass. The same checks can be run without CTest:

```sh
python3 "$REPO/example/benchmark_fb/smoke.py" \
  --server "$WORK/example/benchmark_fb_server" \
  --client "$WORK/example/benchmark_fb_client"
```

The default example links `libbrpc.a`. For `LINK_SO=ON`, first build the runtime
with `BUILD_SHARED_LIBS=ON` and the `brpc-shared` target, then reconfigure/rebuild
the example. `LINK_SO` alone cannot create a shared runtime. The
[example README](../../example/benchmark_fb/README.md) includes that sequence.

### Run the programs separately

Both processes must run on the **same host/container**: this example accepts
only `127.0.0.1`. In the build terminal:

```sh
"$WORK/example/benchmark_fb_server" --listen_addr=127.0.0.1:0 --duration_s=300
```

Wait for `BRPC_FB_READY 127.0.0.1:<port>`. In another terminal, set `WORK` again
(shell variables are not shared), replace the port, and run:

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

All four commands must exit 0. Their `(completed, successes,
expected_rejections, failures)` results are respectively `(16,16,0,0)`,
`(2,2,0,0)`, `(2,0,2,0)` and `(2,2,0,0)`. `failures` is a 0/1 flag, not an RPC
count. A corrupt request counts as an expected rejection only for the generated
server's schema error (-1) with an empty response/attachment; timeouts and
connection failures do not count. The last command verifies recovery. Stop the
server with Ctrl-C or let its 300-second lifetime expire. See the example README
for all bounded client/server parameters.

## Additional tests and the ON gate

For library unit tests, configure with `BUILD_UNIT_TESTS=ON`,
`FLATBUFFERS_FLATC_EXECUTABLE` pointing to the matching official compiler, and
`BRPC_SYSTEM_GTEST_SOURCE_DIR` pointing to GoogleTest sources when
`DOWNLOAD_GTEST=OFF`. Build and run `brpc_flatbuffers_unittest` and
`brpc_flatbuffers_protocol_unittest`; enabling the library alone does not run them.

For the complete CMake path (both library suites, two codegen tests, and the
example smoke), the shared [ON runner](../../.github/scripts/flatbuffers-on.py)
can use the prefixes above. This full CMake gate requires **CMake/CTest 3.21+**
for JUnit XML output (`--output-junit`). **Its work directory must not already
exist**:

```sh
python3 "$REPO/.github/scripts/flatbuffers-on.py" \
  --build-system cmake --source "$REPO" --work "$WORK/on-gate" --jobs "$JOBS" \
  --flatbuffers-prefix "$FB" \
  --dependency-prefix "$DEPS" --dependency-prefix "$OPENSSL"
```

This gate requires FlatBuffers 25.2.10. Omit `--flatbuffers-prefix` to let it
download/checksum/build that version; GoogleTest defaults to a checksum-pinned
1.14.0 download, or can be supplied with `--gtest-source`. Normal platform
dependencies must already be installed. Make/Bazel modes validate the two
library suites; only CMake mode also builds the generator and example. Inspect
`WORK/on-gate/evidence/summary.json` for `status: passed`, all required test names,
nonzero executed counts and zero failed/skipped cases. Commands, logs and XML
reports are retained even on failure. Empty/filtered/skipped runs are rejected.

The [workflow](../../.github/workflows/flatbuffers-on.yml) runs Linux CMake/Make
with GCC and Clang, Linux Bazel with GCC, and macOS CMake. A local gate pass does
not imply the hosted GitHub matrix or a different dependency combination passed.

## Troubleshooting

| Symptom | Check / action |
| --- | --- |
| `BRPC_ROOT is not FlatBuffers-enabled` or missing FB symbols | Rebuild bRPC with the enable option, then rebuild all consumers. Check the actual output `include/butil/config.h`; do not force the macro or mix an ON header with an OFF library. |
| Missing `flatbuffers/idl.h` or `libflatbuffers` | The service generator needs the full official development installation, not only the runtime headers. Set `FLATBUFFERS_INCLUDE_DIR` and, if needed, `FLATBUFFERS_LIBRARY`. |
| Header/compiler version mismatch | Check `flatc --version`, select matching headers/compiler/library, and regenerate bindings in a fresh build directory. Do not delete upstream version checks. |
| Missing Protobuf/Abseil headers or link symbols | Use one compatible Protobuf installation throughout and expose its CMake config and Abseil prefixes. Do not mix system and private headers/libraries. |
| OpenSSL not found on macOS | Set `OPENSSL_ROOT_DIR` and include the actual installed prefix in `CMAKE_PREFIX_PATH`; check compiler/SDK/architecture consistency. |
| Cannot write `src/butil/config.h.tmp` | Root configuration needs a writable checkout. Use a private source copy; separate build directories do not isolate concurrent source configuration. |
| Old dependency policy error with CMake 4 | Try the appropriate `CMAKE_POLICY_VERSION_MINIMUM` compatibility setting for that dependency, or update it; this does not change the required C++ or dependency versions. |
| `No tests were found` | Use the example build directory, configure `BUILD_TESTING=ON`, build the executables, then run the named test or `smoke.py` directly. |
| `LINK_SO=ON` cannot find a library or the loader fails | Build `brpc-shared` with runtime `BUILD_SHARED_LIBS=ON` first. Check the shared library's dependencies and runtime search paths. |
| Connection refused/timeout | Wait for the readiness line, use its current port on the same host/container, and check the server's finite lifetime. These are not successful schema rejections. |

Flatc 2.0.x emits unqualified `flatbuffers::` names. Keep business schemas outside
the `brpc` namespace (for example `myapp.rpc`); do not depend on include order to
avoid shadowing. Flatc 25.2.10 emits fully-qualified names.

## Message construction and ownership

Use upstream `flatc --cpp` to generate the schema's `*_generated.h`. Pass a
`brpc::flatbuffers::MessageBuilder` to the generated `Create...` functions,
call `Finish(root)`, and finally call `ReleaseMessage()`.

* `ReleaseMessage()` does not copy payload bytes. The returned move-only Message
  owns an IOBuf block reference and survives builder reuse or destruction.
* Message/builder moves leave the source reusable. Moving a shared-string builder
  discards its optional deduplication cache; existing offsets remain valid.
* Importing an ordinary `::flatbuffers::FlatBufferBuilder` copies its payload and
  scratch while preserving unfinished table state. The original allocator frees
  the original storage, including owned custom allocators. No `free`/`delete[]`
  guess or assumption about spare bytes before the payload is made.
* Use MessageBuilder's own move, swap, and release operations. Do not transfer it
  through a base-class cast or use inherited raw-buffer release operations: a raw
  FlatBuffers detached buffer would retain the address of its member allocator.
* 64 zero-initialized bytes precede a released payload. They may be shortened
  with `reduce_meta_size_and_get_buf`; growing them is rejected without mutation.
  Payload addresses and bytes are unchanged by shortening metadata.
* Serialization accepts a const Message and retains its storage in the output
  IOBuf. The buffer is shared, not copy-on-write: do not mutate payload/metadata
  while another reader or serialized buffer is using it.
* Allocation sizes are checked before narrowing to SingleIOBuf's uint32_t size.
  Allocation failure is fatal, including release builds, independently of
  bRPC's `crash_on_fatal_log` setting. These paths explicitly abort rather than
  relying on `CHECK`/`LOG(FATAL)`. Upstream `vector_downward` cannot safely
  continue with a null allocation result.

`ParseFbFromIOBUF` checks sizes/framing and retains independent ownership. It
shares a contiguous input when the payload address is 64-byte aligned; fragmented
or insufficiently aligned input is copied into aligned storage. A builder's
allocation is 64-byte aligned, but the final payload need only have the alignment
required by its schema, so not every local message qualifies for receive-side
zero-copy. Alignments above 64 bytes are not supported.

**Framing is not schema verification.** Call `msg.Verify<YourRoot>()` before
`GetRoot<YourRoot>()` or `GetMutableRoot<YourRoot>()` on received data. Optional
FlatBuffers strings/vectors can still be null in a valid message. Failed framing
checks leave the prior message intact.

## Service IDs and generation

`BrpcDescriptorTable` contains a namespace, service name, whitespace-separated
method names, and explicit method IDs. IDs must be unique nonnegative int32 values.
An empty ID list assigns ordinal IDs to manually constructed descriptors;
generated services require explicit IDs:

```fbs
rpc_service BenchmarkService {
  First(Request):Response (id: 2);
  Second(Request):Response (id: 5);
}
```

* `descriptor.method(position)` enumerates methods in declaration order.
* `method.index()` is the stable wire ID, not its array position.
* `descriptor.FindMethodByIndex(id)` looks up sparse wire IDs. A transport must
  use this lookup rather than indexing a dense array with the wire ID.
* Never recycle a removed method ID for a different method. Removing or reordering
  declarations leaves surviving explicit IDs unchanged.
* Namespace `a.b` and `a.b.` normalize to the same service name; empty namespace
  means global scope. Method full names include their service. The service hash
  uses the canonical full name and MurmurHash3 seed 1. Keep service names stable
  when persisting or transmitting these IDs.
* Descriptors cannot be reinitialized after success. They own methods with RAII;
  generated accessors use function-local static initialization for thread safety.

The companion generator in `tools/flatbuffers/` uses the upstream parser to
produce service bindings. It is independently built; only that optional tool
needs `libflatbuffers`. See its [README](../../tools/flatbuffers/README.md) for
commands and limitations. Generated dispatch verifies requests, rejects
unknown/foreign methods, and runs non-null completion callbacks on failure,
including unimplemented methods. Successful implementations own completion and
must run their callback exactly once.

## Network RPC

Build both the library and its users with the same `BRPC_WITH_FLATBUFFERS`
setting; enabling the feature changes the Channel, Controller and Server ABI.
Register generated services with `server.AddFlatBuffersService(&service,
SERVER_DOESNT_OWN_SERVICE)`. Use `ChannelOptions::protocol = "fb_rpc"` and pass
that `brpc::Channel` to a generated stub. The stub calls `Channel::FBCallMethod`;
`Controller::flatbuffers_method()` returns the typed descriptor, while the
protobuf `Controller::method()` remains null. Manually supplied descriptors
must outlive their calls, including retries and backup requests.

Service registration is separate from protobuf's AddService and ListServices
APIs. Add/remove/clear require a stopped (READY) server, not a server still
STOPPING; management operations must be externally serialized. Ownership
transfers only after successful registration. `RemoveFlatBuffersService`
deletes an owned service, while Stop/Join keep registrations for restart.
`GetFlatBuffersServiceCount()` reports this separate registry. Duplicate service
IDs (including hash collisions) and conflicting full protobuf/FlatBuffers method
names are rejected. String-based `MaxConcurrencyOf` supports FlatBuffers
methods, as do server-wide and default method concurrency limits.

The transport supports synchronous/asynchronous calls, retries, backup requests,
single/pooled/short connections and request/response attachments. It allocates a
small independent header for each send and shares payload storage; it never
rewrites a const request's metadata prefix. Thus retries and concurrent calls
can safely share an immutable request. The application must still keep each
response and Controller alive until completion.

Framing validates lengths, not application schemas. Generated services verify
requests before dispatch. Handwritten services must do the same. Callers must
verify received response schemas before using root accessors; an RPC succeeding
does not replace `response.Verify<Response>()`.

Authentication, compression, checksums and streaming are not supported and are
rejected rather than silently ignored. FlatBuffers services cannot be accessed
through the internal, builtin-only port. SelectiveChannel/ParallelChannel,
HTTP/JSON mapping and RPC-dump replay are not provided by this transport.
Log IDs, user fields and distributed-tracing metadata are not transmitted.

For a complete generated client/server, follow
[Verify with the client/server example](#verify-with-the-clientserver-example)
above. The example demonstrates these APIs without making a performance claim.

### FRPC framing and compatibility

A frame is `[12-byte header][metadata][message][attachment]`. The header contains
`FRPC`, a big-endian uint32 body size, and a big-endian uint32 metadata size; body
size excludes the 12-byte header but includes all three following sections.
The metadata prefix is explicitly little-endian, matching the original FRPC
experiment on little-endian machines without relying on packed structs:

* Request: uint32 service ID, int32 method ID, int32 message size, int32 attachment
  size, uint64 correlation ID (24 bytes).
* Response: int32 error code, int32 message size, int32 attachment size, uint64
  correlation ID (20 bytes).

Readers require the whole known prefix, then skip unknown trailing metadata
using the advertised metadata size. Future optional fields must be appended;
do not reorder, resize or repurpose existing fields. A shorter prefix, negative
size, inconsistent payload length or excessive body is rejected. Error replies
have a nonzero error code and no payload. Method IDs are sparse wire IDs, never
array positions: adding, deleting or reordering declarations preserves surviving
IDs only when explicit IDs are retained. This does not make legacy ordinal-ID
schemas compatible, and removed IDs must never be reused.

The magic is `FRPC`, not `BRPC`. This format does not promise compatibility with
older unpublished variants that used different magic, service hashes or native
big-endian metadata. The library's global Protocol hook signatures remain
unchanged, so existing protocol callback implementations need no adaptation.
Private numeric protocol IDs must not overlap newly assigned builtin IDs;
`PROTOCOL_FLATBUFFERS_RPC` uses ID 30.
