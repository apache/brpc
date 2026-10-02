# FlatBuffers 消息

[English version](../en/flatbuffers.md)

bRPC 提供可选的、基于 IOBuf 的 FlatBuffers 消息、构造器和服务描述符。
消息构造方案基于 [apache/brpc#3196](https://github.com/apache/brpc/pull/3196)。

该组件不会注册 `fb_rpc` 传输协议，也不会向 `brpc::Channel` 和 `brpc::Server`
增加 FlatBuffers 集成。服务代码生成和进程内分派测试不是网络 RPC，也不是性能 benchmark。

## 构建

FlatBuffers 支持默认关闭。消息运行库只需要 FlatBuffers 头文件，不链接
FlatBuffers 库。测试需要与头文件版本匹配的 `flatc`。请保留上游生成代码中的
版本断言；版本不一致时应重新生成头文件，而不是削弱断言。

例如，GoogleTest 源码安装在 `/usr/src/googletest` 时：

```sh
cmake -S . -B build -DWITH_FLATBUFFERS=ON -DBUILD_UNIT_TESTS=ON \
  -DBUILD_BRPC_TOOLS=OFF -DDOWNLOAD_GTEST=OFF \
  -DBRPC_SYSTEM_GTEST_SOURCE_DIR=/usr/src/googletest
cmake --build build --target brpc_flatbuffers_unittest -j6
ctest --test-dir build -R '^brpc_flatbuffers_unittest$' --output-on-failure
```

其他安装方式可按需设置 `FLATBUFFERS_INCLUDE_DIR`、
`FLATBUFFERS_FLATC_EXECUTABLE` 和 `BRPC_SYSTEM_GTEST_SOURCE_DIR`。
项目原有测试依赖仍然适用。

Make 可在 `config_brpc.sh` 中传入 `--with-flatbuffers`；测试可通过
`FLATC=/path/to/flatc` 指定官方生成器。Bazel 可传入
`--define=BRPC_WITH_FLATBUFFERS=true`，并使用匹配的 FlatBuffers 25.2.10
运行时和编译器依赖。Bzlmod 导入与 WORKSPACE 相同的 checksum 固定归档：
`runtime_cc` 和 `flatc` 不需要 FlatBuffers 的外部 gRPC 模块，否则即使该功能关闭，
也可能与 bRPC 固定的 BoringSSL 版本冲突。

公共头文件位于 `brpc/flatbuffers/`，命名空间为 `brpc::flatbuffers`。
构造消息包含 `message.h`，使用服务描述符和接口包含 `service.h`。
`butil/config.h` 中的 `BRPC_WITH_FLATBUFFERS` 始终为 0 或 1；应用应使用
`#if` 判断，而不是 `#ifdef`。

Flatc 2.0.x 会生成未全限定的 `flatbuffers::` 名称。业务 schema 应使用
`brpc` 之外的 namespace（例如 `myapp.rpc`），避免被 `brpc::flatbuffers`
遮蔽；不要依赖 include 顺序。Flatc 25.2.10 会生成全限定名称。

## 消息构造和所有权

使用上游 `flatc --cpp` 生成 schema 对应的 `*_generated.h`。将
`brpc::flatbuffers::MessageBuilder` 传给生成的 `Create...` 函数，调用
`Finish(root)`，最后调用 `ReleaseMessage()`。

* `ReleaseMessage()` 不拷贝 payload 字节。返回的 move-only Message 拥有 IOBuf
  block 引用，并且在 builder 复用或析构后仍然有效。
* Message 和 builder 移动后，源对象仍可复用。移动带 shared string 的 builder 会丢弃
  其可选去重缓存；已经生成的 offset 仍有效。
* 导入普通 `::flatbuffers::FlatBufferBuilder` 会复制其 payload 和 scratch，并保留
  未完成 table 的状态。原始 allocator 负责释放原有存储，包括其拥有的自定义 allocator。
  实现不会猜测该用 `free` 还是 `delete[]`，也不会假设 payload 前存在额外空间。
* 使用 MessageBuilder 自身的 move、swap 和 release 操作。不要通过基类 cast 转移它，
  也不要使用继承来的 raw-buffer release 操作：原始 FlatBuffers detached buffer 会保留
  指向成员 allocator 的地址。
* released payload 前有 64 字节零初始化空间。可用 `reduce_meta_size_and_get_buf`
  缩短这段空间；增长会被拒绝且不修改对象。缩短 metadata 不会改变 payload 地址或字节。
* 序列化接受 const Message，并在输出 IOBuf 中保留其存储引用。该 buffer 是共享的，
  不是 copy-on-write：当其他读者或已序列化 buffer 仍在使用时，不要修改 payload/metadata。
* 分配大小在收窄为 SingleIOBuf 的 uint32_t 长度前会先检查。分配失败在 release build
  中同样 fatal，不受 bRPC `crash_on_fatal_log` 设置影响。这些路径会显式 abort，
  而不是依赖 `CHECK`/`LOG(FATAL)`。上游 `vector_downward` 无法在 null allocation 后安全继续。

`ParseFbFromIOBUF` 检查长度和 framing，并保留独立所有权。当 payload 地址 64 字节对齐时，
它会共享连续输入；输入分片或对齐不足时会复制到对齐存储。builder 分配为 64 字节对齐，
但最终 payload 只需满足 schema 要求的对齐，因此并非所有本地消息都适合接收侧零拷贝。
不支持超过 64 字节的对齐要求。

**Framing 不是 schema 验证。** 对不可信对端收到的数据，应先调用
`msg.Verify<YourRoot>()`，再调用 `GetRoot<YourRoot>()` 或
`GetMutableRoot<YourRoot>()`。合法消息中的可选 FlatBuffers string/vector 仍可能为 null。
framing 检查失败会保持原消息不变。

## Service ID 和代码生成

`BrpcDescriptorTable` 包含 namespace、service 名称、以空白分隔的方法名，以及显式方法 ID。
ID 必须是唯一的非负 int32。空 ID 列表会为手写 descriptor 分配 ordinal ID；生成服务必须
使用显式 ID：

```fbs
rpc_service BenchmarkService {
  First(Request):Response (id: 2);
  Second(Request):Response (id: 5);
}
```

* `descriptor.method(position)` 按声明顺序枚举方法。
* `method.index()` 是稳定的 wire ID，不是数组下标。
* `descriptor.FindMethodByIndex(id)` 用于查找稀疏 wire ID。传输层必须用该查找，不能用
  wire ID 直接索引稠密数组。
* 不要把已删除的方法 ID 复用于另一个方法。删除或重排声明不应改变仍存在方法的显式 ID。
* namespace `a.b` 和 `a.b.` 会规范化为相同 service 名称；空 namespace 表示全局作用域。
  方法全名包含 service。service hash 使用规范化后的全名和 MurmurHash3 seed 1。
  当这些 ID 被持久化或在线路上传输时，应保持 service 名称稳定。
* Descriptor 成功初始化后不能重新初始化。它们通过 RAII 拥有 method；生成的 accessor
  使用函数局部静态初始化以保证线程安全。

`tools/flatbuffers/` 中的配套生成器使用上游 parser 生成服务绑定。它独立构建；只有该
可选工具需要 `libflatbuffers`。命令和限制见其 [README](../../tools/flatbuffers/README.md)。
生成的 dispatch 会校验请求、拒绝未知或外来方法，并在失败时执行非空 completion callback，
包括未实现方法。成功实现拥有 completion，并且必须恰好执行一次回调。
