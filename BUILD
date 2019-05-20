licenses(["notice"])  # Apache v2

exports_files(["LICENSE"])

load(":bazel/brpc.bzl", "brpc_proto_library")

config_setting(
    name = "with_glog",
    define_values = {"with_glog": "true"},
    visibility = ["//visibility:public"],
)

config_setting(
    name = "with_thrift",
    define_values = {"with_thrift": "true"},
    visibility = ["//visibility:public"],
)

config_setting(
    name = "unittest",
    define_values = {"unittest": "true"},
)

config_setting(
    name = "darwin",
    values = {"cpu": "darwin"},
    visibility = ["//visibility:public"],
)

config_setting(
    name = "linux",
    values = {"cpu": "linux"},
    visibility = ["//visibility:public"],
)

COPTS = [
    "-DBTHREAD_USE_FAST_PTHREAD_MUTEX",
    "-D__const__=",
    "-D_GNU_SOURCE",
    "-DUSE_SYMBOLIZE",
    "-DNO_TCMALLOC",
    "-D__STDC_FORMAT_MACROS",
    "-D__STDC_LIMIT_MACROS",
    "-D__STDC_CONSTANT_MACROS",
    "-DGFLAGS_NS=google",
] + select({
    ":with_glog": ["-DBRPC_WITH_GLOG=1"],
    "//conditions:default": ["-DBRPC_WITH_GLOG=0"],
}) + select({
    ":with_thrift": ["-DENABLE_THRIFT_FRAMED_PROTOCOL=1"],
    "//conditions:default": [""],
})

LINKOPTS = [
    "-lpthread",
    "-ldl",
    "-lz", 
    "-lssl",
    "-lcrypto",
] + select({
    ":darwin": [
        "-framework CoreFoundation",
        "-framework CoreGraphics",
        "-framework CoreData",
        "-framework CoreText",
        "-framework Security",
        "-framework Foundation",
        "-Wl,-U,_MallocExtension_ReleaseFreeMemory",
        "-Wl,-U,_ProfilerStart",
        "-Wl,-U,_ProfilerStop",
        "-Wl,-U,_RegisterThriftProtocol",
    ],
    "//conditions:default": [
      "-lrt",
    ],
}) + select({
    ":with_thrift": [
        "-lthriftnb",
        "-levent",
        "-lthrift"],
    "//conditions:default": [],
})

genrule(
    name = "config_h",
    outs = [
        "src/butil/config.h",
    ],
    cmd = """cat << EOF  >$@""" + """
// This file is auto-generated.
#ifndef  BUTIL_CONFIG_H
#define  BUTIL_CONFIG_H

#ifdef BRPC_WITH_GLOG
#undef BRPC_WITH_GLOG
#endif
#define BRPC_WITH_GLOG """ + select({
    ":with_glog": "1",
    "//conditions:default": "0",
}) +
"""
#endif  // BUTIL_CONFIG_H
EOF
    """
)

