# FlatBuffers 消息

[English version](../en/flatbuffers.md)

bRPC 支持基于 IOBuf 的 FlatBuffers 消息、构造器和服务描述符，默认不启用。
消息构造方案源自 [apache/brpc#3196](https://github.com/apache/brpc/pull/3196)。

本组件不包含 `fb_rpc` 传输协议，也不改动 `brpc::Channel` 或 `brpc::Server`。
服务代码生成和进程内分派测试不涉及网络 RPC，也不是性能测试。

## 构建

运行库只需要 FlatBuffers 头文件，不链接其库；测试还需要版本匹配的 `flatc`。
版本不一致时应重新生成头文件，不要删除或放宽上游生成代码中的版本断言。

假设 GoogleTest 源码位于 `/usr/src/googletest`：

```sh
cmake -S . -B build -DWITH_FLATBUFFERS=ON -DBUILD_UNIT_TESTS=ON \
  -DBUILD_BRPC_TOOLS=OFF -DDOWNLOAD_GTEST=OFF \
  -DBRPC_SYSTEM_GTEST_SOURCE_DIR=/usr/src/googletest
cmake --build build --target brpc_flatbuffers_unittest -j6
ctest --test-dir build -R '^brpc_flatbuffers_unittest$' --output-on-failure
```

非标准安装可设置 `FLATBUFFERS_INCLUDE_DIR`、`FLATBUFFERS_FLATC_EXECUTABLE`
和 `BRPC_SYSTEM_GTEST_SOURCE_DIR`。其他测试依赖与 bRPC 原有配置相同。

Make 使用 `config_brpc.sh --with-flatbuffers`，测试时可通过
`FLATC=/path/to/flatc` 指定生成器。Bazel 使用
`--define=BRPC_WITH_FLATBUFFERS=true`。

运行库使用了 FlatBuffers builder 的内部结构，目前只支持 25.2.10，其他版本会在
编译期报错。Bzlmod 和 WORKSPACE 使用同一个源码归档，并用校验和锁定内容。
未采用 `bazel_dep`，是为了避免引入 gRPC、多语言工具以及与 bRPC 冲突的 BoringSSL
依赖。

公共头文件位于 `brpc/flatbuffers/`，命名空间为 `brpc::flatbuffers`。
构造消息时包含 `message.h`；使用服务描述符和接口时包含 `service.h`。`butil/config.h` 中的
`BRPC_WITH_FLATBUFFERS` 始终是 0 或 1，因此应使用 `#if` 判断。

旧版 flatc 2.0.x 可能生成未全限定名称。业务 schema 不要使用 `brpc` 命名空间，也不要依赖 include 顺序。

## 消息构造和所有权

先用上游 `flatc --cpp` 生成 `*_generated.h`，再将
`brpc::flatbuffers::MessageBuilder` 传给生成的 `Create...` 函数。
调用 `Finish(root)` 后，通过 `ReleaseMessage()` 取得消息。

* `ReleaseMessage()` 不复制 payload。返回的 Message 是 move-only 对象，持有 IOBuf block 引用；builder 复用或析构后，消息仍然有效。
* Message 和 builder 被移动后，源对象仍可复用。移动启用了 shared string 的 builder 会清空去重缓存，但已生成的 offset 不受影响。
* `Message::CopyFrom` 和 `MergeFrom` 共享同一个引用计数 IOBuf block，不复制
  payload 或 metadata。通过任一别名修改数据，其他别名也会看到变化。有并发读者时，
  写操作必须由外部同步保护。
* 从普通 `::flatbuffers::FlatBufferBuilder` 导入时，会复制 payload 和 scratch，
  保留未完成 table 的状态。旧缓冲区由原分配器释放；若源 builder 拥有该分配器，
  导入后也会销毁它。导入过程不猜测应使用 `free` 还是 `delete[]`，也不要求 payload
  前有预留空间。
* 只使用 MessageBuilder 自身提供的 move、swap 和 release 操作。不要通过基类
  cast 转移对象，也不要调用继承来的 raw-buffer release；这类 detached buffer
  会保留成员 allocator 的地址。
* payload 前预留 64 字节并初始化为零。`reduce_meta_size_and_get_buf` 可以缩短
  这段空间；扩大时返回失败，原对象保持不变。缩短 metadata 不会移动或改写 payload。
* 序列化 const Message 时，输出 IOBuf 会继续引用原存储。它不是 copy-on-write；仍有读者或序列化结果存活时，不要修改 payload 或 metadata。
* 分配长度在转成 SingleIOBuf 的 uint32_t 长度前会检查。SlabAllocator/builder
  的分配失败会终止进程，release build 也一样，不受 `crash_on_fatal_log` 影响。
  这是因为 FlatBuffers 的 `vector_downward` 无法在分配器返回 null 后继续工作。
  解析时复制数据所需的内存若分配失败，则返回 false，保留原 Message。

`ParseFbFromIOBUF` 负责检查长度和 framing，解析后的 Message 持有自己的存储引用，
但该接口不负责限制接收消息大小。若 `msg_size` 来自不可信对端，调用方必须先按
自己的 max-message-size 策略检查，再调用解析接口。分片或未对齐的数据会在
schema 校验前复制到新存储，因此大小限制必须放在解析之前。

连续输入的 payload 地址按 64 字节对齐时，解析可以共享原存储。MessageBuilder
的分配按 64 字节对齐，但最终 payload 只保证满足 schema 的对齐要求，所以本地
构造的消息未必都能在接收侧零拷贝。当前不支持超过 64 字节的对齐要求。

**Framing 检查不等于 schema 校验。** 读取不可信数据前，先调用
`msg.Verify<YourRoot>()`，成功后再使用 `GetRoot<YourRoot>()` 或
`GetMutableRoot<YourRoot>()`。合法消息中的可选 string/vector 仍可能为 null。
Framing 检查失败不会修改原 Message。

## Service ID 和代码生成

`BrpcDescriptorTable` 保存 namespace、service 名称、按空白分隔的方法名和显式
方法 ID。ID 必须是唯一的非负 int32。手写 descriptor 可以传空 ID 列表，此时按
声明顺序分配 ordinal ID；生成服务必须显式声明 ID：

```fbs
rpc_service BenchmarkService {
  First(Request):Response (id: 2);
  Second(Request):Response (id: 5);
}
```

* `descriptor.method(position)` 按声明顺序取方法。
* `method.index()` 是稳定的 wire ID，不是数组下标。
* 稀疏 ID 必须通过 `descriptor.FindMethodByIndex(id)` 查找。传输层不能直接用 wire ID 索引稠密数组。
* 删除方法后，不要把原 ID 分配给其他方法。调整声明顺序也不应改变已有方法的 ID。
* namespace `a.b` 和 `a.b.` 会规范化为同一个 service 名称；空 namespace 表示
  全局作用域。方法全名包含 service 名称。service hash 使用规范化全名和
  MurmurHash3 seed 1。若 ID 需要持久化或在线路上传输，service 名称必须保持稳定。
* Descriptor 初始化成功后不能再次初始化。它通过 RAII 持有 method；生成的 accessor 使用函数局部静态对象，初始化过程是线程安全的。

`tools/flatbuffers/` 中的生成器使用上游 parser 生成服务绑定，并单独链接
`libflatbuffers`。命令和限制见其 [README](../../tools/flatbuffers/README.md)。

生成的 dispatch 会验证请求，拒绝未知、不属于当前 service 或尚未实现的方法。
失败时，它会执行非空 completion callback；成功分派后，completion 由业务实现负责，
并且必须恰好执行一次。
