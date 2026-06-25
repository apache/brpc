# io_uring 支持

## 编译

io_uring 依赖内核 5.1+（推荐 5.10+），仅支持 Linux 系统。需要先安装或编译 [liburing](https://github.com/axboe/liburing)。

### 准备 liburing

```bash
# 方式一：发行版包管理（推荐）
sudo apt install liburing-dev        # Debian / Ubuntu
sudo yum install liburing-devel      # CentOS / RHEL

# 方式二：源码编译
git clone https://github.com/axboe/liburing.git /path/to/liburing
cd /path/to/liburing && make
```

### 使用 CMake

CMake 查找 liburing 的顺序：

1. **标准系统路径**（`/usr/include`、`/usr/local/include`、`/usr/lib` 等）——通过包管理安装后直接找到，无需额外参数。
2. **CMake 全局搜索**——查找失败时自动回退，覆盖 `CMAKE_PREFIX_PATH` 指定的自定义安装前缀。
3. **显式指定**——若以上均失败，可手动通过 `-DIOURING_INCLUDE_PATH` / `-DIOURING_LIB` 覆盖。

**方式一：包管理安装（推荐）**，直接构建，无需额外参数：

```bash
mkdir bld && cd bld
cmake -DWITH_IOURING=ON .. && make
```

**方式二：liburing 安装在非标准前缀**，通过 `CMAKE_PREFIX_PATH` 指定：

```bash
cmake -DWITH_IOURING=ON -DCMAKE_PREFIX_PATH=/path/to/liburing/install .. && make
```

**方式三：从源码编译的 liburing**，显式指定头文件和库路径：

```bash
cmake -DWITH_IOURING=ON \
      -DIOURING_INCLUDE_PATH=/path/to/liburing/src/include \
      -DIOURING_LIB=/path/to/liburing/src/liburing.a ..
make
```

> **注意**：显式变量名为 `IOURING_INCLUDE_PATH` 和 `IOURING_LIB`，不是 `LIBURING_INCLUDE_PATH` / `LIBURING_LIBRARY`，拼写错误会触发 CMake Warning 且实际不生效。

#### 编译示例程序

```bash
cd example/iouring_echo_c++
mkdir bld && cd bld
cmake -DBRPC_WITH_IOURING=ON ..
make
```

### 使用 Bazel

Bazel 构建通过 `--define=BRPC_WITH_IOURING=true` 开关启用 io_uring 支持。liburing 通过 `http_archive` 从 GitHub 自动下载（当前锁定到 liburing-2.14），无需手动准备源码。

#### 编译 brpc 库

```bash
bazel build //:brpc --define=BRPC_WITH_IOURING=true
```

#### 编译示例程序

```bash
bazel build //example:iouring_echo_server //example:iouring_echo_client --define=BRPC_WITH_IOURING=true
```

---

## 基本原理

io_uring 通过内核与用户态共享的提交队列（SQ）和完成队列（CQ）实现异步 I/O，避免了每次 I/O 的系统调用开销。brpc 的实现复用了原有的 `Socket` 类，每个 Socket 持有一个 `IouringEndpoint`（`src/brpc/iouring/iouring_endpoint.cpp`）。

- **写路径**：`CutFromIOBufList` 将 `IOBuf` 中的数据段提交为 `IORING_OP_WRITEV`（或注册模式下的 `IORING_OP_WRITE_FIXED`）SQE。
- **读路径**：`SubmitRead` 提交 `IORING_OP_READ`（或 `IORING_OP_READ_FIXED`）SQE，Poller 线程通过 `PollCq` 收割 CQE，复用 `InputMessenger` 完成消息解析。

### one-thread-per-ring 架构

每个 `bthread_tag` 拥有一个 `PollerGroup`，包含**恰好一个** Poller bthread，持有一个独立的 `io_uring` 实例（ring）。

**所有 SQ 操作（`SubmitRead`、`CutFromIOBufList`）都在 Poller bthread 上执行**，不需要任何锁。新连接通过 MPSC 队列（`op_queue`）发送 ADD/REMOVE 消息给 Poller，由 Poller 在主循环中处理。

> **为什么每个 tag 只能有一个 Poller？**
> io_uring 的 SQ 是单生产者设计，不支持并发写入。bthread 采用 work-stealing 调度，同一 `bthread_tag` 内的 bthread 会在多个 pthread 上运行。若同一 tag 存在多个 Poller，两个 Poller bthread 可能被 steal 到不同 pthread 并发操作各自 ring 的 SQ，同时业务 bthread 也可能在另一个 pthread 上提交 SQE，产生竞争。因此**一个 bthread_tag 只能有一个 Poller**，水平扩展应通过增加 bthread_tag 数量（`--task_group_ntags`）实现，每个 tag 独占一个 Poller 和一个 ring。

### CQE 收割策略

通过 `--iouring_polling_mode` 选择 Poller 线程的收割策略：

- **none**（默认）：中断驱动模式，Poller 调用 `io_uring_wait_cqe_timeout`（超时 1 ms）阻塞等待内核通知。1 ms 超时的目的是保持 Poller 循环对 `op_queue` 中新连接的响应性，同时在无 I/O 时避免 CPU 空转。适合通用场景。
- **sqpoll**：启用 `IORING_SETUP_SQPOLL`，内核线程持续轮询 SQ，无需 `io_uring_submit` 系统调用，延迟最低，需要 `CAP_SYS_NICE`。
- **iopoll**：`IORING_SETUP_IOPOLL`，仅对 O_DIRECT 块设备有效。
- **hybrid**：先忙转 N 次（`--iouring_hybrid_spin_count`），无 CQE 后再阻塞，兼顾延迟和 CPU 利用率。

---

## 启动与初始化

在调用 `brpc::Server::Start()` 之前完成 io_uring 初始化：

```cpp
#include <brpc/iouring/iouring_helper.h>

// 1. 全局初始化：探测内核能力，初始化内存池（若启用了 --iouring_register_buffers）
brpc::iouring::GlobalIouringInitializeOrDie();

// 2. 为指定 bthread tag 创建 ring 并启动 Poller bthreads
//    tag=0 为默认 tag，覆盖所有普通 bthread
if (!brpc::iouring::InitPollingModeWithTag(/*tag=*/0)) {
    LOG(FATAL) << "Failed to init io_uring";
}
```

`InitPollingModeWithTag` 支持三个可选回调，均在 Poller 线程上调用：

```cpp
brpc::iouring::InitPollingModeWithTag(
    /*tag=*/0,
    /*callback=*/[](brpc::iouring::IouringPollerHandle h) {
        // 每次 PollCq 之后调用，用于提交用户自定义 SQE 或收割用户 CQE。
        // 注意：bRPC 的 PollCq 会跳过 user_data bit63=0 的 CQE 且不调
        // io_uring_cqe_seen()，用户必须在此处手动 drain，否则 CQ 会满。
        h.Submit([](::io_uring* r) -> int {
            // drain 用户 CQE
            struct io_uring_cqe* cqe = nullptr;
            while (io_uring_peek_cqe(r, &cqe) == 0) {
                if (cqe->user_data & brpc::iouring::kBrpcCqeTag) break;
                // 处理 cqe->res …
                io_uring_cqe_seen(r, cqe);
            }
            // 提交用户 SQE
            ::io_uring_sqe* sqe = io_uring_get_sqe(r);
            if (!sqe) return 0;
            io_uring_prep_nop(sqe);
            sqe->user_data = my_token;  // bit63 必须为 0
            return 1;
        });
    },
    /*init_fn=*/nullptr,     // ring 创建后调用一次
    /*release_fn=*/nullptr   // ring 销毁前调用一次
);
```

完整示例见 `example/iouring_echo_c++/server.cpp`。

---

## 参数说明

所有参数均通过 gflags 在命令行传入。

### Ring 大小

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `iouring_sq_size` | `256` | 每个 ring 的 SQ 深度（并发 in-flight SQE 上限） |
| `iouring_cq_size` | `0` | 每个 ring 的 CQ 深度（0 表示 2 × sq_size） |

### Poller 线程

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `iouring_poller_yield` | `false` | 每轮 poll 后 yield，降低 CPU 占用（会增加尾延迟） |
| `iouring_max_cqe_poll_once` | `32` | 每次 `io_uring_peek_batch_cqe` 最多收割的 CQE 数 |

### 轮询模式

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `iouring_polling_mode` | `none` | CQE 收割策略，见下表 |
| `iouring_sqpoll_idle_ms` | `2000` | SQPOLL 内核线程空闲超时（ms） |
| `iouring_sqpoll_cpu` | `-1` | SQPOLL 内核线程绑定的 CPU（-1 = 不绑定） |
| `iouring_hybrid_spin_count` | `1000` | hybrid 模式下忙转的迭代次数后再阻塞 |

`iouring_polling_mode` 可选值：

| 值 | 说明 |
|----|------|
| `none` | 中断驱动（默认）：Poller 调用 `io_uring_wait_cqe_timeout`（超时 1 ms）阻塞等待内核通知，1 ms 超时保持对新连接的响应性 |
| `sqpoll` | 内核 SQ 轮询线程（`IORING_SETUP_SQPOLL`），Poller 用 peek 收割 CQE，最低延迟，需要 `CAP_SYS_NICE` |
| `iopoll` | 块设备完成轮询（`IORING_SETUP_IOPOLL`），内核不产生中断，Poller 每次 submit 后主动 peek，仅对 O_DIRECT 块设备有效 |
| `hybrid` | 先忙转 N 次再阻塞，兼顾延迟和 CPU 利用率 |

### 注册内存（零拷贝）

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `iouring_register_buffers` | `false` | 启用 `IORING_OP_READ_FIXED` / `IORING_OP_WRITE_FIXED` 零拷贝模式 |
| `iouring_mem_pool_initial_mb` | `256` | 初始注册内存大小（MiB） |
| `iouring_mem_pool_increase_mb` | `256` | 内存池扩容步长（MiB） |
| `iouring_mem_pool_max_regions` | `8` | 最大扩容次数 |
| `iouring_iobuf_block_size` | `8192` | IOBuf block 大小（字节），须与内存池对齐；READ_FIXED 每次从内存池按此大小分配接收缓冲区 |
| `iouring_mem_pool_free_buckets` | `8` | 全局空闲链表的分桶数量（范围 1–64），桶越多并发吞吐越高，但内存略多 |
| `iouring_mem_pool_tls_cache_num` | `128` | 每线程 TLS 缓存的最大 block 数量（范围 1–4096），越大越少访问全局桶 |

> **注意**：启用 `--iouring_register_buffers` 时，系统要么全部使用注册内存（`READ_FIXED` / `WRITE_FIXED`），要么完全不用，**不支持混用**。若初始化失败（内存不足、内核不支持等），整个 io_uring transport 会被禁用。
>
> **接收缓冲区生命周期**：每次 `SubmitRead` 都从 `IouringMemPool` 按需分配一个 block，内核 DMA 写入后直接以零拷贝方式交给 `IOBuf`；当 `IOBuf` 最后一个引用释放时，deleter 自动将 block 归还到内存池（线程安全），无需额外的 slot 管理。

---

## 内存池分配路径与可观测指标

### 三级分配路径

启用 `--iouring_register_buffers` 后，所有 IOBuf block 的分配/释放都走 `IouringMemPool`，内部采用三级路径：

```
Allocate()
  └─ 1. TLS 快路径：从当前线程的 tls_free_ 链表头取块（无锁，O(1)）
         命中 → 返回，计入 iouring_mem_pool_tls_alloc_count
         未命中 ↓
       2. 全局桶路径：对 thread_id % num_free_buckets_ 选桶，加锁后
          批量搬移 tls_cache_max_ 个块到 TLS，再从 TLS 返回
          命中 → 返回，计入 iouring_mem_pool_global_alloc_count
          桶也空 ↓
       3. 扩容路径：调用 AddRegion() mmap 新 slab 并向所有已注册 ring 调
          io_uring_register_buffers_update()，计入 iouring_mem_pool_pool_grow_count

Deallocate()
  └─ TLS 未满（< tls_cache_max_）→ 直接压入 tls_free_（无锁）
     TLS 已满 → 将一半块批量归还到全局桶（加锁），再压入剩余到 TLS
```

**全局桶（Bucket）设计**：全局空闲链表被拆分为 `num_free_buckets_`（默认 8）个独立桶，每桶一把 `Mutex`。多线程并发刷 TLS 缓存时各自操作不同的桶，互不阻塞，显著降低锁竞争。所有内存仍是同一块注册 slab，可跨 `bthread_tag` 使用。

### bvar 监控指标

内存池暴露以下 bvar，可通过 brpc 内置的 `/vars` 页面或 `bvar_dump` 查看：

| bvar 名称 | 类型 | 说明 |
|-----------|------|------|
| `iouring_mem_pool_tls_alloc_count` | Window(累计) | 历史 TLS 命中分配次数 |
| `iouring_mem_pool_tls_alloc_second` | PerSecond | 近 1 s TLS 命中分配速率 |
| `iouring_mem_pool_global_alloc_count` | Window(累计) | 历史全局桶命中分配次数 |
| `iouring_mem_pool_global_alloc_second` | PerSecond | 近 1 s 全局桶命中分配速率 |
| `iouring_mem_pool_pool_grow_count` | Window(累计) | 历史 AddRegion 扩容次数 |
| `iouring_mem_pool_tls_hit_rate` | PassiveStatus | TLS 命中率 = tls / (tls + global)，越接近 1.0 越好 |

**调优建议**：

- `iouring_mem_pool_tls_hit_rate` 持续低于 `0.99`，或 `iouring_mem_pool_global_alloc_second` 持续非零，说明 TLS 缓存不够大，可尝试增大 `--iouring_mem_pool_tls_cache_num`（默认 128）。
- `iouring_mem_pool_pool_grow_count` 在运行期间持续增长，说明初始内存不足，可增大 `--iouring_mem_pool_initial_mb` 或 `--iouring_mem_pool_increase_mb`。
- 线程数较多（> 32）时，若仍有锁竞争，可尝试增大 `--iouring_mem_pool_free_buckets`（默认 8，上限 64）。

---

## 用户提交自定义 SQE

用户可以通过 `InitPollingModeWithTag` 的 `callback` 在每次 Poller 迭代中提交自定义 SQE。

### CQE 标记约定

bRPC 提交的每条 SQE 都会在 `user_data` 的 **bit 63** 置 1：

```cpp
sqe->user_data = reinterpret_cast<uint64_t>(ctx) | brpc::iouring::kBrpcCqeTag;
```

- `PollCq` 只处理 bit 63 = 1 的 CQE，跳过 bit 63 = 0 的用户 CQE，且**不调用 `io_uring_cqe_seen()`**。
- 用户 **必须** 在 callback 中主动 drain 自己的 CQE，否则 CQ 会被撑满，导致新 SQE 无法提交。
- 用户 SQE 的 `user_data` **bit 63 必须为 0**（普通指针或整数均满足此条件）。

### drain 用户 CQE 的正确方式

在 `Submit` 的 `prepare_fn` 中：

```cpp
h.Submit([](::io_uring* r) -> int {
    struct io_uring_cqe* cqe = nullptr;
    while (io_uring_peek_cqe(r, &cqe) == 0) {
        // 遇到 bRPC 的 CQE 立即停止——不能调 cqe_seen，也不能跳过继续。
        // CQ 是 FIFO，不 seen 头部就无法推进，继续 peek 仍返回同一条。
        if (cqe->user_data & brpc::iouring::kBrpcCqeTag) break;
        process(cqe->user_data, cqe->res);
        io_uring_cqe_seen(r, cqe);  // 必须调用，否则头指针不前进
    }
    // 提交新 SQE …
    return n;
});
```

---

## 示例程序

示例位于 `example/iouring_echo_c++/`，演示了所有模式的启动方式。

```bash
cd example/iouring_echo_c++
mkdir bld && cd bld
cmake -DBRPC_WITH_IOURING=ON .. && make
cd ..

# 默认：epoll（不使用 io_uring）
./echo_server

# io_uring，none 模式（Poller 最多等待 1 ms，适合通用场景）
./echo_server --use_iouring

# io_uring + SQPOLL 模式（最低延迟，需要 CAP_SYS_NICE / root）
./echo_server --use_iouring --iouring_polling_mode=sqpoll

# io_uring + hybrid 模式（忙转 N 次后阻塞，兼顾延迟和 CPU）
./echo_server --use_iouring --iouring_polling_mode=hybrid

# io_uring + 零拷贝注册内存（最高吞吐量）
./echo_server --use_iouring --iouring_register_buffers

# io_uring + SQPOLL + 零拷贝（延迟和吞吐量双优化）
./echo_server --use_iouring --iouring_polling_mode=sqpoll --iouring_register_buffers

# 客户端（发送至 8000 端口，每秒一次）
./echo_client --server=0.0.0.0:8000
```

---

## 与 RDMA 的对比

| 特性 | io_uring | RDMA |
|------|----------|------|
| 硬件要求 | 无（标准 Linux，内核 ≥ 5.1） | 需要 RDMA 网卡（InfiniBand/RoCE） |
| 零拷贝发送 | ✅（`WRITE_FIXED`，内核直接 DMA） | ✅（硬件 DMA，绕过内核） |
| 零拷贝接收 | ✅（`READ_FIXED`，按需分配注册 block） | ✅（内存注册 + DMA） |
| 内存注册 | 可选（`--iouring_register_buffers`） | 必须预注册内存池 |
| 延迟 | 低（减少系统调用） | 极低（硬件实现，μs 级） |
| 吞吐量 | 高（受限于 NIC 带宽） | 极高（可达数十 Gbps） |
| 配置复杂度 | 低 | 高（GID/QPN/内存池等） |
