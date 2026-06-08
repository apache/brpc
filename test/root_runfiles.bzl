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

"""A small Starlark rule that exposes files at the *runfiles workspace root*.

Why this exists
---------------
`bazel test` sets the test process cwd to `${TEST_SRCDIR}/${TEST_WORKSPACE}`,
which under bzlmod is `<test.runfiles>/_main/` -- the workspace root inside
the runfiles tree. Files declared via the standard `data` attribute on a
target in `//test` are placed under `_main/test/<file>` (the rule's package
path), which is NOT the cwd.

Several brpc tests (e.g. brpc_alpn_protocol_unittest, brpc_ssl_unittest,
brpc_protobuf_json_unittest) open data files such as `cert1.crt`, `cert1.key`,
`jsonout` by *plain relative paths* (this works under the CMake build because
file(COPY ...) places them next to the test binary). To make those same
relative paths work under Bazel without rewriting the test code, we need the
files to appear at `_main/<file>`, i.e. directly at the cwd.

A genrule cannot help here, because its `outs` must live in the genrule's own
package. The clean way is `ctx.runfiles(symlinks = {...})`, which places the
given files at arbitrary paths *relative to the workspace root inside the
runfiles tree* -- which is exactly the cwd of a Bazel-launched test.

Beware: `ctx.runfiles` has TWO related dict parameters:
  * symlinks      -> paths are relative to <runfiles>/<workspace>/ (the cwd)
  * root_symlinks -> paths are relative to <runfiles>/ (one level above cwd)
We want the first one.

Usage
-----
    load("//test:root_runfiles.bzl", "root_runfiles")

    root_runfiles(
        name = "test_runfiles_root_data",
        srcs = [
            "cert1.crt",
            "cert1.key",
            "jsonout",
        ],
    )

Then add `:test_runfiles_root_data` to a cc_test's `data` attribute.
"""

def _root_runfiles_impl(ctx):
    # Map each input file to its basename, placed at the workspace root inside
    # the runfiles tree. That root is the cwd of a `bazel test` process, so
    # tests can open the files via plain relative paths like "cert1.crt".
    symlinks = {f.basename: f for f in ctx.files.srcs}
    runfiles = ctx.runfiles(symlinks = symlinks)
    return [DefaultInfo(runfiles = runfiles)]

root_runfiles = rule(
    implementation = _root_runfiles_impl,
    attrs = {
        "srcs": attr.label_list(
            allow_files = True,
            doc = "Files to expose at the workspace root inside the runfiles " +
                  "tree (i.e. the cwd of `bazel test`), keyed by basename.",
        ),
    },
    doc = "Exposes data files at the workspace root inside the runfiles tree " +
          "(the cwd of `bazel test`), so tests can open them via plain " +
          "relative paths.",
)