BUTIL_SRCS = [
    "src/butil/third_party/dmg_fp/g_fmt.cc",
    "src/butil/third_party/dmg_fp/dtoa_wrapper.cc",
    "src/butil/third_party/dynamic_annotations/dynamic_annotations.c",
    "src/butil/third_party/icu/icu_utf.cc",
    "src/butil/third_party/superfasthash/superfasthash.c",
    "src/butil/third_party/modp_b64/modp_b64.cc",
    "src/butil/third_party/nspr/prtime.cc",
    "src/butil/third_party/symbolize/demangle.cc",
    "src/butil/third_party/symbolize/symbolize.cc",
    "src/butil/third_party/snappy/snappy-sinksource.cc",
    "src/butil/third_party/snappy/snappy-stubs-internal.cc",
    "src/butil/third_party/snappy/snappy.cc",
    "src/butil/third_party/murmurhash3/murmurhash3.cpp",
    "src/butil/arena.cpp",
    "src/butil/at_exit.cc",
    "src/butil/atomicops_internals_x86_gcc.cc",
    "src/butil/base64.cc",
    "src/butil/big_endian.cc",
    "src/butil/cpu.cc",
    "src/butil/debug/alias.cc",
    "src/butil/debug/asan_invalid_access.cc",
    "src/butil/debug/crash_logging.cc",
    "src/butil/debug/debugger.cc",
    "src/butil/debug/debugger_posix.cc",
    "src/butil/debug/dump_without_crashing.cc",
    "src/butil/debug/proc_maps_linux.cc",
    "src/butil/debug/stack_trace.cc",
    "src/butil/debug/stack_trace_posix.cc",
    "src/butil/environment.cc",
    "src/butil/files/file.cc",
    "src/butil/files/file_posix.cc",
    "src/butil/files/file_enumerator.cc",
    "src/butil/files/file_enumerator_posix.cc",
    "src/butil/files/file_path.cc",
    "src/butil/files/file_path_constants.cc",
    "src/butil/files/memory_mapped_file.cc",
    "src/butil/files/memory_mapped_file_posix.cc",
    "src/butil/files/scoped_file.cc",
    "src/butil/files/scoped_temp_dir.cc",
    "src/butil/file_util.cc",
    "src/butil/file_util_posix.cc",
    "src/butil/guid.cc",
    "src/butil/guid_posix.cc",
    "src/butil/hash.cc",
    "src/butil/lazy_instance.cc",
    "src/butil/location.cc",
    "src/butil/md5.cc",
    "src/butil/memory/aligned_memory.cc",
    "src/butil/memory/ref_counted.cc",
    "src/butil/memory/ref_counted_memory.cc",
    "src/butil/memory/singleton.cc",
    "src/butil/memory/weak_ptr.cc",
    "src/butil/posix/file_descriptor_shuffle.cc",
    "src/butil/posix/global_descriptors.cc",
    "src/butil/process_util.cc",
    "src/butil/rand_util.cc",
    "src/butil/rand_util_posix.cc",
    "src/butil/fast_rand.cpp",
    "src/butil/safe_strerror_posix.cc",
    "src/butil/sha1_portable.cc",
    "src/butil/strings/latin1_string_conversions.cc",
    "src/butil/strings/nullable_string16.cc",
    "src/butil/strings/safe_sprintf.cc",
    "src/butil/strings/string16.cc",
    "src/butil/strings/string_number_conversions.cc",
    "src/butil/strings/string_split.cc",
    "src/butil/strings/string_piece.cc",
    "src/butil/strings/string_util.cc",
    "src/butil/strings/string_util_constants.cc",
    "src/butil/strings/stringprintf.cc",
    "src/butil/strings/utf_offset_string_conversions.cc",
    "src/butil/strings/utf_string_conversion_utils.cc",
    "src/butil/strings/utf_string_conversions.cc",
    "src/butil/synchronization/cancellation_flag.cc",
    "src/butil/synchronization/condition_variable_posix.cc",
    "src/butil/synchronization/waitable_event_posix.cc",
    "src/butil/threading/non_thread_safe_impl.cc",
    "src/butil/threading/platform_thread_posix.cc",
    "src/butil/threading/simple_thread.cc",
    "src/butil/threading/thread_checker_impl.cc",
    "src/butil/threading/thread_collision_warner.cc",
    "src/butil/threading/thread_id_name_manager.cc",
    "src/butil/threading/thread_local_posix.cc",
    "src/butil/threading/thread_local_storage.cc",
    "src/butil/threading/thread_local_storage_posix.cc",
    "src/butil/threading/thread_restrictions.cc",
    "src/butil/threading/watchdog.cc",
    "src/butil/time/clock.cc",
    "src/butil/time/default_clock.cc",
    "src/butil/time/default_tick_clock.cc",
    "src/butil/time/tick_clock.cc",
    "src/butil/time/time.cc",
    "src/butil/time/time_posix.cc",
    "src/butil/version.cc",
    "src/butil/logging.cc",
    "src/butil/class_name.cpp",
    "src/butil/errno.cpp",
    "src/butil/find_cstr.cpp",
    "src/butil/status.cpp",
    "src/butil/string_printf.cpp",
    "src/butil/thread_local.cpp",
    "src/butil/unix_socket.cpp",
    "src/butil/endpoint.cpp",
    "src/butil/fd_utility.cpp",
    "src/butil/files/temp_file.cpp",
    "src/butil/files/file_watcher.cpp",
    "src/butil/time.cpp",
    "src/butil/zero_copy_stream_as_streambuf.cpp",
    "src/butil/crc32c.cc",
    "src/butil/containers/case_ignored_flat_map.cpp",
    "src/butil/iobuf.cpp",
    "src/butil/binary_printer.cpp",
    "src/butil/recordio.cc",
    "src/butil/popen.cpp",
] + select({
        ":darwin": [
            "src/butil/time/time_mac.cc",
            "src/butil/mac/scoped_mach_port.cc",
        ],
        "//conditions:default": [
            "src/butil/file_util_linux.cc",
            "src/butil/threading/platform_thread_linux.cc",
            "src/butil/strings/sys_string_conversions_posix.cc",
        ],
})

