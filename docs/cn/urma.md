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
（`URMA_TM_RM`）Jetty，并使用 CTP 或 RTP 传输路径；通过
`urma_post_jetty_send_wr` 提交发送 WR，通过 `urma_post_jfr_wr` 提交接收
WR。完成事件既可由 JFC 忙轮询获取，也可通过 JFCE 事件 fd 获取。

## 编译配置

### CMake 编译

```bash
cmake -S . -B build -DWITH_URMA=ON
cmake --build build -j8
cmake --install build --prefix "$PWD/build/install"
cmake -S example/urma_performance -B build/urma-example \
    -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build/urma-example -j8
```

CMake 优先使用系统 UMDK 头文件，缺失时下载固定版本；
`DOWNLOAD_URMA_HEADERS=OFF` 关闭下载。`URMA_USE_MOCK=AUTO` 按 `liburma` 选择实现，
`ON` 使用 mock，`OFF` 使用硬件库。示例通过 `URMA_ROOT` 或 `URMA_INCLUDE_PATH` 指定 SDK 头文件。

### Make 编译

```bash
sh config_brpc.sh --headers=/usr/include --libs=/usr/lib \
    --with-urma
make -j4

# 强制使用 mock，或强制使用真实 liburma
sh config_brpc.sh --headers=/usr/include --libs=/usr/lib \
    --with-urma --with-urma-mock
sh config_brpc.sh --headers=/usr/include --libs=/usr/lib \
    --with-urma --without-urma-mock
```

默认根据 `liburma` 是否存在自动选择；`--without-urma-mock` 要求配置路径中
存在 `liburma`。

### Bazel 编译

```bash
# 编译带 URMA 支持的 brpc
bazel build --define=BRPC_WITH_URMA=true //:brpc

# 使用本地声明的 SDK 头文件
bazel build --define=BRPC_WITH_URMA=true \
    --define=BRPC_DOWNLOAD_URMA_HEADERS=false //:brpc

# 编译并运行 URMA 单元测试（使用 mock，不需要 URMA 硬件）
bazel test --define=BRPC_WITH_URMA=true \
    --define=BRPC_URMA_USE_MOCK=true //test:brpc_unittests

# 使用真实 liburma；要求链接器可找到 -lurma
bazel build --define=BRPC_WITH_URMA=true \
    --define=BRPC_URMA_USE_MOCK=false //:brpc
```

Bazel 通过 `@umdk//:urma_headers` 使用固定版本的 UMDK 头文件。
关闭下载时，由工具链或本地 BUILD 目标声明 `urma_api.h`、`urma_types.h` 和 `urma_ubagg.h`。
`BRPC_URMA_USE_MOCK=true` 使用 mock，`false` 链接 `liburma`；默认使用 mock。

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
4. 双方依次调用 `urma_import_seg` 和 `urma_import_jetty`，建立远端路由。
5. 4 字节 ACK（`HELLO_ACK_URMA_OK = 0x1`）确认双方均要 URMA。
6. 成功后 TCP fd 仅保留用于 epoll 生命周期和回退，数据走 URMA。

### 内存管理

通过 `mmap` 分配内存，调用 `urma_register_seg` 注册后切为固定大小的缓冲区（默认 8KB）。
IOBuf 使用注册池分配器，发送路径从 block refs 构建 `urma_sge_t`。
用户内存通过 `RegisterMemoryForUrma` / `DeregisterMemoryForUrma` 管理。

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
| `--urma_copy_send` | false | 将主机发送数据复制合并到 64 字节对齐的注册缓冲区 |
| `--urma_zerocopy_min_size` | 512 | 小于此值的接收拷贝 |
| `--urma_device` | "" | URMA 设备名（空=首个） |
| `--urma_tp_type` | auto | RM 路径：auto 优先 CTP，其次 RTP；ctp 或 rtp 显式选择路径 |
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

### 传输路径选择

`--urma_tp_type=auto` 按设备能力优先选择 CTP，其次 RTP；`ctp`、`rtp` 显式指定路径。
两端使用相同路径，进程初始化后保持固定。Jetty 取首个匹配优先级，支持组合能力位；
设备映射决定 SL，例如 priority 0 映射 SL 2。日志记录实际 `tp_type` 和 `priority`。

设备未提供能力表时，auto/ctp 使用 CTP priority 6，rtp 使用 RTP priority 0；
路径选择失败时初始化回落 TCP。

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

## 运行环境

Linux、`baidu_std` 协议，以及支持目标设备的 UMDK provider 和 `liburma`。

## 运行期故障恢复

恢复策略按进程配置，作用于新连接：

| 模式 | 行为 |
| --- | --- |
| LEGACY（默认） | 沿用原有握手和错误处理 |
| AUTO | 对端故障后冷却，期间使用 TCP，到期恢复 URMA 准入 |
| TCP_ONLY | 直接使用 TCP |

```cpp
#include "brpc/urma/urma_recovery_policy.h"

brpc::urma::SetUrmaRecoveryCooldownMs(5000);
brpc::urma::SetUrmaRecoveryMode(brpc::urma::UrmaRecoveryMode::AUTO);
```

默认冷却 5 秒，故障来源包括重试耗尽、RNR、JFC 错误和 WR 提交失败。
同一连接记录首个运行期故障；访问错误单独计数，窗口耗尽保持 EAGAIN。
恢复策略维护进程级模式、epoch 和对端冷却缓存，模式切换过滤旧连接上报。

