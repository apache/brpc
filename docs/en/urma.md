# UrmaTransport: URMA-based Remote Memory RPC

UrmaTransport is a transport implementation that uses openEuler's
[URMA](https://atomgit.com/openeuler/umdk) (Unified Remote Memory Access) SDK
for remote-memory RPC. It is the Route B transport proposed in
[#3217](https://github.com/apache/brpc/discussions/3217) and complements the
OBMM-based UBRing transport (Route A, [#3226](https://github.com/apache/brpc/issues/3226))
for large-packet / cross-node scenarios (Route C, "double backend").

## Technical Background

URMA exposes a verbs-style API over devices supported by UMDK. This
implementation creates a reliable-message (`URMA_TM_RM`) Jetty with a CTP or RTP
transport path, posts send work requests with `urma_post_jetty_send_wr`, and
posts receive work requests with `urma_post_jfr_wr`. Completions are consumed
from a JFC either by busy polling or through a JFCE event fd.

## Build Configuration

### Build with CMake

```bash
cmake -S . -B build -DWITH_URMA=ON
cmake --build build -j8
cmake --install build --prefix "$PWD/build/install"
cmake -S example/urma_performance -B build/urma-example \
    -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build/urma-example -j8
```

CMake uses installed UMDK headers or downloads a pinned version.
`DOWNLOAD_URMA_HEADERS=OFF` disables downloading. `URMA_USE_MOCK=AUTO` selects by
`liburma` availability; `ON` uses the mock and `OFF` uses the hardware library.
Set `URMA_ROOT` or `URMA_INCLUDE_PATH` to select SDK headers for the example.

### Build with Make

```bash
sh config_brpc.sh --headers=/usr/include --libs=/usr/lib \
    --with-urma
make -j4

# Force the mock, or require the real liburma
sh config_brpc.sh --headers=/usr/include --libs=/usr/lib \
    --with-urma --with-urma-mock
sh config_brpc.sh --headers=/usr/include --libs=/usr/lib \
    --with-urma --without-urma-mock
```

Without either override, the Make configuration selects the implementation
automatically from whether `liburma` is available. `--without-urma-mock`
requires `liburma` in the configured library paths.

### Build with Bazel

```bash
# Build brpc with URMA support
bazel build --define=BRPC_WITH_URMA=true //:brpc

# Use locally declared SDK headers
bazel build --define=BRPC_WITH_URMA=true \
    --define=BRPC_DOWNLOAD_URMA_HEADERS=false //:brpc

# Build and run the URMA unit tests (using the mock, without URMA hardware)
bazel test --define=BRPC_WITH_URMA=true \
    --define=BRPC_URMA_USE_MOCK=true //test:brpc_unittests

# Use the real liburma; the linker must be able to find -lurma
bazel build --define=BRPC_WITH_URMA=true \
    --define=BRPC_URMA_USE_MOCK=false //:brpc
```

Bazel uses pinned UMDK headers through `@umdk//:urma_headers`.
With downloading disabled, declare `urma_api.h`, `urma_types.h` and `urma_ubagg.h`
through the toolchain or a local BUILD target. `BRPC_URMA_USE_MOCK=true` uses the
mock; `false` links `liburma`. The default is the mock.

## Usage

Select the transport by setting `socket_mode` on the channel / server:

```cpp
// Client
brpc::ChannelOptions opt;
opt.socket_mode = brpc::SOCKET_MODE_URMA;
opt.protocol = "baidu_std";   // URMA supports baidu_std only
brpc::Channel channel;
channel.Init("127.0.0.1:8003", &opt);

// Server
brpc::ServerOptions sopt;
sopt.socket_mode = brpc::SOCKET_MODE_URMA;
server.Start(port, &sopt);
```

If the peer does not speak URMA (e.g. a TCP-only client connecting to a
URMA-enabled server), the transport transparently falls back to TCP after
the 4-byte magic handshake. No application code change is required.

## Architecture

UrmaTransport follows the same two-layer design as `RdmaTransport` and
`UBShmTransport`:

```
UrmaTransport : public Transport         (urma_transport.{h,cpp})
  +-- std::shared_ptr<TcpTransport>      (fallback path)
  +-- urma::UrmaEndpoint*                (URMA data path)
  +-- UrmaState { URMA_ON, URMA_OFF, URMA_UNKNOWN }

urma::UrmaEndpoint : public SocketUser   (urma/urma_endpoint.{h,cpp})
  +-- UrmaResource { jfc, jfce, jfr, jetty, remote_jetty, remote_seg }
  +-- handshake state machine (C/S symmetric, driven over the TCP fd)
  +-- send path: urma_post_jetty_send_wr(URMA_OPC_SEND)
  +-- recv path: urma_poll_jfc -> HandleCompletion -> InputMessenger
  +-- two-window credit flow control
      (_remote_rq_window_size / _sq_window_size)
```

### Connection establishment (dual-plane)

Like RDMA / UBRing, the control plane is TCP and the data plane is URMA:

1. TCP connect completes.
2. `UrmaConnect::StartConnect` spawns the client handshake bthread.
3. Both sides exchange a `UrmaHello` message (magic `URMA` for v2 binary,
   `URM3` for v3 protobuf) over the TCP fd. The message carries the local
   EID, jetty id, recv buffer count, and the flattened buffer-pool segment.
4. Each side calls `urma_import_seg`, then `urma_import_jetty`, to establish the remote route.
5. A 4-byte ACK (`HELLO_ACK_URMA_OK = 0x1`) confirms both sides want URMA.
6. On success, the TCP fd is kept only for the epoll lifecycle and fallback;
   payloads flow through URMA.

### Memory management

A region allocated with `mmap` is registered with `urma_register_seg` and split
into fixed-size buffers (default 8 KB). IOBuf uses the registered-pool allocator;
the send path builds `urma_sge_t` from block refs. Manage user memory with
`RegisterMemoryForUrma` / `DeregisterMemoryForUrma`.

## Configuration

All flags use the `urma_` prefix (mirroring RDMA's `rdma_` prefix):

| Flag | Default | Purpose |
|------|---------|---------|
| `--urma_use_polling` | false | Busy-poll the JFC instead of event mode |
| `--urma_poller_num` | 1 | Poller bthreads per bthread tag (polling mode) |
| `--urma_disable_bthread` | false | Run message processing inline |
| `--urma_sq_size` | 128 | Local JFS depth [16, 4096] |
| `--urma_rq_size` | 128 | Local JFR depth [16, 4096] |
| `--urma_cqe_poll_once` | 32 | Max CQEs per `urma_poll_jfc` |
| `--urma_recv_zerocopy` | true | Zero-copy receives above `--urma_zerocopy_min_size` |
| `--urma_copy_send` | false | Copy and coalesce host payloads into a 64-byte aligned registered send buffer |
| `--urma_zerocopy_min_size` | 512 | Receives smaller than this are copied |
| `--urma_device` | "" | URMA device name (empty = first) |
| `--urma_tp_type` | auto | RM path: auto prefers CTP, then RTP; ctp or rtp selects a path explicitly |
| `--urma_max_sge` | 0 | Max SGEs per WR (0 = device max) |
| `--urma_bonding_mode` | 0 | Bonding mode: 0=standalone, 1=active-backup, 2=balance |
| `--urma_bonding_level` | 0 | Bonding level: 0=IODIE, 1=port |
| `--urma_prepared_jetty_cnt` | 8 | Requested pre-allocated Jetty+CQ sets; automatically capped according to `RLIMIT_NOFILE` |
| `--urma_buffer_size` | 8192 | Per-buffer size in the pool (bytes) |
| `--urma_buffer_count` | 65536 | Number of buffers in the pool |
| `--urma_poller_yield` | false | Yield in the busy-poll loop |
| `--urma_client_handshake_version` | 2 | Client wire version (2=binary, 3=protobuf) |

For a device whose name starts with `bonding`, brpc configures the provider
immediately after context creation and before creating segments or queues.
The default standalone+IODIE combination matches the UMDK performance tool.
Bonding support requires the provider extension header `urma_ubagg.h`.

### Transport path selection

`--urma_tp_type=auto` prefers CTP, then RTP, from device capabilities; `ctp` and
`rtp` select a path explicitly. Both peers use the same path, fixed at process
initialization. Jettys use the first matching priority, including combined
capability bits. The device maps priority to SL, such as priority 0 to SL 2.
Logs report the selected `tp_type` and `priority`.

Providers without a capability table use CTP priority 6 for auto/ctp or RTP
priority 0 for rtp. Path selection failure uses initialization's TCP fallback.

## Coexistence with UBRing

UrmaTransport is the recommended transport for **large packets and
cross-node** high-throughput paths, while UBRing (`SOCKET_MODE_UBRING`) is
optimal for **small packets and same-node IPC** (sub-microsecond, zero
syscalls). For a mixed workload, select the transport per service:

| Scenario | Recommended `socket_mode` |
|----------|---------------------------|
| Same-host IPC | `SOCKET_MODE_UBRING` |
| Cross-node small packets (< 64 KB) | `SOCKET_MODE_UBRING` (UBS-Mem) or `SOCKET_MODE_URMA` |
| Cross-node large packets (>= 64 KB) | `SOCKET_MODE_URMA` |
| Traditional RoCE / IB datacenter | `SOCKET_MODE_URMA` or `SOCKET_MODE_RDMA` |

## Runtime environment

Linux, the `baidu_std` protocol, and a UMDK provider with `liburma` for the target device.

## Runtime recovery

Configure recovery per process for new connections:

| Mode | Behavior |
| --- | --- |
| LEGACY (default) | Existing handshake and error handling |
| AUTO | Cool a failed peer, use TCP during cooldown, then resume URMA admission |
| TCP_ONLY | Select TCP |

```cpp
#include "brpc/urma/urma_recovery_policy.h"

brpc::urma::SetUrmaRecoveryCooldownMs(5000);
brpc::urma::SetUrmaRecoveryMode(brpc::urma::UrmaRecoveryMode::AUTO);
```

The default cooldown is five seconds. Sources include retry exhaustion, RNR,
JFC errors and WR submission failures. Each connection records its first runtime
failure; access errors have a separate counter, and exhausted windows return EAGAIN.
The recovery policy maintains process-level modes, epochs and peer cooldowns.
Mode changes filter reports from older connections.

A client reports a transport failure when an established URMA connection's
control TCP socket closes or fails to read, cooling the peer in AUTO mode.
Server-side EOF from a normal short-connection client simply closes the socket.

`GetUrmaRecoveryStatus()` and `/vars/urma_recovery_status` expose failure reasons,
cooldown state and negotiation counts. `/vars/urma_recovery_tcp_fallback_admissions`
counts TCP admissions per connection.

### Functional checks

Use the `urma_performance_server` and `urma_recovery_client` built above with the
same path on both peers. Client example:

```bash
./build/urma-example/urma_recovery_client --transport=urma \
    --server=SERVER_IP:8003 --urma_device=URMA_DEVICE --urma_tp_type=rtp \
    --mode=auto --connection_type=short --seconds=15 --interval_ms=200
```

Healthy AUTO requires `failure=0`, increasing URMA negotiations and zero TCP
admissions. TCP_ONLY requires `failure=0` and increasing TCP admissions.
Use `--mode_file` to switch modes while running.
Use `--connection_type=single` or `pooled` for connection reuse and
`--attachment_size=1048576` for a 1 MiB echo attachment. The client checks every
byte and reports `validation_failures` in the summary.

### Targeted send failure test

Enable the Linux injector to reject one data SEND with EIO in the selected client:

```bash
cmake -S example/urma_performance -B build/urma-example -DBUILD_URMA_FAULT_INJECTOR=ON
cmake --build build/urma-example --target urma_send_failure_injector -j8
python3 example/urma_performance/run_recovery_injection.py \
    --server=SERVER_IP:8003 --device=URMA_DEVICE --tp-type=rtp
```

The runner waits for successful URMA RPCs, checks the metric listener PID, and
saves CSV, logs and three phase snapshots. Defaults are v3, a 40-second run and
10-second cooldown. Checks require `send_post=1` and TCP admission during cooldown.
`short` requires one expected RPC failure and resumed URMA negotiations;
`--connection-type=single` or `pooled` checks sustained RPC recovery after
reconnecting and reusing TCP. The runner supports `--attachment-size` for echo
validation and `--polling` for polling mode.
Build `urma_send_failure_injector_test` and run CTest in the example build
directory for the tool self-tests.

The main library's `brpc_urma_remote_recovery_*` CTest cases inject one server
SEND failure across CTP/RTP, v2/v3 and event/polling modes. They check client
peer cooldown, TCP admission, URMA recovery and 64 KiB attachment echoes,
including normal short-connection closes before the injection.

### Comparing send access failures

`--urma_copy_send=true` coalesces each data WR into a 64-byte aligned registered
buffer held until completion, adding one CPU copy. Keep device, queue and payload
settings consistent when comparing send paths. Startup logs report `copy_send`;
failure logs report direction, send slot and registered segment range.

## Business SL and priority

Set `ChannelOptions.urma_qos` for outgoing requests and
`ServerOptions.urma_qos` for outgoing responses:

```cpp
brpc::ChannelOptions client_options;
client_options.socket_mode = brpc::SOCKET_MODE_URMA;
client_options.urma_qos.service_level = 2;

brpc::ServerOptions server_options;
server_options.socket_mode = brpc::SOCKET_MODE_URMA;
server_options.urma_qos.service_level = 2;
```

`--urma_tp_type=auto` prefers CTP, then RTP, using device capabilities.
Use `ctp` or `rtp` to select a path explicitly. Both peers use the same path,
fixed at process initialization. Providers without a capability table use
priority 6 for auto/CTP and priority 0 for RTP.

| Option | Default | Explicit range | Meaning |
| --- | --- | --- | --- |
| `service_level` | -1 | 0–255 | Select the first priority mapping to this SL on the selected path |
| `priority` | -1 | 0–15 | Select a priority supported by the selected path |

Both fields at -1 select the device default. When both are explicit, they must
match the same device mapping. Channel initialization and Server startup reject
unsupported or conflicting settings and report supported priority:SL pairs.
For example, RTP priority 0 mapping to SL 2 resolves `service_level=2` to priority 0.
The platform configures mappings and scheduling; brpc uses the initialization snapshot.

Different configurations use separate connections; equal configurations share
connections. Naming services, pooled and short connections, accepted sockets and
rebuilt Jettys inherit the configuration. The prepared pool serves the default
priority; other priorities allocate Jettys on demand. Both v2/v3 handshakes carry
the selected path.

### Functional validation

Inspect the selected path's priority:SL map with
`urma_admin show --dev "$URMA_DEVICE" --whole`.
Both examples in `example/urma_performance` accept `--urma_tp_type=rtp --urma_sl=2`
and an optional matching `--urma_priority`. Establish RPCs with defaults, then
change request and response SLs separately. Check supported mappings, unsupported
SLs and conflicting pairs across v2/v3, single/pooled and polling modes.

Connection diagnostics expose `state`, `urma_effective_priority` and
`urma_effective_sl`. Check that URMA is `ESTABLISHED`, the mapping matches the
device and RPCs succeed. Evaluate scheduling with competing traffic, device queue
counters, throughput and P99.
