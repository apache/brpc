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

load("@com_google_protobuf//:protobuf.bzl", "cc_proto_library")

def brpc_proto_library(name, srcs, deps=[], include=None, visibility=None, testonly=0):
    native.filegroup(name=name + "_proto_srcs",
                     srcs=srcs,
                     visibility=visibility,)
    cc_proto_library(name=name,
                     srcs=srcs,
                     deps=deps,
                     cc_libs=["@com_google_protobuf//:protobuf"],
                     include=include,
                     protoc="@com_google_protobuf//:protoc",
                     default_runtime="@com_google_protobuf//:protobuf",
                     testonly=testonly,
                     visibility=visibility,)

