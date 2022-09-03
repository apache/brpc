# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

#
# WARNING: This example is not a best practice for how to build with bRPC in bazel.
#

workspace(name = "com_github_brpc_brpc_example_build_with_old_bazel")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

#
# Constants
#

BAZEL_SKYLIB_VERSION = "1.1.1"  # 2021-09-27T17:33:49Z

BAZEL_SKYLIB_SHA256 = "c6966ec828da198c5d9adbaa94c05e3a1c7f21bd012a0b29ba8ddbccb2c93b0d"

RULES_PROTO_COMMIT_ID = "9f8407ec90b579cba157ce481682b2beb1f7409f"

RULES_PROTO_SHA256 = "3a27bf90d4cd3e4546afa801857d35c3c4db5f0680c840167f6fb2f7078de177"

RULES_CC_COMMIT_ID = "b7fe9697c0c76ab2fd431a891dbb9a6a32ed7c3e"

RULES_CC_SHA256 = "29daf0159f0cf552fcff60b49d8bcd4f08f08506d2da6e41b07058ec50cfeaec"

#
# Starlark libraries
#

http_archive(
    name = "bazel_skylib",
    sha256 = BAZEL_SKYLIB_SHA256,
    urls = [
        "https://github.com/bazelbuild/bazel-skylib/releases/download/{version}/bazel-skylib-{version}.tar.gz".format(version = BAZEL_SKYLIB_VERSION),
        "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/{version}/bazel-skylib-{version}.tar.gz".format(version = BAZEL_SKYLIB_VERSION),
    ],
)

http_archive(
    name = "rules_proto",
    sha256 = RULES_PROTO_SHA256,
    strip_prefix = "rules_proto-{version}".format(version = RULES_PROTO_COMMIT_ID),
    urls = ["https://github.com/bazelbuild/rules_proto/archive/{version}.tar.gz".format(version = RULES_PROTO_COMMIT_ID)],
)

http_archive(
    name = "rules_cc",
    sha256 = RULES_CC_SHA256,
    strip_prefix = "rules_cc-{commit_id}".format(commit_id = RULES_CC_COMMIT_ID),
    urls = [
        "https://github.com/bazelbuild/rules_cc/archive/{commit_id}.tar.gz".format(commit_id = RULES_CC_COMMIT_ID),
    ],
)

#
# C++ Dependencies
#
# Ordered lexicographical.
#

local_repository(
    name = "com_github_brpc_brpc",
    path = "../../",
)

http_archive(
    name = "com_github_gflags_gflags",
    sha256 = "a8263376b409900dd46830e4e34803a170484707327854cc252fc5865275a57d",
    strip_prefix = "gflags-46f73f88b18aee341538c0dfc22b1710a6abedef",
    url = "https://github.com/gflags/gflags/archive/46f73f88b18aee341538c0dfc22b1710a6abedef.tar.gz",
)

http_archive(
    name = "com_github_google_glog",
    build_file = "//:glog.BUILD",
    strip_prefix = "glog-a6a166db069520dbbd653c97c2e5b12e08a8bb26",
    url = "https://github.com/google/glog/archive/a6a166db069520dbbd653c97c2e5b12e08a8bb26.tar.gz",
)

http_archive(
    name = "com_github_google_leveldb",
    build_file = "//:leveldb.BUILD",
    sha256 = "3912ac36dbb264a62797d68687711c8024919640d89b6733f9342ada1d16cda1",
    strip_prefix = "leveldb-a53934a3ae1244679f812d998a4f16f2c7f309a6",
    url = "https://github.com/google/leveldb/archive/a53934a3ae1244679f812d998a4f16f2c7f309a6.tar.gz",
)

http_archive(
    name = "com_github_madler_zlib",  # 2017-01-15T17:57:23Z
    build_file = "@com_github_brpc_brpc//bazel/third_party/zlib:zlib.BUILD",
    sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
    strip_prefix = "zlib-1.2.11",
    urls = ["https://zlib.net/zlib-1.2.11.tar.gz"],
)

http_archive(
    name = "com_google_googletest",
    strip_prefix = "googletest-0fe96607d85cf3a25ac40da369db62bbee2939a5",
    url = "https://github.com/google/googletest/archive/0fe96607d85cf3a25ac40da369db62bbee2939a5.tar.gz",
)

http_archive(
    name = "com_google_protobuf",
    sha256 = "9510dd2afc29e7245e9e884336f848c8a6600a14ae726adb6befdb4f786f0be2",
    strip_prefix = "protobuf-3.6.1.3",
    type = "zip",
    url = "https://github.com/protocolbuffers/protobuf/archive/v3.6.1.3.zip",
)

# This is not a correct approach, just for simplicity.
# rules_foreign_cc didn't support too early version of bazel.
# bRPC need to be patched to work with boringssl for now.

new_local_repository(
    name = "openssl",
    build_file = "//:openssl.BUILD",
    path = "/usr",
)