已建立 URMA 连接的客户端收到控制 TCP 连接关闭或读取错误时，上报传输故障，
AUTO 下冷却该对端。服务端收到正常短连接的 EOF 时直接关闭连接。

`GetUrmaRecoveryStatus()` 和 `/vars/urma_recovery_status` 提供故障原因、冷却状态及协商数；
`/vars/urma_recovery_tcp_fallback_admissions` 按连接累计 TCP 准入。

### 功能验证

使用上文构建的 `urma_performance_server` 和 `urma_recovery_client`，两端指定相同设备路径。
客户端示例：

```bash
./build/urma-example/urma_recovery_client --transport=urma \
    --server=SERVER_IP:8003 --urma_device=URMA_DEVICE --urma_tp_type=rtp \
    --mode=auto --connection_type=short --seconds=15 --interval_ms=200
```

健康 AUTO 检查 `failure=0`、URMA 协商增加、TCP 准入为 0；
TCP_ONLY 检查 `failure=0`、TCP 准入增加。运行中通过 `--mode_file` 切换模式。
使用 `--connection_type=single` 或 `pooled` 验证连接复用，`--attachment_size=1048576`
验证 1 MiB 回显附件；客户端逐字节校验，汇总输出 `validation_failures`。

### 定向发送失败测试

启用 Linux 注入模块，在指定客户端模拟一次数据 SEND 提交失败（EIO）：

```bash
cmake -S example/urma_performance -B build/urma-example -DBUILD_URMA_FAULT_INJECTOR=ON
cmake --build build/urma-example --target urma_send_failure_injector -j8
python3 example/urma_performance/run_recovery_injection.py \
    --server=SERVER_IP:8003 --device=URMA_DEVICE --tp-type=rtp
```

运行器等待 URMA RPC 成功后注入，校验指标 PID，保存 CSV、日志和三阶段快照。
默认 v3 握手、运行 40 秒、冷却 10 秒；验收 `send_post=1` 和冷却期间 TCP 准入。
`short` 检查一次预期 RPC 失败和冷却后 URMA 协商恢复；`--connection-type=single` 或
`pooled` 检查重连后 RPC 持续成功，复用已建立的 TCP 连接。
运行器支持 `--attachment-size` 回显校验和 `--polling` 轮询模式。
构建 `urma_send_failure_injector_test` 后，通过示例目录的 CTest 执行工具自测。

主库 CTest 的 `brpc_urma_remote_recovery_*` 用例在服务端注入一次 SEND 失败，
覆盖 CTP/RTP、v2/v3、event/polling，检查客户端对端冷却、TCP 准入、
URMA 恢复及 64 KiB 附件回显。正常短连接关闭也在同一测试中验证。

### 发送访问错误对照

`--urma_copy_send=true` 将每个数据 WR 合并到 64 字节对齐的注册缓冲区，并持有至完成，
增加一次 CPU 拷贝。两端保持设备、队列和消息大小一致，对比原发送路径与复制路径。
初始化日志记录 `copy_send`，失败日志记录完成方向、槽位及注册段范围。

## 业务 SL 与优先级

`ChannelOptions.urma_qos` 控制请求发送方向，`ServerOptions.urma_qos` 控制响应发送方向：

```cpp
brpc::ChannelOptions client_options;
client_options.socket_mode = brpc::SOCKET_MODE_URMA;
client_options.urma_qos.service_level = 2;

brpc::ServerOptions server_options;
server_options.socket_mode = brpc::SOCKET_MODE_URMA;
server_options.urma_qos.service_level = 2;
```

`--urma_tp_type=auto` 按设备能力优先选择 CTP，其次 RTP；`ctp`、`rtp` 显式选择路径。
两端使用相同路径，进程初始化后保持固定。设备未提供能力表时，auto/ctp 使用 priority 6，
rtp 使用 priority 0。

| 配置 | 默认 | 显式范围 | 含义 |
| --- | --- | --- | --- |
| `service_level` | -1 | 0–255 | 选择当前路径下首个映射到该 SL 的 priority |
| `priority` | -1 | 0–15 | 指定当前路径支持的 priority |

两项均为 -1 时使用设备默认 priority；同时指定时须匹配同一设备映射。
不支持或冲突时，Channel 初始化、Server 启动报错，并列出当前路径支持的 priority:SL。
例如 RTP priority 0→SL 2 时配置 `service_level=2`，Jetty 使用 priority 0。
平台配置设备映射与调度策略，brpc 使用进程初始化时的映射快照。

不同配置使用独立连接，相同配置共享连接。配置随服务发现、连接池、短连接和
服务端 accept 传递，Jetty 重建时重新应用。预建池服务于默认 priority，
其他 priority 按需创建。v2/v3 握手使用所选路径。

### 功能验证

用 `urma_admin show --dev "$URMA_DEVICE" --whole` 核对当前路径的 priority:SL。
`example/urma_performance` 的两端均支持 `--urma_tp_type=rtp --urma_sl=2`，
也可通过 `--urma_priority` 指定匹配项。先以默认配置完成 RPC，再分别改变请求和响应 SL，
验证合法映射、不支持的 SL 和 SL/priority 冲突。对 v2/v3、single/pooled 及 polling 模式重复验证。

连接调试信息提供 `state`、`urma_effective_priority` 和 `urma_effective_sl`。
核对 URMA 连接为 `ESTABLISHED`、映射符合设备配置、RPC 无错误。
调度效果测试结合竞争流量、设备队列计数器、吞吐与 P99。
