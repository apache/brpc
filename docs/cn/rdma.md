# 编译

RDMA 功能目前仅支持在 Linux 上编译和运行，需要相应的网卡与驱动。

使用 config_brpc：

```bash
sh config_brpc.sh --with-rdma --headers="/usr/include" --libs="/usr/lib64 /usr/bin"
make

cd example/rdma_performance  # 示例程序
make
```

使用 CMake：

```bash
mkdir bld && cd bld && cmake -DWITH_RDMA=ON ..
make

cd example/rdma_performance  # 示例程序
mkdir bld && cd bld && cmake ..
make
```

使用 Bazel：

```bash
# Server
bazel build --define=BRPC_WITH_RDMA=true example:rdma_performance_server
# Client
bazel build --define=BRPC_WITH_RDMA=true example:rdma_performance_client
```

# 基本实现

RDMA 通过 verbs API 传输数据，复用 bRPC 的 Socket 接口。默认每条连接使用独立的接收队列，称为专用 RQ。`RdmaTransport` 负责 Socket 适配和 TCP 回退，`RdmaEndpoint` 负责握手和消息解析，`RqConnection` 管理每条连接的 QP 和专用 RQ/CQ。完成通知由 EventDispatcher 分发，`RqConnection::PollCq` 处理 CQ 中的完成记录，再通过 `RdmaEndpoint::Deliver` 将收到的数据交给 InputMessenger 解析。参见 [专用 RQ 实现](../../src/brpc/rdma/rq_connection.cpp)。

专用 RQ 会预先创建一批 QP/CQ，供新连接使用。连接关闭后直接销毁这些资源，不再重置 QP 后放回池中，避免旧连接的异步事件影响新连接。预先创建的资源用完后，新连接需要重新创建 QP/CQ。


bRPC 使用 RDMA RC 模式，每个 `RdmaEndpoint` 对应一个 QP。建连时先通过 TCP 交换 GID、QPN 等参数，再建立 RDMA 连接，这个握手过程复用了 bRPC 的 AppConnect 逻辑。RDMA 连接建立后，原来的 TCP 连接保持 EST 状态，不再传输业务数据。TCP 连接断开时，对应的 RDMA 连接也会置错。

客户端每次新建逻辑连接都先使用 v3 握手，无需配置版本。如果服务端在返回 v3 标识之前断开或重置连接，或返回 v2 标识，客户端会新建 TCP 连接并用 v2 重试一次。TCP 建连、握手及重连共用原始连接截止时间。已经识别为 v3 的报文损坏、握手超时或 v2 重试失败时，连接失败；服务端正常要求回退 TCP 时，客户端继续使用当前连接。

`RqConnection` 使用 IOBuf 支持零拷贝收发。发送时，`_sbuf` 保存数据块的引用，直到确认发送完成后才释放。接收时，预先分配的 IOBuf 块由 `_rbuf` 持有，并通过接收请求提交给网卡。每个接收块的大小固定，默认使用 8 KiB 的块。单个发送工作请求（WR）的数据长度不能超过对端接收块的有效容量，较大的 RPC 按此上限分片。

`RqConnection` 通过滑动窗口限制发送量，避免发送速度超过对端的接收能力。接收端通过 ACK 归还发送额度。ACK 使用立即数传递，可以随反向数据一起发送，减少单独发送控制消息的开销。

为减少事件通知，`RqConnection` 根据数据量、窗口余量和 ACK 数量，选择哪些发送请求设置 solicited 标志，以控制对端的接收完成通知。发送端也采用批量完成通知，减少 CQE 数量。

RDMA 收发缓冲区使用前需要向网卡注册内存。注册开销较大，因此 bRPC 通过注册内存池统一管理普通 IOBuf 块，参见 [内存池实现](../../src/brpc/rdma/block_pool.cpp)。可以根据 IOBuf 的峰值用量设置初始池大小，减少运行时分配和注册内存的开销。

应用也可以自行管理发送内存。先通过 `rdma::RegisterMemoryForRdma` 注册，再调用 `IOBuf::append_user_data_with_meta`，将返回的 lkey 作为 meta 传入。参见 [内存注册接口](../../src/brpc/rdma/rdma_helper.h)。

初始化时，bRPC 会读取网卡能力并选择设备、端口和 GID 等参数。如果默认值不适合当前网络，可以通过下文的配置参数指定。参见 [RDMA 初始化实现](../../src/brpc/rdma/rdma_helper.cpp)。

专用 RQ 默认采用事件驱动，设置 `rdma_use_polling=true` 可开启轮询模式。`rdma_poller_num` 控制轮询器数量，`rdma_poller_yield` 控制轮询器是否主动让出 CPU。轮询模式还支持每轮执行一个回调，可用于配合 io_uring、SPDK 等。

`event_dispatcher_edisp_unsched` 同时影响 TCP 和 RDMA 的 EventDispatcher。默认值为 `false`，EventDispatcher 可被调度，设为 `true` 时不可被调度。

# 共享接收队列（SRQ）

