# 启用 FlatBuffers 消息与 RPC

[English version](../en/flatbuffers.md)

bRPC 提供基于 IOBuf 的 FlatBuffers 消息、构造器、服务描述及 `fb_rpc` 协议。
支持默认关闭，需先编译启用该功能的 bRPC，再让客户端和服务端使用同一套头文件、
生成配置和运行库。完整示例位于 [example/benchmark_fb](../../example/benchmark_fb/README.md)。

## 1. 启用选项与依赖

| 构建方式 | 启用选项 | 可供独立 example 使用的运行库前缀 |
| --- | --- | --- |
| CMake | `-DWITH_FLATBUFFERS=ON` | `<build>/output` |
| Make | `config_brpc.sh --with-flatbuffers` | `<checkout>/output` |
| Bazel | build/test 命令均传 `--define=BRPC_WITH_FLATBUFFERS=true` | 原始 Bazel 输出不是 example 所需的安装前缀 |

**不能只给应用增加 `-DBRPC_WITH_FLATBUFFERS=1`。** 该功能会改变 Channel、Controller、
Server 的 ABI；ON 头文件与 OFF 运行库混用不受支持。`butil/config.h` 中的宏始终为 0 或 1，
应用应使用 `#if BRPC_WITH_FLATBUFFERS`，而不是 `#ifdef`。

先按[入门指南](getting_started.md)准备 C++ 工具链、Protobuf 编译器及开发库、gflags、
LevelDB、OpenSSL、zlib。FlatBuffers RPC **并不消除 Protobuf 依赖**。

| 组件 | FlatBuffers 或测试依赖 |
| --- | --- |
| bRPC 消息及 RPC 运行库 | FlatBuffers 头文件，不链接 `libflatbuffers` |
| 官方 schema 代码生成 | 与头文件版本一致的 `flatc` |
| bRPC 服务代码生成 | `brpc_flatc`，其构建另需匹配的官方头文件和 `libflatbuffers` |
| example 烟测 | Python 3，不需要 GoogleTest |
| 库单元测试 | GoogleTest 及项目原有测试依赖 |

本仓库的 Bazel 和 ON gate 固定使用 **FlatBuffers 25.2.10**。复现该流程时，应使用同版本的
头文件、官方 `flatc` 和用于构建 `brpc_flatc` 的库。不要删除生成头文件中的版本断言。

三个容易混淆的参数：

- 根项目 CMake 的库测试：`FLATBUFFERS_FLATC_EXECUTABLE` 指向官方 `flatc`。
- 生成器验收及 example：`FLATC_EXECUTABLE` 指向官方 `flatc`。
- example：`BRPC_FLATC_EXECUTABLE` 指向 bRPC 的 `brpc_flatc`，不能填成官方 `flatc`。

各阶段还必须使用兼容的同一套 Protobuf。当 CMake 检测到 `Protobuf_VERSION > 4.21` 时，
需要 C++17 及匹配的 Abseil 依赖；生成器验收和 example 需要 Protobuf 的 CMake config 包
导出传递依赖。运行库与 example 最低要求 CMake 3.16；下文 CTest 命令使用 3.17+ 的 `--no-tests=error`
防止空测试误报成功。只有 3.16 时，可直接执行 `smoke.py`。以下按 Linux/macOS 的
Unix Makefiles 或 Ninja 单配置生成器编写。

运行库使用了 FlatBuffers builder 的内部结构，目前只支持 25.2.10，其他版本会在编译期报错。
版本不一致时应重新生成头文件，不要删除或放宽上游生成代码中的版本断言。Bzlmod 和 WORKSPACE
使用同一个源码归档，并用校验和锁定内容；未采用 `bazel_dep`，是为了避免引入 gRPC、多语言工具
以及与 bRPC 冲突的 BoringSSL 依赖。

