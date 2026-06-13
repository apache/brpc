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

def generate_unittests(
        name,
        srcs,
        deps,
        copts,
        linkopts = [],
        data = [],
        per_test_tags = {},
        per_test_size = {}):
    tests = []
    for s in srcs:
        ut_name = s.replace(".cpp", "")
        # per_test_size raises Bazel's per-test timeout for a specific file
        # ("large" = 900s vs the default medium = 300s), for binaries whose
        # cumulative real-time waits would otherwise blow the limit on
        # contended CI runners.
        kwargs = {}
        size = per_test_size.get(s)
        if size != None:
            kwargs["size"] = size
        native.cc_test(
            name = ut_name,
            srcs = [s],
            copts = copts,
            deps = deps,
            linkopts = linkopts,
            data = data,
            # Integration tests that fork a real server binary (e.g. redis-server)
            # need extra tags: "external" forces a real run instead of a cached
            # pass, and "local" runs them outside the sandbox so the PATH-located
            # server binary is visible and loopback works.
            tags = per_test_tags.get(s, []),
            **kwargs
        )
        tests.append(":" + ut_name)

    native.test_suite(
        name  = name,
        tests = tests,
    )