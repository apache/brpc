cmake_minimum_required(VERSION 2.8.10)
project(brpc C CXX)

option(WITH_GLOG "With glog" OFF)
option(DEBUG "Print debug logs" OFF)
option(WITH_DEBUG_SYMBOLS "With debug symbols" ON)
option(WITH_THRIFT "With thrift framed protocol supported" OFF)
option(BUILD_UNIT_TESTS "Whether to build unit tests" OFF)
option(DOWNLOAD_GTEST "Download and build a fresh copy of \
                            googletest. Requires Internet access." ON)

# Enable MACOSX_RPATH. Run "cmake --help-policy CMP0042" for policy details.
if(POLICY CMP0042)
    cmake_policy(SET CMP0042 NEW)
endif()

set(BRPC_VERSION 0.9.0)

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "brpc authors")
INCLUDE(CPack)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # require at least gcc 4.8
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC is too old, please install a newer version supporting C++11")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # require at least clang 3.3
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
        message(FATAL_ERROR "Clang is too old, please install a newer version supporting C++11")
    endif()
else()
    message(WARNING "You are using an unsupported compiler! Compilation has only been tested with Clang and GCC.")
endif()

set(WITH_GLOG_VAL "0")
if(WITH_GLOG)
    set(WITH_GLOG_VAL "1")
    set(BRPC_WITH_GLOG 1)
endif()

if(WITH_DEBUG_SYMBOLS)
    set(DEBUG_SYMBOL "-g")
endif()

if(WITH_THRIFT)
    set(THRIFT_CPP_FLAG "-DENABLE_THRIFT_FRAMED_PROTOCOL")
    set(THRIFTNB_LIB "thriftnb")
    set(THRIFT_LIB "thrift")
endif()

include(GNUInstallDirs)

configure_file(${PROJECT_SOURCE_DIR}/config.h.in ${PROJECT_SOURCE_DIR}/src/butil/config.h @ONLY)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

find_package(GFLAGS REQUIRED)

execute_process(
    COMMAND bash -c "grep \"namespace [_A-Za-z0-9]\\+ {\" ${GFLAGS_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $2}' | tr -d '\n'"
    OUTPUT_VARIABLE GFLAGS_NS
)
if(${GFLAGS_NS} STREQUAL "GFLAGS_NAMESPACE")
    execute_process(
        COMMAND bash -c "grep \"#define GFLAGS_NAMESPACE [_A-Za-z0-9]\\+\" ${GFLAGS_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $3}' | tr -d '\n'"
        OUTPUT_VARIABLE GFLAGS_NS
    )
endif()

include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

execute_process(
    COMMAND bash -c "git rev-parse --short HEAD | tr -d '\n'"
    OUTPUT_VARIABLE BRPC_REVISION
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(CheckFunctionExists)
    CHECK_FUNCTION_EXISTS(clock_gettime HAVE_CLOCK_GETTIME)
    if(NOT HAVE_CLOCK_GETTIME)
        set(DEFINE_CLOCK_GETTIME "-DNO_CLOCK_GETTIME_IN_MAC")
    endif()
endif()

set(CMAKE_CPP_FLAGS "${DEFINE_CLOCK_GETTIME} -DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -DBRPC_REVISION=\\\"${BRPC_REVISION}\\\" -D__STRICT_ANSI__")
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} ${DEBUG_SYMBOL} ${THRIFT_CPP_FLAG}")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-unused-parameter -fno-omit-frame-pointer")

macro(use_cxx11)
if(CMAKE_VERSION VERSION_LESS "3.1.3")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    endif()
else()
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()
endmacro(use_cxx11)

use_cxx11()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    #required by butil/crc32.cc to boost performance for 10x
    if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.4))
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4 -msse4.2")
    endif()
    if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0))
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-aligned-new")
    endif()
endif()

include(FindProtobuf)
include(FindThreads)

find_path(LEVELDB_INCLUDE_PATH NAMES leveldb/db.h)
find_library(LEVELDB_LIB NAMES leveldb)
if ((NOT LEVELDB_INCLUDE_PATH) OR (NOT LEVELDB_LIB))
    message(FATAL_ERROR "Fail to find leveldb")
endif()

