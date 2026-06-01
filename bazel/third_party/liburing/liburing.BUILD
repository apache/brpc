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

# BUILD file for liburing (io_uring userspace library).
# Mirrors the approach used in the Bazel Central Registry overlay for liburing:
# a genrule runs ./configure (to generate config-host.h, compat.h and
# io_uring_version.h) and a plain cc_library compiles the sources directly,
# avoiding any dependency on rules_foreign_cc.

load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # MIT / GPL-2.0-only (dual-licensed)

# Run ./configure to produce the three generated headers.  We only need the
# headers, not the full make build, so we skip the library compilation step.
genrule(
    name = "generate_headers",
    srcs = [
        "Makefile.common",
        "liburing.spec",
    ],
    outs = [
        "config-host.h",
        "src/include/liburing/compat.h",
        "src/include/liburing/io_uring_version.h",
    ],
    cmd = """
            export CC=$(CC) CXX=$(CC)++

            # switch to the package dir to execute "configure" right there
            pushd $$(dirname $(location configure))
              mkdir -p src/include/liburing
              ./configure --use-libc
            popd

            # collect the outputs
            for out in $(OUTS); do
              cp $$(realpath --relative-to=$(BINDIR) $$out) $$out
            done
          """,
    toolchains = ["@bazel_tools//tools/cpp:current_cc_toolchain"],
    tools = ["configure"],
)

cc_library(
    name = "liburing",
    srcs = [
        "config-host.h",
        "src/include/liburing/compat.h",
        "src/include/liburing/io_uring_version.h",
        "src/queue.c",
        "src/register.c",
        "src/setup.c",
        "src/syscall.c",
        "src/version.c",
    ] + glob([
        "src/arch/**/*.h",
        "src/*.h",
    ]),
    hdrs = glob(["src/include/**/*.h"]),
    # cflags aligned with upstream:
    # https://github.com/axboe/liburing/blob/master/src/Makefile#L13
    copts = [
        "-D_GNU_SOURCE",
        "-D_LARGEFILE_SOURCE",
        "-D_FILE_OFFSET_BITS=64",
        "-DLIBURING_INTERNAL",
        "-Wall",
        "-Wextra",
        "-fno-stack-protector",
        "-include config-host.h",
    ],
    includes = ["src/include"],
    visibility = ["//visibility:public"],
)