SRQ（Shared Receive Queue）让多个连接共享接收队列和预分配的缓冲区，减少接收资源占用。每个连接仍有独立的 RC QP 和发送队列（SQ），同一个 worker 上的连接共享 SRQ、接收完成队列（RX CQ）、发送完成队列（TX CQ）和接收内存池。

每个 bthread tag 下默认有 4 个 worker，按需创建，连接分配到其中一个 worker。每个 worker 管理一个 SRQ，收到事件时由 bthread 处理队列，不占用专门的常驻线程。

`RdmaEndpoint` 通过 [RdmaConnection 接口](../../src/brpc/rdma/rdma_connection.h) 选择 `RqConnection`（专用 RQ/CQ）或 `SrqConnection`（共享 SRQ/CQ），两条路径都通过 `RdmaEndpoint::Deliver` 将数据交给 InputMessenger 解析。开启 SRQ 时跳过专用 RQ 模式的 QP 预建。

## 适用场景

SRQ 适用于海量连接、稀疏请求的 RPC 场景。例如，客户端需要连接大量分片或下游节点，但同一时刻只有少量连接在收发请求，大部分连接处于空闲状态。专用 RQ 为每条连接预留接收块和 CQ，内存随连接数增长。SRQ 将这些资源集中到 worker 上，按同时活跃的请求量配置容量。

对于单连接持续高并发、少量连接持续满载的负载，SRQ 节省的内存有限，共享资源竞争还可能影响吞吐和尾延迟。选择时，应按实际连接数、活跃比例、消息大小和突发并发，比较两种模式的内存、CPU、吞吐及 P50/P99 延迟。

## 优势

- **节省接收资源**：多个连接共享接收块和 CQ，无须为每条空闲连接单独分配接收缓冲区。QP、SQ、发送记录和 Socket 状态仍按连接分配。
- **首次发送无需申请额度**：握手时确定初始发送窗口，连接可以直接发送数据。后续通过 soft request / hard request 请求新的发送额度，由对端返回 grant。grant 可随反向数据返回，必要时单独发送控制消息。
- **支持零拷贝**：发送可直接引用已注册的 IOBuf 数据。启用 `rdma_recv_zerocopy` 后，接收片段不小于 `rdma_zerocopy_min_size` 且空闲块足够时，可以直接把接收块交给上层。小片段或空闲块不足时，将数据复制到普通 IOBuf，原接收块立即复用。这可以防止未收完的消息或长期持有的附件占满接收缓冲区。

## 限制

- **连接间有资源竞争**：同一 worker 的连接共用接收队列、TX CQ 额度和 CQ 处理能力。大量连接同时发送时，可能需要等待发送额度，或因对端暂无接收请求而触发 RNR 重试。TX CQ 额度目前不保证在连接间严格公平分配。共享 CQ 或 SRQ 的致命错误会影响该 worker 上的所有连接。
- **仅支持事件模式和 v3 握手**：不能同时开启 `rdma_use_srq` 与 `rdma_use_polling`。网卡及驱动需要支持 RC、SRQ 和配置的队列容量，bRPC 通过 verbs 查询设备能力，不限制网卡名称。
- **握手兼容性**：先升级服务端，再开启客户端的 v3/SRQ。SRQ 服务端仍可接受专用 RQ 连接。若对端支持 v3 但未启用 SRQ，则回退到 TCP，不会改用专用 RQ。不支持 v3 的旧服务端无法保证回退。

## 使用方法

