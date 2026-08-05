# UrmaTransport：基于 URMA 的远程内存 RPC

UrmaTransport 是使用 openEuler
[URMA](https://atomgit.com/openeuler/umdk)（Unified Remote Memory Access）SDK
实现的传输层。它是
[#3217](https://github.com/apache/brpc/discussions/3217) 中提出的路线 B，
与基于 OBMM 的 UBRing 传输（路线 A，
[#3226](https://github.com/apache/brpc/issues/3226)）互补，承担大包/跨节点
高吞吐场景，共同构成路线 C（双后端）。

## 技术背景

URMA 在 UMDK 支持的设备上提供 verbs 风格接口。当前实现创建可靠消息
（`URMA_TM_RM`）Jetty，并使用 CTP 传输路径；通过
`urma_post_jetty_send_wr` 提交发送 WR，通过 `urma_post_jfr_wr` 提交接收
WR。完成事件既可由 JFC 忙轮询获取，也可通过 JFCE 事件 fd 获取。

## 编译配置

### CMake 编译

```bash
# 带 URMA 支持编译 brpc
cmake -B build -DWITH_URMA=ON
make -C build -j$(nproc)

# 编译 urma_performance 示例
cd example/urma_performance
cmake -B build
make -C build -j$(nproc)
```

`WITH_URMA=ON` 使用上游 UMDK 头文件进行编译。CMake 优先使用系统安装的
SDK；找不到头文件时，会参照 Mooncake 的 mock 构建方式下载固定版本的
UMDK，可通过 `DOWNLOAD_URMA_HEADERS=OFF` 禁止下载。找到 `liburma` 时使用
真实硬件数据通路，否则链接 brpc 的 mock，使 URMA 代码和测试仍可在无硬件
环境编译。

## 使用

通过在 channel / server 上设置 `socket_mode` 选择传输层：

```cpp
// 客户端
brpc::ChannelOptions opt;
opt.socket_mode = brpc::SOCKET_MODE_URMA;
opt.protocol = "baidu_std";   // URMA 仅支持 baidu_std
brpc::Channel channel;
channel.Init("127.0.0.1:8003", &opt);

// 服务端
brpc::ServerOptions sopt;
sopt.socket_mode = brpc::SOCKET_MODE_URMA;
server.Start(port, &sopt);
```

若对端不支持 URMA（例如 TCP 客户端连接 URMA 服务端），在 4 字节 magic
握手后透明回退到 TCP，应用代码无需改动。

## 架构

UrmaTransport 沿用与 `RdmaTransport` / `UBShmTransport` 一致的两层设计：

```
UrmaTransport : public Transport         (urma_transport.{h,cpp})
  +-- std::shared_ptr<TcpTransport>      (回退路径)
  +-- urma::UrmaEndpoint*                (URMA 数据路径)
  +-- UrmaState { URMA_ON, URMA_OFF, URMA_UNKNOWN }

urma::UrmaEndpoint : public SocketUser   (urma/urma_endpoint.{h,cpp})
  +-- UrmaResource { jfc, jfce, jfr, jetty, remote_jetty, remote_seg }
  +-- 握手状态机（C/S 对称，在 TCP fd 上驱动）
  +-- 发送路径：urma_post_jetty_send_wr(URMA_OPC_SEND)
  +-- 接收路径：urma_poll_jfc -> HandleCompletion -> InputMessenger
  +-- 双窗口信用流控（_remote_rq_window_size / _sq_window_size）
```

### 建链流程（双平面）

与 RDMA / UBRing 一致，控制面为 TCP，数据面为 URMA：

1. TCP 连接建立。
2. `UrmaConnect::StartConnect` 起客户端握手 bthread。
3. 双方在 TCP fd 上交换 `UrmaHello` 消息（v2 二进制 magic `URMA`，
   v3 protobuf magic `URM3`），携带本地 EID、jetty id、recv buffer 数量、
   以及扁平化的 buffer 池 segment。
4. 双方先调用 `urma_import_seg` **再**调用 `urma_import_jetty`，为远端 EID
   建立传输路径（TP）路由。跳过 `import_seg` 会导致首个 SEND 被硬件以
   `URMA_CR_RNR_RETRY_CNT_EXC_ERR` 拒绝。
5. 4 字节 ACK（`HELLO_ACK_URMA_OK = 0x1`）确认双方均要 URMA。
6. 成功后 TCP fd 仅保留用于 epoll 生命周期和回退，数据走 URMA。

### 内存管理

申请一大段 `mmap` 内存，用 `urma_register_seg` 一次性注册，再切成固定大小
buffer（默认 8KB）。劫持 `butil::iobuf::blockmem_allocate` 使每个 IOBuf
block 都由注册 segment 支撑，发送路径可直接从 IOBuf block refs 构建
`urma_sge_t`，无需逐消息注册（与 RDMA `block_pool` 设计一致）。用户注册
内存通过 `urma::RegisterMemoryForUrma` / `DeregisterMemoryForUrma` 支持。

## 配置

所有 flag 使用 `urma_` 前缀（对标 RDMA 的 `rdma_` 前缀）：

| Flag | 默认 | 用途 |
|------|------|------|
| `--urma_use_polling` | false | 轮询 JFC 而非事件模式 |
| `--urma_poller_num` | 1 | 每 bthread tag 的轮询器数（轮询模式） |
| `--urma_disable_bthread` | false | 内联处理消息（不起 bthread） |
| `--urma_sq_size` | 128 | 本地 JFS 深度 [16, 4096] |
| `--urma_rq_size` | 128 | 本地 JFR 深度 [16, 4096] |
| `--urma_cqe_poll_once` | 32 | 每次 `urma_poll_jfc` 的上限 |
| `--urma_recv_zerocopy` | true | 大于 `--urma_zerocopy_min_size` 的接收零拷贝 |
| `--urma_zerocopy_min_size` | 512 | 小于此值的接收拷贝 |
| `--urma_device` | "" | URMA 设备名（空=首个） |
| `--urma_max_sge` | 0 | 每 WR SGE 上限（0=设备上限） |
| `--urma_bonding_mode` | 0 | bonding 模式：0=standalone，1=active-backup，2=balance |
| `--urma_bonding_level` | 0 | bonding 层级：0=IODIE，1=port |
| `--urma_prepared_jetty_cnt` | 8 | 预连接 Jetty+CQ 请求数量；会根据 `RLIMIT_NOFILE` 自动限制 |
| `--urma_buffer_size` | 8192 | 池中每个 buffer 大小（字节） |
| `--urma_buffer_count` | 65536 | 池中 buffer 数量 |
| `--urma_poller_yield` | false | 忙轮询循环中主动让出 bthread |
| `--urma_client_handshake_version` | 2 | 客户端握手版本（2=二进制，3=protobuf） |

设备名以 `bonding` 开头时，brpc 会在创建 context 后、创建 segment 和队列
前配置 provider。默认 standalone+IODIE 配置与 UMDK 性能工具保持一致。
bonding 支持需要 provider 扩展头文件 `urma_ubagg.h`。

## 与 UBRing 协同

UrmaTransport 推荐用于**大包和跨节点**高吞吐路径，而 UBRing
（`SOCKET_MODE_UBRING`）对**小包和同机 IPC**最优（亚微秒、零系统调用）。
混合负载可按服务选择传输层：

| 场景 | 建议 `socket_mode` |
|------|---------------------|
| 同机 IPC | `SOCKET_MODE_UBRING` |
| 跨节点小包（< 64KB） | `SOCKET_MODE_UBRING`（UBS-Mem）或 `SOCKET_MODE_URMA` |
| 跨节点大包（>= 64KB） | `SOCKET_MODE_URMA` |
| 传统 RoCE/IB 数据中心 | `SOCKET_MODE_URMA` 或 `SOCKET_MODE_RDMA` |

单连接内按包大小自动分流的方案（路线 C 方案 B）作为后续演进方向。

## 限制

- 仅支持 `baidu_std` 协议（与 RDMA 一致）。SSL、RTMP、NSHEAD、MONGO 在
  `ContextInitOrDie` 阶段拒绝。
- 硬件数据通路需要受支持的 UMDK provider 和 `liburma`。
- 当前实现面向 Linux。
