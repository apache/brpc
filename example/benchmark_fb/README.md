# FlatBuffers RPC example

[English guide](../../docs/en/flatbuffers.md) |
[中文指南](../../docs/cn/flatbuffers.md)

Independent client and server programs using the `fb_rpc` protocol, a generated
FlatBuffers service, and `brpc::flatbuffers::Message`. By default the client
runs a **bounded functional check**. A timed mode compares `fb_rpc` with an
equivalent protobuf service over `baidu_std` (see
[Timed comparison](#timed-comparison)).

The schema declares `BenchmarkService.Echo` with stable wire method ID **7**.
The generated stub calls `Channel::FBCallMethod`, and the server registers the
generated service with `Server::AddFlatBuffersService`. The same server also
registers `benchmark_fb_pb.BenchmarkService` from `echo_pb.proto`, a protobuf
service with the same fields, and serves both `fb_rpc` and `baidu_std`.

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

By default the server and client accept only IPv4 loopback endpoints. In the
build terminal:

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
| `server` | required | `127.0.0.1:<nonzero-port>`, any IPv4 with `allow_remote` |
| `protocol` | `fb_rpc` | `fb_rpc`, `baidu_std` (protobuf control service) |
| `request_count` | 16 | 1..1000 total, not per thread |
| `thread_num` | 2 | 1..16 synchronous callers (1..1024 when timed) |
| `request_size` | 64 | 0..1048576 string bytes |
| `attachment_size` | 0 | 0..1048576 bytes |
| `timeout_ms` | 1500 | 1..5000 per RPC |
| `deadline_ms` | 30000 | 1..120000 for the client run |
| `connection_type` | `single` | `single`, `pooled`, `short` |
| `omit_message` | false | Preserve absent rather than empty string |
| `corrupt_request` | false | Require schema rejection (`fb_rpc`, count mode only) |
| `duration_s` | 0 | 0 for count mode, 1..3600 for timed mode |
| `warmup_s` | 0 | 0..600 unmeasured seconds before a timed run |
| `allow_remote` | false | Accept a non-loopback `server` |
| `use_bthread` | false | Run callers as bthreads, like `multi_threaded_echo_c++ --use_bthread` |

The server defaults to `127.0.0.1:0`, `duration_s=60` (1..86400) and
`max_concurrency=32` (1..4096), and stops earlier on SIGINT/SIGTERM. Use
`--allow_remote` with a non-loopback `listen_addr`. Builtin
services are disabled. FRPC has no authentication, compression, checksums or
streaming, so this is not a production or public-network configuration.

## Timed comparison

With `--duration_s` the client keeps one synchronous RPC in flight per caller
thread, verifies every reply, and prints one JSON line with QPS, payload
throughput, latency percentiles and the client's CPU cores. Run each protocol
with identical sizes and concurrency against the same server:

```sh
"$WORK/example/benchmark_fb_server" --listen_addr=SERVER_IP:8901 --allow_remote \
  --duration_s=3600 --max_concurrency=4096
for p in fb_rpc baidu_std; do
  "$WORK/example/benchmark_fb_client" --server=SERVER_IP:8901 --allow_remote \
    --protocol=$p --duration_s=30 --warmup_s=3 --thread_num=64 --request_size=8192
done
```

Both services echo the request string and attachment; the FlatBuffers path
builds and verifies a FlatBuffers response, while the protobuf path parses and
serializes the equivalent message. The results are only comparable when
binaries, hosts, concurrency, sizes and duration are the same.

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
