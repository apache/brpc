gdb（ptrace）+ gdb_bthread_stack.py主要的缺点是要慢和阻塞进程，需要一种高效的追踪bthread调用栈的方法。

bRPC框架的协作式用户态协程无法像Golang内建的抢占式协程一样实现高效的STW（Stop the World），框架也无法干预用户逻辑的执行，所以要追踪bthread调用栈是比较困难的。

在线追踪bthread调用栈需要解决以下问题：
1. 追踪挂起bthread的调用栈。 
2. 追踪运行中bthread的调用栈。

# bthread状态模型

以下是目前的bthread状态模型。

![bthread状态模型](../images/bthread_status_model.svg)

# 设计方案

## 核心思路

为了解决上述两个问题，该方案实现了STB（Stop The Bthread），核心思路可以简单总结为，在追踪bthread调用栈的过程中，状态不能流转到当前追踪方法不支持的状态。STB包含了两种追踪模式：上下文（context）追踪模式和信号追踪模式。

### 上下文（context）追踪模式
上下文追踪模式可以追踪挂起bthread的调用栈。挂起的bthread栈是稳定的，利用TaskMeta.stack中保存的上下文信息（x86_64下关键的寄存器主要是RIP、RSP、RBP），通过一些可以回溯指定上下文调用栈的库来追踪bthread调用栈。但是挂起的bthread随时可能会被唤醒，执行逻辑（包括jump_stack），则bthread栈会一直变化。不稳定的上下文是不能用来追踪调用栈的，需要在jump_stack前拦截bthread的调度，等到调用栈追踪完成后才继续运行bthread。所以，上下文追踪模式支持就绪、挂起这两个状态。

### 信号追踪模式

信号追踪模式可以追踪运行中bthread的调用栈。运行中bthread是不稳定的，不能使用TaskMeta.stack来追踪bthread调用栈。只能另辟蹊径，使用信号中断bthread运行逻辑，在信号处理函数中回溯bthread调用栈。使用信号有两个问题：

1. 异步信号安全问题。
2. 信号追踪模式不支持jump_stack。调用栈回溯需要寄存器信息，但jump_stack会操作寄存器，这个过程是不安全的，所以jump_stack不能被信号中断，需要在jump_stack前拦截bthread的调度，等到bthread调用栈追踪完成后才继续挂起bthread。

所以，追踪模式只支持运行状态。

### 小结

jump_stack是bthread挂起或者运行的必经之路，也是STB的拦截点。STB将状态分成三类：
1. 上下文追踪模式的状态：就绪、挂起。
2. 支持信号追踪模式的状态：运行。
3. 不支持追踪的状态。jump_stack的过程是不允许使用以上两种调用栈追踪方法，需要在jump_stack前拦截bthread的调度，等到调用栈追踪完成后才继续调度bthread。

### 详细流程

以下是引入STB后的bthread状态模型，在原来bthread状态模型的基础上，加入两个状态（拦截点）：将运行、挂起中。

![bthread STB状态模型](../images/bthread_stb_model.svg)

经过上述分析，总结出STB的流程：

1. TaskTracer（实现STB的一个模块）收到追踪bthread调用栈的请求时，标识正在追踪。追踪完成后，标识追踪完成，并TaskTracer发信号通知可能处于将运行或者挂起中状态的bthread。根据bthread状态，TaskTracer执行不同的逻辑：
- 创建、就绪但还没分配栈、销毁：直接结束追踪。
- 挂起、就绪：使用上下文追踪模式追踪bthread的调用栈。
- 运行：使用信号追踪模式追踪bthread的调用栈。
- 将运行、挂起中：TaskTracer自旋等到bthread状态流转到下一个状态（挂起或者运行）后继续追踪。

2. TaskTracer追踪时，bthread根据状态也会执行不同的逻辑：
- 创建、就绪但还没分配栈、就绪：不需要额外处理。
- 挂起、运行：通知TaskTracer继续追踪。
- 将运行、挂起中、销毁：bthread通过条件变量等到TaskTracer追踪完成。TaskTracer追踪完成后会通过条件变量通知bthread继续执行jump_stack。

# 使用方法

