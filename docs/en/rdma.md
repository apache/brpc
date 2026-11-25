# Build

RDMA currently builds and runs only on Linux and requires a compatible NIC and driver.

Using config_brpc:

```bash
sh config_brpc.sh --with-rdma --headers="/usr/include" --libs="/usr/lib64 /usr/bin"
make

cd example/rdma_performance  # Example
make
```

Using CMake:

```bash
mkdir bld && cd bld && cmake -DWITH_RDMA=ON ..
make

cd example/rdma_performance  # Example
mkdir bld && cd bld && cmake ..
make
```

Using Bazel:

```bash
# Server
bazel build --define=BRPC_WITH_RDMA=true example:rdma_performance_server
# Client
bazel build --define=BRPC_WITH_RDMA=true example:rdma_performance_client
```

# Basic Implementation

RDMA transfers data through the verbs API while reusing bRPC's Socket interface. By default, each connection has its own receive queue, referred to here as a dedicated RQ. `RdmaTransport` handles Socket integration and TCP fallback, `RdmaEndpoint` handles the handshake and message parsing, and `RqConnection` manages each connection's QP and dedicated RQ/CQ. EventDispatcher dispatches completion notifications. `RqConnection::PollCq` processes CQ entries and passes received data to InputMessenger through `RdmaEndpoint::Deliver`. See the [dedicated RQ implementation](../../src/brpc/rdma/rq_connection.cpp).

The dedicated RQ implementation precreates a batch of QPs and CQs for new connections. These resources are destroyed when a connection closes, rather than resetting the QP and returning it to the pool. This prevents asynchronous events from an old connection from affecting a new one. Once the precreated resources are used up, new connections create their own QPs and CQs.

bRPC uses RDMA RC mode, with one QP per `RdmaEndpoint`. Connection setup first exchanges parameters such as GID and QPN over TCP, then establishes the RDMA connection. This handshake reuses bRPC's AppConnect mechanism. After RDMA is established, the TCP connection remains in the EST state but no longer carries application data. Closing the TCP connection also fails the corresponding RDMA connection.

Each new logical connection starts with handshake v3, with no version configuration required. If the server closes or resets the connection before returning v3 magic, or replies with v2 magic, the client reconnects and retries v2 once. TCP connection establishment, the handshake, and reconnection share the original connection deadline. Malformed replies identified as v3, handshake timeouts, and failed v2 retries fail the connection. A normal server request to fall back to TCP continues on the existing connection.

`RqConnection` uses IOBuf to support zero-copy sends and receives. On send, `_sbuf` retains references to data blocks until completion is confirmed. On receive, `_rbuf` holds preallocated IOBuf blocks, which are submitted to the NIC through receive requests. Receive blocks have a fixed size, using 8 KiB blocks by default. A single send work request (WR) cannot exceed the usable capacity of the peer's receive block. Larger RPCs are split into fragments that fit this limit.

`RqConnection` uses a sliding window to limit sends so that the sender does not exceed the peer's receive capacity. The receiver returns send credits through ACKs. ACKs use immediate data and can accompany data sent in the reverse direction, reducing the need for separate control messages.

To reduce notifications, `RqConnection` selects which send requests carry the solicited flag based on data volume, remaining window capacity, and ACK count. This controls receive completion notifications at the peer. The sender also uses batched completion reporting to reduce the number of CQEs.

RDMA buffers need to be registered with the NIC before use. Registration is expensive, so bRPC manages ordinary IOBuf blocks through a registered memory pool. See the [memory pool implementation](../../src/brpc/rdma/block_pool.cpp). Sizing the initial pool for peak IOBuf usage can reduce runtime allocation and registration costs.

Applications can also manage their own send buffers. Register the memory with `rdma::RegisterMemoryForRdma`, then pass the returned lkey as the meta argument to `IOBuf::append_user_data_with_meta`. See the [memory registration interface](../../src/brpc/rdma/rdma_helper.h).

During initialization, bRPC queries NIC capabilities and selects the device, port, GID, and other parameters. If the defaults do not suit the network, use the configuration parameters below to override them. See the [RDMA initialization implementation](../../src/brpc/rdma/rdma_helper.cpp).

The dedicated RQ implementation uses event mode by default. Set `rdma_use_polling=true` to enable polling. `rdma_poller_num` controls the number of pollers, and `rdma_poller_yield` controls whether they voluntarily yield the CPU. Polling mode also supports a callback on each iteration for integration with systems such as io_uring or SPDK.

`event_dispatcher_edisp_unsched` affects EventDispatcher in both TCP and RDMA modes. It defaults to `false`, meaning EventDispatcher is schedulable. Setting it to `true` makes EventDispatcher unschedulable.

# Shared Receive Queue (SRQ)

