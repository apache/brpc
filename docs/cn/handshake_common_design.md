# 公共传输握手

RDMA、UBSHM 和 URMA 共用握手流程，以 TCP 交换控制帧。升级成功后使用高速
数据面，协商不可用时继续使用 TCP。

## 分工

| 组件 | 职责 |
| --- | --- |
| `AdapterTransport` | 持有 TCP 与高速 Transport，启动握手、分发输入并选择数据面 |
| `HandshakeSession` | 编排客户端和服务端流程，管理帧收发、增量解析和终态发布 |
| `HandshakeProtocol` | 编解码 hello、ACK 和扩展帧，校验协议版本及字段 |
| `HandshakeTransport` | 准备和协商资源，处理升级、回退及失败清理 |
| 各传输的 Endpoint | 管理传输资源和数据面 |

客户端在 TCP 连接建立后启动握手 bthread；服务端通过 `InputMessenger`
增量解析控制帧。在 `EXTENSION_WAIT` 和 `ACK_WAIT`，服务端保留解析上下文，
按已选协议版本继续处理后续帧。

## 状态与回退

- `ESTABLISHED`：业务数据走 RDMA/UBSHM/URMA，TCP 保留为控制连接。服务端
  完成最终 ACK 解析并退出 TCP 解析循环后启动高速接收；额外 TCP 数据触发连接错误。
- `FALLBACK_TCP`：释放升级资源后以 release 顺序发布终态，事件线程以
  acquire 顺序读取并继续处理已缓存及后续的 TCP 业务数据。
- `FAILED`：握手 I/O 错误或畸形帧导致失败，清理资源并关闭连接。

完整帧按边界消费；分片输入返回 `NOT_ENOUGH_DATA` 等待补齐，
与握手 magic 不匹配的前缀返回 `TRY_OTHERS`。

## 协议兼容

- RDMA 保持 v2/v3 wire format 和协议数值 ID `1`。公共握手使用枚举
  `PROTOCOL_TRANSPORT_HANDSHAKE`，注册名称为 `transport_handshake`。
- UBSHM 使用 64 字节 v3 hello、4 字节网络序格式扩展和 4 字节 ACK。
  IPC 后端选择 `IPC_V2`，UBS 后端选择 `LEGACY_64`；格式相同则升级，否则
  回退 TCP。资源映射使用对端共享内存长度，数据面激活前发布协商格式。

- URMA 保持 v2 二进制和 v3 protobuf 格式；资源导入和收发信用由
  `UrmaEndpoint` 管理。服务端退出 TCP 握手解析后注册 CQ 回调。

## 测试

覆盖分片与粘连输入、magic 分流、版本与格式校验、TCP 回退、资源释放、
终态发布、最终 ACK 与首包并发，以及 RDMA、UBSHM 和 URMA 回归测试。
