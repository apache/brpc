# FlatBuffers messages and RPC

[中文版](../cn/flatbuffers.md)

bRPC provides optional IOBuf-backed FlatBuffers messages, builders, service
descriptors, and the `fb_rpc` transport. The implementation builds on
[apache/brpc#3196](https://github.com/apache/brpc/pull/3196) and
[apache/brpc#3197](https://github.com/apache/brpc/pull/3197), while preserving
Protocol's existing protobuf callback signatures.

## Build

The runtime needs only FlatBuffers headers; tests also need a matching `flatc`.
Regenerate incompatible headers rather than removing or weakening upstream
version assertions.

With GoogleTest sources at `/usr/src/googletest`:

```sh
cmake -S . -B build -DWITH_FLATBUFFERS=ON -DBUILD_UNIT_TESTS=ON \
  -DBUILD_BRPC_TOOLS=OFF -DDOWNLOAD_GTEST=OFF \
  -DBRPC_SYSTEM_GTEST_SOURCE_DIR=/usr/src/googletest
cmake --build build --target brpc_flatbuffers_unittest brpc_flatbuffers_protocol_unittest -j6
ctest --test-dir build -R '^brpc_flatbuffers(_protocol)?_unittest$' --output-on-failure
```

For nonstandard installations, set `FLATBUFFERS_INCLUDE_DIR`,
`FLATBUFFERS_FLATC_EXECUTABLE`, and `BRPC_SYSTEM_GTEST_SOURCE_DIR` as needed.
The usual bRPC test dependencies still apply.

For Make, use `config_brpc.sh --with-flatbuffers` and set `FLATC=/path/to/flatc`
when testing. For Bazel, use `--define=BRPC_WITH_FLATBUFFERS=true`.

The runtime depends on FlatBuffers builder internals and supports only 25.2.10;
other versions fail at compile time. Bzlmod and WORKSPACE use the same
checksum-pinned archive. Using this instead of `bazel_dep` avoids gRPC,
other language tools, and conflicting BoringSSL dependencies.

Public headers are in `brpc/flatbuffers/`, with namespace `brpc::flatbuffers`.
Include `message.h` for message construction and `service.h` for service
descriptors and interfaces. `BRPC_WITH_FLATBUFFERS` in `butil/config.h` is
0 or 1; test it with `#if`.

Older flatc 2.0.x may emit unqualified names. Keep business schemas outside
the `brpc` namespace and do not rely on include order.

To use another FlatBuffers version, update the version checks in the runtime,
the generator and the codegen tests, then run `tools/flatbuffers/verify.sh`
against that version. See the generator
[README](../../tools/flatbuffers/README.md#using-another-flatbuffers-version)
for the steps, using 23.5.26 as an example. They apply to CMake builds only.

## Message construction and ownership

Generate `*_generated.h` with upstream `flatc --cpp`. Pass a
`brpc::flatbuffers::MessageBuilder` to the generated `Create...` functions,
then call `Finish(root)` and `ReleaseMessage()`.

* `ReleaseMessage()` returns a move-only Message without copying payload bytes.
  Its IOBuf block reference survives builder reuse or destruction.
* Moves leave the source Message or builder reusable. Moving a shared-string
  builder clears its deduplication cache but preserves existing offsets.
* `Message::CopyFrom` and `MergeFrom` share a ref-counted IOBuf block without
  copying payload or metadata. Changes through one alias affect all others;
  concurrent reads and writes require external synchronization.
* Importing a regular `::flatbuffers::FlatBufferBuilder` copies payload and
  scratch, preserving unfinished tables. The original allocator frees the old
  storage and is destroyed if owned by the source builder. Import neither
  guesses between `free` and `delete[]` nor requires a payload prefix.
* Use only MessageBuilder's own move, swap, and release operations. Base-class
  transfers and inherited raw-buffer releases can leave a detached buffer
  pointing to the member allocator.
* Released payloads have a 64-byte zeroed prefix. `reduce_meta_size_and_get_buf`
  can shrink it without changing the payload address or bytes. Attempts to grow
  it fail without changing the message.
* Serializing a const Message shares its storage with the output IOBuf; it is
  not copy-on-write. Do not modify payload or metadata while readers or
  serialized buffers still use it.
* Sizes are checked before conversion to SingleIOBuf's uint32_t length.
  Allocation failure in SlabAllocator or a builder terminates the process even
  in release builds, regardless of `crash_on_fatal_log`: FlatBuffers'
  `vector_downward` cannot continue after a null allocation. Allocation failure
  when copying during parsing returns false and leaves the Message unchanged.

`ParseFbFromIOBUF` checks lengths and framing and retains a storage reference;
it does not limit received message sizes. Bound untrusted `msg_size` with your
max-message-size policy before parsing: fragmented or unaligned input is copied
to new storage before schema validation.

Parsing shares contiguous input when its payload is 64-byte aligned. Builder
allocations have this alignment, but finished payloads need only meet their
schema's alignment, so local messages may still require a copy. Alignment
requirements above 64 bytes are unsupported.

**Framing is not schema verification.** Before reading untrusted data, call
`msg.Verify<YourRoot>()` and use `GetRoot<YourRoot>()` or
`GetMutableRoot<YourRoot>()` only if it succeeds. Optional strings/vectors can
still be null in valid messages. Failed framing checks leave the Message intact.

## Service IDs and generation

`BrpcDescriptorTable` holds a namespace, service name, whitespace-separated
method names, and unique nonnegative int32 IDs. Handwritten descriptors may
use an empty ID list to assign IDs in declaration order; generated services
require explicit IDs:

```fbs
rpc_service BenchmarkService {
  First(Request):Response (id: 2);
  Second(Request):Response (id: 5);
}
```

* `descriptor.method(position)` returns methods in declaration order.
* `method.index()` is the stable wire ID, not an array index.
* Look up sparse IDs with `descriptor.FindMethodByIndex(id)`, not by indexing
  a dense array with a wire ID.
* Do not reuse removed method IDs or change existing IDs when reordering
  methods.
* Namespaces `a.b` and `a.b.` normalize to the same service name; an empty
  namespace means global scope. Method full names include the service name.
  Service hashes use the normalized full name and MurmurHash3 seed 1. Keep
  service names stable when persisting or transmitting IDs.
* Descriptors own methods through RAII and cannot be reinitialized after
  success.
  Generated accessors use thread-safe function-local static initialization.

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