SRQ (Shared Receive Queue) lets multiple connections share a receive queue and preallocated buffers, reducing receive resource usage. Each connection still has its own RC QP and send queue (SQ). Connections on the same worker share the SRQ, receive completion queue (RX CQ), send completion queue (TX CQ), and receive memory pool.

Each bthread tag has four workers by default, created on demand, and each connection is assigned to one worker. A worker manages one SRQ and processes its queues in a bthread when events arrive. It does not occupy a dedicated persistent thread.

`RdmaEndpoint` selects `RqConnection` (dedicated RQ/CQ) or `SrqConnection` (shared SRQ/CQ) through the [RdmaConnection interface](../../src/brpc/rdma/rdma_connection.h). Both pass data to InputMessenger through `RdmaEndpoint::Deliver`. Enabling SRQ skips QP precreation for the dedicated RQ mode.

## Use Cases

SRQ suits RPC workloads with many connections and sparse requests. For example, a client may connect to many shards or downstream nodes while only a few connections are active at a time. With dedicated RQs, every connection reserves receive blocks and CQs, so memory use grows with the connection count. SRQ shares these resources at the worker level, allowing capacity to be sized for concurrent activity.

For sustained high concurrency on a single connection, or a few continuously busy connections, SRQ saves less memory, and contention for shared resources may affect throughput and tail latency. Compare memory use, CPU use, throughput, and P50/P99 latency under the expected connection count, active fraction, message sizes, and bursts.

## Advantages

- **Lower receive resource usage**: Connections share receive blocks and CQs, avoiding separate receive buffer allocations for every idle connection. QPs, SQs, send records, and Socket state are still allocated per connection.
- **No credit request before the first send**: The handshake establishes the initial send window, so the connection can send data immediately. Later, soft requests and hard requests ask the peer for more send credits through grants. A grant can accompany reverse-direction data or be sent as a separate control message when needed.
- **Zero-copy support**: Sends can reference registered IOBuf data directly. With `rdma_recv_zerocopy` enabled, a received fragment can be passed to the application without copying if it is at least `rdma_zerocopy_min_size` bytes and enough free blocks remain. Small fragments, or fragments received when free blocks are scarce, are copied into ordinary IOBufs so the original receive blocks can be reused immediately. This prevents incomplete messages or long-lived attachments from occupying all receive buffers.

## Limitations

- **Contention between connections**: Connections on a worker share receive capacity, TX CQ credits, and CQ processing capacity. Concurrent bursts may wait for send credits or trigger RNR retries when the peer has no receive request available. TX CQ credit allocation does not currently guarantee strict fairness between connections. A fatal error in a shared CQ or SRQ affects every connection on that worker.
- **Event mode and v3 handshake only**: `rdma_use_srq` and `rdma_use_polling` cannot both be enabled. The NIC and driver must support RC, SRQ, and the configured queue capacities. bRPC queries device capabilities through verbs and does not restrict device names.
- **Handshake compatibility**: Upgrade servers before enabling v3 and SRQ on clients. An SRQ server still accepts dedicated RQ connections. If a peer supports v3 but has SRQ disabled, the connection falls back to TCP, not dedicated RQ. Fallback is not guaranteed with older servers that do not support v3.

## Usage

