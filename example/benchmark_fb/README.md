# FlatBuffers RPC example

[English guide](../../docs/en/flatbuffers.md) |
[中文指南](../../docs/cn/flatbuffers.md)

Independent client and server programs using the `fb_rpc` protocol, a generated
FlatBuffers service, and `brpc::flatbuffers::Message`. This is a **bounded
functional example**, not a performance comparison; the directory name is kept
for continuity.

The schema declares `BenchmarkService.Echo` with stable wire method ID **7**.
The generated stub calls `Channel::FBCallMethod`, and the server registers the
generated service with `Server::AddFlatBuffersService`.

## Build

Dependencies, the FlatBuffers-enabled runtime install, the generator and the
example build are described in the guide's
[verification section](../../docs/en/flatbuffers.md#verify-with-the-clientserver-example).
`BRPC_ROOT` must be an install prefix (or a Make `output/` directory) of a bRPC
built with FlatBuffers ON; the example reads its `include/butil/config.h` and
rejects an OFF build. Both `flatc` and `brpc_flatc` write generated code only to
the build directory; do not commit `*_generated.h` or `*.brpc.fb.*`.

To link the shared library, build and install the runtime with
`-DBUILD_SHARED_LIBS=ON` and the `brpc-shared` target, then configure the
example with `-DLINK_SO=ON`. Extra runtime dependencies can be passed with
`-DBRPC_EXTRA_LIBRARIES='library1;library2'`.

## Run separately

The server and client must run on the **same host**; both accept only IPv4
loopback endpoints. In the build terminal:

```sh
"$WORK/example/benchmark_fb_server" --listen_addr=127.0.0.1:0 --duration_s=300
```

The server prints `BRPC_FB_READY 127.0.0.1:<port>` after `Server::Start`
succeeds. Use that port in another terminal (shell variables are not shared, so
set `WORK` again):

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

All four commands exit 0. The first prints
`{"completed":16,"successes":16,"expected_rejections":0,"failures":0}`. The
corrupt-request run prints `{"completed":2,"successes":0,"expected_rejections":2,"failures":0}`,
and the last one checks recovery on the same server (`completed=2`,
`successes=2`).

The client verifies the response schema before reading it, then compares the
request ID, every string byte, optional-string presence, and the attachment.
`--corrupt_request=true` damages the FlatBuffers root offset but keeps the FRPC
framing valid. It succeeds only if every request gets the generated service's
schema rejection (error code -1); timeouts and connection errors are failures.
`failures` is a 0/1 flag, not a count, and no throughput is reported.

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

The server defaults to `127.0.0.1:0`, `duration_s=60` (1..3600) and
`max_concurrency=32` (1..64), and stops earlier on SIGINT/SIGTERM. Builtin
services are disabled. FRPC has no authentication, compression, checksums or
streaming, so this is not a production or public-network configuration.

## Smoke test

CTest runs the standard-library-only `smoke.py`, which starts its own server on
an ephemeral loopback port and checks:

1. finite RPCs with binary attachments, empty and absent strings, and
   single/pooled/short connections (13 verified replies);
2. two malformed-schema requests are rejected;
3. normal requests still work on the same server afterwards;
4. the server it started is stopped and reaped.

The orchestration deadline is 35 seconds and CTest's timeout is 50 seconds.
Needing the SIGKILL fallback fails the test. The smoke test can also run
without CTest:

```sh
python3 "$REPO/example/benchmark_fb/smoke.py" \
  --server "$WORK/example/benchmark_fb_server" \
  --client "$WORK/example/benchmark_fb_client"
```

For common failures see the guide's
[troubleshooting table](../../docs/en/flatbuffers.md#troubleshooting).
