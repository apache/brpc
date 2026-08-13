## bRPC as a Bazel third-party dependency

The recommended way to depend on bRPC from a Bazel project is to use bzlmod
(`MODULE.bazel`). See `example/build_with_bazel_module` for a runnable example.

Add the registries used by bRPC to your `.bazelrc`:

```shell
common --registry=https://bcr.bazel.build
common --registry=https://baidu.github.io/babylon/registry
common --registry=https://raw.githubusercontent.com/apache/brpc/master/registry
```

Add bRPC to your `MODULE.bazel`:

```python
module(
    name = "my_brpc_app",
    version = "0.1.0",
)

bazel_dep(name = "protobuf", version = "27.3", repo_name = "com_google_protobuf")
bazel_dep(name = "brpc", version = "1.17.0", repo_name = "apache_brpc")
```

When developing against a local bRPC checkout, add a local override:

```python
local_path_override(
    module_name = "brpc",
    path = "/path/to/brpc",
)
```

Then link bRPC from your targets:

```python
cc_binary(
    name = "server",
    srcs = ["server.cpp"],
    deps = [
        "@apache_brpc//:brpc",
    ],
)
```

If your service uses protobuf, load `brpc_proto_library` from bRPC:

```python
load("@apache_brpc//bazel/tools:brpc_proto_library.bzl", "brpc_proto_library")

brpc_proto_library(
    name = "cc_echo_proto",
    srcs = ["echo.proto"],
)
```

## Legacy WORKSPACE usage

For projects that still use `WORKSPACE`, see `example/build_with_bazel`.

1. Move `example/build_with_bazel/*.BUILD` and
   `example/build_with_bazel/brpc_workspace.bzl` to the root of your project.
2. Add the following to your `WORKSPACE`:

```python
load("@//:brpc_workspace.bzl", "brpc_workspace")

brpc_workspace()
```

3. Link `apache_brpc` from your targets:

```python
deps = [
    "@apache_brpc//:bthread",
    "@apache_brpc//:brpc",
    "@apache_brpc//:butil",
    "@apache_brpc//:bvar",
]
```