如需使用其他 FlatBuffers 版本，需要修改运行库、生成器和 codegen 测试中的版本检查，
再用该版本完整运行 `tools/flatbuffers/verify.sh`。步骤见生成器
[README](../../tools/flatbuffers/README.md#using-another-flatbuffers-version)，
其中以 23.5.26 为例，仅适用于 CMake 构建。

## 2. 用 CMake 构建启用后的运行库

从 bRPC checkout 开始。按需提前设置有效的 `CC`、`CXX`。
CMake 构建把生成文件放在 build 目录，example 的绑定代码与构建产物放在 `WORK` 中。
独立 example 需要公共头文件和 `config.h`，build 目录里没有这些文件，所以要把运行库安装到一个前缀。

将下列变量替换为已有安装前缀，即包含 `include/`、`lib/` 或 `lib64/` 的目录；多个变量
可以指向同一前缀。

```sh
REPO="$PWD"
DEPS=/absolute/path/to/dependency-prefix
FB=/absolute/path/to/flatbuffers-prefix
OPENSSL=/absolute/path/to/openssl-prefix
WORK="$(mktemp -d "${TMPDIR:-/tmp}/brpc-benchmark-fb.XXXXXX")"
JOBS=2
printf 'WORK=%s\n' "$WORK"
```

若尚未安装 FlatBuffers，可先从匹配版本的官方源码构建。已有完整安装时跳过此段。
`FB` 必须是可写的安装前缀，不是源码目录：

```sh
FB_SOURCE=/absolute/path/to/flatbuffers-25.2.10-source
cmake -S "$FB_SOURCE" -B "$WORK/flatbuffers" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_INSTALL_PREFIX="$FB" -DCMAKE_INSTALL_LIBDIR=lib \
  -DFLATBUFFERS_BUILD_TESTS=OFF -DFLATBUFFERS_BUILD_FLATC=ON \
  -DFLATBUFFERS_BUILD_FLATLIB=ON -DFLATBUFFERS_BUILD_SHAREDLIB=OFF \
  -DFLATBUFFERS_INSTALL=ON -DFLATBUFFERS_LIBCXX_WITH_CLANG=OFF
cmake --build "$WORK/flatbuffers" --parallel "$JOBS"
cmake --install "$WORK/flatbuffers"
"$FB/bin/flatc" --version
```

构建静态运行库，不启用库单元测试，因此这一阶段不需要 GoogleTest：

```sh
cmake -S "$REPO" -B "$WORK/runtime" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_PREFIX_PATH="$DEPS;$FB;$OPENSSL" \
  -DOPENSSL_ROOT_DIR="$OPENSSL" \
  -DCMAKE_INSTALL_PREFIX="$WORK/prefix" \
  -DWITH_FLATBUFFERS=ON -DBUILD_SHARED_LIBS=OFF \
  -DFLATBUFFERS_INCLUDE_DIR="$FB/include" \
  -DBUILD_UNIT_TESTS=OFF -DDOWNLOAD_GTEST=OFF -DBUILD_BRPC_TOOLS=OFF
cmake --build "$WORK/runtime" --parallel "$JOBS"
cmake --install "$WORK/runtime"
grep -E '^#define BRPC_WITH_FLATBUFFERS +1$' "$WORK/prefix/include/butil/config.h"
```

最后一条检查必须输出 `#define BRPC_WITH_FLATBUFFERS 1`。公共接口位于
`brpc/flatbuffers/message.h` 和 `brpc/flatbuffers/service.h`。
上面的 policy 参数是兼容 CMake 4 下某些旧依赖策略的可选设置，不是启用 FlatBuffers 的
开关，也不能替代编译器和依赖版本要求。

## 3. 通过 example 完成端到端验证

该示例默认做**有界功能验证**，另有可选的计时模式，用于对比 `fb_rpc` 和等价的 `baidu_std` 服务。[echo.fbs](../../example/benchmark_fb/echo.fbs)
声明 `BenchmarkService.Echo`，显式 wire method ID 为 7。
[server.cpp](../../example/benchmark_fb/server.cpp) 使用 `AddFlatBuffersService` 注册服务；
[client.cpp](../../example/benchmark_fb/client.cpp) 使用生成的 `BenchmarkService::Stub`
及 `fb_rpc` channel。

### 构建生成器和示例

在前一节同一个 shell 中继续，沿用 `REPO`、`DEPS`、`FB`、`OPENSSL`、`WORK`、`JOBS`：

```sh
cmake -S "$REPO/tools/flatbuffers" -B "$WORK/codegen" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH="$FB;$DEPS" \
  -DFLATBUFFERS_INCLUDE_DIR="$FB/include"
cmake --build "$WORK/codegen" --parallel "$JOBS"

cmake -S "$REPO/example/benchmark_fb" -B "$WORK/example" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$DEPS;$FB;$OPENSSL" \
  -DOPENSSL_ROOT_DIR="$OPENSSL" \
  -DBRPC_ROOT="$WORK/prefix" \
  -DFLATBUFFERS_INCLUDE_DIR="$FB/include" \
  -DFLATC_EXECUTABLE="$FB/bin/flatc" \
  -DBRPC_FLATC_EXECUTABLE="$WORK/codegen/brpc_flatc"
cmake --build "$WORK/example" --parallel "$JOBS"
(cd "$WORK/example" && ctest -V --no-tests=error --output-on-failure -R '^benchmark_fb_smoke$')
```

构建时会执行两个生成器，文件位于 `WORK/example/generated/`：

- 官方 `flatc`：`echo_generated.h`，定义 table 类型。
- `brpc_flatc`：`echo.brpc.fb.h`、`echo.brpc.fb.cpp`，定义服务及 stub。

不要提交这些生成文件，也不要在更换版本后沿用旧头文件。生成器若选错库，可在其 CMake
配置中显式传入 `FLATBUFFERS_LIBRARY`。导入 schema、稳定方法 ID 等规则见
[生成器说明](../../tools/flatbuffers/README.md)。

### 烟测的成功标准

必须看到名为 `benchmark_fb_smoke` 的 **1 项 CTest 通过**、命令退出码为 0，并输出：

```text
benchmark_fb smoke passed: 13 verified replies, 2 schema rejections, clean shutdown
```

这是 15 次 RPC（13 次正常响应、2 次预期 schema 拒绝），不是 15 项 CTest。`No tests were found`
不算通过。烟测验证的内容、期限，以及不经 CTest 直接运行的方法见
[example README](../../example/benchmark_fb/README.md#smoke-test)。

### 手工分别运行 server/client

手工运行客户端的命令、预期结果和有界参数见
[example README](../../example/benchmark_fb/README.md#run-separately)。

### 复用已有运行库或改为动态链接

已有兼容的 FB ON 运行库时，无需重编：将 example 配置中的 `BRPC_ROOT` 替换为其安装前缀
（CMake）或 `output/`（Make），`BRPC_FLATC_EXECUTABLE` 指向匹配的生成器，其他步骤不变。
example 会读取该前缀真实的 `include/butil/config.h`。

默认链接 `libbrpc.a`。使用 `LINK_SO=ON` 前，先用 `BUILD_SHARED_LIBS=ON` 构建 `brpc-shared`
并再次执行 `cmake --install`，再用 `-DLINK_SO=ON` 重新配置并构建 example；`LINK_SO` 本身
不会构建运行库。运行库若增加可选依赖，可通过 `BRPC_EXTRA_LIBRARIES` 补充链接库。

## 4. Make、Bazel 和完整回归

### Make

在独立可写 checkout 中配置，沿用前述依赖变量：

```sh
sh config_brpc.sh --with-flatbuffers \
  --headers="$FB/include $DEPS/include $OPENSSL/include" \
  --libs="$DEPS/lib $OPENSSL/lib" --cc="${CC:-cc}" --cxx="${CXX:-c++}"
make -j"$JOBS"
```

按平台替换为实际 `lib64` 或库目录。产出的 `output/` 可以替换 example 命令中的 `$WORK/prefix`，作为 `BRPC_ROOT`。
Make 单测通过 `FLATC="$FB/bin/flatc"` 选择官方生成器，但还需要已安装的 GoogleTest 库和
gperftools，不是只提供 GoogleTest 源码即可；执行时也需能够加载 `test/libbrpc.dbg.*`。
下方 ON runner 会构建 GoogleTest、设置测试库路径并校验报告；gperftools 等系统测试依赖
仍需预先安装。

### Bazel

```sh
bazel build --define=BRPC_WITH_FLATBUFFERS=true //:brpc
bazel test --define=BRPC_WITH_FLATBUFFERS=true --cache_test_results=no \
  //test:brpc_flatbuffers_unittest //test:brpc_flatbuffers_protocol_unittest
```

这些目标验证库，不会自动执行独立 example。不要将原始 `bazel-bin` 当成 `BRPC_ROOT`；
example 需要 CMake 安装前缀或 Make `output/` 的 include/lib 布局。

### 库单测与 ON gate

单独配置库单测时，设置 `BUILD_UNIT_TESTS=ON`、匹配的 `FLATBUFFERS_FLATC_EXECUTABLE`；
若 `DOWNLOAD_GTEST=OFF`，还需指定 `BRPC_SYSTEM_GTEST_SOURCE_DIR`。
构建并运行 `brpc_flatbuffers_unittest`、`brpc_flatbuffers_protocol_unittest`。
仅打开库的 FlatBuffers 选项，不会自动执行测试。

要一次验证两组库单测、两项 codegen 验收及 example，可使用
[ON runner](../../.github/scripts/flatbuffers-on.py)。完整 CMake 门禁需要 **CMake/CTest 3.21+**
以生成 JUnit XML 报告（`--output-junit`）。**`--work` 目录必须尚不存在**：

```sh
python3 "$REPO/.github/scripts/flatbuffers-on.py" \
  --build-system cmake --source "$REPO" --work "$WORK/on-gate" --jobs "$JOBS" \
  --flatbuffers-prefix "$FB" \
  --dependency-prefix "$DEPS" --dependency-prefix "$OPENSSL"
```

该门禁要求 FlatBuffers 25.2.10。不传 `--flatbuffers-prefix` 时会下载、校验并构建该版本。
GoogleTest 默认下载并校验固定的 1.14.0，也可用 `--gtest-source` 复用源码。
平台常规依赖仍需预先安装。Make/Bazel 模式验证两组库测试，只有 CMake 模式额外构建
生成器和 example。

检查 `WORK/on-gate/evidence/summary.json`：`status` 应为 `passed`，应包含所有预期测试，
执行数量非零，失败和跳过均为零。各步骤命令、日志及 XML 即使失败也会保留；
空测试、过滤后少跑或跳过不会被当作成功。

[CI 工作流](../../.github/workflows/flatbuffers-on.yml)覆盖 Linux CMake/Make 的 GCC、Clang，
Linux Bazel GCC，以及 macOS CMake。本地 gate 通过，不代表 GitHub 托管矩阵或另一套依赖
组合已经通过。

## 5. 常见问题

| 现象 | 检查与处理 |
| --- | --- |
| `BRPC_ROOT is not FlatBuffers-enabled` 或缺少 FB 符号 | 用正确选项重编运行库和使用方，检查安装前缀里的 `include/butil/config.h`；不要强制宏或混用 ON 头文件与 OFF 库。 |
| 找不到 `flatbuffers/idl.h`、`libflatbuffers` | 生成器需要完整官方开发安装，单有运行时头文件不够；明确指定 include 和 library 路径。 |
| 头文件与 `flatc` 版本不匹配 | 检查 `flatbuffers/base.h` 和 `flatc --version`，统一版本，在新目录重新生成代码，不删除版本断言。 |
| Protobuf/Abseil 缺头文件或链接符号 | 统一 Protobuf，提供其 config 包和 Abseil 前缀，避免系统与私有安装混用。 |
| macOS 找不到 OpenSSL | 设置实际 `OPENSSL_ROOT_DIR` 和 `CMAKE_PREFIX_PATH`，保持编译器、SDK、架构一致。 |
| 无法写 `src/butil/config.h.tmp` | 只有 `config_brpc.sh`（Make）会往源码目录写这个文件，所以源码必须可写；用独立副本隔离。CMake 把 `config.h` 放在 build 目录。 |
| CMake 4 报旧依赖策略不兼容 | 对相关依赖使用适当的 policy compatibility 设置或升级依赖；不能借此降低 C++ 和依赖版本要求。 |
| `No tests were found` | 检查 example 的 build 目录、`BUILD_TESTING=ON` 和已构建的程序，或直接运行 `smoke.py`。 |
| `LINK_SO=ON` 找不到库或动态加载失败 | 先用 `BUILD_SHARED_LIBS=ON` 构建 `brpc-shared`，再检查共享库及传递依赖的运行时搜索路径。 |
| 连接拒绝或超时 | 等待 readiness，用当前端口，确保同一主机/容器且服务端未超过生命周期；不能计作 schema 拒绝成功。 |

## 6. 消息构造和所有权

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

## 7. Service ID 和代码生成

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

## 8. 网络 RPC

库和使用方必须用相同的 `BRPC_WITH_FLATBUFFERS` 设置构建；启用该功能会改变 Channel、Controller、Server 的 ABI。用 `server.AddFlatBuffersService(&service, SERVER_DOESNT_OWN_SERVICE)` 注册生成的服务；将 `ChannelOptions::protocol` 设为 `"fb_rpc"`，并把这个 `brpc::Channel` 传给生成的 stub。stub 调用 `Channel::FBCallMethod`；`Controller::flatbuffers_method()` 返回带类型的 descriptor，protobuf 的 `Controller::method()` 仍为空。手工提供的 descriptor 必须活过整个调用，包括重试和 backup request。

服务注册独立于 protobuf 的 AddService 和 ListServices。Add/Remove/Clear 要求服务端处于已停止（READY）状态，不能是正在 STOPPING；管理操作必须由外部串行化。只有注册成功后所有权才转移。`RemoveFlatBuffersService` 会删除自有服务，Stop/Join 则保留注册以便重启。`GetFlatBuffersServiceCount()` 返回这个独立注册表的数量。重复的 service ID（包括 hash 冲突）以及与 protobuf 方法全名冲突的 FlatBuffers 方法会被拒绝。按字符串调用的 `MaxConcurrencyOf` 支持 FlatBuffers 方法，服务端整体并发和默认方法并发限制同样适用。

传输层支持同步/异步调用、重试、backup request、single/pooled/short 连接以及请求/响应附件。每次发送都会分配一个独立的小 header 并共享 payload 存储，不会改写 const 请求的 metadata 前缀，因此重试和并发调用可以安全共享同一个不可变请求。应用仍须保证每个 response 和 Controller 活到调用完成。

Framing 只检查长度，不检查应用 schema。生成的服务会在分派前验证请求，手写服务也必须这样做。调用方使用 root accessor 前必须验证收到的响应 schema，RPC 成功不能替代 `response.Verify<Response>()`。

不支持鉴权、压缩、校验和与 streaming，遇到时会明确拒绝而不是悄悄忽略。FlatBuffers 服务不能通过仅限内置服务的内部端口访问。本传输层不提供 SelectiveChannel/ParallelChannel、HTTP/JSON 映射和 RPC-dump 回放，也不传输 log ID、用户字段和分布式追踪元数据。

完整的客户端/服务端示例见[第 3 节](#3-通过-example-完成端到端验证)；示例演示这些 API，不做性能结论。

### FRPC 帧格式与兼容性

一帧由 `[12 字节 header][metadata][message][attachment]` 组成。header 包含 `FRPC`、uint32 的 body 大小和 uint32 的 metadata 大小；body 大小不含 12 字节 header，但包含后面三段。header 和固定 metadata 前缀中的所有整数都使用大端（网络字节序），与 `baidu_std` 的 header 一致。FlatBuffers message 本身保持 FlatBuffers 定义的小端布局，不属于帧格式。

* 请求：uint32 service ID、int32 method ID、int32 message 大小、int32 attachment 大小、uint64 correlation ID（共 24 字节）。
* 响应：int32 错误码、int32 message 大小、int32 attachment 大小、uint64 correlation ID（共 20 字节）。

读取方要求完整的已知前缀，然后根据 header 中的 metadata 大小跳过未知的尾部。以后新增的可选字段只能追加，不要重排、改变大小或改变已有字段的含义。前缀过短、大小为负、payload 长度不一致或 body 过大都会被拒绝。错误响应带非零错误码且没有 payload。Method ID 是稀疏的 wire ID，不是数组下标：只有保留显式 ID 时，增加、删除或重排声明才不会改变已有方法的 ID。这并不意味着旧的按序号编号的 schema 能兼容，已删除的 ID 也绝不能复用。

magic 是 `FRPC`，不是 `BRPC`；和 `baidu_std` 的 `PRPC` 一样，server 靠它在与其他协议共用的端口上识别协议。该格式不保证与更早的实验性 FRPC 版本兼容，那些版本使用了不同的 magic、service hash 或字节序。库的全局 Protocol hook 签名保持不变，现有协议回调实现无需修改。自定义的数字协议 ID 不得与新分配的内置 ID 重叠，`PROTOCOL_FLATBUFFERS_RPC` 使用 ID 30。
