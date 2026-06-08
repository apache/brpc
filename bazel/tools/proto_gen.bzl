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
# brpc's self-implemented proto -> .pb.{h,cc} compilation rule.
#
# Cross-protobuf-version compatibility strategy (inspired by trpc.bzl):
#   1) Do NOT load `@com_google_protobuf//:protobuf.bzl` and do NOT
#      call `native.cc_proto_library` -- this side-steps the double
#      incompatibility of newer protobuf removing the macro and older
#      protobuf missing a complete `ProtoInfo` provider.
#   2) Do NOT hardcode any protobuf-repo-internal filegroup names
#      (such as `well_known_protos` / `descriptor_proto_srcs`) -- the
#      names of those filegroups have shifted between protobuf 3.x
#      and 27.x+.
#   3) Propagate dependencies via Bazel's native `ProtoInfo` provider:
#      callers pass `proto_library` targets through `proto_deps`
#      (defaulting to `@com_google_protobuf//:descriptor_proto`, a
#      `proto_library` whose label has been stable across PB versions),
#      and this rule walks `transitive_sources` / `transitive_proto_path`
#      to collect .proto source files and protoc `-I` paths -- fully
#      decoupled from the PB version.
#   4) Among `brpc_proto_gen` targets themselves, dependencies are
#      propagated via the custom `BrpcProtoInfo` provider declared
#      below.
#
# Note on minimum Bazel version: this rule itself does not call any
# Bazel-version-specific API, but it still consumes `ProtoInfo` from
# `proto_library` targets (e.g. `descriptor_proto`) defined inside
# the protobuf repo. Protobuf >= 34 calls
# `ProtoInfo(option_deps = ..., extension_declarations = ...)` in its
# own `proto_library` implementation, and those fields are only
# accepted by Bazel >= 8. In other words, the minimum Bazel version
# brpc requires is whatever the bundled protobuf version requires --
# we just don't add any extra requirement on top of that.

BrpcProtoInfo = provider(
    doc = "Carries transitive .proto sources and protoc -I flags between brpc_proto_gen targets.",
    fields = {
        "transitive_srcs": "depset of all transitive .proto Files.",
        "transitive_imports": "depset of strings, all -I flags needed by protoc.",
    },
)

def _resolve_include_dir(ctx):
    """Compute this target's protoc include root (workspace-relative).

    Examples:
      ctx.label.package = ""     + include = "src" -> "src"
      ctx.label.package = "test" + include = ""    -> "test"
      ctx.label.package = ""     + include = ""    -> "."
    """
    pkg = ctx.label.package
    inc = ctx.attr.include.rstrip("/")
    if pkg and inc:
        return pkg + "/" + inc
    if pkg:
        return pkg
    if inc:
        return inc
    return "."