if(WITH_GLOG)
    find_path(GLOG_INCLUDE_PATH NAMES glog/logging.h)
    find_library(GLOG_LIB NAMES glog)
    if((NOT GLOG_INCLUDE_PATH) OR (NOT GLOG_LIB))
        message(FATAL_ERROR "Fail to find glog")
    endif()
    include_directories(${GLOG_INCLUDE_PATH})
endif()

find_library(PROTOC_LIB NAMES protoc)
if(NOT PROTOC_LIB)
    message(FATAL_ERROR "Fail to find protoc lib")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(OPENSSL_ROOT_DIR
        "/usr/local/opt/openssl"    # Homebrew installed OpenSSL
        )
endif()

include(FindOpenSSL)

include_directories(
        ${GFLAGS_INCLUDE_PATH}
        ${PROTOBUF_INCLUDE_DIRS}
        ${LEVELDB_INCLUDE_PATH}
        ${OPENSSL_INCLUDE_DIR}
        )

set(DYNAMIC_LIB
    ${GFLAGS_LIBRARY}
    ${PROTOBUF_LIBRARIES}
    ${LEVELDB_LIB}
    ${PROTOC_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${THRIFT_LIB}
    ${THRIFTNB_LIB}
    ${OPENSSL_LIBRARIES}
    ${OPENSSL_CRYPTO_LIBRARY}
    dl
    z)
set(BRPC_PRIVATE_LIBS "-lgflags -lprotobuf -lleveldb -lprotoc -lssl -lcrypto -ldl -lz")

if(WITH_GLOG)
    set(DYNAMIC_LIB ${DYNAMIC_LIB} ${GLOG_LIB})
    set(BRPC_PRIVATE_LIBS "${BRPC_PRIVATE_LIBS} -lglog")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DYNAMIC_LIB ${DYNAMIC_LIB} rt)
    set(BRPC_PRIVATE_LIBS "${BRPC_PRIVATE_LIBS} -lrt")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(DYNAMIC_LIB ${DYNAMIC_LIB}
        pthread
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreData"
        "-framework CoreText"
        "-framework Security"
        "-framework Foundation"
        "-Wl,-U,_MallocExtension_ReleaseFreeMemory"
        "-Wl,-U,_ProfilerStart"
        "-Wl,-U,_ProfilerStop")
endif()

# for *.so
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/output/lib)
# for *.a
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/output/lib)