objc_library(
    name = "macos_lib",
    hdrs = [":config_h",
        "src/butil/atomicops.h",
        "src/butil/atomicops_internals_atomicword_compat.h",
        "src/butil/atomicops_internals_mac.h",
        "src/butil/base_export.h",        
        "src/butil/basictypes.h",
        "src/butil/build_config.h",
        "src/butil/compat.h",
        "src/butil/compiler_specific.h",
        "src/butil/containers/hash_tables.h",
        "src/butil/debug/debugger.h",
        "src/butil/debug/leak_annotations.h",
        "src/butil/file_util.h",
        "src/butil/file_descriptor_posix.h",
        "src/butil/files/file_path.h",
        "src/butil/files/file.h",
        "src/butil/files/scoped_file.h",
        "src/butil/lazy_instance.h",
        "src/butil/logging.h",
        "src/butil/mac/bundle_locations.h",
        "src/butil/mac/foundation_util.h",
        "src/butil/mac/scoped_cftyperef.h",
        "src/butil/mac/scoped_typeref.h",
        "src/butil/macros.h",
        "src/butil/memory/aligned_memory.h",
        "src/butil/memory/scoped_policy.h",
        "src/butil/memory/scoped_ptr.h",
        "src/butil/move.h",
        "src/butil/port.h",
        "src/butil/posix/eintr_wrapper.h",
        "src/butil/scoped_generic.h",
        "src/butil/strings/string16.h",
        "src/butil/strings/string_piece.h",
        "src/butil/strings/string_util.h",
        "src/butil/strings/string_util_posix.h",
        "src/butil/strings/sys_string_conversions.h",
        "src/butil/synchronization/lock.h",
        "src/butil/time/time.h",
        "src/butil/time.h",        
        "src/butil/third_party/dynamic_annotations/dynamic_annotations.h",
        "src/butil/threading/platform_thread.h",
        "src/butil/threading/thread_restrictions.h",
        "src/butil/threading/thread_id_name_manager.h",
        "src/butil/type_traits.h",
    ],
    non_arc_srcs = [
        "src/butil/mac/bundle_locations.mm",
        "src/butil/mac/foundation_util.mm",
        "src/butil/file_util_mac.mm",
        "src/butil/threading/platform_thread_mac.mm",
        "src/butil/strings/sys_string_conversions_mac.mm",
    ],
    deps = [
        "@com_github_gflags_gflags//:gflags",
    ] + select({
        ":with_glog": ["@com_github_google_glog//:glog"],
        "//conditions:default": [],
    }),
    includes = ["src/"],
    enable_modules = True,
    tags = ["manual"],
)