启用 RDMA 后，两端设置 `rdma_use_srq=true`，客户端设置 `rdma_client_handshake_version=3`，并保持 `rdma_use_polling=false`。开关说明见 [SRQ 开关与参数](#srq-开关与参数)。

## 容量规划

`rdma_recv_block_type` 控制接收块大小，默认每块 8 KiB。按默认配置，每个 worker 用于 SRQ 接收的 2048 个缓冲区约占 16 MiB，接收池最多占用约 128 MiB，4 个 worker 合计最多约 512 MiB。这些数值只计算接收块内存，元数据、QP/SQ/CQ 和普通 IOBuf 内存池等开销需要另外计算。

初始化时按 `rdma_srq_depth` 预分配接收块，并分配一批备用块。运行中空闲块不足时，后台按段扩容。`rdma_srq_pool_blocks` 是容量上限，不表示启动时的分配量，也不限制 RPC 消息大小或进程总内存。已分配的分段目前不会因空闲自动释放。复制接收数据所用的 IOBuf 来自普通内存池，由 `rdma_memory_pool_*` 参数管理。

调整参数时需要注意：

- `rdma_srq_pool_blocks` 必须大于 `rdma_srq_depth`。留足备用块，才能在业务或解析器持有接收数据时继续零拷贝。备用块不足时会改用复制。
- `rdma_srq_window` 表示每条连接的发送额度，不对应为该连接预留的物理接收块。接收深度应按同时活跃连接的突发流量设置，单纯增大发送窗口可能加重接收队列压力。
- `rdma_srq_tx_cq_size` 必须大于 `rdma_srq_control_reserve`。可提交的发送请求数还受每连接 SQ 和发送窗口限制，只增加其中一个参数不一定提高吞吐。
- 增加 `rdma_srq_workers` 可以让更多 worker 分担不同连接的处理工作，但也会增加 SRQ、CQ 和接收池的内存开销。同一连接始终由一个 worker 处理。

`rdma_sq_size`、`rdma_rq_size` 和 `rdma_cqe_poll_once` 用于专用 RQ，不控制 SRQ。SRQ 每次查询 TX CQ 或 RX CQ 最多读取 32 个 CQE。全局 `rdma_max_sge` 仍会限制 SRQ 发送的 SGE 数。接收深度、CQ 容量、SGE 和 inline 大小还受网卡及驱动能力限制。

# 参数

可配置参数如下：
* `rdma_trace_verbose`：打印 RDMA 建连日志，默认 `false`。
* rdma_client_handshake_version: 已废弃，仅为兼容旧启动参数保留。该值不再参与版本选择；客户端始终先尝试 v3，并在对端拒绝 v3 后重连并用 v2 重试一次。
* `rdma_recv_zerocopy`：启用接收零拷贝，默认 `true`。
* `rdma_zerocopy_min_size`：接收零拷贝的最小片段大小，默认 512 B。
* `rdma_recv_block_type`：接收缓冲区的块类型，可选 `default`（8 KiB）、`large`（64 KiB）或 `huge`（2 MiB），默认 `default`。
* `rdma_prepared_qp_size`：预先创建的 QP 队列大小，默认 128。
* `rdma_prepared_qp_cnt`：预先创建的 QP 数量，默认 1024。
* `rdma_max_sge`：每个发送请求允许的最大 SGE 数，默认 0，表示使用设备支持的上限。
* `rdma_sq_size`：SQ 大小，默认 128。
* `rdma_rq_size`：RQ 大小，默认 128。
* `rdma_cqe_poll_once`：每次从 CQ 获取的最大 CQE 数，默认 32。
* `rdma_gid_index`：本地 GID 表的索引，默认 -1，表示选择最大的可用索引。
* `rdma_port`：IB 设备的端口号，默认 1。
* `rdma_device`：IB 设备名称，默认为空，表示使用第一个 active 的设备。
* `rdma_memory_pool_initial_size_mb`：内存池初始大小，单位为 MB，默认 1024。
* `rdma_memory_pool_increase_size_mb`：内存池每次扩容的大小，单位为 MB，默认 1024。
* `rdma_memory_pool_max_regions`：内存池的最大分段数，默认 3。
* `rdma_memory_pool_buckets`：内存池用于分散锁竞争的 bucket 数量，默认 4。
* `rdma_memory_pool_tls_cache_num`：每个线程缓存的块数，默认 128。
* `rdma_use_polling`：启用 RDMA 轮询模式，默认 `false`。
* `rdma_poller_num`：轮询器数量，默认 1。
* `rdma_poller_yield`：轮询器是否主动让出 CPU，默认 `false`。
* `rdma_client_handshake_version`：客户端 RDMA 握手版本，默认 2。使用 SRQ 时设为 3，服务端根据收到的报文识别版本。
* `event_dispatcher_edisp_unsched`：控制 EventDispatcher 是否不可被调度，默认 `false`。设为 `true` 时不可被调度。
* `rdma_disable_bthread`：禁用 bthread，默认 `false`。

## SRQ 开关与参数

以下为 SRQ 专用配置，在进程启动时设置。除另有说明外，容量均按**每个 worker**计算：
* `rdma_use_srq`：SRQ 开关，默认 `false`。需两端启用并使用 v3 握手，仅开启此项而保留默认 v2 握手仍会使用专用 RQ。
* `rdma_srq_workers`：每个 bthread tag 的 worker 数量，默认 4。
* `rdma_srq_depth`：SRQ 中接收请求（WR）的目标数量，也用于配置 RX CQ 容量，默认 2048。
* `rdma_srq_pool_blocks`：接收内存池的块数上限，包含供网卡接收、空闲和上层持有的块，默认 16384。
* `rdma_srq_pool_grow_blocks`：运行时每次后台扩容的块数，默认 256。
* `rdma_srq_window`：每条连接告知对端的发送窗口，初始及收到 grant 后使用此值，单位为数据 WR，默认 512。
* `rdma_srq_sq_size`：每连接 SQ 深度，包含控制消息预留槽位，默认 128。
* `rdma_srq_tx_cq_size`：创建共享 TX CQ 时指定的容量，默认 8192。
* `rdma_srq_control_reserve`：TX CQ 中为独立 grant 控制消息预留的额度，默认 64。
* `rdma_srq_max_sge`：每个发送 WR 的最大 SGE 数，还受全局 `rdma_max_sge` 和设备能力限制，默认 4。
* `rdma_srq_inline_size`：创建 QP 时请求的 inline 字节数，默认 128。
* `rdma_srq_pool_growth_retry_ms`：后台分配或注册内存失败后的重试间隔，单位为毫秒，默认 100。
* `rdma_srq_pool_hugepages`：尝试按 2 MiB 对齐内存并申请透明大页，是否使用大页由内核决定，默认 `false`。