def _proto_gen_impl(ctx):
    srcs = ctx.files.srcs
    include_dir = _resolve_include_dir(ctx)
    bin_root = ctx.bin_dir.path

    # `-I` flags for this target itself: the source-tree root plus
    # the corresponding bin-dir root. The bin-dir entry is needed
    # when a transitive dep generates .proto files into bazel-bin
    # (e.g. via a custom code generator).
    own_imports = ["-I" + include_dir]
    if include_dir == ".":
        own_imports.append("-I" + bin_root)
    else:
        own_imports.append("-I" + bin_root + "/" + include_dir)

    # Collect transitive info from other `brpc_proto_gen` deps.
    dep_srcs_list = [d[BrpcProtoInfo].transitive_srcs for d in ctx.attr.deps]
    dep_imports_list = [d[BrpcProtoInfo].transitive_imports for d in ctx.attr.deps]

    # Collect native `proto_library` deps (well-known protos or
    # external .proto libraries). Key design: .proto sources and
    # `-I` paths are obtained via Bazel's native `ProtoInfo`,
    # avoiding any reliance on protobuf-repo-internal filegroup
    # names (descriptor_proto_srcs / well_known_protos / ...).
    # `transitive_proto_path` already accounts for the
    # `_virtual_imports` paths created by `strip_import_prefix`, so
    # protoc can consume the paths directly.
    #
    # Important: `strip_import_prefix` causes .proto files to live at
    # virtual paths under bazel-out/<config>/bin/<workspace_root>/<virtual>
    # (see trpc.bzl for prior art). For every `proto_dep` repo we
    # therefore expose FOUR candidate `-I` paths to be safe:
    #   1) <workspace_root>                  -- source root (plain proto_library)
    #   2) <bin_dir>/<workspace_root>        -- virtual root after strip_import_prefix
    #   3) <workspace_root>/src              -- fallback for older PB descriptor_proto without strip
    #   4) <bin_dir>/<workspace_root>/src    -- same as #3, under bin-dir
    # protoc only warns on non-existent `-I` paths, so this is harmless.
    proto_dep_src_depsets = []
    proto_dep_imports = []
    extra_pb_root_imports = []
    # Belt-and-suspenders: also feed each `proto_dep`'s default outputs
    # (i.e. the .proto files themselves) into `inputs`, in case the
    # ProtoInfo's transitive_sources does not cover everything we need.
    proto_dep_src_depsets.append(depset(direct = ctx.files.proto_deps))
    for pd in ctx.attr.proto_deps:
        pi = pd[ProtoInfo]
        proto_dep_src_depsets.append(pi.transitive_sources)
        for path in pi.transitive_proto_path.to_list():
            proto_dep_imports.append("-I" + path)
        wsroot = pd.label.workspace_root
        if wsroot:
            extra_pb_root_imports.append("-I" + wsroot)
            extra_pb_root_imports.append("-I" + bin_root + "/" + wsroot)
            extra_pb_root_imports.append("-I" + wsroot + "/src")
            extra_pb_root_imports.append("-I" + bin_root + "/" + wsroot + "/src")
    # Deduplicate the workspace-level `-I` entries so the same repo
    # is not listed multiple times when several proto_deps share it.
    proto_dep_imports.extend(depset(extra_pb_root_imports).to_list())

    all_imports = depset(
        direct = own_imports + proto_dep_imports,
        transitive = dep_imports_list,
    )

    # Output files: every .proto produces a .pb.h and a .pb.cc.
    # NB: the `path` argument of `ctx.actions.declare_file(path)` is
    # *relative to the current package*, while `src.short_path` is
    # *relative to the workspace root*. The two differ by the package
    # prefix; we must strip that prefix before calling declare_file,
    # otherwise the outputs would land at
    # bazel-bin/<pkg>/<pkg>/... (one level too deep).
    pkg = ctx.label.package
    pkg_prefix = pkg + "/" if pkg else ""
    outs = []
    for src in srcs:
        if not src.short_path.endswith(".proto"):
            fail("brpc_proto_gen srcs must be .proto, got: %s" % src.short_path)

        rel = src.short_path
        if pkg_prefix and rel.startswith(pkg_prefix):
            rel = rel[len(pkg_prefix):]
        base = rel[:-len(".proto")]
        outs.append(ctx.actions.declare_file(base + ".pb.h"))
        outs.append(ctx.actions.declare_file(base + ".pb.cc"))

    # protoc's --cpp_out points at the include root under bin_root.
    # After protoc organizes outputs by their import-relative path,
    # the .pb.{h,cc} files land exactly where declare_file declared
    # them above.
    if include_dir == ".":
        cpp_out_dir = bin_root
    else:
        cpp_out_dir = bin_root + "/" + include_dir

    args = ctx.actions.args()
    args.add_all(all_imports.to_list())
    args.add("--cpp_out=" + cpp_out_dir)
    args.add_all([s.path for s in srcs])

    ctx.actions.run(
        executable = ctx.executable.protoc,
        arguments = [args],
        inputs = depset(
            direct = srcs,
            transitive = dep_srcs_list + proto_dep_src_depsets,
        ),
        outputs = outs,
        mnemonic = "BrpcProtoCompile",
        progress_message = "BrpcProtoCompile %s" % ctx.label,
    )

    hdr_files = [f for f in outs if f.path.endswith(".pb.h")]
    src_files = [f for f in outs if f.path.endswith(".pb.cc")]

    return [
        DefaultInfo(files = depset(outs)),
        # Split outputs so the brpc_proto_library macro can route
        # them to cc_library.hdrs vs cc_library.srcs separately.
        OutputGroupInfo(
            hdrs = depset(hdr_files),
            srcs = depset(src_files),
        ),
        BrpcProtoInfo(
            transitive_srcs = depset(direct = srcs, transitive = dep_srcs_list),
            transitive_imports = all_imports,
        ),
    ]

brpc_proto_gen = rule(
    implementation = _proto_gen_impl,
    attrs = {
        "srcs": attr.label_list(
            allow_files = [".proto"],
            mandatory = True,
        ),
        # Other `brpc_proto_gen` targets; deps are propagated via the
        # custom `BrpcProtoInfo` provider.
        "deps": attr.label_list(
            providers = [BrpcProtoInfo],
            default = [],
        ),
        # The include root, relative to the current BUILD package
        # (e.g. "src" for the brpc root BUILD).
        "include": attr.string(default = ""),
        "protoc": attr.label(
            default = Label("@com_google_protobuf//:protoc"),
            executable = True,
            # `"host"` is the legacy spelling of `"exec"`. Modern
            # Bazel (>= 6) prefers `"exec"`, but still accepts
            # `"host"` as an alias through at least Bazel 8.x. We
            # keep `"host"` here for the broad range of Bazel
            # versions used to build brpc; switch to `"exec"` once
            # `"host"` is finally removed (likely a future major
            # Bazel release).
            cfg = "host",
            allow_files = True,
        ),
        # Native `proto_library` deps (well-known protos or external
        # .proto libraries). .proto sources and -I paths are obtained
        # automatically through Bazel's native `ProtoInfo`, fully
        # decoupled from the protobuf version.
        "proto_deps": attr.label_list(
            providers = [ProtoInfo],
            default = [],
        ),
    },
)