cc_library(
    name = "butil",
    srcs = BUTIL_SRCS,
    hdrs = glob([
        "src/butil/*.h",
        "src/butil/*.hpp",
        "src/butil/**/*.h",
        "src/butil/**/*.hpp",
        "src/butil/**/**/*.h",
        "src/butil/**/**/*.hpp",
        "src/butil/**/**/**/*.h",
        "src/butil/**/**/**/*.hpp",
        "src/butil/third_party/dmg_fp/dtoa.cc",
    ]) + [":config_h"],
    deps = [
        "@com_google_protobuf//:protobuf",
        "@com_github_gflags_gflags//:gflags",
    ] + select({
        ":with_glog": ["@com_github_google_glog//:glog"],
        ":darwin": [":macos_lib"],
        "//conditions:default": [],
    }),
    includes = [
        "src/",
    ],
    copts = COPTS + select({
        ":unittest": [
            "-DBVAR_NOT_LINK_DEFAULT_VARIABLES",
            "-DUNIT_TEST",
        ],
        "//conditions:default": [],
    }),
    linkopts = LINKOPTS,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "bvar",
    srcs = glob([
        "src/bvar/*.cpp",
        "src/bvar/detail/*.cpp",
    ],
    exclude = [
        "src/bvar/default_variables.cpp",
    ]) + select({
        ":unittest": [],
        "//conditions:default": ["src/bvar/default_variables.cpp"],
    }),
    hdrs = glob([
        "src/bvar/*.h",
        "src/bvar/utils/*.h",
        "src/bvar/detail/*.h",
    ]),
    includes = [
        "src/",
    ],
    deps = [
        ":butil",
    ],
    copts = COPTS + select({
        ":unittest": [
            "-DBVAR_NOT_LINK_DEFAULT_VARIABLES",
            "-DUNIT_TEST",
        ],
        "//conditions:default": [],
    }),
    linkopts = LINKOPTS,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "bthread",
    srcs = glob([
        "src/bthread/*.cpp",
    ]),
    hdrs = glob([
        "src/bthread/*.h",
        "src/bthread/*.list",
    ]),
    includes = [
        "src/"
    ],
    deps = [
        ":butil",
        ":bvar",
    ],
    copts = COPTS,
    linkopts = LINKOPTS,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "json2pb",
    srcs = glob([
        "src/json2pb/*.cpp",
    ]),
    hdrs = glob([
        "src/json2pb/*.h",
    ]),
    includes = [
        "src/",
    ],
    deps = [
        ":butil",
    ],
    copts = COPTS,
    linkopts = LINKOPTS,
    visibility = ["//visibility:public"],
)

cc_library(
    name = "mcpack2pb",
    srcs = [
        "src/mcpack2pb/field_type.cpp",
        "src/mcpack2pb/mcpack2pb.cpp",
        "src/mcpack2pb/parser.cpp",
        "src/mcpack2pb/serializer.cpp",
    ],
    hdrs = glob([
        "src/mcpack2pb/*.h",
    ]),
    includes = [
        "src/",
    ],
    deps = [
        ":butil",
        ":cc_brpc_idl_options_proto",
        "@com_google_protobuf//:protoc_lib",
    ],
    copts = COPTS,
    linkopts = LINKOPTS,
    visibility = ["//visibility:public"],
)

brpc_proto_library(
    name = "cc_brpc_idl_options_proto",
    srcs = [
        "src/idl_options.proto",
    ],
    deps = [
        "@com_google_protobuf//:cc_wkt_protos"
    ],
    visibility = ["//visibility:public"],
)

brpc_proto_library(
    name = "cc_brpc_internal_proto",
    srcs = glob([
        "src/brpc/*.proto",
        "src/brpc/policy/*.proto",
    ]),
    include = "src/",
    deps = [
        ":cc_brpc_idl_options_proto",
        "@com_google_protobuf//:cc_wkt_protos"
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "brpc",
    srcs = glob([
        "src/brpc/*.cpp",
        "src/brpc/**/*.cpp",
    ],
    exclude = [
        "src/brpc/thrift_service.cpp",
        "src/brpc/thrift_message.cpp",
        "src/brpc/policy/thrift_protocol.cpp",
    ]) + select({
        ":with_thrift" : glob([
            "src/brpc/thrift*.cpp",
            "src/brpc/**/thrift*.cpp"]),
        "//conditions:default" : [],
    }),
    hdrs = glob([
        "src/brpc/*.h",
        "src/brpc/**/*.h"
    ]),
    includes = [
        "src/",
    ],
    deps = [
        ":butil",
        ":bthread",
        ":bvar",
        ":json2pb",
        ":mcpack2pb",
        ":cc_brpc_internal_proto",
        "@com_github_google_leveldb//:leveldb",
    ],
    copts = COPTS,
    linkopts = LINKOPTS,
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "protoc-gen-mcpack",
    srcs = [
        "src/mcpack2pb/generator.cpp",
    ],
    deps = [
        ":cc_brpc_idl_options_proto",
        ":brpc",
    ],
    copts = COPTS,
    linkopts = LINKOPTS,
    visibility = ["//visibility:public"],
)

