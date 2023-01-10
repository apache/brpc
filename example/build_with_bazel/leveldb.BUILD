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

package(default_visibility = ["//visibility:public"])


config_setting(
    name = "darwin",
    values = {"cpu": "darwin"},
    visibility = ["//visibility:public"],
)

SOURCES = ["db/builder.cc",
         "db/c.cc",
         "db/dbformat.cc",
         "db/db_impl.cc",
         "db/db_iter.cc",
         "db/dumpfile.cc",
         "db/filename.cc",
         "db/log_reader.cc",
         "db/log_writer.cc",
         "db/memtable.cc",
         "db/repair.cc",
         "db/table_cache.cc",
         "db/version_edit.cc",
         "db/version_set.cc",
         "db/write_batch.cc",
         "table/block_builder.cc",
         "table/block.cc",
         "table/filter_block.cc",
         "table/format.cc",
         "table/iterator.cc",
         "table/merger.cc",
         "table/table_builder.cc",
         "table/table.cc",
         "table/two_level_iterator.cc",
         "util/arena.cc",
         "util/bloom.cc",
         "util/cache.cc",
         "util/coding.cc",
         "util/comparator.cc",
         "util/crc32c.cc",
         "util/env.cc",
         "util/env_posix.cc",
         "util/filter_policy.cc",
         "util/hash.cc",
         "util/histogram.cc",
         "util/logging.cc",
         "util/options.cc",
         "util/status.cc",
         "port/port_posix.cc",
         "port/port_posix_sse.cc",
         "helpers/memenv/memenv.cc",
    ]

cc_library(
    name = "leveldb",
    srcs = SOURCES,
    hdrs = glob([ 
        "helpers/memenv/*.h",
        "util/*.h",
        "port/*.h",
        "port/win/*.h",
        "table/*.h",
        "db/*.h",
        "include/leveldb/*.h"
    ],
    exclude = [
            "**/*test.*",
    ]),
    includes = [
        "include/",
    ],
    copts = [
        "-fno-builtin-memcmp",
        "-DLEVELDB_PLATFORM_POSIX=1",
        "-DLEVELDB_ATOMIC_PRESENT",
    ],
    defines = [
        "LEVELDB_PLATFORM_POSIX",
    ] + select({
        ":darwin": ["OS_MACOSX"],
        "//conditions:default": [],
    }),
)