With RDMA enabled, set `rdma_use_srq=true` on both peers, set `rdma_client_handshake_version=3` on the client, and keep `rdma_use_polling=false`. See [SRQ Flags and Parameters](#srq-flags-and-parameters).

## Capacity Planning

`rdma_recv_block_type` controls receive block size, which defaults to 8 KiB. With the default settings, each worker's 2048 SRQ receive buffers occupy about 16 MiB, and its receive pool can grow to about 128 MiB. Four workers can therefore use about 512 MiB for receive pools. These figures count only receive block memory. Metadata, QPs, SQs, CQs, and the ordinary IOBuf pool add further overhead.

Initialization preallocates `rdma_srq_depth` receive blocks plus spare blocks. When free blocks run low, a background task grows the pool in segments. `rdma_srq_pool_blocks` is a capacity limit, not the initial allocation size, an RPC message size limit, or a process memory limit. Allocated segments are not currently released automatically when idle. IOBufs used to copy received data come from the ordinary memory pool, controlled by `rdma_memory_pool_*`.

When tuning these parameters:

- `rdma_srq_pool_blocks` must exceed `rdma_srq_depth`. Leave enough spare blocks to continue receiving without copying while the application or parser holds receive data. When spare blocks run low, receives use copying instead.
- `rdma_srq_window` controls send credits per connection. It does not reserve an equivalent number of physical receive blocks for that connection. Size the receive queue for bursts from concurrently active connections. Increasing only the send window may add pressure to the receive queue.
- `rdma_srq_tx_cq_size` must exceed `rdma_srq_control_reserve`. The number of send requests that can be posted is also limited by each connection's SQ and send window. Increasing just one limit may not improve throughput.
- Increasing `rdma_srq_workers` distributes connection processing across more workers but also increases memory use for SRQs, CQs, and receive pools. Each connection remains assigned to one worker.

`rdma_sq_size`, `rdma_rq_size`, and `rdma_cqe_poll_once` apply to dedicated RQs, not SRQ. SRQ reads at most 32 CQEs per TX or RX CQ poll. The global `rdma_max_sge` setting still limits the number of SGEs in SRQ sends. Receive depth, CQ capacity, SGE count, and inline size are also constrained by NIC and driver capabilities.

# Parameters

Configurable parameters:
* `rdma_trace_verbose`: Log RDMA connection setup details. Default: `false`.
* rdma_client_handshake_version: deprecated and ignored. Retained for compatibility with existing command-line arguments. Clients always start with v3 and reconnect to retry v2 once if the peer rejects v3.
* `rdma_recv_zerocopy`: Enable zero-copy receives. Default: `true`.
* `rdma_zerocopy_min_size`: Minimum fragment size for zero-copy receives. Default: 512 B.
* `rdma_recv_block_type`: Receive buffer block type. Choices are `default` (8 KiB), `large` (64 KiB), and `huge` (2 MiB). Default: `default`.
* `rdma_prepared_qp_size`: Queue size of precreated QPs. Default: 128.
* `rdma_prepared_qp_cnt`: Number of precreated QPs. Default: 1024.
* `rdma_max_sge`: Maximum number of SGEs per send request. Default: 0, meaning the device's supported limit.
* `rdma_sq_size`: SQ size. Default: 128.
* `rdma_rq_size`: RQ size. Default: 128.
* `rdma_cqe_poll_once`: Maximum number of CQEs retrieved per CQ poll. Default: 32.
* `rdma_gid_index`: Local GID table index. Default: -1, meaning the highest available index.
* `rdma_port`: IB device port number. Default: 1.
* `rdma_device`: IB device name. Default: empty, meaning the first active device.
* `rdma_memory_pool_initial_size_mb`: Initial memory pool size in MB. Default: 1024.
* `rdma_memory_pool_increase_size_mb`: Memory pool growth increment in MB. Default: 1024.
* `rdma_memory_pool_max_regions`: Maximum number of memory pool regions. Default: 3.
* `rdma_memory_pool_buckets`: Number of memory pool buckets used to distribute lock contention. Default: 4.
* `rdma_memory_pool_tls_cache_num`: Number of blocks cached per thread. Default: 128.
* `rdma_use_polling`: Enable RDMA polling mode. Default: `false`.
* `rdma_poller_num`: Number of pollers. Default: 1.
* `rdma_poller_yield`: Whether pollers voluntarily yield the CPU. Default: `false`.
* `rdma_client_handshake_version`: Client RDMA handshake version. Default: 2. Set to 3 for SRQ. The server detects the version from the received message.
* `event_dispatcher_edisp_unsched`: Control whether EventDispatcher is unschedulable. Default: `false`. Set to `true` to make it unschedulable.
* `rdma_disable_bthread`: Disable bthread. Default: `false`.

## SRQ Flags and Parameters

These settings are specific to SRQ and should be configured at process startup. Unless otherwise stated, capacities are **per worker**:

* `rdma_use_srq`: Enable SRQ. Default: `false`. Both peers must enable it and use the v3 handshake. Enabling this flag while keeping the default v2 handshake still uses dedicated RQ.
* `rdma_srq_workers`: Number of workers per bthread tag. Default: 4.
* `rdma_srq_depth`: Target number of receive WRs posted to the SRQ. Also sets the requested RX CQ capacity. Default: 2048.
* `rdma_srq_pool_blocks`: Maximum number of receive pool blocks, including blocks used for NIC receives, free blocks, and blocks held by the application. Default: 16384.
* `rdma_srq_pool_grow_blocks`: Number of blocks added by each runtime background growth operation. Default: 256.
* `rdma_srq_window`: Send window advertised to the peer for each connection, used initially and restored by a grant. Measured in data WRs. Default: 512.
* `rdma_srq_sq_size`: SQ depth per connection, including slots reserved for control messages. Default: 128.
* `rdma_srq_tx_cq_size`: Capacity requested when creating the shared TX CQ. Default: 8192.
* `rdma_srq_control_reserve`: TX CQ credits reserved for standalone grant control messages. Default: 64.
* `rdma_srq_max_sge`: Maximum number of SGEs per send WR, also limited by the global `rdma_max_sge` setting and device capabilities. Default: 4.
* `rdma_srq_inline_size`: Inline size in bytes requested when creating a QP. Default: 128.
* `rdma_srq_pool_growth_retry_ms`: Retry delay after a background memory allocation or registration failure, in milliseconds. Default: 100.
* `rdma_srq_pool_hugepages`: Attempt 2 MiB alignment and request transparent huge pages. The kernel decides whether huge pages are used. Default: `false`.