# the reason why not using file(GLOB_RECURSE...) is that we want to
# include different files on different platforms.
set(BUTIL_SOURCES
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/dmg_fp/g_fmt.cc
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/dmg_fp/dtoa_wrapper.cc
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/dynamic_annotations/dynamic_annotations.c
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/icu/icu_utf.cc
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/superfasthash/superfasthash.c
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/modp_b64/modp_b64.cc
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/nspr/prtime.cc
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/symbolize/demangle.cc
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/symbolize/symbolize.cc
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/snappy/snappy-sinksource.cc
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/snappy/snappy-stubs-internal.cc
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/snappy/snappy.cc
    ${PROJECT_SOURCE_DIR}/src/butil/third_party/murmurhash3/murmurhash3.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/arena.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/at_exit.cc
    ${PROJECT_SOURCE_DIR}/src/butil/atomicops_internals_x86_gcc.cc
    ${PROJECT_SOURCE_DIR}/src/butil/base64.cc
    ${PROJECT_SOURCE_DIR}/src/butil/big_endian.cc
    ${PROJECT_SOURCE_DIR}/src/butil/cpu.cc
    ${PROJECT_SOURCE_DIR}/src/butil/debug/alias.cc
    ${PROJECT_SOURCE_DIR}/src/butil/debug/asan_invalid_access.cc
    ${PROJECT_SOURCE_DIR}/src/butil/debug/crash_logging.cc
    ${PROJECT_SOURCE_DIR}/src/butil/debug/debugger.cc
    ${PROJECT_SOURCE_DIR}/src/butil/debug/debugger_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/debug/dump_without_crashing.cc
    ${PROJECT_SOURCE_DIR}/src/butil/debug/proc_maps_linux.cc
    ${PROJECT_SOURCE_DIR}/src/butil/debug/stack_trace.cc
    ${PROJECT_SOURCE_DIR}/src/butil/debug/stack_trace_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/environment.cc
    ${PROJECT_SOURCE_DIR}/src/butil/files/file.cc
    ${PROJECT_SOURCE_DIR}/src/butil/files/file_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/files/file_enumerator.cc
    ${PROJECT_SOURCE_DIR}/src/butil/files/file_enumerator_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/files/file_path.cc
    ${PROJECT_SOURCE_DIR}/src/butil/files/file_path_constants.cc
    ${PROJECT_SOURCE_DIR}/src/butil/files/memory_mapped_file.cc
    ${PROJECT_SOURCE_DIR}/src/butil/files/memory_mapped_file_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/files/scoped_file.cc
    ${PROJECT_SOURCE_DIR}/src/butil/files/scoped_temp_dir.cc
    ${PROJECT_SOURCE_DIR}/src/butil/file_util.cc
    ${PROJECT_SOURCE_DIR}/src/butil/file_util_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/guid.cc
    ${PROJECT_SOURCE_DIR}/src/butil/guid_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/hash.cc
    ${PROJECT_SOURCE_DIR}/src/butil/lazy_instance.cc
    ${PROJECT_SOURCE_DIR}/src/butil/location.cc
    ${PROJECT_SOURCE_DIR}/src/butil/md5.cc
    ${PROJECT_SOURCE_DIR}/src/butil/memory/aligned_memory.cc
    ${PROJECT_SOURCE_DIR}/src/butil/memory/ref_counted.cc
    ${PROJECT_SOURCE_DIR}/src/butil/memory/ref_counted_memory.cc
    ${PROJECT_SOURCE_DIR}/src/butil/memory/singleton.cc
    ${PROJECT_SOURCE_DIR}/src/butil/memory/weak_ptr.cc
    ${PROJECT_SOURCE_DIR}/src/butil/posix/file_descriptor_shuffle.cc
    ${PROJECT_SOURCE_DIR}/src/butil/posix/global_descriptors.cc
    ${PROJECT_SOURCE_DIR}/src/butil/process_util.cc
    ${PROJECT_SOURCE_DIR}/src/butil/rand_util.cc
    ${PROJECT_SOURCE_DIR}/src/butil/rand_util_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/fast_rand.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/safe_strerror_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/sha1_portable.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/latin1_string_conversions.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/nullable_string16.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/safe_sprintf.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/string16.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/string_number_conversions.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/string_split.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/string_piece.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/string_util.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/string_util_constants.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/stringprintf.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/utf_offset_string_conversions.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/utf_string_conversion_utils.cc
    ${PROJECT_SOURCE_DIR}/src/butil/strings/utf_string_conversions.cc
    ${PROJECT_SOURCE_DIR}/src/butil/synchronization/cancellation_flag.cc
    ${PROJECT_SOURCE_DIR}/src/butil/synchronization/condition_variable_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/synchronization/waitable_event_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/non_thread_safe_impl.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/platform_thread_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/simple_thread.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/thread_checker_impl.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/thread_collision_warner.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/thread_id_name_manager.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/thread_local_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/thread_local_storage.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/thread_local_storage_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/thread_restrictions.cc
    ${PROJECT_SOURCE_DIR}/src/butil/threading/watchdog.cc
    ${PROJECT_SOURCE_DIR}/src/butil/time/clock.cc
    ${PROJECT_SOURCE_DIR}/src/butil/time/default_clock.cc
    ${PROJECT_SOURCE_DIR}/src/butil/time/default_tick_clock.cc
    ${PROJECT_SOURCE_DIR}/src/butil/time/tick_clock.cc
    ${PROJECT_SOURCE_DIR}/src/butil/time/time.cc
    ${PROJECT_SOURCE_DIR}/src/butil/time/time_posix.cc
    ${PROJECT_SOURCE_DIR}/src/butil/version.cc
    ${PROJECT_SOURCE_DIR}/src/butil/logging.cc
    ${PROJECT_SOURCE_DIR}/src/butil/class_name.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/errno.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/find_cstr.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/status.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/string_printf.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/thread_local.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/unix_socket.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/endpoint.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/fd_utility.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/files/temp_file.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/files/file_watcher.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/time.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/zero_copy_stream_as_streambuf.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/crc32c.cc
    ${PROJECT_SOURCE_DIR}/src/butil/containers/case_ignored_flat_map.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/iobuf.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/binary_printer.cpp
    ${PROJECT_SOURCE_DIR}/src/butil/recordio.cc
    ${PROJECT_SOURCE_DIR}/src/butil/popen.cpp
    )

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BUTIL_SOURCES ${BUTIL_SOURCES}
        ${PROJECT_SOURCE_DIR}/src/butil/file_util_linux.cc
        ${PROJECT_SOURCE_DIR}/src/butil/threading/platform_thread_linux.cc
        ${PROJECT_SOURCE_DIR}/src/butil/strings/sys_string_conversions_posix.cc)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(BUTIL_SOURCES ${BUTIL_SOURCES}
        ${PROJECT_SOURCE_DIR}/src/butil/mac/bundle_locations.mm
        ${PROJECT_SOURCE_DIR}/src/butil/mac/foundation_util.mm
        ${PROJECT_SOURCE_DIR}/src/butil/file_util_mac.mm
        ${PROJECT_SOURCE_DIR}/src/butil/threading/platform_thread_mac.mm
        ${PROJECT_SOURCE_DIR}/src/butil/strings/sys_string_conversions_mac.mm
        ${PROJECT_SOURCE_DIR}/src/butil/time/time_mac.cc
        ${PROJECT_SOURCE_DIR}/src/butil/mac/scoped_mach_port.cc)
