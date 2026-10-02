# FlatBuffers messages

bRPC provides optional IOBuf-backed FlatBuffers messages, builders, and service
descriptors. The message-construction approach builds on
[apache/brpc#3196](https://github.com/apache/brpc/pull/3196).

This component does not register an `fb_rpc` transport or add FlatBuffers
integration to `brpc::Channel` and `brpc::Server`. Service-generation and
in-process dispatch tests are not network RPC or performance benchmarks.

## Build

FlatBuffers support is disabled by default. The message runtime needs only
FlatBuffers headers; it does not link a FlatBuffers library. Tests need a `flatc`
matching those headers. Keep upstream's generated version assertions intact:
regenerate the header rather than weakening the assertion.

For example, with GoogleTest sources installed under `/usr/src/googletest`:

```sh
cmake -S . -B build -DWITH_FLATBUFFERS=ON -DBUILD_UNIT_TESTS=ON \
  -DBUILD_BRPC_TOOLS=OFF -DDOWNLOAD_GTEST=OFF \
  -DBRPC_SYSTEM_GTEST_SOURCE_DIR=/usr/src/googletest
cmake --build build --target brpc_flatbuffers_unittest -j6
ctest --test-dir build -R '^brpc_flatbuffers_unittest$' --output-on-failure
```

For other installations set `FLATBUFFERS_INCLUDE_DIR`,
`FLATBUFFERS_FLATC_EXECUTABLE`, and `BRPC_SYSTEM_GTEST_SOURCE_DIR` as needed.
The project's usual test dependencies still apply.

Make accepts `--with-flatbuffers` on `config_brpc.sh`; its tests accept
`FLATC=/path/to/flatc`. Bazel accepts `--define=BRPC_WITH_FLATBUFFERS=true`,
with matching FlatBuffers 25.2.10 runtime and compiler dependencies. Bzlmod
imports the same checksum-pinned archive as WORKSPACE: `runtime_cc` and `flatc`
do not need FlatBuffers' external gRPC module, which would otherwise conflict
with bRPC's pinned BoringSSL even when the feature is disabled.

Public headers live in `brpc/flatbuffers/`, matching namespace
`brpc::flatbuffers`. Include `message.h` for construction and `service.h` for
service descriptors/interfaces. `BRPC_WITH_FLATBUFFERS` in `butil/config.h`
is always 0 or 1; test it with `#if`, not `#ifdef`.

Flatc 2.0.x emits unqualified `flatbuffers::` names. Use a business schema
namespace outside `brpc` (for example `myapp.rpc`) to avoid shadowing by
`brpc::flatbuffers`; do not rely on include order. Flatc 25.2.10 emits fully
qualified names instead.

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
