# 公共传输握手

`AdapterTransport` 统一 RDMA、UBSHM 和 URMA 的握手入口。TCP 承担控制面，
升级成功后使用高速数据面，协商不可用时继续使用 TCP。

## 组件分工

| 组件 | 职责 |
| --- | --- |
| `AdapterTransport` | 持有 TCP 与候选 Transport，选择协议及数据面 |
| `HandshakeSession` | 编排客户端、服务端状态机和增量帧解析 |
| `HandshakeProtocol` | 编解码 hello、ACK 和扩展帧，处理版本兼容 |
| `HandshakeTransport` | 准备资源，执行升级、回退和失败清理 |
| 各传输的 Endpoint | 管理资源及数据收发 |

客户端在 TCP 建连后启动握手 bthread；服务端通过 `InputMessenger` 增量解析
hello、扩展字段和 ACK。ACK 不带 magic，服务端使用已选协议继续解析。

## 状态与回退

- `ESTABLISHED`：使用高速数据面，TCP 保留为控制连接；额外 TCP 业务数据按协议错误处理。
- `FALLBACK_TCP`：资源不可用或协商拒绝时释放升级资源，继续解析已缓存及后续 TCP 数据。
- `FAILED`：握手帧畸形或处理失败，清理资源并关闭连接。

终态以 release/acquire 顺序发布和读取。未收齐的帧返回 `NOT_ENOUGH_DATA`，
不消费输入；非握手 magic 返回 `TRY_OTHERS`。帧边界处理支持 TCP 分片和粘连。

## 协议与数据面衔接

- RDMA 保持既有 v2/v3 wire format、协议 ID 和注册名称。
- UBSHM 使用 v3 hello：64 字节 hello 后交换 4 字节网络序格式扩展，IPC 选择
  `IPC_V2`，UBS 选择 `LEGACY_64`；格式一致后升级，ACK 为 4 字节。
- URMA 保持 v2 二进制和 v3 protobuf 格式；资源导入和收发信用由
  `UrmaEndpoint` 管理，详见 [UrmaTransport](urma.md)。
- 激活数据面后，等待 TCP 握手解析退出再注册 URMA CQ 回调，避免两条输入路径并行解析。

测试覆盖分片与粘连、magic 分流、版本协商、终态发布、TCP 回退、
资源清理及各传输的数据面回归。
