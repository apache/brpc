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
    name = "thrift",
    cache_entries = {
        "BUILD_COMPILER": "OFF",
        "BUILD_TESTING": "OFF",
        "BUILD_TUTORIALS": "OFF",
        "WITH_AS3": "OFF",
        "WITH_CPP": "ON",
        "WITH_C_GLIB": "OFF",
        "WITH_JAVA": "OFF",
        "WITH_JAVASCRIPT": "OFF",
        "WITH_NODEJS": "OFF",
        "WITH_PYTHON": "OFF",
        "BUILD_SHARED_LIBS": "OFF",
        "Boost_USE_STATIC_LIBS": "ON",
        "BOOST_ROOT": "$$EXT_BUILD_DEPS$$",
        "LIBEVENT_INCLUDE_DIRS": "$$EXT_BUILD_DEPS$$/event/include",
        "LIBEVENT_LIBRARIES": "$$EXT_BUILD_DEPS$$/event/lib/libevent.a",
        "OPENSSL_ROOT_DIR": "$$EXT_BUILD_DEPS$$/openssl",
        "ZLIB_ROOT": "$$EXT_BUILD_DEPS$$/zlib",
    },
    generate_args = ["-GNinja"],
    lib_source = ":all_srcs",
    linkopts = [
        "-pthread",
    ],
    out_static_libs = select({
        "@platforms//os:windows": [
            "thrift.lib",
            "thriftnb.lib",
            "thriftz.lib",
        ],
        "//conditions:default": [
            "libthrift.a",
            "libthriftnb.a",
            "libthriftz.a",
        ],
    }),
    visibility = ["//visibility:public"],
    deps = [
        "@boost//:algorithm",
        "@boost//:locale",
        "@boost//:noncopyable",
        "@boost//:numeric_conversion",
        "@boost//:scoped_array",
        "@boost//:smart_ptr",
        "@boost//:tokenizer",
        "@com_github_libevent_libevent//:event",
        "@com_github_madler_zlib//:zlib",
        "@openssl//:crypto",
        "@openssl//:ssl",
    ],
)