1. 下载安装libunwind和abseil-cpp。**注意：libunwind 必须从源码编译，不要使用系统包管理器安装的 `libunwind-dev` / `libunwind-devel`**，否则会触发本文末尾「[已知问题：libunwind 与 libgcc_s 的 `_Unwind_*` 符号冲突](#已知问题libunwind-与-libgcc_s-的-_unwind_-符号冲突)」中描述的崩溃。bazel 构建可以跳过此步，直接使用 brpc 仓库自维护的 libunwind 版本。
2. 给config_brpc.sh增加`--with-bthread-tracer`选项或者给cmake增加`-DWITH_BTHREAD_TRACER=ON`选项或者给bazel（Bzlmod模式）增加`--define with_bthread_tracer=true`选项。
3. 访问服务的内置服务：`http://ip:port/bthreads/<bthread_id>?st=1`或者代码里调用`bthread::stack_trace()`函数。
4. 如果希望追踪pthread的调用栈，在对应pthread上调用`bthread::init_for_pthread_stack_trace()`函数获取一个伪bthread_t，然后使用步骤3即可获取pthread调用栈。

下面是追踪bthread调用栈的输出示例：
```shell
#0 0x00007fdbbed500b5 __clock_gettime_2
#1 0x000000000041f2b6 butil::cpuwide_time_ns()
#2 0x000000000041f289 butil::cpuwide_time_us()
#3 0x000000000041f1b9 butil::EveryManyUS::operator bool()
#4 0x0000000000413289 (anonymous namespace)::spin_and_log()
#5 0x00007fdbbfa58dc0 bthread::TaskGroup::task_runner()
```

# 已知问题

## libunwind 与 libgcc_s 的 `_Unwind_*` 符号冲突

### 现象

启用 bthread tracer 后，可能在 `bthread_exit` / `pthread_exit` 或者 C++ 异常处理路径上偶发段错误，类似如下调用栈：

```text
#0  0x0000000000000000 in ?? ()
#1  0x00007fa2b5d6458a in _ULx86_64_dwarf_find_proc_info ()
   from /root/.cache/bazel/_bazel_root/743b333b2429a1dbd390ef66b59c771d/execroot/_main/bazel-out/k8-fastbuild/bin/test/../_solib_k8/libexternal_Slibunwind~_Slibunwind.so
#2  0x00007fa2b5d6668d in fetch_proc_info ()
   from /root/.cache/bazel/_bazel_root/743b333b2429a1dbd390ef66b59c771d/execroot/_main/bazel-out/k8-fastbuild/bin/test/../_solib_k8/libexternal_Slibunwind~_Slibunwind.so
#3  0x00007fa2b5d681a1 in _ULx86_64_dwarf_make_proc_info ()
   from /root/.cache/bazel/_bazel_root/743b333b2429a1dbd390ef66b59c771d/execroot/_main/bazel-out/k8-fastbuild/bin/test/../_solib_k8/libexternal_Slibunwind~_Slibunwind.so
#4  0x00007fa2b5d70cfd in _ULx86_64_get_proc_info ()
   from /root/.cache/bazel/_bazel_root/743b333b2429a1dbd390ef66b59c771d/execroot/_main/bazel-out/k8-fastbuild/bin/test/../_solib_k8/libexternal_Slibunwind~_Slibunwind.so
#5  0x00007fa2b5d6c775 in __libunwind_Unwind_GetLanguageSpecificData ()
   from /root/.cache/bazel/_bazel_root/743b333b2429a1dbd390ef66b59c771d/execroot/_main/bazel-out/k8-fastbuild/bin/test/../_solib_k8/libexternal_Slibunwind~_Slibunwind.so
#6  0x00007fa2b503c6df in __gxx_personality_v0 () from /lib/x86_64-linux-gnu/libstdc++.so.6
#7  0x00007fa2b5452ce5 in ?? () from /lib/x86_64-linux-gnu/libgcc_s.so.1
#8  0x00007fa2b54533c0 in _Unwind_ForcedUnwind () from /lib/x86_64-linux-gnu/libgcc_s.so.1
#9  0x00007fa2b4ca57a4 in __GI___pthread_unwind (buf=<optimized out>) at ./nptl/unwind.c:130
#10 0x00007fa2b4c9dd22 in __do_cancel () at ../sysdeps/nptl/pthreadP.h:271
#11 __GI___pthread_exit (value=0x0) at ./nptl/pthread_exit.c:36
#12 0x0000000000000000 in ?? ()
```

### 根因

libunwind 的 `src/unwind/*.c` 实现了 GCC 的 `_Unwind_*` ABI 兼容层（`_Unwind_GetLanguageSpecificData`、`_Unwind_ForcedUnwind`、`_Unwind_Resume` 等），与 `libgcc_s.so.1` 提供同名的全局符号。当 libunwind 以**动态库**形式被链接，并且在最终二进制的 `DT_NEEDED` 列表中位置比 `libgcc_s.so.1` 靠前时，运行时动态链接器 `ld.so` 会把 `pthread_exit` / 异常处理触发的 `_Unwind_*` 调用解析到 libunwind 中的 DWARF 实现。该实现需要的内部上下文在 `pthread_exit` 路径上未被 bRPC 初始化好，从而触发空指针访问。

这是一个 ELF **运行时符号解析顺序**问题，与编译器（GCC / Clang）无关 —— Clang 默认运行时同样使用 `libstdc++ + libgcc_s`，会复现完全一致的崩溃。

### 解决方案

> **重要：不要使用系统包管理器安装的 libunwind**（例如 `apt install libunwind-dev`、`yum install libunwind-devel`）。多数发行版打包的 `libunwind.so` 仍把 `_Unwind_*` 暴露在动态符号表中，会触发本节描述的崩溃。
>
> 必须使用**从源码编译的 libunwind**。上游 `./configure` + `make` 默认会通过 `-Wl,--version-script` 把 `_Unwind_*` 标为 local，不导出到动态符号表，从而避免冲突。

下表汇总了三种构建方式的推荐方案：

| 构建方式 | 推荐方案 |
|---|---|
| `config_brpc.sh` + `make` | 从源码编译并安装 libunwind，把头文件与库目录显式传给 `config_brpc.sh` |
| `cmake` | 从源码编译并安装 libunwind，把头文件与库目录显式传给 `cmake` |
| `bazel`（Bzlmod） | 直接使用 brpc 仓库自维护的 libunwind 版本 |

### make (config_brpc.sh)

源码编译并安装 libunwind 到一个独立目录（避免污染系统目录），然后让 `config_brpc.sh` 显式从该目录查找 libunwind。

```bash
# 1) 源码编译 libunwind（推荐 v1.8.1 或以上版本）
git clone https://github.com/libunwind/libunwind.git
cd libunwind && git checkout tags/v1.8.1
mkdir -p /opt/libunwind
autoreconf -i
./configure --prefix=/opt/libunwind
make -j$(nproc) && make install
cd ..

# 2) 让 config_brpc.sh 使用 /libunwind 下的头文件与库（不要让它自动找到系统的 libunwind-dev）
cd brpc
sh config_brpc.sh \
    --with-bthread-tracer \
    --headers="/opt/libunwind/include /usr/include" \
    --libs="/opt/libunwind/lib /usr/lib /usr/lib64"
make -j$(nproc)
```

构建完成后可用以下命令确认 libunwind.so 没有导出 `_Unwind_*`：

```bash
nm -D /libunwind/lib/libunwind.so | grep ' T _Unwind_' \
    && echo "WARN: _Unwind_* exported" \
    || echo "OK: _Unwind_* hidden"
```

### cmake

[`CMakeLists.txt`](../../CMakeLists.txt:90-100) 通过 `find_library(... NAMES unwind unwind-x86_64)` 查找 libunwind。同样需要先源码编译 libunwind 到独立前缀，再用 `CMAKE_PREFIX_PATH` 让 cmake 优先在该前缀下查找：

```bash
# 1) 源码编译 libunwind（同 make 章节）

# 2) 让 cmake 在 /libunwind 下优先查找头文件和库
cd brpc
mkdir build && cd build
cmake -DWITH_BTHREAD_TRACER=ON \
      -DCMAKE_PREFIX_PATH=/opt/libunwind \
      ..
make -j$(nproc)
```

> 提示：如果系统已经装了 `libunwind-dev`，`find_library` 仍可能优先匹配到 `/usr/lib`。可在 cmake 命令上额外指定
> `-DLIBUNWIND_LIB=/libunwind/lib/libunwind.so -DLIBUNWIND_X86_64_LIB=/libunwind/lib/libunwind-x86_64.so -DLIBUNWIND_INCLUDE_PATH=/libunwind/include`
> 强制走自编译版本，避免系统包混入。

### bazel (Bzlmod)

bRPC 仓库已经在 [`registry/modules/libunwind/`](../../registry/modules/libunwind/) 维护了一份 libunwind 的 Bzlmod overlay，并通过 [`.bazelrc`](../../.bazelrc) 中的 `--registry=https://github.com/apache/brpc/registry` 使用 bRPC 维护的 overlay。版本号采用 `<base>.brpc-no-unwind` 后缀（例如 `1.8.3.brpc-no-unwind`），用以区别于 BCR 上的同基版本。该 overlay 增加了一个开关：

```
--define libunwind_hide_unwind_symbols=true
```

开启后，libunwind 的 `src/unwind/*.c`（即 GCC `_Unwind_*` 兼容层）整体不参与编译，等效于上游 autoconf 默认效果。bRPC 只使用 libunwind 的 `unw_*` 原生 API（`unw_getcontext`、`unw_init_local`、`unw_step` 等），不依赖 `_Unwind_*` 兼容层，因此该开关安全无副作用。

`.bazelrc` 已默认在 `build:test` / `test` 配置下打开此开关：

```
build:test --define libunwind_hide_unwind_symbols=true
test       --define libunwind_hide_unwind_symbols=true
```

用户按文档使用方法第 2 步加上 `--define=with_bthread_tracer=true` 即可：

```bash
# 测试场景，.bazelrc 中 test 配置已自动带上 hide 开关
bazel test //test:bthread_unittest

# 非测试构建（生产部署），需要显式同时带上两个 define
bazel build --define=with_bthread_tracer=true \
            --define=libunwind_hide_unwind_symbols=true \
            //...
```

> **特别注意**：如果在生产构建中只启用 `--define=with_bthread_tracer=true` 而漏掉 `--define=libunwind_hide_unwind_symbols=true`，binary 在 `pthread_exit` / 异常路径上会概率性崩溃。

构建后可用以下命令验证 libunwind 共享库没有导出 `_Unwind_*`：

```bash
nm -D bazel-bin/external/_solib_*/libexternal*libunwind*.so 2>/dev/null \
    | grep ' T _Unwind_' || echo "OK: no _Unwind_* exported by libunwind.so"
```


# 相关flag

- `signal_trace_timeout_ms`：信号追踪模式的超时时间，默认为50ms。