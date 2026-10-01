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

## 2. 用 CMake 构建启用后的运行库

从 **可写的 bRPC checkout** 开始。按需提前设置有效的 `CC`、`CXX`。
下面的 example 绑定代码与构建产物在 `WORK` 中，但根项目配置仍会写入
**源码目录的 `src/butil/config.h`**。只读源码挂载会失败；同一 checkout 即使使用不同的
build 目录，也不能并发配置 ON/OFF 或混用不同构建系统。需要隔离时使用独立可写副本。

将下列变量替换为已有安装前缀，即包含 `include/`、`lib/` 或 `lib64/` 的目录；多个变量
可以指向同一前缀。macOS 上需保持编译器、SDK、架构一致，并显式指定实际 OpenSSL 前缀，
不要假定 Apple Silicon 环境中存在 `/usr/local/opt/openssl`。

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
  -DWITH_FLATBUFFERS=ON -DBUILD_SHARED_LIBS=OFF \
  -DFLATBUFFERS_INCLUDE_DIR="$FB/include" \
  -DBUILD_UNIT_TESTS=OFF -DDOWNLOAD_GTEST=OFF -DBUILD_BRPC_TOOLS=OFF
cmake --build "$WORK/runtime" --parallel "$JOBS"
grep -E '^#define BRPC_WITH_FLATBUFFERS +1$' "$WORK/runtime/output/include/butil/config.h"
```

最后一条检查必须输出 `#define BRPC_WITH_FLATBUFFERS 1`。公共接口位于
`brpc/flatbuffers/message.h` 和 `brpc/flatbuffers/service.h`。
上面的 policy 参数是兼容 CMake 4 下某些旧依赖策略的可选设置，不是启用 FlatBuffers 的
开关，也不能替代编译器和依赖版本要求。

## 3. 通过 example 完成端到端验证

该示例是**有界功能验证，不是性能对比**。[echo.fbs](../../example/benchmark_fb/echo.fbs)
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
  -DBRPC_ROOT="$WORK/runtime/output" \
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

这是 **15 次 RPC：13 次正常响应、2 次预期 schema 拒绝**，不是 15 项 CTest。
验证内容包括二进制字节、空字符串与缺失字符串、附件、并发、single/pooled/short 连接、
拒绝后的正常调用恢复，以及服务端的干净退出。

脚本自行启动 `127.0.0.1:0` 服务端并读取系统分配的端口，不依赖固定端口或外部服务。
编排期限为 35 秒，CTest 外层超时为 50 秒；正常清理应完成 Stop/Join，若必须 SIGKILL
才退出则烟测失败。`No tests were found` 不算通过。也可直接运行相同校验：

```sh
python3 "$REPO/example/benchmark_fb/smoke.py" \
  --server "$WORK/example/benchmark_fb_server" \
  --client "$WORK/example/benchmark_fb_client"
```

### 手工分别运行 server/client

两个进程必须位于**同一主机或容器**，该 example 刻意拒绝非 loopback 地址。
在构建终端启动服务端：

```sh
"$WORK/example/benchmark_fb_server" --listen_addr=127.0.0.1:0 --duration_s=300
```

等待 `BRPC_FB_READY 127.0.0.1:<port>`。另开终端时，必须重新设置 `WORK` 为构建时打印的
绝对路径；shell 变量不会自动共享。将 `PORT_FROM_READY` 换为实际数字端口：

```sh
WORK=/absolute/path/printed/by/the/build
SERVER=127.0.0.1:PORT_FROM_READY
"$WORK/example/benchmark_fb_client" --server="$SERVER" \
  --request_count=16 --thread_num=2 --request_size=8193 --attachment_size=257
"$WORK/example/benchmark_fb_client" --server="$SERVER" \
  --request_count=2 --thread_num=1 \
  --request_size=0 --omit_message=true
"$WORK/example/benchmark_fb_client" --server="$SERVER" \
  --request_count=2 --thread_num=1 --corrupt_request=true
"$WORK/example/benchmark_fb_client" --server="$SERVER" \
  --request_count=2 --thread_num=1
```

四条 client 命令均应退出 0，JSON 结果依次为：

```text
{"completed":16,"successes":16,"expected_rejections":0,"failures":0}
{"completed":2,"successes":2,"expected_rejections":0,"failures":0}
{"completed":2,"successes":0,"expected_rejections":2,"failures":0}
{"completed":2,"successes":2,"expected_rejections":0,"failures":0}
```

`failures` 是 0/1 错误标志，不是失败 RPC 数量。破坏请求的命令只有在服务端返回 schema
拒绝错误码 -1，且响应和附件为空时才成功；连接失败、超时不能算作预期拒绝。
最后一条正常请求验证同一服务端在拒绝后仍可工作。

服务端会在 300 秒后退出，也可用 Ctrl-C 提前停止。客户端的 `request_count` 为所有线程的
总请求数，不是每线程数量；`request_size` 为字符串字节数，不是整个 frame 大小。
字符串和附件含确定性的二进制数据，包括零字节。

| 参数 | 范围或含义 |
| --- | --- |
| `request_count` / `thread_num` | 1..1000 / 1..16 |
| `request_size` / `attachment_size` | 各为 0..1048576 字节 |
| `timeout_ms` / `deadline_ms` | 每 RPC 1..5000 毫秒 / 客户端总期限 1..120000 毫秒 |
| `connection_type` | `single`、`pooled`、`short` |
| `omit_message=true` | 不设置字符串，与空字符串不同 |
| `corrupt_request=true` | 破坏根偏移并要求 schema 拒绝 |
| 服务端 `duration_s` / `max_concurrency` | 1..3600 秒 / 1..64 |

