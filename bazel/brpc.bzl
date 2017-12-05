
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

