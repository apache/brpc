# FlatBuffers messages

[中文版](../cn/flatbuffers.md)

bRPC supports IOBuf-backed FlatBuffers messages, builders, and service
descriptors, disabled by default. Message construction is based on
[apache/brpc#3196](https://github.com/apache/brpc/pull/3196).

This component does not include `fb_rpc` transport or change `brpc::Channel`
or `brpc::Server`. Service generation and in-process dispatch tests do not
exercise network RPC or measure performance.

## Build

The runtime needs only FlatBuffers headers; tests also need a matching `flatc`.
Regenerate incompatible headers rather than removing or weakening upstream
version assertions.

With GoogleTest sources at `/usr/src/googletest`:

```sh
cmake -S . -B build -DWITH_FLATBUFFERS=ON -DBUILD_UNIT_TESTS=ON \
  -DBUILD_BRPC_TOOLS=OFF -DDOWNLOAD_GTEST=OFF \
  -DBRPC_SYSTEM_GTEST_SOURCE_DIR=/usr/src/googletest
cmake --build build --target brpc_flatbuffers_unittest -j6
ctest --test-dir build -R '^brpc_flatbuffers_unittest$' --output-on-failure
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

The generator in `tools/flatbuffers/` uses the upstream parser and links
`libflatbuffers` separately. See its [README](../../tools/flatbuffers/README.md)
for commands and limitations.

Generated dispatch validates requests and rejects unknown, foreign, or
unimplemented methods. On failure it runs a non-null completion callback. After
successful dispatch, the application owns completion and must run it exactly
once.
