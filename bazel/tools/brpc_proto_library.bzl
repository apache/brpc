# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# brpc_proto_library: a proto -> cc_library macro that is robust against
# protobuf and Bazel version churn.
#
# Background:
#   - Old protobuf (< 3.21) used to expose a `cc_proto_library` macro
#     via `@com_google_protobuf//:protobuf.bzl`; newer protobuf removed
#     that macro.
#   - Newer protobuf expects callers to use Bazel's native
#     `proto_library` / `cc_proto_library` (or the `cc_proto_library`
#     re-exported from `@com_google_protobuf//bazel:cc_proto_library.bzl`),
#     but older protobuf does not surface a complete `ProtoInfo`, so
#     the native rules cannot build it.
#   - Bazel 8 additionally removed `cc_proto_library` from
#     `@rules_cc//cc:defs.bzl` (the load now resolves to a stub rule
#     that fails analysis), forcing every caller to either load it
#     from `@com_google_protobuf` or replace it.
#   - Bazel's `load()` is static and cannot be conditional on the
#     protobuf version, so a single brpc_proto_library.bzl cannot
#     directly satisfy both old and new protobuf by switching imports.
#
# Solution (inspired by https://github.com/trpc-group/trpc-cpp/blob/a69d2950f4a9c16b3cb40a2750c69d8940c96820/trpc/trpc.bzl):
# We implement the proto compilation rule from scratch (see
# proto_gen.bzl in this directory). Dependency information is
# propagated through Bazel's native `ProtoInfo` provider, and the
# default well-known-proto source is
# `@com_google_protobuf//:descriptor_proto` -- a `proto_library`
# whose name has been stable across protobuf 3.x / 27.x / 34.x.
load("//bazel/tools:proto_gen.bzl", "brpc_proto_gen")

# `:descriptor_proto` is the protobuf repo's `proto_library` target;
# its label has been stable since protobuf 3.x. We consume it through
# Bazel's native `ProtoInfo` provider, which is uniform across PB
# versions. If a caller's .proto needs additional well-known protos
# (any.proto, timestamp.proto, ...), they can pass extra entries via
# the `proto_deps` argument.
_DEFAULT_PROTO_DEPS = ["@com_google_protobuf//:descriptor_proto"]

def brpc_proto_library(
        name,
        srcs,
        deps = [],
        include = None,
        proto_deps = None,
        visibility = None,
        testonly = 0):
    """Generate a cc_library from a set of .proto files.

    Args:
      name: target name.
      srcs: list of .proto files, paths relative to the current package.
      deps: list of other `brpc_proto_library` targets this target
            depends on. The macro internally translates these labels
            to the underlying `<name>_genproto` rule so that .proto
            include paths and sources propagate.
      include: protoc `-I` root AND the resulting cc_library `includes`
               root, relative to the current package.
               When omitted, "" or None, the include root is the
               current package itself (suitable for .proto files
               sitting directly under the package root, as in `test/`
               and `example/...`). The root `BUILD.bazel` of brpc must
               pass `"src"` so that code can reference the protos as
               `import "brpc/foo.proto"`.
      proto_deps: list of native `proto_library` dependencies
                  (well-known protos or external .proto libraries).
                  Defaults to
                  `["@com_google_protobuf//:descriptor_proto"]`.
                  Pass `[]` explicitly to disable the default; pass
                  None (the default) to use it.
      visibility: same semantics as cc_library.
      testonly: same semantics as cc_library.
    """

    real_include = include if include != None else ""
    real_proto_deps = proto_deps if proto_deps != None else _DEFAULT_PROTO_DEPS

    gen_name = name + "_genproto"
    brpc_proto_gen(
        name = gen_name,
        srcs = srcs,
        # Rewrite each `brpc_proto_library` dep to its underlying
        # `<dep>_genproto` rule, which exports BrpcProtoInfo.
        deps = [d + "_genproto" for d in deps],
        include = real_include,
        proto_deps = real_proto_deps,
        visibility = visibility,
        testonly = testonly,
    )

    # Split the generated files into .pb.h / .pb.cc via OutputGroupInfo
    # and feed them to cc_library's hdrs / srcs separately. Bazel
    # rejects declaring the same File in both hdrs and srcs, so a
    # plain DefaultInfo wouldn't work here.
    native.filegroup(
        name = gen_name + "_hdrs",
        srcs = [":" + gen_name],
        output_group = "hdrs",
        visibility = visibility,
        testonly = testonly,
    )
    native.filegroup(
        name = gen_name + "_srcs",
        srcs = [":" + gen_name],
        output_group = "srcs",
        visibility = visibility,
        testonly = testonly,
    )

    native.cc_library(
        name = name,
        srcs = [":" + gen_name + "_srcs"],
        hdrs = [":" + gen_name + "_hdrs"],
        # cc_library `includes` is required, otherwise the .pb.cc
        # files inside this cc_library cannot find the .pb.h headers
        # they just generated (the headers live under
        # bazel-bin/<package>/<include>/...). When include="" we pass
        # "." to mean "the current package itself"; Bazel then exposes
        # both `-I <package>` and `-I bazel-bin/<package>`
        # automatically to dependents.
        includes = [real_include if real_include else "."],
        deps = deps + ["@com_google_protobuf//:protobuf"],
        visibility = visibility,
        testonly = testonly,
    )
