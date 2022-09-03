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

load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

filegroup(
    name = "all_srcs",
    srcs = glob(["**"]),
)

cmake(
    name = "event",
    cache_entries = {
        "EVENT__DISABLE_BENCHMARK": "ON",
        "EVENT__DISABLE_TESTS": "ON",
        "EVENT__DISABLE_SAMPLES": "ON",
        "EVENT__LIBRARY_TYPE": "STATIC",
        "OPENSSL_ROOT_DIR": "$$EXT_BUILD_DEPS$$/openssl",
    },
    generate_args = ["-GNinja"],
    lib_source = ":all_srcs",
    linkopts = [
        "-pthread",
    ],
    out_static_libs = select({
        "@platforms//os:windows": [
            "event.lib",
            "event_core.lib",
            "event_extra.lib",
            "event_openssl.lib",
            "event_pthreads.lib",
        ],
        "//conditions:default": [
            "libevent.a",
            "libevent_core.a",
            "libevent_extra.a",
            "libevent_openssl.a",
            "libevent_pthreads.a",
        ],
    }),
    visibility = ["//visibility:public"],
    deps = [
        # Zlib is only used for testing.
        "@openssl//:crypto",
        "@openssl//:ssl",
    ],
)
