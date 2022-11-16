## bRPC 作为Bazel第三方依赖
1. bRPC 依赖于一些开源库, 但这些库并没有提供bazel支持, 所以需要你手动将一部分依赖加入到你的构建项目中.
2. 将 /example/build_with_bazel/*.BUILD 和 brpc_workspace.bzl 该文件移动到你的项目根目录下, 将
```c++
    load("@//:brpc_workspace.bzl", "brpc_workspace")
    brpc_workspace();
```
内容添加到你的WORKSPACE中.

3. 链接请使用
  ```c++
  ...
  deps = [
    "@apache_brpc//:bthread",
    "@apache_brpc//:brpc",
    "@apache_brpc//:butil",
    "@apache_brpc//:bvar",
  ]
  ...
  ```
