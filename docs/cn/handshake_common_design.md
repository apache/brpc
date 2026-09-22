# RDMA 与 UBSHM 公共握手

## 范围

本次重构将 RDMA 和 UBSHM 的握手流程收敛到公共层。`Socket` 持有
`AdapterTransport`；后者以 TCP 作为握手控制通道，在升级成功后选择高速
数据面，协商不可用时继续使用 TCP。URMA 目前仍使用独立的 `UrmaTransport`
和握手实现，不在本次迁移范围内。

## 分工

| 组件 | 职责 |
| --- | --- |
| `AdapterTransport` | 持有 TCP 与候选高速 Transport，启动客户端握手、分发服务端输入，并选择数据面 |
| `HandshakeSession` | 管理握手阶段、帧收发、增量解析和终态发布 |
| 协议 adapter / `HandshakeCodec` | 编解码 RDMA 或 UBSHM 的协议字段，校验版本和参数 |
| `TransportUpgradeOps` | 调用具体 Transport 的资源准备、协商、激活和回退操作 |
| `RdmaEndpoint` / `UBShmEndpoint` | 管理各自的资源和数据面，不驱动完整握手流程 |

客户端在 TCP 连接建立后启动握手 bthread；服务端通过 `InputMessenger`
增量解析 hello、扩展字段（若协议需要）和 ACK。公共层保留选中的协议
adapter，因为 ACK 本身不带 magic。

## 状态与回退

握手终态为 `ESTABLISHED`、`FALLBACK_TCP` 或 `FAILED`。升级成功后，TCP
仍是控制连接，业务数据走 RDMA/UBSHM；控制连接上出现额外业务数据视为
协议错误。资源不可用或协商拒绝时，释放本次升级资源，设置 TCP 为数据面，
再以 release 顺序发布 `FALLBACK_TCP`。事件线程以 acquire 顺序读取终态，
继续解析已缓存及后续的 TCP 业务数据。已确认属于握手协议的畸形帧则失败，
不作为普通 RPC 数据回放。

增量解析中，不完整帧返回 `NOT_ENOUGH_DATA` 且不消费输入；不可能匹配
握手 magic 的前缀返回 `TRY_OTHERS`。TCP 分片、握手帧与后续数据粘连都由
帧边界处理，不能假设一次 read 恰好得到一帧。

## 协议兼容

- RDMA 保持既有 v2/v3 wire format、协议 ID 和注册名称。
- UBSHM 使用 v3 hello。64 字节 hello 后，双方交换 4 字节网络序格式扩展；
  只有双方选择 `LEGACY_64` 才能升级，否则回退 TCP。ACK 仍为 4 字节。
- 公共 framing 仅负责帧边界，协议字段由各自 codec 解释。

## 验证重点

测试覆盖分片和粘连输入、magic 前缀分流、协议版本与格式校验、TCP 回退、
资源释放及终态发布，并分别运行 RDMA 与 UBSHM 回归测试。URMA 的公共握手
迁移需要单独设计和测试，不应仅凭本次重构推断其已完成。
