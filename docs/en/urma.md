# UrmaTransport: URMA-based Remote Memory RPC

UrmaTransport is a transport implementation that uses openEuler's
[URMA](https://atomgit.com/openeuler/umdk) (Unified Remote Memory Access) SDK
for remote-memory RPC. It is the Route B transport proposed in
[#3217](https://github.com/apache/brpc/discussions/3217) and complements the
OBMM-based UBRing transport (Route A, [#3226](https://github.com/apache/brpc/issues/3226))
for large-packet / cross-node scenarios (Route C, "double backend").

## Technical Background

URMA exposes a verbs-style API over devices supported by UMDK. This
implementation creates a reliable-message (`URMA_TM_RM`) Jetty with a CTP
transport path, posts send work requests with `urma_post_jetty_send_wr`, and
posts receive work requests with `urma_post_jfr_wr`. Completions are consumed
from a JFC either by busy polling or through a JFCE event fd.

## Build Configuration

### Build with CMake

```bash
# Build brpc with URMA support
cmake -B build -DWITH_URMA=ON
make -C build -j$(nproc)

# Build the urma_performance example
cd example/urma_performance
cmake -B build
make -C build -j$(nproc)
```

`WITH_URMA=ON` compiles against upstream UMDK headers. CMake prefers an
installed SDK and, following Mooncake's mock setup, downloads a pinned UMDK
release when the headers are unavailable. Set `DOWNLOAD_URMA_HEADERS=OFF` to
disable downloading.
When `liburma` is found it is linked for the hardware data path. Otherwise,
brpc uses its link-time mock so URMA code and tests can still be built without
hardware.

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
4. Each side calls `urma_import_seg` **before** `urma_import_jetty` to
   establish transport-path (TP) routing for the remote EID. Skipping the
   `import_seg` step causes the first SEND to be rejected by hardware with
   `URMA_CR_RNR_RETRY_CNT_EXC_ERR`.
5. A 4-byte ACK (`HELLO_ACK_URMA_OK = 0x1`) confirms both sides want URMA.
6. On success, the TCP fd is kept only for the epoll lifecycle and fallback;
   payloads flow through URMA.

### Memory management

A single large region is `mmap`-ed and registered once with
`urma_register_seg`, then sliced into fixed-size buffers (default 8 KB).
`butil::iobuf::blockmem_allocate` is hijacked so every IOBuf block is backed
by the registered segment, allowing the send path to build `urma_sge_t`
directly from IOBuf block refs without per-message registration (mirroring the
RDMA `block_pool` design). User-registered memory is supported via
`urma::RegisterMemoryForUrma` / `DeregisterMemoryForUrma`.

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
| `--urma_zerocopy_min_size` | 512 | Receives smaller than this are copied |
| `--urma_device` | "" | URMA device name (empty = first) |
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

A single-connection hybrid that auto-routes by packet size (Route C, scheme B)
is tracked as a future enhancement.

## Limitations

- `baidu_std` protocol only (same as RDMA). SSL, RTMP, NSHEAD, MONGO are
  rejected at `ContextInitOrDie`.
- A hardware data path requires a supported UMDK provider and `liburma`.
- The current implementation targets Linux.