### 复用已有运行库或改为动态链接

已有兼容的 FB ON 运行库时，无需重编它：将 example 配置中的 `BRPC_ROOT` 替换为其
`output/` 或安装前缀；`BRPC_FLATC_EXECUTABLE` 指向匹配的生成器，其他步骤不变。
example 会读取该前缀真实的 `include/butil/config.h`，不会用替代头文件假装启用功能。

默认链接 `libbrpc.a`。`LINK_SO=ON` 只选择已经存在的共享库，不会替你构建运行库。
在上面的完整构建之后，可按以下顺序切换并验证：

```sh
cmake -S "$REPO" -B "$WORK/runtime" -DBUILD_SHARED_LIBS=ON
cmake --build "$WORK/runtime" --target brpc-shared --parallel "$JOBS"
cmake -S "$REPO/example/benchmark_fb" -B "$WORK/example" -DLINK_SO=ON
cmake --build "$WORK/example" --parallel "$JOBS"
(cd "$WORK/example" && ctest -V --no-tests=error --output-on-failure -R '^benchmark_fb_smoke$')
```

这几条重配置命令依赖此前已填写的 CMake cache；全新 build 目录仍需传完整参数。
定制运行库若增加可选依赖，可通过 `BRPC_EXTRA_LIBRARIES` 补充链接库。

## 4. Make、Bazel 和完整回归

### Make

在独立可写 checkout 中配置，沿用前述依赖变量：

```sh
sh config_brpc.sh --with-flatbuffers \
  --headers="$FB/include $DEPS/include $OPENSSL/include" \
  --libs="$DEPS/lib $OPENSSL/lib" --cc="${CC:-cc}" --cxx="${CXX:-c++}"
make -j"$JOBS"
```

按平台替换为实际 `lib64` 或库目录。产出的 `output/` 可以作为 example 的 `BRPC_ROOT`。
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
example 需要 CMake/Make output 或安装前缀的 include/lib 布局。

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
| `BRPC_ROOT is not FlatBuffers-enabled` 或缺少 FB 符号 | 用正确选项重编运行库和使用方，检查实际 output 的配置头；不要强制宏或混用 ON 头文件与 OFF 库。 |
| 找不到 `flatbuffers/idl.h`、`libflatbuffers` | 生成器需要完整官方开发安装，单有运行时头文件不够；明确指定 include 和 library 路径。 |
| 头文件与 `flatc` 版本不匹配 | 检查 `flatbuffers/base.h` 和 `flatc --version`，统一版本，在新目录重新生成代码，不删除版本断言。 |
| Protobuf/Abseil 缺头文件或链接符号 | 统一 Protobuf，提供其 config 包和 Abseil 前缀，避免系统与私有安装混用。 |
| macOS 找不到 OpenSSL | 设置实际 `OPENSSL_ROOT_DIR` 和 `CMAKE_PREFIX_PATH`，保持编译器、SDK、架构一致。 |
| 无法写 `src/butil/config.h.tmp` | 源码 checkout 必须可写；用独立副本隔离配置，不只隔离 build 目录。 |
| CMake 4 报旧依赖策略不兼容 | 对相关依赖使用适当的 policy compatibility 设置或升级依赖；不能借此降低 C++ 和依赖版本要求。 |
| `No tests were found` | 检查 example 的 build 目录、`BUILD_TESTING=ON` 和已构建的程序，或直接运行 `smoke.py`。 |
| `LINK_SO=ON` 找不到库或动态加载失败 | 先用 `BUILD_SHARED_LIBS=ON` 构建 `brpc-shared`，再检查共享库及传递依赖的运行时搜索路径。 |
| 连接拒绝或超时 | 等待 readiness，用当前端口，确保同一主机/容器且服务端未超过生命周期；不能计作 schema 拒绝成功。 |

## 6. 接入应用时的边界

- 用官方 `flatc --cpp` 生成 table，再用 `brpc_flatc` 生成服务。每个 RPC 方法必须有稳定的
  非负 int32 `(id: N)`；不要用声明顺序替代 ID，也不要将已删除 ID 分配给不同方法。
- 业务 schema 使用独立 namespace。尤其 `flatc 2.0.x` 生成未全限定的 `flatbuffers::` 名称，
  不要把业务 schema 放在 `brpc` 下并依赖 include 顺序规避遮蔽。
- `MessageBuilder` 完成 `Finish` 后调用 `ReleaseMessage()`；消息拥有 IOBuf 存储引用。
  序列化共享的消息不是 copy-on-write，不要在其他调用仍读取时修改其内容。
- frame 长度检查不等于 schema 验证。收到消息后先 `Verify<Root>()`，再访问 root；
  生成服务会在分派前检查请求，手写服务也应如此。RPC 成功不能替代响应 schema 验证。
- Channel 使用 `fb_rpc`；服务通过 `AddFlatBuffersService` 注册。管理服务需在停止状态下
  进行；成功实现必须按约定恰好执行一次 completion。借用的服务、描述符、请求及回调
  所需对象应保持到相应生命周期结束。
- 支持同步/异步、重试、backup request、三类连接及附件；不支持鉴权、压缩、校验和、
  streaming、HTTP/JSON 映射、SelectiveChannel/ParallelChannel 和 RPC-dump 回放。
  本 example 不是公网或生产部署配置。
- 协议 magic 是 `FRPC`，不是 `BRPC`；不要假定可与历史实验版本的 wire format 互通。

完整消息所有权、描述符及线格式约束参见[英文参考](../en/flatbuffers.md#message-construction-and-ownership)。