endif()

file(GLOB_RECURSE BVAR_SOURCES "${PROJECT_SOURCE_DIR}/src/bvar/*.cpp")
file(GLOB_RECURSE BTHREAD_SOURCES "${PROJECT_SOURCE_DIR}/src/bthread/*.cpp")
file(GLOB_RECURSE JSON2PB_SOURCES "${PROJECT_SOURCE_DIR}/src/json2pb/*.cpp")
file(GLOB_RECURSE BRPC_SOURCES "${PROJECT_SOURCE_DIR}/src/brpc/*.cpp")
file(GLOB_RECURSE THRIFT_SOURCES "thrift*.cpp")

if(WITH_THRIFT)
    message("brpc compile with thrift proctol")
else()
    # Remove thrift sources
    foreach(v ${THRIFT_SOURCES})
        list(REMOVE_ITEM BRPC_SOURCES ${v})
    endforeach()
    set(THRIFT_SOURCES "")
endif()

set(MCPACK2PB_SOURCES
    ${PROJECT_SOURCE_DIR}/src/mcpack2pb/field_type.cpp
    ${PROJECT_SOURCE_DIR}/src/mcpack2pb/mcpack2pb.cpp
    ${PROJECT_SOURCE_DIR}/src/mcpack2pb/parser.cpp
    ${PROJECT_SOURCE_DIR}/src/mcpack2pb/serializer.cpp)

include(CompileProto)
set(PROTO_FILES idl_options.proto
                brpc/rtmp.proto
                brpc/rpc_dump.proto
                brpc/get_favicon.proto
                brpc/span.proto
                brpc/builtin_service.proto
                brpc/get_js.proto
                brpc/errno.proto
                brpc/nshead_meta.proto
                brpc/options.proto
                brpc/policy/baidu_rpc_meta.proto
                brpc/policy/hulu_pbrpc_meta.proto
                brpc/policy/public_pbrpc_meta.proto
                brpc/policy/sofa_pbrpc_meta.proto
                brpc/policy/mongo.proto
                brpc/trackme.proto
                brpc/streaming_rpc_meta.proto)
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/output/include/brpc)
set(PROTOC_FLAGS ${PROTOC_FLAGS} -I${PROTOBUF_INCLUDE_DIR})
compile_proto(PROTO_HDRS PROTO_SRCS ${PROJECT_BINARY_DIR}
                                    ${PROJECT_BINARY_DIR}/output/include
                                    ${PROJECT_SOURCE_DIR}/src
                                    "${PROTO_FILES}")
add_library(PROTO_LIB OBJECT ${PROTO_SRCS} ${PROTO_HDRS})

set(SOURCES
    ${BVAR_SOURCES}
    ${BTHREAD_SOURCES}
    ${JSON2PB_SOURCES}
    ${MCPACK2PB_SOURCES}
    ${BRPC_SOURCES}
    ${THRIFT_SOURCES}
    )

add_subdirectory(src)
if(BUILD_UNIT_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
add_subdirectory(tools)

file(COPY ${CMAKE_CURRENT_BINARY_DIR}/brpc/
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/output/include/brpc/
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        )
file(COPY ${PROJECT_SOURCE_DIR}/src/
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/output/include/
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        )
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/output/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        )

# Install pkgconfig
configure_file(cmake/brpc.pc.in ${PROJECT_BINARY_DIR}/brpc.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/brpc.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
