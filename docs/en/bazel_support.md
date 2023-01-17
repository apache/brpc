## bRPC as a Bazel third-party dependency
1. bRPC relies on a number of open source libraries that do not provide bazel support, so you will need to manually add some of these dependencies to your build project.
2. Move the BUILD file /example/build_with_bazel/*.BUILD and brpc_workspace.bzl to the root of your project, and add the contents of 
```c++
    load("@//:brpc_workspace.bzl", "brpc_workspace")
    brpc_workspace();
```
to your WORKSPACE

3. link apache_brpc like:
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
