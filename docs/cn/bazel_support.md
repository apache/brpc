## bRPC 作为 Bazel 第三方依赖

推荐在 Bazel 项目中使用 bzlmod（`MODULE.bazel`）依赖本地 bRPC 源码。
`example/build_with_bazel_module` 中有一个包含 server 和 client 的可运行示例：

```shell
$ cd example/build_with_bazel_module
$ bazel build //:echo_c++_server //:echo_c++_client
$ bazel run //:echo_c++_server &
$ bazel run //:echo_c++_client
```

先在你的 `.bazelrc` 中添加 bRPC 使用的 registry：

```shell
common --registry=https://bcr.bazel.build
common --registry=https://baidu.github.io/babylon/registry
common --registry=https://raw.githubusercontent.com/apache/brpc/master/registry
```

然后在 `MODULE.bazel` 中添加 bRPC，并指向本地 bRPC 源码：

```python
module(
    name = "my_brpc_app",
    version = "0.1.0",
)

bazel_dep(name = "protobuf", version = "27.3", repo_name = "com_google_protobuf")
bazel_dep(name = "brpc", version = "1.17.0", repo_name = "apache_brpc")

local_path_override(
    module_name = "brpc",
    path = "/path/to/brpc",
)

single_version_override(
    module_name = "leveldb",
    registry = "https://raw.githubusercontent.com/secretflow/bazel-registry/main",
)

single_version_override(
    module_name = "openssl",
    version = "3.3.2.bcr.1",
    registry = "https://raw.githubusercontent.com/secretflow/bazel-registry/main",
)
```

`bazel_dep` 用来声明模块名和仓库映射，`local_path_override` 让 Bazel 使用本地源码，而不是从 registry 解析 bRPC。

bzlmod 不会传递依赖模块中的 override。把 bRPC 当依赖时，需要在自己项目的根
`MODULE.bazel` 里重复上面的 `single_version_override`；它们在 aarch64 上是必需的，否则 leveldb toolchain 解析会失败。

之后在目标中链接 bRPC：

```python
cc_binary(
    name = "server",
    srcs = ["server.cpp"],
    deps = [
        "@apache_brpc//:brpc",
    ],
)
```

如果服务使用 protobuf，可以从 bRPC 加载 `brpc_proto_library`：

```python
load("@apache_brpc//bazel/tools:brpc_proto_library.bzl", "brpc_proto_library")

brpc_proto_library(
    name = "cc_echo_proto",
    srcs = ["echo.proto"],
)
```

## 旧版 WORKSPACE 用法

仍在使用 `WORKSPACE` 的项目可以参考 `example/build_with_bazel`。

1. 将 `example/build_with_bazel/*.BUILD` 和
   `example/build_with_bazel/brpc_workspace.bzl` 移动到你的项目根目录下。
2. 在 `WORKSPACE` 中添加：

```python
load("@//:brpc_workspace.bzl", "brpc_workspace")

brpc_workspace()
```

3. 在目标中链接 `apache_brpc`：

```python
deps = [
    "@apache_brpc//:bthread",
    "@apache_brpc//:brpc",
    "@apache_brpc//:butil",
    "@apache_brpc//:bvar",
]
```
